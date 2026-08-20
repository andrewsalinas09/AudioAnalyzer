#include "deconvolve.h"

#include <algorithm>
#include <cmath>

#include "fft.h"

namespace aa::dsp {

namespace {

std::size_t nextPow2(std::size_t n) {
    std::size_t p = 32;  // PFFFT minimum
    while (p < n) p <<= 1;
    return p;
}

// Complex multiply of ordered real-FFT spectra: out = a * b (or a * conj(b)).
// Ordered layout: [DC.re, Nyq.re, re1, im1, re2, im2, ...].
void spectrumMultiply(const std::vector<float>& a, const std::vector<float>& b,
                      std::vector<float>& out, bool conjugateB) {
    const std::size_t n = a.size();
    out.resize(n);
    out[0] = a[0] * b[0];
    out[1] = a[1] * b[1];
    for (std::size_t k = 2; k + 1 < n; k += 2) {
        const double ar = a[k], ai = a[k + 1];
        const double br = b[k];
        const double bi = conjugateB ? -b[k + 1] : b[k + 1];
        out[k] = static_cast<float>(ar * br - ai * bi);
        out[k + 1] = static_cast<float>(ar * bi + ai * br);
    }
}

}  // namespace

std::vector<float> resampleLinear(const std::vector<float>& x, double ratio) {
    if (x.size() < 2) return x;
    const auto n = static_cast<std::size_t>(
        static_cast<double>(x.size() - 1) / ratio);
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double src = static_cast<double>(i) * ratio;
        const auto k = static_cast<std::size_t>(src);
        const double frac = src - static_cast<double>(k);
        out[i] = static_cast<float>(x[k] * (1.0 - frac) + x[k + 1] * frac);
    }
    return out;
}

std::vector<float> fftXcorr(const std::vector<float>& x,
                            const std::vector<float>& h) {
    if (x.size() < h.size()) return {};
    const std::size_t n = nextPow2(x.size() + h.size());
    RealFft fft(static_cast<int>(n));

    std::vector<float> xa(n, 0.0f), ha(n, 0.0f);
    std::copy(x.begin(), x.end(), xa.begin());
    std::copy(h.begin(), h.end(), ha.begin());

    std::vector<float> X(n), H(n), P;
    fft.forward(xa.data(), X.data());
    fft.forward(ha.data(), H.data());
    // Correlation = IFFT(X * conj(H)); lag k lands at index k.
    spectrumMultiply(X, H, P, /*conjugateB=*/true);
    std::vector<float> c(n);
    fft.inverse(P.data(), c.data());
    c.resize(x.size() - h.size() + 1);
    return c;
}

namespace {

struct Peak {
    double pos = 0.0;
    double value = 0.0;
};

Peak peakOf(const std::vector<float>& c, std::size_t from, std::size_t to) {
    Peak best;
    to = std::min(to, c.size());
    for (std::size_t i = from; i < to; ++i) {
        const double v = std::fabs(c[i]);
        if (v > best.value) {
            best.value = v;
            best.pos = static_cast<double>(i);
        }
    }
    const auto k = static_cast<std::size_t>(best.pos);
    if (k > from && k + 1 < to) {
        const double a = std::fabs(c[k - 1]), b = std::fabs(c[k]),
                     d = std::fabs(c[k + 1]);
        const double denom = a - 2 * b + d;
        if (std::fabs(denom) > 1e-12) best.pos += 0.5 * (a - d) / denom;
    }
    return best;
}

double segmentEnergy(const std::vector<float>& x, std::size_t from,
                     std::size_t len) {
    double e = 0;
    const std::size_t to = std::min(from + len, x.size());
    for (std::size_t i = from; i < to; ++i) {
        e += static_cast<double>(x[i]) * x[i];
    }
    return e;
}

}  // namespace

SyncDetection detectSyncFrameFft(const std::vector<float>& x, double fs,
                                 const SyncFrameSpec& spec,
                                 std::size_t payloadSamples) {
    SyncDetection det;
    const auto pre = renderSyncChirp(fs, spec, false);
    const auto post = renderSyncChirp(fs, spec, true);
    if (x.size() < pre.size() + post.size()) return det;

    const auto cPre = fftXcorr(x, pre);
    const auto prePeak = peakOf(cPre, 0, cPre.size());

    const double nominalSpacing =
        static_cast<double>(pre.size() + spec.guardSamples(fs) +
                            payloadSamples + spec.guardSamples(fs));
    const double center = prePeak.pos + nominalSpacing;
    const auto cPost = fftXcorr(x, post);
    const auto from = static_cast<std::size_t>(
        std::max(0.0, center - 0.02 * nominalSpacing));
    const auto to = static_cast<std::size_t>(
        std::min(static_cast<double>(cPost.size()),
                 center + 0.02 * nominalSpacing));
    const auto postPeak = peakOf(cPost, from, to);

    double hEnergy = 0;
    for (const float v : pre) hEnergy += static_cast<double>(v) * v;
    const auto norm = [&](const Peak& p, std::size_t len) {
        const auto s = static_cast<std::size_t>(std::max(0.0, p.pos));
        const double se = segmentEnergy(x, s, len);
        const double d = std::sqrt(hEnergy * se);
        return d > 0 ? p.value / d : 0.0;
    };

    det.preambleStart = prePeak.pos;
    det.postambleStart = postPeak.pos;
    det.preamblePeak = norm(prePeak, pre.size());
    det.postamblePeak = norm(postPeak, post.size());
    det.found = det.preamblePeak > 0.4 && det.postamblePeak > 0.4 &&
                postPeak.pos > prePeak.pos;
    if (det.found) {
        det.clockRatio = (postPeak.pos - prePeak.pos) / nominalSpacing;
    }
    return det;
}

std::vector<float> deconvolve(const std::vector<float>& capture,
                              const std::vector<float>& reference,
                              std::size_t irLength, double epsRel) {
    const std::size_t n = nextPow2(capture.size() + reference.size());
    RealFft fft(static_cast<int>(n));

    std::vector<float> xa(n, 0.0f), sa(n, 0.0f);
    std::copy(capture.begin(), capture.end(), xa.begin());
    std::copy(reference.begin(), reference.end(), sa.begin());

    std::vector<float> X(n), S(n);
    fft.forward(xa.data(), X.data());
    fft.forward(sa.data(), S.data());

    // max |S|^2 for the regularization floor.
    double maxS2 = std::max(static_cast<double>(S[0]) * S[0],
                            static_cast<double>(S[1]) * S[1]);
    for (std::size_t k = 2; k + 1 < n; k += 2) {
        const double m = static_cast<double>(S[k]) * S[k] +
                         static_cast<double>(S[k + 1]) * S[k + 1];
        if (m > maxS2) maxS2 = m;
    }
    const double eps = epsRel * maxS2;

    std::vector<float> Q(n);
    // DC and Nyquist (purely real).
    Q[0] = static_cast<float>(X[0] * S[0] / (S[0] * S[0] + eps));
    Q[1] = static_cast<float>(X[1] * S[1] / (S[1] * S[1] + eps));
    for (std::size_t k = 2; k + 1 < n; k += 2) {
        const double xr = X[k], xi = X[k + 1];
        const double sr = S[k], si = S[k + 1];
        const double d = sr * sr + si * si + eps;
        Q[k] = static_cast<float>((xr * sr + xi * si) / d);
        Q[k + 1] = static_cast<float>((xi * sr - xr * si) / d);
    }

    std::vector<float> ir(n);
    fft.inverse(Q.data(), ir.data());
    ir.resize(std::min(irLength, ir.size()));
    return ir;
}

}  // namespace aa::dsp
