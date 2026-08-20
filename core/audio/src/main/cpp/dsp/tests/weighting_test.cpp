// Verifies the A/C weighting filters against the IEC 61672-1 nominal
// frequency-weighting table. The filter is checked analytically (biquad
// frequency response, no signals) at the standard third-octave frequencies.
//
// Tolerances: at and below 8 kHz we hold ourselves to ±0.3–0.7 dB, well
// inside class 1. Above 10 kHz the bilinear transform at fs = 48 kHz
// compresses the response toward Nyquist; the standard's own lower
// acceptance limits are strongly relaxed there (microphone roll-off), and we
// allow a documented negative deviation. Actual deviations are printed so
// validation entry 004 can record them.
#include "check.h"
#include <cmath>
#include <cstdio>

#include "weighting.h"

using aa::dsp::BiquadCascade;
using aa::dsp::Weighting;
using aa::dsp::designWeighting;

namespace {

struct Row {
    double freq;
    double nominal;
    double tolUp;    // allowed positive deviation (dB)
    double tolDown;  // allowed negative deviation (dB, positive number)
};

// IEC 61672-1 nominal A-weighting.
constexpr Row kTableA[] = {
    {20.0, -50.5, 0.3, 0.3},   {25.0, -44.7, 0.3, 0.3},
    {31.5, -39.4, 0.3, 0.3},   {40.0, -34.6, 0.3, 0.3},
    {50.0, -30.2, 0.3, 0.3},   {63.0, -26.2, 0.3, 0.3},
    {80.0, -22.5, 0.3, 0.3},   {100.0, -19.1, 0.3, 0.3},
    {125.0, -16.1, 0.3, 0.3},  {160.0, -13.4, 0.3, 0.3},
    {200.0, -10.9, 0.3, 0.3},  {250.0, -8.6, 0.3, 0.3},
    {315.0, -6.6, 0.3, 0.3},   {400.0, -4.8, 0.3, 0.3},
    {500.0, -3.2, 0.3, 0.3},   {630.0, -1.9, 0.3, 0.3},
    {800.0, -0.8, 0.3, 0.3},   {1000.0, 0.0, 0.05, 0.05},
    {1250.0, 0.6, 0.3, 0.3},   {1600.0, 1.0, 0.3, 0.3},
    {2000.0, 1.2, 0.3, 0.3},   {2500.0, 1.3, 0.3, 0.3},
    {3150.0, 1.2, 0.3, 0.3},   {4000.0, 1.0, 0.3, 0.3},
    {5000.0, 0.5, 0.5, 0.5},   {6300.0, -0.1, 0.5, 0.5},
    {8000.0, -1.1, 0.7, 0.7},  {10000.0, -2.5, 1.0, 2.0},
    {12500.0, -4.3, 1.0, 3.5}, {16000.0, -6.6, 1.0, 8.0},
};

// IEC 61672-1 nominal C-weighting.
constexpr Row kTableC[] = {
    {20.0, -6.2, 0.3, 0.3},   {31.5, -3.0, 0.3, 0.3},
    {63.0, -0.8, 0.3, 0.3},   {125.0, -0.2, 0.3, 0.3},
    {250.0, 0.0, 0.3, 0.3},   {500.0, 0.0, 0.3, 0.3},
    {1000.0, 0.0, 0.05, 0.05},{2000.0, -0.2, 0.3, 0.3},
    {4000.0, -0.8, 0.3, 0.3}, {8000.0, -3.0, 0.7, 0.7},
    {12500.0, -6.2, 1.0, 3.5},{16000.0, -8.5, 1.0, 8.0},
};

bool checkTable(const char* name, const BiquadCascade& c, double fs,
                const Row* rows, std::size_t n) {
    bool ok = true;
    std::printf("%s-weighting @ %.0f Hz sample rate:\n", name, fs);
    for (std::size_t i = 0; i < n; ++i) {
        const double got = c.magnitudeDbAt(rows[i].freq, fs);
        const double dev = got - rows[i].nominal;
        const bool pass = dev <= rows[i].tolUp && dev >= -rows[i].tolDown;
        std::printf("  %8.1f Hz  nominal %6.1f  got %7.2f  dev %+6.2f  %s\n",
                    rows[i].freq, rows[i].nominal, got, dev,
                    pass ? "ok" : "FAIL");
        if (!pass) ok = false;
    }
    return ok;
}

}  // namespace

int main() {
    for (const double fs : {48000.0, 44100.0, 96000.0}) {
        const auto a = designWeighting(Weighting::A, fs);
        const auto c = designWeighting(Weighting::C, fs);
        CHECK(checkTable("A", a, fs, kTableA, std::size(kTableA)));
        CHECK(checkTable("C", c, fs, kTableC, std::size(kTableC)));

        // Z is exactly flat (empty cascade).
        const auto z = designWeighting(Weighting::Z, fs);
        CHECK(std::fabs(z.magnitudeDbAt(20.0, fs)) < 1e-12);
        CHECK(std::fabs(z.magnitudeDbAt(20000.0, fs)) < 1e-12);
    }
    std::puts("weighting_test: all assertions passed");
    return 0;
}
