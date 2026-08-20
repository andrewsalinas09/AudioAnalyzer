// Round-trip tests for the acoustic sync frame: render, embed, detect,
// including clock-drift estimation on a resampled capture.
#include "check.h"
#include <cmath>
#include <cstdio>
#include <vector>

#include "generator.h"
#include "syncframe.h"

using aa::dsp::SyncFrameSpec;
using aa::dsp::detectSyncFrame;
using aa::dsp::wrapWithSyncFrame;

namespace {
constexpr double kFs = 48000.0;

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// Deterministic white noise.
struct Lcg {
    std::uint64_t s = 22222;
    float next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<float>(
                   static_cast<double>(s >> 11) / 9007199254740992.0) *
                   2.0f - 1.0f;
    }
};

// Linear-interpolation resample by ratio r (output has n/r samples of the
// same signal, simulating a capture clock running r times faster).
std::vector<float> resample(const std::vector<float>& x, double r) {
    const std::size_t n = static_cast<std::size_t>(x.size() / r) - 1;
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double src = i * r;
        const std::size_t k = static_cast<std::size_t>(src);
        const double frac = src - k;
        out[i] = static_cast<float>(x[k] * (1.0 - frac) + x[k + 1] * frac);
    }
    return out;
}
}  // namespace

int main() {
    const SyncFrameSpec spec;
    // Payload: a 1 s exponential sweep, as in a real measurement.
    const auto payload = aa::dsp::renderExpSweep(kFs, 100.0, 10000.0, 1.0, 0.5);
    const auto frame = wrapWithSyncFrame(payload, kFs, spec, 0.5);

    // Clean capture with a known offset: sample-accurate detection.
    {
        const std::size_t offset = 12345;
        std::vector<float> capture(offset, 0.0f);
        capture.insert(capture.end(), frame.begin(), frame.end());
        capture.insert(capture.end(), 4800, 0.0f);

        const auto det = detectSyncFrame(capture.data(), capture.size(), kFs,
                                         spec, payload.size());
        CHECK(det.found);
        CHECK(near(det.preambleStart, static_cast<double>(offset), 0.5));
        CHECK(near(det.clockRatio, 1.0, 20e-6));  // within 20 ppm
        std::printf("clean: pre %.2f (expected %zu), ratio %.7f, peaks %.2f/%.2f\n",
                    det.preambleStart, offset, det.clockRatio,
                    det.preamblePeak, det.postamblePeak);
    }

    // Noisy capture (marker-to-noise ~10 dB): still detects, still accurate
    // to within a sample.
    {
        const std::size_t offset = 7000;
        std::vector<float> capture(offset + frame.size() + 4800, 0.0f);
        Lcg rng;
        for (auto& v : capture) v = 0.16f * rng.next();
        for (std::size_t i = 0; i < frame.size(); ++i) {
            capture[offset + i] += frame[i];
        }
        const auto det = detectSyncFrame(capture.data(), capture.size(), kFs,
                                         spec, payload.size());
        CHECK(det.found);
        CHECK(near(det.preambleStart, static_cast<double>(offset), 1.0));
        std::printf("noisy: pre %.2f (expected %zu), peaks %.2f/%.2f\n",
                    det.preambleStart, offset, det.preamblePeak,
                    det.postamblePeak);
    }

    // Clock drift: capture resampled by 500 ppm; the estimated ratio must
    // recover it within 20 ppm.
    {
        const double drift = 1.0005;
        std::vector<float> emitted(9600, 0.0f);
        emitted.insert(emitted.end(), frame.begin(), frame.end());
        emitted.insert(emitted.end(), 4800, 0.0f);
        const auto capture = resample(emitted, drift);

        const auto det = detectSyncFrame(capture.data(), capture.size(), kFs,
                                         spec, payload.size());
        CHECK(det.found);
        const double measuredPpm = (det.clockRatio - 1.0) * 1e6;
        const double truePpm = (1.0 / drift - 1.0) * 1e6;
        std::printf("drift: measured %.1f ppm, true %.1f ppm\n", measuredPpm,
                    truePpm);
        CHECK(near(measuredPpm, truePpm, 20.0));
    }

    std::puts("syncframe_test: all assertions passed");
    return 0;
}
