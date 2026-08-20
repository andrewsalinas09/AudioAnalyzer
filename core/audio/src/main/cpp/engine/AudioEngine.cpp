#include "AudioEngine.h"

#include <android/log.h>
#include <oboe/OboeExtensions.h>
#include <time.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "deconvolve.h"
#include "levels.h"
#include "syncframe.h"

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

// --- Generator ---

int32_t AudioEngine::openOutputLocked(int32_t deviceId) {
    if (outStream_) {
        outStream_->stop();
        outStream_->close();
        outStream_.reset();
    }
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setUsage(oboe::Usage::Media)
        ->setFormat(oboe::AudioFormat::Float)
        ->setFormatConversionAllowed(true)
        ->setDataCallback(&outCallback_)
        ->setErrorCallback(&outCallback_);
    if (deviceId > 0) builder.setDeviceId(deviceId);

    const oboe::Result result = builder.openStream(outStream_);
    if (result != oboe::Result::OK) {
        ALOGW("output openStream failed: %s", oboe::convertToText(result));
        outStream_.reset();
        return static_cast<int32_t>(result);
    }
    outSampleRate_ = outStream_->getSampleRate();
    outChannels_ = outStream_->getChannelCount();
    // RT rule: the callback must never allocate, so size the mono scratch
    // for the largest burst we could be asked for.
    outScratch_.assign(
        static_cast<std::size_t>(
            std::max(outStream_->getBufferCapacityInFrames(), 8192)),
        0.0f);
    return 0;
}

int32_t AudioEngine::genStartTone(int32_t deviceId, int32_t kind,
                                  double freqHz, double levelDb) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int32_t rc = openOutputLocked(deviceId);
    if (rc != 0) return rc;

    synth_.configure(outSampleRate_,
                     static_cast<dsp::ToneSynth::Kind>(kind));
    synth_.setFrequency(freqHz);
    synth_.setAmplitude(std::pow(10.0, levelDb / 20.0));
    genKind_.store(kind);
    genDurSec_ = 0.0;
    outMode_.store(1);
    genRunning_.store(true);

    const oboe::Result r = outStream_->requestStart();
    if (r != oboe::Result::OK) {
        genStopLocked();
        return static_cast<int32_t>(r);
    }
    ALOGI("gen tone started: kind=%d freq=%.1f level=%.1f rate=%d ch=%d",
          kind, freqHz, levelDb, outSampleRate_, outChannels_);
    return 0;
}

int32_t AudioEngine::genStartSweep(int32_t deviceId, bool exponential,
                                   double f1, double f2, double durationSec,
                                   double levelDb, bool syncFrame) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int32_t rc = openOutputLocked(deviceId);
    if (rc != 0) return rc;

    const double amp = std::pow(10.0, levelDb / 20.0);
    std::vector<float> payload =
        exponential
            ? dsp::renderExpSweep(outSampleRate_, f1, f2, durationSec, amp)
            : dsp::renderLinSweep(outSampleRate_, f1, f2, durationSec, amp);
    if (syncFrame) {
        playBuffer_ = dsp::wrapWithSyncFrame(payload, outSampleRate_,
                                             dsp::SyncFrameSpec{}, amp);
    } else {
        playBuffer_ = std::move(payload);
    }
    playPos_.store(0);
    genKind_.store(exponential ? kGenSweepExp : kGenSweepLin);
    genDurSec_ = static_cast<double>(playBuffer_.size()) / outSampleRate_;
    outMode_.store(2);
    genRunning_.store(true);

    const oboe::Result r = outStream_->requestStart();
    if (r != oboe::Result::OK) {
        genStopLocked();
        return static_cast<int32_t>(r);
    }
    ALOGI("gen sweep started: %s %.0f-%.0f Hz %.1fs sync=%d total=%.2fs",
          exponential ? "exp" : "lin", f1, f2, durationSec, syncFrame ? 1 : 0,
          genDurSec_);
    return 0;
}

void AudioEngine::genSetTone(double freqHz, double levelDb) {
    // Atomics only; safe without the lock.
    synth_.setFrequency(freqHz);
    synth_.setAmplitude(std::pow(10.0, levelDb / 20.0));
}

void AudioEngine::genStopLocked() {
    outMode_.store(0);
    genRunning_.store(false);
    genKind_.store(-1);
    if (outStream_) {
        outStream_->stop();
        outStream_->close();
        outStream_.reset();
    }
}

void AudioEngine::genStop() {
    std::lock_guard<std::mutex> lock(mutex_);
    genStopLocked();
}

