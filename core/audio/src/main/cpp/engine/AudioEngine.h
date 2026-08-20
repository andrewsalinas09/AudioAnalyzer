#pragma once
#include <oboe/Oboe.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "CallbackStats.h"
#include "SpscRing.h"
#include "generator.h"
#include "ir_analysis.h"
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
    // Generator (valid regardless of the input stream's state).
    kGenRunning = 37,
    kGenKind = 38,     // GenKind values; -1 when idle
    kGenPosSec = 39,   // playback position for one-shot signals
    kGenDurSec = 40,   // 0 for continuous signals
    kSnapshotSize = 41,
};

// Generator signal kinds crossing JNI (mirrored by GenSignal in Kotlin).
enum GenKind : int {
    kGenSine = 0,
    kGenWhite = 1,
    kGenPink = 2,
    kGenSweepExp = 3,
    kGenSweepLin = 4,
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

    // Generator (output stream). Continuous tones/noise are synthesized in
    // the output callback; sweeps are pre-rendered (optionally wrapped in
    // the sync frame) and played one-shot. Returns oboe::Result as int.
    int32_t genStartTone(int32_t deviceId, int32_t kind, double freqHz,
                         double levelDb);
    int32_t genStartSweep(int32_t deviceId, bool exponential, double f1,
                          double f2, double durationSec, double levelDb,
                          bool syncFrame);
    // Live tone updates while running (click-free: level ramped, phase
    // continuous).
    void genSetTone(double freqHz, double levelDb);
    void genStop();

    // --- IR measurement ---
    // Capture states: 0 idle, 1 capturing, 2 captured, 3 analyzing, 4 done,
    // -1 sync not found, -2 invalid state/params.
    // Flow: input stream running -> irBeginCapture -> play sync-framed sweep
    // (genStartSweep) -> wait for state 2 -> irAnalyze on a worker thread ->
    // state 4 -> read results.
    int32_t irBeginCapture(double seconds);
    void irAbort();
    int32_t irState() const { return irState_.load(); }
    double irCapturedSec() const;
    // f1/f2/durationSec must match the played sweep.
    int32_t irAnalyze(double f1, double f2, double durationSec);
    // Summary layout (doubles): 0 fs, 1 capturedSec, 2 peakSample, 3 peakDb,
    // 4 edtSec, 5 t20Sec, 6 t30Sec, 7 c50Db, 8 c80Db, 9 driftPpm,
    // 10 preambleQuality, 11 postambleQuality, 12 irSamples, 13 magBins,
    // 14 magBinHz. Mirrored by IrSummary in Kotlin.
    static constexpr int kIrSummarySize = 15;
    void irSummary(double* out, std::size_t n);
    // Fills out[n] with the decimated ETC in dB (0 = IR start).
    int32_t irEtc(float* out, int32_t n);
    // Fills magnitude (dB, uncalibrated) and excess group delay (ms) of the
    // windowed IR; returns the bin count.
    int32_t irMag(float* magDb, float* gdMs, int32_t maxBins);

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

    // --- Output / generator ---
    // Separate callback object because AudioEngine's onAudioReady is the
    // input callback.
    class OutputCallback : public oboe::AudioStreamDataCallback,
                           public oboe::AudioStreamErrorCallback {
    public:
        explicit OutputCallback(AudioEngine* owner) : owner_(owner) {}
        oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream,
                                              void* audioData,
                                              int32_t numFrames) override;
        void onErrorAfterClose(oboe::AudioStream* stream,
                               oboe::Result error) override;

    private:
        AudioEngine* owner_;
    };
    friend class OutputCallback;

    int32_t openOutputLocked(int32_t deviceId);  // caller holds mutex_
    void genStopLocked();

    // --- IR measurement state ---
    std::vector<float> irCapture_;      // appended by drainAndProcessLocked
    std::size_t irCaptureTarget_ = 0;
    double irCaptureFs_ = 48000.0;
    std::atomic<bool> irCapturing_{false};
    std::atomic<int32_t> irState_{0};
    std::vector<float> ir_;             // deconvolved impulse response
    dsp::IrMetrics irMetrics_{};
    std::vector<float> irMagDb_;
    std::vector<float> irGdMs_;
    double irMagBinHz_ = 0.0;
    double irDriftPpm_ = 0.0;
    double irPreQ_ = 0.0, irPostQ_ = 0.0;

    OutputCallback outCallback_{this};
    std::shared_ptr<oboe::AudioStream> outStream_;
    dsp::ToneSynth synth_;
    std::vector<float> playBuffer_;   // mono one-shot signal
    std::vector<float> outScratch_;   // mono render scratch (RT: no alloc)
    std::atomic<int> outMode_{0};     // 0 off, 1 synth, 2 buffer
    std::atomic<std::size_t> playPos_{0};
    std::atomic<bool> genRunning_{false};
    std::atomic<int32_t> genKind_{-1};
    int32_t outSampleRate_ = 0;
    int32_t outChannels_ = 0;
    double genDurSec_ = 0.0;

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
