// End-to-end impulse-response pipeline test against a synthetic room:
// sweep -> convolve with a known IR -> sync frame -> clock drift + noise ->
// detect -> drift-correct -> deconvolve -> metrics. The recovered IR and
// room-acoustic numbers must match the constructed truth.
#include "check.h"
#include <cmath>
#include <cstdio>
#include <vector>

#include "deconvolve.h"
#include "generator.h"
#include "ir_analysis.h"
#include "syncframe.h"

using namespace aa::dsp;

namespace {
constexpr double kFs = 48000.0;

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

struct Lcg {
    std::uint64_t s = 777;
    float next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<float>(
                   static_cast<double>(s >> 11) / 9007199254740992.0) *
                   2.0f - 1.0f;
    }
};

// Full linear convolution via fftXcorr (correlate with the reversed kernel
// on a zero-padded input).
std::vector<float> fftConv(const std::vector<float>& x,
                           const std::vector<float>& h) {
    std::vector<float> hr(h.rbegin(), h.rend());
    std::vector<float> padded(h.size() - 1, 0.0f);
    padded.insert(padded.end(), x.begin(), x.end());
    padded.insert(padded.end(), h.size() - 1, 0.0f);
    return fftXcorr(padded, hr);
}

// Synthetic room: direct sound at 40 ms (0.5), a discrete echo at 100 ms
// (0.1), and an exponentially decaying diffuse tail with RT60 = 0.4 s. Tail
// amplitude is chosen so the tail carries most of the reverberant energy
// (as in real rooms) — with a too-weak tail the Schroeder curve is dominated
// by the discrete-echo step and no fit recovers the design RT60.
std::vector<float> makeRoomIr() {
    const auto n = static_cast<std::size_t>(0.7 * kFs);
    std::vector<float> ir(n, 0.0f);
    const auto direct = static_cast<std::size_t>(0.040 * kFs);
    const auto echo = static_cast<std::size_t>(0.100 * kFs);
    ir[direct] = 0.5f;
    ir[echo] = 0.1f;
    Lcg rng;
    const double rt60 = 0.4;
    for (std::size_t i = direct + 100; i < n; ++i) {
        const double t = static_cast<double>(i - direct) / kFs;
        ir[i] += static_cast<float>(0.05 * rng.next() *
                                    std::exp(-6.9078 * t / rt60));
    }
    return ir;
}
}  // namespace

