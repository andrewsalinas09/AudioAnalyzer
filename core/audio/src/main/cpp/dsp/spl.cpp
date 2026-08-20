#include "spl.h"

#include <cmath>
#include <limits>

namespace aa::dsp {

namespace {
constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kPowerFloor = 1e-20;  // -200 dB

double powerToDb(double power) {
    if (power < kPowerFloor) power = kPowerFloor;
    return 10.0 * std::log10(power);
}

double detectorAlpha(double tauSec, double fs) {
    return 1.0 - std::exp(-1.0 / (tauSec * fs));
}
}  // namespace

void SplProcessor::configure(double sampleRateHz, Weighting w, TimeWeighting tw) {
    fs_ = sampleRateHz;
    weighting_ = w;
    timeWeighting_ = tw;
    filter_ = designWeighting(w, fs_);

    double tauRise, tauFall;
    switch (tw) {
        case TimeWeighting::Slow:    tauRise = tauFall = 1.0;   break;
        case TimeWeighting::Impulse: tauRise = 0.035; tauFall = 1.5; break;
        case TimeWeighting::Fast:
        default:                     tauRise = tauFall = 0.125; break;
    }
    alphaAttack_ = detectorAlpha(tauRise, fs_);
    alphaDecay_ = detectorAlpha(tauFall, fs_);

    detector_ = 0;
    detectorPrimed_ = false;
    histIntervalSamples_ = static_cast<std::uint64_t>(fs_ / 10.0);  // 10 Hz
    resetStats();
}

void SplProcessor::resetStats() {
    sumSquares_ = 0;
    sampleCount_ = 0;
    maxPower_ = 0;
    minPower_ = 0;
    extremaPrimed_ = false;
    // Give the detector half a second to settle before tracking extrema, so
    // Lmin isn't forever pinned to the start-up transient.
    warmupSamplesLeft_ = static_cast<std::uint64_t>(fs_ / 2.0);
    hist_.fill(0);
    histCount_ = 0;
    samplesUntilHistPush_ = histIntervalSamples_;
}

void SplProcessor::process(const float* interleaved, std::size_t frames,
                           int channelCount, int channel) {
    if (interleaved == nullptr || channelCount <= 0 || channel < 0 ||
        channel >= channelCount) {
        return;
    }
    for (std::size_t i = 0; i < frames; ++i) {
        const double x = filter_.process(
            interleaved[i * static_cast<std::size_t>(channelCount) +
                        static_cast<std::size_t>(channel)]);
        const double p = x * x;

        // Exponential detector on the squared signal.
        if (!detectorPrimed_) {
            detector_ = p;
            detectorPrimed_ = true;
        } else {
            const double a = (p > detector_) ? alphaAttack_ : alphaDecay_;
            detector_ += a * (p - detector_);
        }

        sumSquares_ += p;
        ++sampleCount_;

        if (warmupSamplesLeft_ > 0) {
            --warmupSamplesLeft_;
        } else if (!extremaPrimed_) {
            maxPower_ = minPower_ = detector_;
            extremaPrimed_ = true;
        } else {
            if (detector_ > maxPower_) maxPower_ = detector_;
            if (detector_ < minPower_) minPower_ = detector_;
        }

        if (--samplesUntilHistPush_ == 0) {
            samplesUntilHistPush_ = histIntervalSamples_;
            pushHistogram(powerToDb(detector_));
        }
    }
}

void SplProcessor::pushHistogram(double powerDb) {
    int idx = static_cast<int>((powerDb - kHistMinDb) / kHistStepDb);
    if (idx < 0) idx = 0;
    if (idx >= kHistBins) idx = kHistBins - 1;
    ++hist_[static_cast<std::size_t>(idx)];
    ++histCount_;
}

double SplProcessor::percentileDb(double exceededFraction) const {
    if (histCount_ == 0) return kNan;
    // L_N = level exceeded N% of the time: walk down from the top bin until
    // the cumulative count reaches N% of all samples.
    const double target = exceededFraction * static_cast<double>(histCount_);
    std::uint64_t cum = 0;
    for (int i = kHistBins - 1; i >= 0; --i) {
        cum += hist_[static_cast<std::size_t>(i)];
        if (static_cast<double>(cum) >= target) {
            return kHistMinDb + (i + 0.5) * kHistStepDb;
        }
    }
    return kHistMinDb;
}

SplProcessor::Stats SplProcessor::stats() const {
    Stats s{};
    s.instantDb = detectorPrimed_ ? powerToDb(detector_) : kNan;
    s.leqDb = sampleCount_ > 0
                  ? powerToDb(sumSquares_ / static_cast<double>(sampleCount_))
                  : kNan;
    s.lmaxDb = extremaPrimed_ ? powerToDb(maxPower_) : kNan;
    s.lminDb = extremaPrimed_ ? powerToDb(minPower_) : kNan;
    s.l10Db = percentileDb(0.10);
    s.l50Db = percentileDb(0.50);
    s.l90Db = percentileDb(0.90);
    s.elapsedSec = static_cast<double>(sampleCount_) / fs_;
    return s;
}

}  // namespace aa::dsp
