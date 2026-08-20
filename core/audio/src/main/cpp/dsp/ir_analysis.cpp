#include "ir_analysis.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "fft.h"

namespace aa::dsp {

namespace {
constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kPi = 3.14159265358979323846;

std::size_t peakIndex(const std::vector<float>& ir) {
    std::size_t p = 0;
    float best = 0;
    for (std::size_t i = 0; i < ir.size(); ++i) {
        const float a = std::fabs(ir[i]);
        if (a > best) {
            best = a;
            p = i;
        }
    }
    return p;
}

// Least-squares fit of the Schroeder curve (dB vs seconds) between the
// first crossings of hiDb and loDb; returns RT60 extrapolated from the
// slope, or NaN.
double schroederRt(const std::vector<double>& schroederDb, double fs,
                   double hiDb, double loDb) {
    std::size_t i0 = schroederDb.size(), i1 = schroederDb.size();
    for (std::size_t i = 0; i < schroederDb.size(); ++i) {
        if (i0 == schroederDb.size() && schroederDb[i] <= hiDb) i0 = i;
        if (schroederDb[i] <= loDb) {
            i1 = i;
            break;
        }
    }
    if (i0 >= i1 || i1 == schroederDb.size() || i1 - i0 < 10) return kNan;

    double st = 0, sd = 0, stt = 0, std_ = 0;
    const auto n = static_cast<double>(i1 - i0);
    for (std::size_t i = i0; i < i1; ++i) {
        const double t = static_cast<double>(i) / fs;
        st += t;
        sd += schroederDb[i];
        stt += t * t;
        std_ += t * schroederDb[i];
    }
    const double denom = n * stt - st * st;
    if (std::fabs(denom) < 1e-18) return kNan;
    const double slope = (n * std_ - st * sd) / denom;  // dB per second
    if (slope >= -1e-9) return kNan;
    return -60.0 / slope;
}
}  // namespace

IrMetrics analyzeIr(const std::vector<float>& ir, double fs) {
    IrMetrics m;
    if (ir.size() < 32) return m;

    const std::size_t p = peakIndex(ir);
    m.peakSample = static_cast<double>(p);
    m.peakDb = 20.0 * std::log10(std::max(1e-12, std::fabs(
        static_cast<double>(ir[p]))));

    // Schroeder backward integration from the peak.
    const std::size_t n = ir.size() - p;
    std::vector<double> sch(n);
    double acc = 0;
    for (std::size_t i = n; i-- > 0;) {
        const double v = ir[p + i];
        acc += v * v;
        sch[i] = acc;
    }
    const double total = std::max(acc, 1e-30);
    for (auto& v : sch) v = 10.0 * std::log10(std::max(v / total, 1e-30));

    // schroederRt extrapolates to 60 dB from the fitted slope; the ranges
    // below only select the fit region (EDT / T20 / T30 conventions).
    m.edtSec = schroederRt(sch, fs, -0.01, -10.0);
    m.t20Sec = schroederRt(sch, fs, -5.0, -25.0);
    m.t30Sec = schroederRt(sch, fs, -5.0, -35.0);

    // Clarity: early/late split at 50/80 ms after the direct sound.
    auto clarity = [&](double ms) {
        const auto split = p + static_cast<std::size_t>(ms * 1e-3 * fs);
        double early = 0, late = 0;
        for (std::size_t i = p; i < ir.size(); ++i) {
            const double v = static_cast<double>(ir[i]) * ir[i];
            if (i < split) early += v; else late += v;
        }
        if (late <= 0) return kNan;
        return 10.0 * std::log10(early / late);
    };
    m.c50Db = clarity(50.0);
    m.c80Db = clarity(80.0);
    return m;
}

void etcTrace(const std::vector<float>& ir, float* out, std::size_t n) {
    if (ir.empty() || n == 0) return;
    float peak = 1e-12f;
    for (const float v : ir) peak = std::max(peak, std::fabs(v));
    for (std::size_t b = 0; b < n; ++b) {
        const std::size_t from = b * ir.size() / n;
        const std::size_t to = std::max(from + 1, (b + 1) * ir.size() / n);
        float m = 0;
        for (std::size_t i = from; i < to && i < ir.size(); ++i) {
            m = std::max(m, std::fabs(ir[i]));
        }
        out[b] = static_cast<float>(
            20.0 * std::log10(std::max(m / peak, 1e-10f)));
    }
}

void irFrequencyResponse(const std::vector<float>& ir, double fs,
                         int fftSize, double preSec, double windowSec,
                         float* magDb, float* gdMs) {
    const std::size_t nb = static_cast<std::size_t>(fftSize) / 2 + 1;
    std::fill(magDb, magDb + nb, -200.0f);
    std::fill(gdMs, gdMs + nb, 0.0f);
    if (ir.size() < 32) return;

    const std::size_t p = peakIndex(ir);
    const auto pre = static_cast<std::size_t>(preSec * fs);
    const std::size_t start = p > pre ? p - pre : 0;
    const std::size_t peakOffset = p - start;  // known bulk delay in samples
    std::size_t len = static_cast<std::size_t>(windowSec * fs) + peakOffset;
    len = std::min({len, ir.size() - start,
                    static_cast<std::size_t>(fftSize)});

    std::vector<float> buf(static_cast<std::size_t>(fftSize), 0.0f);
    for (std::size_t i = 0; i < len; ++i) buf[i] = ir[start + i];

    // Half-Hann fade-in before the peak, half-Hann fade-out over the last
    // quarter of the window.
    for (std::size_t i = 0; i < peakOffset; ++i) {
        buf[i] *= static_cast<float>(
            0.5 * (1.0 - std::cos(kPi * i / std::max<std::size_t>(1, peakOffset))));
    }
    const std::size_t fade = len / 4;
    for (std::size_t i = 0; i < fade; ++i) {
        buf[len - 1 - i] *= static_cast<float>(
            0.5 * (1.0 - std::cos(kPi * i / fade)));
    }

    RealFft fft(fftSize);
    std::vector<float> X(static_cast<std::size_t>(fftSize));
    fft.forward(buf.data(), X.data());

    auto reim = [&](std::size_t k, double& re, double& im) {
        if (k == 0) {
            re = X[0];
            im = 0;
        } else if (k == nb - 1) {
            re = X[1];
            im = 0;
        } else {
            re = X[2 * k];
            im = X[2 * k + 1];
        }
    };

    const double binHz = fs / fftSize;
    const double bulkSec = static_cast<double>(peakOffset) / fs;
    for (std::size_t k = 0; k < nb; ++k) {
        double re, im;
        reim(k, re, im);
        magDb[k] = static_cast<float>(
            10.0 * std::log10(std::max(re * re + im * im, 1e-24)));
        if (k + 1 < nb) {
            double re2, im2;
            reim(k + 1, re2, im2);
            // arg(X[k+1] * conj(X[k])) = local phase increment.
            const double dphi =
                std::atan2(im2 * re - re2 * im, re2 * re + im2 * im);
            const double gdSec = -dphi / (2.0 * kPi * binHz) - bulkSec;
            gdMs[k] = static_cast<float>(gdSec * 1000.0);
        }
    }
    if (nb >= 2) gdMs[nb - 1] = gdMs[nb - 2];
}

}  // namespace aa::dsp
