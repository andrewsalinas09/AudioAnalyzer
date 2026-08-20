#include "weighting.h"

#include <cmath>

namespace aa::dsp {

namespace {

// IEC 61672-1 pole frequencies (Hz).
constexpr double kF1 = 20.598997057568145;
constexpr double kF2 = 107.65264864304628;
constexpr double kF3 = 737.8622307362899;
constexpr double kF4 = 12194.21714799801;

// Bilinear transform of H(s) = s^2 / ((s+w)^2): double zero at DC,
// double real pole at w.
Biquad hpDoublePole(double w, double fs) {
    const double k = 2.0 * fs;
    const double A = k + w;
    const double B = k - w;
    Biquad q;
    const double g = (k * k) / (A * A);
    q.b0 = g;
    q.b1 = -2.0 * g;
    q.b2 = g;
    q.a1 = -2.0 * A * B / (A * A);
    q.a2 = (B * B) / (A * A);
    return q;
}

// Bilinear transform of H(s) = s^2 / ((s+wa)(s+wb)): double zero at DC,
// two distinct real poles.
Biquad hpTwoPoles(double wa, double wb, double fs) {
    const double k = 2.0 * fs;
    const double Aa = k + wa, Ba = k - wa;
    const double Ab = k + wb, Bb = k - wb;
    const double a0 = Aa * Ab;
    Biquad q;
    const double g = (k * k) / a0;
    q.b0 = g;
    q.b1 = -2.0 * g;
    q.b2 = g;
    q.a1 = (-Aa * Bb - Ab * Ba) / a0;
    q.a2 = (Ba * Bb) / a0;
    return q;
}

// Bilinear transform of H(s) = w^2 / ((s+w)^2): unity DC gain low-pass,
// double real pole at w.
Biquad lpDoublePole(double w, double fs) {
    const double k = 2.0 * fs;
    const double A = k + w;
    const double B = k - w;
    Biquad q;
    const double g = (w * w) / (A * A);
    q.b0 = g;
    q.b1 = 2.0 * g;
    q.b2 = g;
    q.a1 = -2.0 * A * B / (A * A);
    q.a2 = (B * B) / (A * A);
    return q;
}

constexpr double kTwoPi = 2.0 * M_PI;

// The exact analog weighting magnitude (dB, normalized to 0 dB at 1 kHz).
// This is the design target the digital filter is tuned against.
double analogWeightDb(Weighting type, double f) {
    auto mag = [type](double f) {
        const double f2 = f * f;
        const double den1 = f2 + kF1 * kF1;
        const double den4 = f2 + kF4 * kF4;
        if (type == Weighting::C) {
            return (kF4 * kF4 * f2) / (den1 * den4);
        }
        // A
        const double den2 = std::sqrt(f2 + kF2 * kF2);
        const double den3 = std::sqrt(f2 + kF3 * kF3);
        return (kF4 * kF4 * f2 * f2) / (den1 * den2 * den3 * den4);
    };
    return 20.0 * std::log10(mag(f) / mag(1000.0));
}

// Frequency prewarping: the bilinear transform maps analog frequency
// 2*fs*tan(w/(2*fs)) onto digital frequency w.
double prewarp(double w, double fs) {
    return 2.0 * fs * std::tan(w / (2.0 * fs));
}

BiquadCascade build(Weighting type, double fs, double w4eff) {
    BiquadCascade c;
    // The low-frequency poles sit far below Nyquist at any supported rate;
    // full prewarping there is exact and free.
    c.sections.push_back(hpDoublePole(prewarp(kTwoPi * kF1, fs), fs));
    if (type == Weighting::A) {
        c.sections.push_back(hpTwoPoles(prewarp(kTwoPi * kF2, fs),
                                        prewarp(kTwoPi * kF3, fs), fs));
    }
    c.sections.push_back(lpDoublePole(w4eff, fs));

    // Normalize to 0 dB at exactly 1 kHz.
    const double correction = 1.0 / c.magnitudeAt(1000.0, fs);
    c.sections.front().b0 *= correction;
    c.sections.front().b1 *= correction;
    c.sections.front().b2 *= correction;
    return c;
}

}  // namespace

BiquadCascade designWeighting(Weighting type, double fs) {
    if (type == Weighting::Z) return {};

    // The 12.2 kHz pole pair is the one the bilinear transform distorts:
    // with no prewarp the response is several dB low near Nyquist; with full
    // prewarp the corner is exact but the 5-10 kHz band reads ~0.6 dB high.
    // Bisect the prewarp amount so the digital response matches the exact
    // analog value at 8 kHz; errors on either side stay within a few tenths
    // of a dB up to 10 kHz (verified against IEC 61672-1 in the host tests).
    const double w4 = kTwoPi * kF4;
    const double w4Warped = prewarp(w4, fs);
    const double target8k = analogWeightDb(type, 8000.0);

    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 40; ++i) {
        const double t = 0.5 * (lo + hi);
        const double w4eff = w4 * std::pow(w4Warped / w4, t);
        const double dev =
            build(type, fs, w4eff).magnitudeDbAt(8000.0, fs) - target8k;
        if (dev < 0) {
            lo = t;  // response too low -> more prewarp
        } else {
            hi = t;
        }
    }
    const double t = 0.5 * (lo + hi);
    return build(type, fs, w4 * std::pow(w4Warped / w4, t));
}

}  // namespace aa::dsp
