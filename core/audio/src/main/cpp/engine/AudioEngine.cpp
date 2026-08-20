#include "AudioEngine.h"

#include <android/log.h>
#include <oboe/OboeExtensions.h>
#include <time.h>

#include <cmath>
#include <limits>

#include "levels.h"

#define LOG_TAG "aa_engine"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace aa {

namespace {
constexpr std::size_t kRingCapacitySamples = 1 << 18;  // 262144 samples
constexpr std::size_t kMaxTimestamps = 600;            // ~60 s at 10 Hz polling
constexpr std::size_t kMinTimestampsForRate = 10;
constexpr double kMinTimestampSpanSec = 0.5;

int64_t nowNanos() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}
}  // namespace

AudioEngine& AudioEngine::instance() {
    static AudioEngine engine;
    return engine;
}

int32_t AudioEngine::start(int32_t deviceId, int32_t sampleRate,
                           int32_t channelCount, int32_t inputPreset) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_) {
        stream_->stop();
        stream_->close();
        stream_.reset();
    }

    ring_ = std::make_unique<SpscRing>(kRingCapacitySamples);
    scratch_.assign(kRingCapacitySamples, 0.0f);
    cbStats_.reset();
    timestamps_.clear();
    framesRead_.store(0);
    lastError_.store(0);
    rmsDbfs_[0] = rmsDbfs_[1] = -200.0;
    peakDbfs_[0] = peakDbfs_[1] = -200.0;

    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Input)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setFormatConversionAllowed(true)
        ->setInputPreset(static_cast<oboe::InputPreset>(inputPreset))
        ->setDataCallback(this)
        ->setErrorCallback(this);
    // 0 = unspecified: take the device-native value and avoid hidden
    // resampling/channel conversion — this is a measurement tool.
    if (deviceId > 0) builder.setDeviceId(deviceId);
    if (sampleRate > 0) {
        builder.setSampleRate(sampleRate);
        builder.setSampleRateConversionQuality(oboe::SampleRateConversionQuality::None);
    }
    if (channelCount > 0) builder.setChannelCount(channelCount);

    const oboe::Result result = builder.openStream(stream_);
    if (result != oboe::Result::OK) {
        ALOGW("openStream failed: %s", oboe::convertToText(result));
        lastError_.store(static_cast<int32_t>(result));
        stream_.reset();
        return static_cast<int32_t>(result);
    }

    sampleRateNominal_ = stream_->getSampleRate();
    channelCount_ = stream_->getChannelCount();
    framesPerBurst_ = stream_->getFramesPerBurst();
    bufferCapacity_ = stream_->getBufferCapacityInFrames();
    audioApi_ = static_cast<int32_t>(stream_->getAudioApi());
    performanceMode_ = static_cast<int32_t>(stream_->getPerformanceMode());
    sharingMode_ = static_cast<int32_t>(stream_->getSharingMode());
    deviceId_ = stream_->getDeviceId();
    inputPresetActual_ = static_cast<int32_t>(stream_->getInputPreset());
    mmapUsed_ = oboe::OboeExtensions::isMMapUsed(stream_.get()) ? 1 : 0;

    const oboe::Result startResult = stream_->requestStart();
    if (startResult != oboe::Result::OK) {
        ALOGW("requestStart failed: %s", oboe::convertToText(startResult));
        lastError_.store(static_cast<int32_t>(startResult));
        stream_->close();
        stream_.reset();
        return static_cast<int32_t>(startResult);
    }

    spl_.configure(sampleRateNominal_,
                   static_cast<dsp::Weighting>(splWeighting_),
                   static_cast<dsp::TimeWeighting>(splTimeWeighting_));
    spectrum_.configure(sampleRateNominal_, spectrumFftSize_,
                        static_cast<dsp::WindowType>(spectrumWindow_),
                        spectrumAvgTau_);

    running_.store(true);
    ALOGI("input started: rate=%d ch=%d burst=%d api=%d mmap=%d preset=%d",
          sampleRateNominal_, channelCount_, framesPerBurst_, audioApi_,
          mmapUsed_, inputPresetActual_);
    return 0;
}

