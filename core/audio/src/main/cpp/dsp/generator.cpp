#include "generator.h"

#include <cmath>

namespace aa::dsp {

namespace {
constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

void applyFades(std::vector<float>& x, double fs, double fadeSec) {
    const std::size_t nf = static_cast<std::size_t>(fadeSec * fs);
    if (nf == 0 || x.size() < 2 * nf) return;
    for (std::size_t i = 0; i < nf; ++i) {
        const float g = static_cast<float>(
            0.5 * (1.0 - std::cos(3.14159265358979323846 * i / nf)));
        x[i] *= g;
        x[x.size() - 1 - i] *= g;
    }
}
}  // namespace

void ToneSynth::configure(double sampleRateHz, Kind kind) {
    fs_ = sampleRateHz;
    kind_ = kind;
    phase_ = 0.0;
    ampCurrent_ = 0.0;  // fade in from silence
    for (auto& s : pk_) s = 0.0;
}

float ToneSynth::nextWhite() {
    // xorshift64*: fast, decent spectrum, RT-safe.
    rng_ ^= rng_ >> 12;
    rng_ ^= rng_ << 25;
    rng_ ^= rng_ >> 27;
    const std::uint64_t v = rng_ * 2685821657736338717ULL;
    return static_cast<float>(
               static_cast<double>(v >> 11) / 9007199254740992.0) *
               2.0f - 1.0f;
}

void ToneSynth::render(float* out, std::size_t frames) {
    const double ampTarget = ampTarget_.load(std::memory_order_relaxed);
    const double ampStep =
        frames > 0 ? (ampTarget - ampCurrent_) / static_cast<double>(frames) : 0.0;
    const double freq = freqHz_.load(std::memory_order_relaxed);
    const double phaseInc = kTwoPi * freq / fs_;

    for (std::size_t i = 0; i < frames; ++i) {
        double s;
        switch (kind_) {
            case Kind::Sine:
                s = std::sin(phase_);
                phase_ += phaseInc;
                if (phase_ > kTwoPi) phase_ -= kTwoPi;
                break;
            case Kind::White:
                s = nextWhite();
                break;
            case Kind::Pink:
            default: {
                // Paul Kellet's pink filter (-3 dB/oct, ~+-0.5 dB accuracy
                // over the audio band), scaled to roughly unity peak.
                const double w = nextWhite();
                pk_[0] = 0.99886 * pk_[0] + w * 0.0555179;
                pk_[1] = 0.99332 * pk_[1] + w * 0.0750759;
                pk_[2] = 0.96900 * pk_[2] + w * 0.1538520;
                pk_[3] = 0.86650 * pk_[3] + w * 0.3104856;
                pk_[4] = 0.55000 * pk_[4] + w * 0.5329522;
                pk_[5] = -0.7616 * pk_[5] - w * 0.0168980;
                s = (pk_[0] + pk_[1] + pk_[2] + pk_[3] + pk_[4] + pk_[5] +
                     pk_[6] + w * 0.5362) *
                    0.11;
                pk_[6] = w * 0.115926;
                break;
            }
        }
        ampCurrent_ += ampStep;
        out[i] = static_cast<float>(s * ampCurrent_);
    }
    ampCurrent_ = ampTarget;
}

std::vector<float> renderExpSweep(double fs, double f1, double f2,
                                  double durationSec, double amplitude,
                                  double fadeSec) {
    const std::size_t n = static_cast<std::size_t>(durationSec * fs);
    std::vector<float> x(n);
    const double L = durationSec / std::log(f2 / f1);
    const double K = kTwoPi * f1 * L;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        x[i] = static_cast<float>(amplitude *
                                  std::sin(K * (std::exp(t / L) - 1.0)));
    }
    applyFades(x, fs, fadeSec);
    return x;
}

std::vector<float> renderLinSweep(double fs, double f1, double f2,
                                  double durationSec, double amplitude,
                                  double fadeSec) {
    const std::size_t n = static_cast<std::size_t>(durationSec * fs);
    std::vector<float> x(n);
    const double k = (f2 - f1) / durationSec;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        x[i] = static_cast<float>(
            amplitude * std::sin(kTwoPi * (f1 * t + 0.5 * k * t * t)));
    }
    applyFades(x, fs, fadeSec);
    return x;
}

}  // namespace aa::dsp
