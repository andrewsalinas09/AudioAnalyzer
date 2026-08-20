#pragma once
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace aa::dsp {

// Second-order IIR section, coefficients normalized so a0 == 1.
// Direct Form II transposed.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0;
    double a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    inline float process(float x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return static_cast<float>(y);
    }

    void resetState() { z1 = z2 = 0; }

    // Magnitude response at normalized angular frequency w = 2*pi*f/fs.
    double magnitudeAt(double w) const {
        const std::complex<double> z = std::polar(1.0, -w);
        const std::complex<double> num = b0 + b1 * z + b2 * z * z;
        const std::complex<double> den = 1.0 + a1 * z + a2 * z * z;
        return std::abs(num / den);
    }
};

struct BiquadCascade {
    std::vector<Biquad> sections;

    inline float process(float x) {
        for (auto& s : sections) x = s.process(x);
        return x;
    }

    void resetState() {
        for (auto& s : sections) s.resetState();
    }

    double magnitudeAt(double freqHz, double fs) const {
        const double w = 2.0 * M_PI * freqHz / fs;
        double m = 1.0;
        for (const auto& s : sections) m *= s.magnitudeAt(w);
        return m;
    }

    double magnitudeDbAt(double freqHz, double fs) const {
        return 20.0 * std::log10(magnitudeAt(freqHz, fs));
    }
};

}  // namespace aa::dsp