void AudioEngine::splConfigure(int32_t weighting, int32_t timeWeighting) {
    std::lock_guard<std::mutex> lock(mutex_);
    splWeighting_ = weighting;
    splTimeWeighting_ = timeWeighting;
    if (running_.load() && stream_) {
        spl_.configure(sampleRateNominal_,
                       static_cast<dsp::Weighting>(weighting),
                       static_cast<dsp::TimeWeighting>(timeWeighting));
    }
}

void AudioEngine::splResetStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    spl_.resetStats();
}

void AudioEngine::spectrumConfigure(int32_t fftSize, int32_t window,
                                    double avgTauSec) {
    std::lock_guard<std::mutex> lock(mutex_);
    spectrumFftSize_ = fftSize;
    spectrumWindow_ = window;
    spectrumAvgTau_ = avgTauSec;
    if (running_.load() && stream_) {
        spectrum_.configure(sampleRateNominal_, fftSize,
                            static_cast<dsp::WindowType>(window), avgTauSec);
    }
}

int32_t AudioEngine::spectrumRead(float* avg, float* peak, int32_t maxBins,
                                  bool psd) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_.load() || !stream_) return 0;
    if (spectrum_.bins() > maxBins) return 0;
    drainAndProcessLocked();
    if (!spectrum_.compute()) return 0;
    spectrum_.readAverage(avg, psd);
    spectrum_.readPeak(peak);
    return spectrum_.bins();
}

void AudioEngine::spectrumResetPeak() {
    std::lock_guard<std::mutex> lock(mutex_);
    spectrum_.resetPeak();
}

void AudioEngine::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    running_.store(false);
    if (stream_) {
        stream_->stop();
        stream_->close();
        stream_.reset();
    }
}

oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream* stream,
                                                   void* audioData,
                                                   int32_t numFrames) {
    // Real-time thread: atomics and the SPSC ring only.
    cbStats_.recordCallback(nowNanos());
    const auto* samples = static_cast<const float*>(audioData);
    const std::size_t n =
        static_cast<std::size_t>(numFrames) *
        static_cast<std::size_t>(stream->getChannelCount());
    ring_->write(samples, n);
    framesRead_.fetch_add(numFrames, std::memory_order_relaxed);
    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::onErrorAfterClose(oboe::AudioStream* /*stream*/,
                                    oboe::Result error) {
    ALOGW("stream error (disconnect?): %s", oboe::convertToText(error));
    lastError_.store(static_cast<int32_t>(error));
    running_.store(false);
}

void AudioEngine::drainAndProcessLocked() {
    if (!ring_ || channelCount_ <= 0) return;
    const std::size_t drained = ring_->read(scratch_.data(), scratch_.size());
    if (drained == 0) return;
    const std::size_t frames = drained / static_cast<std::size_t>(channelCount_);
    spl_.process(scratch_.data(), frames, channelCount_, 0);
    spectrum_.feed(scratch_.data(), frames, channelCount_, 0);
    const int chToMeasure = channelCount_ > 2 ? 2 : channelCount_;
    for (int ch = 0; ch < chToMeasure; ++ch) {
        const auto lv = dsp::computeLevels(scratch_.data(), frames,
                                           channelCount_, ch);
        rmsDbfs_[ch] = dsp::toDbfs(lv.rms);
        peakDbfs_[ch] = dsp::toDbfs(lv.peak);
    }
}

double AudioEngine::measuredSampleRateHz() const {
    // Caller holds mutex_.
    if (timestamps_.size() < kMinTimestampsForRate) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double t0 = static_cast<double>(timestamps_.front().timeNanos);
    const double spanSec =
        (static_cast<double>(timestamps_.back().timeNanos) - t0) / 1e9;
    if (spanSec < kMinTimestampSpanSec) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    // Least squares on (t seconds, framePosition), mean-centered for
    // numerical stability.
    double meanT = 0, meanF = 0;
    for (const auto& p : timestamps_) {
        meanT += (static_cast<double>(p.timeNanos) - t0) / 1e9;
        meanF += static_cast<double>(p.framePosition);
    }
    const double n = static_cast<double>(timestamps_.size());
    meanT /= n;
    meanF /= n;
    double num = 0, den = 0;
    for (const auto& p : timestamps_) {
        const double dt = (static_cast<double>(p.timeNanos) - t0) / 1e9 - meanT;
        const double df = static_cast<double>(p.framePosition) - meanF;
        num += dt * df;
        den += dt * dt;
    }
    if (den <= 0) return std::numeric_limits<double>::quiet_NaN();
    return num / den;
}

void AudioEngine::snapshot(double* out, std::size_t n) {
    if (n < kSnapshotSize) return;
    for (std::size_t i = 0; i < kSnapshotSize; ++i) out[i] = 0.0;

    std::lock_guard<std::mutex> lock(mutex_);
    const bool running = running_.load() && stream_ != nullptr;
    out[kRunning] = running ? 1.0 : 0.0;
    out[kLastErrorCode] = lastError_.load();
    out[kMeasuredSampleRateHz] = std::numeric_limits<double>::quiet_NaN();
    out[kClockDriftPpm] = std::numeric_limits<double>::quiet_NaN();
    out[kMmapUsed] = -1.0;
    out[kSplWeighting] = splWeighting_;
    out[kSplTimeWeighting] = splTimeWeighting_;
    for (int f : {kSplInstantDb, kSplLeqDb, kSplLmaxDb, kSplLminDb, kSplL10Db,
                  kSplL50Db, kSplL90Db}) {
        out[f] = std::numeric_limits<double>::quiet_NaN();
    }
    if (!running) return;

    out[kAudioApi] = audioApi_;
    out[kSampleRateNominal] = sampleRateNominal_;
    out[kFramesPerBurst] = framesPerBurst_;
    out[kBufferSizeFrames] = stream_->getBufferSizeInFrames();
    out[kBufferCapacityFrames] = bufferCapacity_;
    out[kChannelCount] = channelCount_;
    out[kPerformanceMode] = performanceMode_;
    out[kSharingMode] = sharingMode_;
    out[kDeviceId] = deviceId_;
    out[kMmapUsed] = mmapUsed_;
    out[kInputPresetActual] = inputPresetActual_;

    const auto xruns = stream_->getXRunCount();
    out[kXRunCount] = xruns ? static_cast<double>(xruns.value()) : -1.0;
    out[kFramesRead] = static_cast<double>(framesRead_.load());

    const auto cb = cbStats_.summarize();
    out[kCallbackCount] = static_cast<double>(cb.callbackCount);
    out[kCbIntervalMeanMs] = cb.meanMs;
    out[kCbIntervalMinMs] = cb.minMs;
    out[kCbIntervalMaxMs] = cb.maxMs;
    out[kCbIntervalP99Ms] = cb.p99Ms;

    // Collect one hardware timestamp per snapshot poll; the regression over
    // the window gives the true ADC clock rate vs CLOCK_MONOTONIC.
    const auto ts = stream_->getTimestamp(CLOCK_MONOTONIC);
    if (ts) {
        timestamps_.push_back({ts.value().position, ts.value().timestamp});
        if (timestamps_.size() > kMaxTimestamps) timestamps_.pop_front();
    }
    out[kTimestampCount] = static_cast<double>(timestamps_.size());
    const double measured = measuredSampleRateHz();
    out[kMeasuredSampleRateHz] = measured;
    if (!std::isnan(measured) && sampleRateNominal_ > 0) {
        out[kClockDriftPpm] =
            (measured / sampleRateNominal_ - 1.0) * 1e6;
    }

    drainAndProcessLocked();
    out[kRmsDbfsCh0] = rmsDbfs_[0];
    out[kPeakDbfsCh0] = peakDbfs_[0];
    out[kRmsDbfsCh1] = channelCount_ > 1 ? rmsDbfs_[1]
                                         : std::numeric_limits<double>::quiet_NaN();
    out[kPeakDbfsCh1] = channelCount_ > 1 ? peakDbfs_[1]
                                          : std::numeric_limits<double>::quiet_NaN();

    const auto spl = spl_.stats();
    out[kSplWeighting] = splWeighting_;
    out[kSplTimeWeighting] = splTimeWeighting_;
    out[kSplInstantDb] = spl.instantDb;
    out[kSplLeqDb] = spl.leqDb;
    out[kSplLmaxDb] = spl.lmaxDb;
    out[kSplLminDb] = spl.lminDb;
    out[kSplL10Db] = spl.l10Db;
    out[kSplL50Db] = spl.l50Db;
    out[kSplL90Db] = spl.l90Db;
    out[kSplElapsedSec] = spl.elapsedSec;
}

}  // namespace aa