oboe::DataCallbackResult AudioEngine::OutputCallback::onAudioReady(
    oboe::AudioStream* stream, void* audioData, int32_t numFrames) {
    auto* e = owner_;
    auto* out = static_cast<float*>(audioData);
    const int ch = stream->getChannelCount();
    const auto frames = static_cast<std::size_t>(numFrames);
    const int mode = e->outMode_.load(std::memory_order_relaxed);

    if (mode == 1) {
        // Continuous synth: render mono in scratch-sized chunks, duplicate
        // into all channels.
        std::size_t done = 0;
        while (done < frames) {
            const std::size_t n =
                std::min(frames - done, e->outScratch_.size());
            e->synth_.render(e->outScratch_.data(), n);
            for (std::size_t i = 0; i < n; ++i) {
                for (int c = 0; c < ch; ++c) {
                    out[(done + i) * static_cast<std::size_t>(ch) +
                        static_cast<std::size_t>(c)] = e->outScratch_[i];
                }
            }
            done += n;
        }
        return oboe::DataCallbackResult::Continue;
    }

    if (mode == 2) {
        const std::size_t pos = e->playPos_.load(std::memory_order_relaxed);
        const std::size_t remaining =
            pos < e->playBuffer_.size() ? e->playBuffer_.size() - pos : 0;
        const std::size_t n = std::min(frames, remaining);
        for (std::size_t i = 0; i < n; ++i) {
            const float v = e->playBuffer_[pos + i];
            for (int c = 0; c < ch; ++c) {
                out[i * static_cast<std::size_t>(ch) +
                    static_cast<std::size_t>(c)] = v;
            }
        }
        // Zero-fill the tail after the signal ends.
        for (std::size_t i = n * static_cast<std::size_t>(ch);
             i < frames * static_cast<std::size_t>(ch); ++i) {
            out[i] = 0.0f;
        }
        e->playPos_.store(pos + n, std::memory_order_relaxed);
        if (n < frames) {
            e->genRunning_.store(false);
            e->outMode_.store(0);
            return oboe::DataCallbackResult::Stop;
        }
        return oboe::DataCallbackResult::Continue;
    }

    // Off: silence.
    std::fill(out, out + frames * static_cast<std::size_t>(ch), 0.0f);
    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::OutputCallback::onErrorAfterClose(
    oboe::AudioStream* /*stream*/, oboe::Result error) {
    ALOGW("output stream error: %s", oboe::convertToText(error));
    owner_->genRunning_.store(false);
    owner_->outMode_.store(0);
}

// --- IR measurement ---

int32_t AudioEngine::irBeginCapture(double seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_.load() || !stream_) return -2;
    irCapture_.clear();
    irCaptureTarget_ = static_cast<std::size_t>(seconds * sampleRateNominal_);
    irCapture_.reserve(irCaptureTarget_);
    irCaptureFs_ = sampleRateNominal_;
    irState_.store(1);
    irCapturing_.store(true);
    return 0;
}

void AudioEngine::irAbort() {
    std::lock_guard<std::mutex> lock(mutex_);
    irCapturing_.store(false);
    irState_.store(0);
    irCapture_.clear();
}

double AudioEngine::irCapturedSec() const {
    // irCapture_.size() written under mutex_, read racily for progress only.
    return static_cast<double>(irCapture_.size()) / irCaptureFs_;
}

int32_t AudioEngine::irAnalyze(double f1, double f2, double durationSec) {
    std::vector<float> cap;
    double fs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (irState_.load() != 2) return -2;
        cap = std::move(irCapture_);
        irCapture_.clear();
        fs = irCaptureFs_;
        irState_.store(3);
    }

    // Heavy pipeline without holding the engine lock (audio keeps running).
    const auto reference = dsp::renderExpSweep(fs, f1, f2, durationSec, 1.0);
    const dsp::SyncFrameSpec spec;
    const auto det = dsp::detectSyncFrameFft(cap, fs, spec, reference.size());
    if (!det.found) {
        ALOGW("ir: sync frame not found (peaks %.2f/%.2f)", det.preamblePeak,
              det.postamblePeak);
        irState_.store(-1);
        return -1;
    }

    const double ppm = (det.clockRatio - 1.0) * 1e6;
    const std::vector<float>& corrected =
        std::fabs(ppm) > 5.0 ? dsp::resampleLinear(cap, det.clockRatio) : cap;
    const double preCorrected = det.preambleStart / det.clockRatio;

    const double lead = 0.05;  // shown before the direct sound
    const double tail = 1.5;   // room decay after the sweep
    const auto payloadStart = static_cast<std::size_t>(
        preCorrected + static_cast<double>(spec.chirpSamples(fs)) +
        static_cast<double>(spec.guardSamples(fs)));
    const auto leadN = static_cast<std::size_t>(lead * fs);
    const std::size_t from = payloadStart > leadN ? payloadStart - leadN : 0;
    const std::size_t to =
        std::min(corrected.size(),
                 payloadStart + reference.size() +
                     static_cast<std::size_t>(tail * fs));
    if (to <= from + reference.size() / 2) {
        irState_.store(-1);
        return -1;
    }
    const std::vector<float> segment(corrected.begin() + static_cast<long>(from),
                                     corrected.begin() + static_cast<long>(to));
    auto ir = dsp::deconvolve(
        segment, reference,
        leadN + static_cast<std::size_t>((tail + 0.2) * fs));

    // Coherent averaging across repetitions: align each new IR to the
    // running average with sub-sample precision, then accumulate. Metrics
    // and the frequency response are computed from the average.
    std::vector<float> averaged;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (irAvgCount_ == 0 || irAccum_.size() != ir.size()) {
            irAccum_.assign(ir.begin(), ir.end());
            irAvgCount_ = 1;
        } else {
            std::vector<float> accumF(irAccum_.size());
            for (std::size_t i = 0; i < irAccum_.size(); ++i) {
                accumF[i] = static_cast<float>(irAccum_[i] / irAvgCount_);
            }
            const auto aligned = dsp::alignTo(accumF, ir, 96);
            for (std::size_t i = 0; i < irAccum_.size(); ++i) {
                irAccum_[i] += aligned[i];
            }
            ++irAvgCount_;
        }
        averaged.resize(irAccum_.size());
        for (std::size_t i = 0; i < irAccum_.size(); ++i) {
            averaged[i] = static_cast<float>(irAccum_[i] / irAvgCount_);
        }
    }

    auto metrics = dsp::analyzeIr(averaged, fs);

    constexpr int kMagFft = 16384;
    std::vector<float> magDb(kMagFft / 2 + 1), gdMs(kMagFft / 2 + 1);
    dsp::irFrequencyResponse(averaged, fs, kMagFft, 0.005, 0.5, magDb.data(),
                             gdMs.data());

    {
        std::lock_guard<std::mutex> lock(mutex_);
        ir_ = std::move(averaged);
        irMetrics_ = metrics;
        irMagDb_ = std::move(magDb);
        irGdMs_ = std::move(gdMs);
        irMagBinHz_ = fs / kMagFft;
        irDriftPpm_ = ppm;
        irPreQ_ = det.preamblePeak;
        irPostQ_ = det.postamblePeak;
        irCaptureFs_ = fs;
        irState_.store(4);
    }
    ALOGI("ir: done, peak %.0f, T20 %.3f s, drift %.1f ppm", metrics.peakSample,
          metrics.t20Sec, ppm);
    return 0;
}