int main() {
    const auto sweep = renderExpSweep(kFs, 50.0, 16000.0, 3.0, 0.5);
    const auto room = makeRoomIr();

    // Stage 1: pure deconvolution, no frame/drift. Recovered IR must place
    // the direct sound and echo exactly, with the right amplitude ratio.
    {
        const auto capture = fftConv(sweep, room);
        const auto ir = deconvolve(capture, sweep,
                                   static_cast<std::size_t>(0.7 * kFs));
        const auto m = analyzeIr(ir, kFs);
        std::printf("stage1: peak at %.1f samples (expect %.0f)\n",
                    m.peakSample, 0.040 * kFs);
        CHECK(near(m.peakSample, 0.040 * kFs, 1.5));

        const auto echoIdx = static_cast<std::size_t>(0.100 * kFs);
        const auto directIdx = static_cast<std::size_t>(m.peakSample);
        const double ratio = std::fabs(ir[echoIdx]) / std::fabs(ir[directIdx]);
        std::printf("stage1: echo/direct = %.3f (expect 0.2)\n", ratio);
        CHECK(near(ratio, 0.1 / 0.5, 0.05));

        std::printf("stage1: EDT %.3f  T20 %.3f  T30 %.3f  C50 %.1f  C80 %.1f\n",
                    m.edtSec, m.t20Sec, m.t30Sec, m.c50Db, m.c80Db);
        CHECK(near(m.t20Sec, 0.4, 0.08));  // within 20 %
        CHECK(near(m.t30Sec, 0.4, 0.08));
        CHECK(m.c50Db > 0.0);  // direct + echo dominate the tail
    }

    // Stage 2: flat system (delta IR) -> magnitude flat in-band, excess
    // group delay ~0.
    {
        std::vector<float> delta(static_cast<std::size_t>(0.1 * kFs), 0.0f);
        delta[480] = 1.0f;  // 10 ms bulk delay
        const auto capture = fftConv(sweep, delta);
        const auto ir = deconvolve(capture, sweep,
                                   static_cast<std::size_t>(0.1 * kFs));

        constexpr int kFft = 16384;
        std::vector<float> mag(kFft / 2 + 1), gd(kFft / 2 + 1);
        irFrequencyResponse(ir, kFs, kFft, 0.005, 0.05, mag.data(), gd.data());
        const double binHz = kFs / kFft;
        const auto binAt = [&](double f) {
            return static_cast<std::size_t>(f / binHz);
        };
        const float ref = mag[binAt(1000.0)];
        for (const double f : {200.0, 500.0, 2000.0, 5000.0, 10000.0}) {
            CHECK(near(mag[binAt(f)], ref, 0.5));
            CHECK(near(gd[binAt(f)], 0.0, 0.5));  // ms
        }
    }

    // Stage 3: the whole chain — sync frame, 300 ppm clock drift, noise.
    {
        const SyncFrameSpec spec;
        const auto emitted = wrapWithSyncFrame(sweep, kFs, spec, 0.5);
        auto sound = fftConv(emitted, room);
        // Leading silence + measurement noise.
        std::vector<float> line(static_cast<std::size_t>(0.3 * kFs), 0.0f);
        line.insert(line.end(), sound.begin(), sound.end());
        line.insert(line.end(), static_cast<std::size_t>(0.3 * kFs), 0.0f);
        Lcg rng;
        for (auto& v : line) v += 0.002f * rng.next();
        const double drift = 1.0003;  // capture clock 300 ppm fast
        const auto capture = resampleLinear(line, drift);

        const auto det = detectSyncFrameFft(capture, kFs, spec, sweep.size());
        CHECK(det.found);
        const double measuredPpm = (det.clockRatio - 1.0) * 1e6;
        std::printf("stage3: drift measured %.0f ppm (true %.0f)\n",
                    measuredPpm, (1.0 / drift - 1.0) * 1e6);
        CHECK(near(measuredPpm, (1.0 / drift - 1.0) * 1e6, 25.0));

        // Drift-correct, then deconvolve the payload region.
        const auto corrected = resampleLinear(capture, det.clockRatio);
        const double preCorrected = det.preambleStart / det.clockRatio;
        const auto payloadStart = static_cast<std::size_t>(
            preCorrected + spec.chirpSamples(kFs) + spec.guardSamples(kFs));
        const auto lead = static_cast<std::size_t>(0.1 * kFs);
        const std::size_t from = payloadStart - lead;
        const std::size_t to = std::min(
            corrected.size(),
            payloadStart + sweep.size() + static_cast<std::size_t>(0.7 * kFs));
        const std::vector<float> segment(corrected.begin() + from,
                                         corrected.begin() + to);
        const auto ir = deconvolve(segment, sweep,
                                   static_cast<std::size_t>(1.0 * kFs));
        const auto m = analyzeIr(ir, kFs);
        // The preamble is detected *as heard through the room*, so the sync
        // reference absorbs the propagation delay (that is the point of an
        // acoustic timing reference): the direct sound lands at the lead
        // offset, not lead + time-of-flight.
        const double expectedPeak = static_cast<double>(lead);
        std::printf("stage3: peak %.1f (expect %.0f), T20 %.3f\n",
                    m.peakSample, expectedPeak, m.t20Sec);
        CHECK(near(m.peakSample, expectedPeak, 48.0));  // within 1 ms
        CHECK(near(m.t20Sec, 0.4, 0.1));
    }

    std::puts("ir_test: all assertions passed");
    return 0;
}
