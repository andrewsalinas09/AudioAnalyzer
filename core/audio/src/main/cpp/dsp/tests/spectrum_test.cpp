// Tests for the FFT wrapper and RTA spectrum normalization.
#include "check.h"
#include <cmath>
#include <cstdio>
#include <vector>

#include "fft.h"
#include "spectrum.h"

using aa::dsp::RealFft;
using aa::dsp::SpectrumProcessor;
using aa::dsp::WindowType;

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 48000.0;

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// Deterministic uniform noise in [-1, 1) (LCG; no std::random to keep runs
// bit-identical across platforms).
struct Lcg {
    std::uint64_t s = 0x853c49e6748fea9bULL;
    float next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<float>(
                   static_cast<double>(s >> 11) / 9007199254740992.0) *
                   2.0f - 1.0f;
    }
};

void feedAndCompute(SpectrumProcessor& p, const std::vector<float>& x) {
    // Feed in chunks and compute as an engine poll would.
    const std::size_t chunk = 4800;
    for (std::size_t i = 0; i < x.size(); i += chunk) {
        const std::size_t n = std::min(chunk, x.size() - i);
        p.feed(x.data() + i, n, 1, 0);
        p.compute();
    }
}
}  // namespace

int main() {
    // FFT wrapper vs naive DFT (N = 64, arbitrary signal): validates the
    // ordered output layout and scaling assumptions.
    {
        constexpr int n = 64;
        RealFft fft(n);
        std::vector<float> x(n);
        Lcg rng;
        for (auto& v : x) v = rng.next();
        std::vector<float> out(n);
        fft.forward(x.data(), out.data());

        for (int k = 0; k <= n / 2; ++k) {
            double re = 0, im = 0;
            for (int i = 0; i < n; ++i) {
                const double ph = -2.0 * kPi * k * i / n;
                re += x[static_cast<std::size_t>(i)] * std::cos(ph);
                im += x[static_cast<std::size_t>(i)] * std::sin(ph);
            }
            double gotRe, gotIm;
            if (k == 0) {
                gotRe = out[0];
                gotIm = 0;
            } else if (k == n / 2) {
                gotRe = out[1];
                gotIm = 0;
            } else {
                gotRe = out[static_cast<std::size_t>(2 * k)];
                gotIm = out[static_cast<std::size_t>(2 * k + 1)];
            }
            CHECK(near(gotRe, re, 1e-3));
            CHECK(near(gotIm, im, 1e-3));
        }
    }

    // Amplitude normalization: a full-scale bin-centered sine reads 0 dBFS
    // at its bin for every window type.
    for (const auto wt : {WindowType::Rectangular, WindowType::Hann,
                          WindowType::FlatTop}) {
        constexpr int n = 4096;
        SpectrumProcessor p;
        p.configure(kFs, n, wt, 0.0);
        const int bin = 100;
        const double f = bin * kFs / n;
        std::vector<float> x(3 * n);
        for (std::size_t i = 0; i < x.size(); ++i) {
            x[i] = static_cast<float>(std::sin(2.0 * kPi * f * i / kFs));
        }
        feedAndCompute(p, x);
        std::vector<float> db(static_cast<std::size_t>(p.bins()));
        CHECK(p.readAverage(db.data(), false));
        CHECK(near(db[bin], 0.0, 0.05));
    }

    // Flat-top: amplitude stays accurate even between bins (that's its job).
    {
        constexpr int n = 4096;
        SpectrumProcessor p;
        p.configure(kFs, n, WindowType::FlatTop, 0.0);
        const double f = 100.5 * kFs / n;  // worst case: half-bin offset
        std::vector<float> x(3 * n);
        for (std::size_t i = 0; i < x.size(); ++i) {
            x[i] = static_cast<float>(std::sin(2.0 * kPi * f * i / kFs));
        }
        feedAndCompute(p, x);
        std::vector<float> db(static_cast<std::size_t>(p.bins()));
        CHECK(p.readAverage(db.data(), false));
        const float peak = std::max(db[100], db[101]);
        CHECK(near(peak, 0.0, 0.1));
    }

    // PSD normalization: white noise of variance 1/3 must read
    // 10*log10((1/3)/(fs/2)) dBFS/Hz, flat, for both windows.
    for (const auto wt : {WindowType::Rectangular, WindowType::Hann}) {
        constexpr int n = 4096;
        SpectrumProcessor p;
        p.configure(kFs, n, wt, 2.0);  // average over many hops
        Lcg rng;
        std::vector<float> x(static_cast<std::size_t>(200) * (n / 2));
        for (auto& v : x) v = rng.next();
        feedAndCompute(p, x);
        std::vector<float> db(static_cast<std::size_t>(p.bins()));
        CHECK(p.readAverage(db.data(), true));
        // Average mid-band bins in dB (edges excluded).
        double sum = 0;
        int cnt = 0;
        for (int k = 64; k < p.bins() - 64; ++k) {
            sum += db[static_cast<std::size_t>(k)];
            ++cnt;
        }
        const double expected = 10.0 * std::log10((1.0 / 3.0) / (kFs / 2.0));
        const double got = sum / cnt;
        std::printf("PSD white noise (window %d): got %.2f expected %.2f\n",
                    static_cast<int>(wt), got, expected);
        CHECK(near(got, expected, 0.3));
    }

    // Peak hold: survives silence; reset clears it.
    {
        constexpr int n = 4096;
        SpectrumProcessor p;
        // 0.25 s averaging; ~4.3 s of silence below gives the exponential
        // average ~100 hops to decay (>70 dB) while the peak trace holds.
        p.configure(kFs, n, WindowType::Hann, 0.25);
        const int bin = 200;
        const double f = bin * kFs / n;
        std::vector<float> loud(3 * n), quiet(50 * n, 0.0f);
        for (std::size_t i = 0; i < loud.size(); ++i) {
            loud[i] = static_cast<float>(std::sin(2.0 * kPi * f * i / kFs));
        }
        feedAndCompute(p, loud);
        feedAndCompute(p, quiet);
        std::vector<float> avg(static_cast<std::size_t>(p.bins()));
        std::vector<float> pk(static_cast<std::size_t>(p.bins()));
        CHECK(p.readAverage(avg.data(), false));
        CHECK(p.readPeak(pk.data()));
        CHECK(pk[bin] > -1.0);       // peak remembers the tone
        CHECK(avg[bin] < -40.0);     // average has decayed
        p.resetPeak();
        CHECK(p.readPeak(pk.data()));
        CHECK(pk[bin] < -100.0);
    }

    std::puts("spectrum_test: all assertions passed");
    return 0;
}
