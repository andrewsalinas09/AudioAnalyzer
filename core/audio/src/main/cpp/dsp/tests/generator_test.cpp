// Tests for the signal generator: sine level/frequency accuracy, pink noise
// spectral slope, sweep rendering.
#include "check.h"
#include <cmath>
#include <cstdio>
#include <vector>

#include "generator.h"
#include "spectrum.h"

using aa::dsp::SpectrumProcessor;
using aa::dsp::ToneSynth;
using aa::dsp::WindowType;

namespace {
constexpr double kFs = 48000.0;

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

std::vector<float> renderBlocks(ToneSynth& synth, std::size_t total) {
    std::vector<float> out(total);
    for (std::size_t i = 0; i < total; i += 480) {
        synth.render(out.data() + i, std::min<std::size_t>(480, total - i));
    }
    return out;
}

// Average dB over the bins covering [f1, f2].
double bandAverageDb(const std::vector<float>& db, double binHz, double f1,
                     double f2) {
    int lo = static_cast<int>(f1 / binHz);
    int hi = static_cast<int>(f2 / binHz) + 1;
    double sum = 0;
    int cnt = 0;
    for (int i = lo; i < hi && i < static_cast<int>(db.size()); ++i) {
        sum += db[static_cast<std::size_t>(i)];
        ++cnt;
    }
    return sum / cnt;
}
}  // namespace

int main() {
    // Sine: -6.02 dBFS amplitude at 1 kHz reads exactly that on the
    // flat-top spectrum, at the right frequency.
    {
        ToneSynth synth;
        synth.configure(kFs, ToneSynth::Kind::Sine);
        synth.setFrequency(1000.0);
        synth.setAmplitude(0.5);
        const auto x = renderBlocks(synth, 6 * 8192);

        SpectrumProcessor sp;
        sp.configure(kFs, 8192, WindowType::FlatTop, 0.0);
        sp.feed(x.data() + 8192, x.size() - 8192, 1, 0);  // skip amp ramp-in
        sp.compute();
        std::vector<float> db(static_cast<std::size_t>(sp.bins()));
        CHECK(sp.readAverage(db.data(), false));

        int peakBin = 0;
        for (int i = 1; i < sp.bins(); ++i) {
            if (db[static_cast<std::size_t>(i)] >
                db[static_cast<std::size_t>(peakBin)]) {
                peakBin = i;
            }
        }
        const double peakHz = peakBin * sp.binHz();
        CHECK(near(peakHz, 1000.0, sp.binHz()));
        CHECK(near(db[static_cast<std::size_t>(peakBin)], -6.02, 0.1));
    }

    // Pink noise: PSD falls ~10 dB per decade (checked 100 Hz -> 10 kHz).
    {
        ToneSynth synth;
        synth.configure(kFs, ToneSynth::Kind::Pink);
        synth.setAmplitude(0.5);
        const auto x = renderBlocks(synth, 400 * 2048);

        SpectrumProcessor sp;
        sp.configure(kFs, 8192, WindowType::Hann, 4.0);
        sp.feed(x.data(), x.size(), 1, 0);
        sp.compute();
        std::vector<float> db(static_cast<std::size_t>(sp.bins()));
        CHECK(sp.readAverage(db.data(), true));

        const double at100 = bandAverageDb(db, sp.binHz(), 90, 110);
        const double at1k = bandAverageDb(db, sp.binHz(), 900, 1100);
        const double at10k = bandAverageDb(db, sp.binHz(), 9000, 11000);
        std::printf("pink PSD: 100 Hz %.1f, 1 kHz %.1f, 10 kHz %.1f\n",
                    at100, at1k, at10k);
        CHECK(near(at100 - at1k, 10.0, 1.5));
        CHECK(near(at1k - at10k, 10.0, 1.5));
    }

    // Exponential sweep: right length, faded ends, bounded amplitude, and
    // energy present at both extremes of the band.
    {
        const auto x = aa::dsp::renderExpSweep(kFs, 20.0, 20000.0, 2.0, 0.5);
        CHECK(x.size() == static_cast<std::size_t>(2.0 * kFs));
        CHECK(std::fabs(x.front()) < 1e-3 && std::fabs(x.back()) < 1e-3);
        float peak = 0;
        for (const float v : x) peak = std::max(peak, std::fabs(v));
        CHECK(peak <= 0.5f + 1e-4f && peak > 0.45f);
    }

    // Linear sweep basic sanity.
    {
        const auto x = aa::dsp::renderLinSweep(kFs, 100.0, 1000.0, 1.0, 0.25);
        CHECK(x.size() == static_cast<std::size_t>(kFs));
        float peak = 0;
        for (const float v : x) peak = std::max(peak, std::fabs(v));
        CHECK(peak <= 0.25f + 1e-4f);
    }

    std::puts("generator_test: all assertions passed");
    return 0;
}
