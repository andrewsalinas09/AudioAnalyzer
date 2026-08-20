// Host-side tests for aa_dsp levels. Assert-based for now; a proper test
// framework decision is deferred until the DSP surface grows (Phase 2).
#include "check.h"
#include <cmath>
#include <cstdio>
#include <vector>

#include "levels.h"

using aa::dsp::computeLevels;
using aa::dsp::toDbfs;

namespace {
constexpr double kPi = 3.14159265358979323846;

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
}  // namespace

int main() {
    // Full-scale sine: RMS = 1/sqrt(2) ≈ -3.0103 dBFS, peak = 0 dBFS.
    {
        constexpr std::size_t n = 48000;
        std::vector<float> mono(n);
        for (std::size_t i = 0; i < n; ++i) {
            mono[i] = static_cast<float>(std::sin(2.0 * kPi * 1000.0 * i / 48000.0));
        }
        const auto lv = computeLevels(mono.data(), n, 1, 0);
        CHECK(near(lv.rms, 1.0 / std::sqrt(2.0), 1e-4));
        CHECK(near(lv.peak, 1.0, 1e-4));
        CHECK(near(toDbfs(lv.rms), -3.0103, 1e-2));
    }

    // Interleaved stereo: channel selection must not leak across channels.
    {
        constexpr std::size_t frames = 1000;
        std::vector<float> stereo(frames * 2);
        for (std::size_t i = 0; i < frames; ++i) {
            stereo[i * 2] = 0.5f;   // ch0: DC 0.5
            stereo[i * 2 + 1] = 0.0f;  // ch1: silence
        }
        const auto ch0 = computeLevels(stereo.data(), frames, 2, 0);
        const auto ch1 = computeLevels(stereo.data(), frames, 2, 1);
        CHECK(near(ch0.rms, 0.5, 1e-6));
        CHECK(near(ch0.peak, 0.5, 1e-6));
        CHECK(ch1.rms == 0.0f && ch1.peak == 0.0f);
    }

    // Silence floors at -200 dB, never -inf/NaN.
    {
        CHECK(near(toDbfs(0.0f), -200.0, 1e-3));
        CHECK(std::isfinite(toDbfs(0.0f)));
    }

    // Degenerate inputs return zeros instead of crashing.
    {
        const auto lv = computeLevels(nullptr, 100, 2, 0);
        CHECK(lv.rms == 0.0f && lv.peak == 0.0f);
        float x = 1.0f;
        const auto lv2 = computeLevels(&x, 1, 2, 5);  // channel out of range
        CHECK(lv2.rms == 0.0f && lv2.peak == 0.0f);
    }

    std::puts("aa_dsp levels_test: all assertions passed");
    return 0;
}
