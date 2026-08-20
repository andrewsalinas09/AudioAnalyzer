#pragma once
#include <oboe/Oboe.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "CallbackStats.h"
#include "SpscRing.h"
#include "spectrum.h"
#include "spl.h"

namespace aa {

// Snapshot layout returned to Kotlin as a flat double[]. Must stay in sync
// with EngineSnapshot.kt (single source of truth documented in both files).
enum SnapshotField : int {
    kRunning = 0,
    kAudioApi = 1,          // 0 unspecified, 1 OpenSLES, 2 AAudio
    kSampleRateNominal = 2,
    kFramesPerBurst = 3,
    kBufferSizeFrames = 4,
    kBufferCapacityFrames = 5,
    kChannelCount = 6,
    kPerformanceMode = 7,   // oboe::PerformanceMode enum value
    kSharingMode = 8,       // oboe::SharingMode enum value
    kDeviceId = 9,
    kMmapUsed = 10,         // 1/0, -1 unknown
    kXRunCount = 11,
    kFramesRead = 12,
    kCallbackCount = 13,
    kCbIntervalMeanMs = 14,
    kCbIntervalMinMs = 15,
    kCbIntervalMaxMs = 16,
    kCbIntervalP99Ms = 17,
    kMeasuredSampleRateHz = 18,  // NaN until enough timestamps collected
    kClockDriftPpm = 19,         // NaN until enough timestamps collected
    kTimestampCount = 20,
    kRmsDbfsCh0 = 21,
    kPeakDbfsCh0 = 22,
    kRmsDbfsCh1 = 23,
    kPeakDbfsCh1 = 24,
    kLastErrorCode = 25,    // oboe::Result as int, 0 = OK
    kInputPresetActual = 26,
    // SPL engine (channel 0, weighted dBFS; Kotlin adds the cal offset).
    kSplWeighting = 27,      // dsp::Weighting enum value
    kSplTimeWeighting = 28,  // dsp::TimeWeighting enum value
    kSplInstantDb = 29,      // time-weighted level (NaN until primed)
    kSplLeqDb = 30,
    kSplLmaxDb = 31,
    kSplLminDb = 32,
    kSplL10Db = 33,
    kSplL50Db = 34,
    kSplL90Db = 35,
    kSplElapsedSec = 36,
    kSnapshotSize = 37,
};

// Owns the Oboe input stream. The audio callback only touches the SPSC ring
// and atomics (real-time safe); everything else runs on caller threads under
// mutex_.
class AudioEngine : public oboe::AudioStreamDataCallback,
                    public oboe::AudioStreamErrorCallback {
public:
    static AudioEngine& instance();

    // Returns oboe::Result as int (0 = OK). deviceId/sampleRate/channelCount
    // of 0 mean "unspecified" (let the platform pick the native value).
    // inputPreset is an oboe::InputPreset enum value.
    int32_t start(int32_t deviceId, int32_t sampleRate, int32_t channelCount,
                  int32_t inputPreset);
    void stop();
    void snapshot(double* out, std::size_t n);

    // SPL engine configuration (applies immediately if a stream is running,
    // and to every subsequently started stream). Values are the dsp enums.
    void splConfigure(int32_t weighting, int32_t timeWeighting);
    void splResetStats();

    // RTA spectrum. Configure applies immediately if running and to later
    // starts. Read computes any due FFT frames from samples fed by
    // snapshot()'s drain, then fills avg/peak (each maxBins floats, dB) and
    // returns the bin count — 0 if not running or no frame yet.
    void spectrumConfigure(int32_t fftSize, int32_t window, double avgTauSec);
    int32_t spectrumRead(float* avg, float* peak, int32_t maxBins, bool psd);
    void spectrumResetPeak();

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream,
                                          void* audioData,
                                          int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

private:
    AudioEngine() = default;

    struct TimestampPoint {
        int64_t framePosition;
        int64_t timeNanos;
    };
    // Least-squares slope of framePosition vs time over the collected window.
    // Returns measured sample rate in Hz, or NaN if the window is too small.
    double measuredSampleRateHz() const;

    // Drains the capture ring into the SPL and spectrum processors and
    // updates the level meters. Caller must hold mutex_. Called from both
    // snapshot() (10 Hz health poll) and spectrumRead() (fast RTA poll) so
    // neither depends on the other's cadence.
    void drainAndProcessLocked();

    std::mutex mutex_;  // guards stream_ lifecycle and timestamps_
    std::shared_ptr<oboe::AudioStream> stream_;
    std::deque<TimestampPoint> timestamps_;
    std::vector<float> scratch_;  // drain buffer for level computation

    std::unique_ptr<SpscRing> ring_;
    CallbackStats cbStats_;

    // Fed from the snapshot drain (never from the audio callback).
    dsp::SplProcessor spl_;
    int32_t splWeighting_ = static_cast<int32_t>(dsp::Weighting::A);
    int32_t splTimeWeighting_ = static_cast<int32_t>(dsp::TimeWeighting::Fast);

    dsp::SpectrumProcessor spectrum_;
    int32_t spectrumFftSize_ = 8192;
    int32_t spectrumWindow_ = static_cast<int32_t>(dsp::WindowType::Hann);
    double spectrumAvgTau_ = 0.5;

    std::atomic<bool> running_{false};
    std::atomic<int64_t> framesRead_{0};
    std::atomic<int32_t> lastError_{0};

    // Cached at open time (stable for the stream's lifetime).
    int32_t sampleRateNominal_ = 0;
    int32_t channelCount_ = 0;
    int32_t framesPerBurst_ = 0;
    int32_t bufferCapacity_ = 0;
    int32_t audioApi_ = 0;
    int32_t performanceMode_ = 0;
    int32_t sharingMode_ = 0;
    int32_t deviceId_ = 0;
    int32_t mmapUsed_ = -1;
    int32_t inputPresetActual_ = 0;

    // Last computed levels (persist between polls with no new samples).
    double rmsDbfs_[2] = {-200.0, -200.0};
    double peakDbfs_[2] = {-200.0, -200.0};
};

}  // namespace aa
