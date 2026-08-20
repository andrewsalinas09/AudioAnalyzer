#include "syncframe.h"

#include <algorithm>
#include <cmath>

namespace aa::dsp {

namespace {
constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

// Cross-correlates x with the template and returns the peak position with
// parabolic sub-sample refinement, searching [from, to).
struct Peak {
    double pos = 0.0;
    double value = 0.0;
};

Peak correlatePeak(const float* x, std::size_t n, const std::vector<float>& h,
                   std::size_t from, std::size_t to) {
    Peak best;
    if (n < h.size()) return best;
    const std::size_t last = std::min(to, n - h.size() + 1);
    std::vector<double> corr(last > from ? last - from : 0);
    for (std::size_t s = from; s < last; ++s) {
        double acc = 0;
        for (std::size_t i = 0; i < h.size(); ++i) {
            acc += static_cast<double>(x[s + i]) * h[i];
        }
        corr[s - from] = std::fabs(acc);
        if (corr[s - from] > best.value) {
            best.value = corr[s - from];
            best.pos = static_cast<double>(s);
        }
    }
    // Parabolic interpolation around the integer peak.
    const std::size_t k = static_cast<std::size_t>(best.pos) - from;
    if (k > 0 && k + 1 < corr.size()) {
        const double a = corr[k - 1], b = corr[k], c = corr[k + 1];
        const double denom = a - 2 * b + c;
        if (std::fabs(denom) > 1e-12) {
            best.pos += 0.5 * (a - c) / denom;
        }
    }
    return best;
}

double templateEnergy(const std::vector<float>& h) {
    double e = 0;
    for (const float v : h) e += static_cast<double>(v) * v;
    return e;
}

double signalEnergy(const float* x, std::size_t n) {
    double e = 0;
    for (std::size_t i = 0; i < n; ++i) e += static_cast<double>(x[i]) * x[i];
    return e;
}
}  // namespace

std::vector<float> renderSyncChirp(double fs, const SyncFrameSpec& spec,
                                   bool reversed) {
    const std::size_t n = spec.chirpSamples(fs);
    std::vector<float> x(n);
    const double k = (spec.chirpF2Hz - spec.chirpF1Hz) / spec.chirpSec;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        x[i] = static_cast<float>(
            std::sin(kTwoPi * (spec.chirpF1Hz * t + 0.5 * k * t * t)));
    }
    // Raised-cosine fades.
    const std::size_t nf = static_cast<std::size_t>(spec.fadeSec * fs);
    for (std::size_t i = 0; i < nf && 2 * nf < n; ++i) {
        const float g = static_cast<float>(
            0.5 * (1.0 - std::cos(3.14159265358979323846 * i / nf)));
        x[i] *= g;
        x[n - 1 - i] *= g;
    }
    if (reversed) std::reverse(x.begin(), x.end());
    return x;
}

std::vector<float> wrapWithSyncFrame(const std::vector<float>& payload,
                                     double fs, const SyncFrameSpec& spec,
                                     double markerAmplitude) {
    const auto pre = renderSyncChirp(fs, spec, false);
    const auto post = renderSyncChirp(fs, spec, true);
    const std::size_t guard = spec.guardSamples(fs);

    std::vector<float> out;
    out.reserve(pre.size() + guard + payload.size() + guard + post.size());
    for (const float v : pre) out.push_back(static_cast<float>(v * markerAmplitude));
    out.insert(out.end(), guard, 0.0f);
    out.insert(out.end(), payload.begin(), payload.end());
    out.insert(out.end(), guard, 0.0f);
    for (const float v : post) out.push_back(static_cast<float>(v * markerAmplitude));
    return out;
}

SyncDetection detectSyncFrame(const float* x, std::size_t n, double fs,
                              const SyncFrameSpec& spec,
                              std::size_t payloadSamples) {
    SyncDetection det;
    const auto pre = renderSyncChirp(fs, spec, false);
    const auto post = renderSyncChirp(fs, spec, true);
    if (n < pre.size() + post.size()) return det;

    const auto prePeak = correlatePeak(x, n, pre, 0, n);
    // Nominal spacing between marker starts (emitter samples).
    const double nominalSpacing =
        static_cast<double>(pre.size() + spec.guardSamples(fs) +
                            payloadSamples + spec.guardSamples(fs));
    // Search the postamble around its nominal position, +-2 % for clock
    // drift and detection slack.
    const double center = prePeak.pos + nominalSpacing;
    const auto from = static_cast<std::size_t>(
        std::max(0.0, center - 0.02 * nominalSpacing));
    const auto to = static_cast<std::size_t>(
        std::min(static_cast<double>(n), center + 0.02 * nominalSpacing));
    const auto postPeak = correlatePeak(x, n, post, from, to);

    const double hEnergy = templateEnergy(pre);
    // Normalize peaks against template*segment energy for a 0..1-ish score.
    const auto norm = [&](const Peak& p, const std::vector<float>& h) {
        const std::size_t s = static_cast<std::size_t>(std::max(0.0, p.pos));
        if (s + h.size() > n) return 0.0;
        const double se = signalEnergy(x + s, h.size());
        const double d = std::sqrt(hEnergy * se);
        return d > 0 ? p.value / d : 0.0;
    };

    det.preambleStart = prePeak.pos;
    det.postambleStart = postPeak.pos;
    det.preamblePeak = norm(prePeak, pre);
    det.postamblePeak = norm(postPeak, post);
    det.found = det.preamblePeak > 0.5 && det.postamblePeak > 0.5 &&
                postPeak.pos > prePeak.pos;
    if (det.found) {
        det.clockRatio = (postPeak.pos - prePeak.pos) / nominalSpacing;
    }
    return det;
}

}  // namespace aa::dsp