void AudioEngine::irSummary(double* out, std::size_t n) {
    if (n < kIrSummarySize) return;
    std::lock_guard<std::mutex> lock(mutex_);
    out[0] = irCaptureFs_;
    out[1] = static_cast<double>(irCapture_.size()) / irCaptureFs_;
    out[2] = irMetrics_.peakSample;
    out[3] = irMetrics_.peakDb;
    out[4] = irMetrics_.edtSec;
    out[5] = irMetrics_.t20Sec;
    out[6] = irMetrics_.t30Sec;
    out[7] = irMetrics_.c50Db;
    out[8] = irMetrics_.c80Db;
    out[9] = irDriftPpm_;
    out[10] = irPreQ_;
    out[11] = irPostQ_;
    out[12] = static_cast<double>(ir_.size());
    out[13] = static_cast<double>(irMagDb_.size());
    out[14] = irMagBinHz_;
    out[15] = irAvgCount_;
}

void AudioEngine::irResetAverage() {
    std::lock_guard<std::mutex> lock(mutex_);
    irAccum_.clear();
    irAvgCount_ = 0;
}

int32_t AudioEngine::irEtc(float* out, int32_t n) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ir_.empty() || n <= 0) return 0;
    dsp::etcTrace(ir_, out, static_cast<std::size_t>(n));
    return n;
}

int32_t AudioEngine::irGet(float* out, int32_t maxN) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto n = static_cast<int32_t>(ir_.size());
    if (n == 0 || n > maxN) return 0;
    std::copy(ir_.begin(), ir_.end(), out);
    return n;
}

int32_t AudioEngine::irMag(float* magDb, float* gdMs, int32_t maxBins) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto nb = static_cast<int32_t>(irMagDb_.size());
    if (nb == 0 || nb > maxBins) return 0;
    std::copy(irMagDb_.begin(), irMagDb_.end(), magDb);
    std::copy(irGdMs_.begin(), irGdMs_.end(), gdMs);
    return nb;
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
    if (irCapturing_.load(std::memory_order_relaxed)) {
        const std::size_t room = irCaptureTarget_ - irCapture_.size();
        const std::size_t take = std::min(room, frames);
        for (std::size_t i = 0; i < take; ++i) {
            irCapture_.push_back(
                scratch_[i * static_cast<std::size_t>(channelCount_)]);
        }
        if (irCapture_.size() >= irCaptureTarget_) {
            irCapturing_.store(false);
            irState_.store(2);
        }
    }
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
    // Generator status is valid regardless of the input stream's state.
    out[kGenRunning] = genRunning_.load() ? 1.0 : 0.0;
    out[kGenKind] = genKind_.load();
    out[kGenPosSec] =
        (outMode_.load() == 2 && outSampleRate_ > 0)
            ? static_cast<double>(playPos_.load()) / outSampleRate_
            : 0.0;
    out[kGenDurSec] = genDurSec_;
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
