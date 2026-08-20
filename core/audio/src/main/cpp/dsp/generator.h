#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aa::dsp {

// Real-time-safe continuous tone/noise synthesizer. configure() is called
// from a control thread before the stream starts; render() runs on the audio
// callback (no locks, no allocation). Frequency and level changes go through
// atomics; level changes are ramped across one render block to avoid clicks
// (sine frequency changes are click-free by phase continuity).
class ToneSynth {
public:
    enum class Kind : int { Sine = 0, White = 1, Pink = 2 };

    void configure(double sampleRateHz, Kind kind);
    void setFrequency(double hz) { freqHz_.store(hz); }
    void setAmplitude(double linear) { ampTarget_.store(linear); }

    // Renders mono samples (caller duplicates into interleaved channels).
    void render(float* out, std::size_t frames);

private:
    float nextWhite();

    double fs_ = 48000.0;
    Kind kind_ = Kind::Sine;
    std::atomic<double> freqHz_{1000.0};
    std::atomic<double> ampTarget_{0.25};
    double ampCurrent_ = 0.0;
    double phase_ = 0.0;      // audio thread only
    std::uint64_t rng_ = 0x9e3779b97f4a7c15ULL;
    // Paul Kellet pink filter state (audio thread only).
    double pk_[7] = {0, 0, 0, 0, 0, 0, 0};
};

// Offline sweep rendering (control thread; result played from a buffer).
// Exponential (Farina) sweep: x(t) = sin(K * (exp(t/L) - 1)),
// L = T / ln(f2/f1), K = 2*pi*f1*L. Raised-cosine fades at both ends.
std::vector<float> renderExpSweep(double fs, double f1, double f2,
                                  double durationSec, double amplitude,
                                  double fadeSec = 0.005);

std::vector<float> renderLinSweep(double fs, double f1, double f2,
                                  double durationSec, double amplitude,
                                  double fadeSec = 0.005);

}  // namespace aa::dsp
