// Tests for the SPL engine: detector time constants, Leq, LN percentiles.
#include "check.h"
#include <cmath>
#include <cstdio>
#include <vector>

#include "spl.h"

using aa::dsp::SplProcessor;
using aa::dsp::TimeWeighting;
using aa::dsp::Weighting;

namespace {
constexpr double kFs = 48000.0;
constexpr double kPi = 3.14159265358979323846;

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

void feedConstant(SplProcessor& p, double amplitude, double seconds) {
    std::vector<float> block(480, static_cast<float>(amplitude));
    long remaining = std::lround(seconds * kFs);  // sample-accurate
    while (remaining > 0) {
        const std::size_t n =
            static_cast<std::size_t>(remaining < 480 ? remaining : 480);
        p.process(block.data(), n, 1, 0);
        remaining -= static_cast<long>(n);
    }
}
}  // namespace

int main() {
    // Fast detector: after exactly one time constant (125 ms) of a unit
    // step, the mean-square detector reads 1 - 1/e -> -1.99 dB.
    {
        SplProcessor p;
        p.configure(kFs, Weighting::Z, TimeWeighting::Fast);
        // Prime the detector at ~zero, then step.
        feedConstant(p, 1e-8, 0.5);
        feedConstant(p, 1.0, 0.125);
        const double level = p.stats().instantDb;
        CHECK(near(level, 10.0 * std::log10(1.0 - std::exp(-1.0)), 0.1));
    }

    // Impulse weighting: fast rise, slow decay. After a loud burst returns
    // to silence, one decay time constant (1.5 s) later the level has
    // dropped by 10*log10(e) = 4.34 dB.
    {
        SplProcessor p;
        p.configure(kFs, Weighting::Z, TimeWeighting::Impulse);
        feedConstant(p, 1.0, 2.0);  // settle high
        const double top = p.stats().instantDb;
        CHECK(near(top, 0.0, 0.1));
        feedConstant(p, 0.0, 1.5);
        const double after = p.stats().instantDb;
        CHECK(near(top - after, 4.34, 0.15));
    }

    // Leq of a full-scale 1 kHz sine is -3.01 dBFS, and the A-weighted Leq
    // at 1 kHz equals the unweighted one (0 dB weighting at 1 kHz).
    {
        for (const auto w : {Weighting::Z, Weighting::A, Weighting::C}) {
            SplProcessor p;
            p.configure(kFs, w, TimeWeighting::Fast);
            std::vector<float> block(48000);
            for (std::size_t i = 0; i < block.size(); ++i) {
                block[i] = static_cast<float>(
                    std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / kFs));
            }
            for (int rep = 0; rep < 5; ++rep) {
                p.process(block.data(), block.size(), 1, 0);
            }
            CHECK(near(p.stats().leqDb, -3.01, 0.05));
        }
    }

    // LN percentiles: 15 s at -20 dB, then 5 s at 0 dB (25 % loud).
    // L10 must sit at the loud level, L50/L90 at the quiet level.
    {
        SplProcessor p;
        p.configure(kFs, Weighting::Z, TimeWeighting::Fast);
        feedConstant(p, 0.1, 15.0);
        feedConstant(p, 1.0, 5.0);
        const auto s = p.stats();
        CHECK(near(s.l10Db, 0.0, 1.0));
        CHECK(near(s.l50Db, -20.0, 1.0));
        CHECK(near(s.l90Db, -20.0, 1.0));
        CHECK(near(s.lmaxDb, 0.0, 0.2));
        CHECK(near(s.elapsedSec, 20.0, 0.05));
        // Leq: 25 % of time at power 1.0, 75 % at 0.01 -> 0.2575 -> -5.89 dB.
        CHECK(near(s.leqDb, 10.0 * std::log10(0.25 * 1.0 + 0.75 * 0.01), 0.1));
    }

    // resetStats clears statistics but not the level display.
    {
        SplProcessor p;
        p.configure(kFs, Weighting::Z, TimeWeighting::Fast);
        feedConstant(p, 1.0, 2.0);
        p.resetStats();
        const auto s = p.stats();
        CHECK(s.elapsedSec == 0.0);
        CHECK(std::isnan(s.l50Db));
        CHECK(near(s.instantDb, 0.0, 0.1));  // detector kept
    }

    // Stereo: processes the selected channel only.
    {
        SplProcessor p;
        p.configure(kFs, Weighting::Z, TimeWeighting::Fast);
        std::vector<float> inter(9600);
        for (std::size_t i = 0; i < inter.size(); i += 2) {
            inter[i] = 0.0f;      // ch0 silent
            inter[i + 1] = 1.0f;  // ch1 loud
        }
        for (int rep = 0; rep < 10; ++rep) {
            p.process(inter.data(), inter.size() / 2, 2, 1);
        }
        CHECK(near(p.stats().instantDb, 0.0, 0.1));
    }

    std::puts("spl_test: all assertions passed");
    return 0;
}
