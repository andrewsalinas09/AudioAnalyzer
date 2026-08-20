#include "spectrum.h"

#include <algorithm>
#include <cmath>

namespace aa::dsp {

namespace {
constexpr double kPowerFloor = 1e-24;

double toDb(double power) {
    return 10.0 * std::log10(std::max(power, kPowerFloor));
}
}  // namespace

void SpectrumProcessor::configure(double sampleRateHz, int fftSize,
                                  WindowType window, double avgTimeConstSec) {
    fs_ = sampleRateHz;
    fftSize_ = fftSize;
    hop_ = fftSize / 2;
    window_ = makeWindow(window, static_cast<std::size_t>(fftSize));
    fft_ = std::make_unique<RealFft>(fftSize);

    const double hopSec = static_cast<double>(hop_) / fs_;
    avgAlpha_ = avgTimeConstSec <= 0.0
                    ? 1.0
                    : 1.0 - std::exp(-hopSec / avgTimeConstSec);

    ring_.assign(static_cast<std::size_t>(fftSize), 0.0f);
    ringPos_ = 0;
    newSamples_ = 0;
    totalFed_ = 0;
    frame_.resize(static_cast<std::size_t>(fftSize));
    fftOut_.resize(static_cast<std::size_t>(fftSize));
    avgPower_.assign(static_cast<std::size_t>(bins()), 0.0);
    peakPower_.assign(static_cast<std::size_t>(bins()), 0.0);
    hasFrame_ = false;
}

void SpectrumProcessor::feed(const float* interleaved, std::size_t frames,
                             int channelCount, int channel) {
    if (interleaved == nullptr || channelCount <= 0 || channel < 0 ||
        channel >= channelCount || fft_ == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < frames; ++i) {
        ring_[ringPos_] = interleaved[i * static_cast<std::size_t>(channelCount) +
                                      static_cast<std::size_t>(channel)];
        ringPos_ = (ringPos_ + 1) % ring_.size();
    }
    newSamples_ += frames;
    totalFed_ += frames;
}

bool SpectrumProcessor::compute() {
    if (fft_ == nullptr) return false;
    // One frame per hop of new data; cap the backlog so a long UI stall
    // doesn't cause a burst of redundant transforms.
    int due = static_cast<int>(newSamples_ / static_cast<std::size_t>(hop_));
    if (due > 4) due = 4;
    if (due > 0) newSamples_ = 0;
    for (int i = 0; i < due; ++i) {
        if (totalFed_ >= ring_.size()) computeFrame();
    }
    return hasFrame_;
}

void SpectrumProcessor::computeFrame() {
    const std::size_t n = ring_.size();
    // Unroll the ring so frame_[0] is the oldest sample, then window.
    for (std::size_t i = 0; i < n; ++i) {
        frame_[i] = ring_[(ringPos_ + i) % n] * window_.coeff[i];
    }
    fft_->forward(frame_.data(), fftOut_.data());

    const std::size_t nb = avgPower_.size();
    for (std::size_t k = 0; k < nb; ++k) {
        double re, im;
        if (k == 0) {
            re = fftOut_[0];
            im = 0.0;
        } else if (k == nb - 1) {
            re = fftOut_[1];
            im = 0.0;
        } else {
            re = fftOut_[2 * k];
            im = fftOut_[2 * k + 1];
        }
        const double p = re * re + im * im;
        if (!hasFrame_) {
            avgPower_[k] = p;
        } else {
            avgPower_[k] += avgAlpha_ * (p - avgPower_[k]);
        }
        if (p > peakPower_[k]) peakPower_[k] = p;
    }
    hasFrame_ = true;
}

void SpectrumProcessor::fillDb(const std::vector<double>& power, float* out,
                               bool psd) const {
    const std::size_t nb = power.size();
    const double s1 = window_.s1;
    const double s2 = window_.s2;
    for (std::size_t k = 0; k < nb; ++k) {
        const bool edge = (k == 0 || k == nb - 1);  // DC and Nyquist
        double db;
        if (psd) {
            // One-sided PSD: 2|X|^2 / (fs * S2); no doubling at the edges.
            db = toDb((edge ? 1.0 : 2.0) * power[k] / (fs_ * s2));
        } else {
            // Amplitude: 2|X| / S1 -> power (2/S1)^2 * |X|^2.
            const double g = (edge ? 1.0 : 2.0) / s1;
            db = toDb(g * g * power[k]);
        }
        out[k] = static_cast<float>(db);
    }
}

bool SpectrumProcessor::readAverage(float* out, bool psd) const {
    if (!hasFrame_) return false;
    fillDb(avgPower_, out, psd);
    return true;
}

bool SpectrumProcessor::readPeak(float* out) const {
    if (!hasFrame_) return false;
    fillDb(peakPower_, out, false);
    return true;
}

void SpectrumProcessor::resetPeak() {
    std::fill(peakPower_.begin(), peakPower_.end(), 0.0);
}

}  // namespace aa::dsp
