#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "weighting.h"

namespace aa::dsp {

enum class TimeWeighting : int {
    Fast = 0,     // tau = 125 ms
    Slow = 1,     // tau = 1 s
    Impulse = 2,  // rise tau = 35 ms, decay tau = 1.5 s
};

// Sound-level engine: frequency weighting -> squared exponential detector ->
// statistics. All levels are in weighted dBFS (power convention: a
// full-scale sine reads -3.01 dB); absolute SPL is obtained by adding the
// calibration offset on the Kotlin side.
//
// Single-channel. Not thread-safe: feed it from one thread.
class SplProcessor {
public:
    // (Re)configures and clears everything, including filter state.
    void configure(double sampleRateHz, Weighting w, TimeWeighting tw);

    // Clears the statistics (Leq, Lmax/Lmin, percentiles, elapsed) but keeps
    // the filter/detector state so the level display doesn't glitch.
    void resetStats();

    // Consumes one channel of an interleaved buffer.
    void process(const float* interleaved, std::size_t frames,
                 int channelCount, int channel);

    struct Stats {
        double instantDb;   // time-weighted level (LAF/LAS/LAI style)
        double leqDb;       // energy average since reset
        double lmaxDb;      // max of time-weighted level since reset
        double lminDb;      // min of time-weighted level since reset
        double l10Db;       // level exceeded 10 % of the time
        double l50Db;
        double l90Db;
        double elapsedSec;  // audio time since reset
        // NaN for values that are not yet defined (warm-up / no data).
    };
    Stats stats() const;

    Weighting weighting() const { return weighting_; }
    TimeWeighting timeWeighting() const { return timeWeighting_; }

private:
    void pushHistogram(double powerDb);
    double percentileDb(double exceededFraction) const;

    // Histogram of instantaneous levels for LN percentiles:
    // 0.1 dB bins covering -150..0 dBFS.
    static constexpr int kHistBins = 1500;
    static constexpr double kHistMinDb = -150.0;
    static constexpr double kHistStepDb = 0.1;

    double fs_ = 48000.0;
    Weighting weighting_ = Weighting::Z;
    TimeWeighting timeWeighting_ = TimeWeighting::Fast;
    BiquadCascade filter_;

    double alphaAttack_ = 0;   // detector coefficient (rising)
    double alphaDecay_ = 0;    // detector coefficient (falling)
    double detector_ = 0;      // detector state (mean-square power)
    bool detectorPrimed_ = false;

    // Statistics.
    double sumSquares_ = 0;
    std::uint64_t sampleCount_ = 0;
    double maxPower_ = 0;
    double minPower_ = 0;
    bool extremaPrimed_ = false;
    std::uint64_t warmupSamplesLeft_ = 0;

    std::array<std::uint32_t, kHistBins> hist_{};
    std::uint64_t histCount_ = 0;
    std::uint64_t samplesUntilHistPush_ = 0;
    std::uint64_t histIntervalSamples_ = 0;
};

}  // namespace aa::dsp
