#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "fft.h"
#include "window.h"

namespace aa::dsp {

// RTA spectrum engine: windowed real FFT over the most recent fftSize
// samples with 75 % overlap, exponential averaging in the power domain, and
// a separate peak-hold trace.
//
// Normalization conventions (verified in spectrum_test.cpp):
//  - Amplitude spectrum, dBFS: a full-scale sine reads 0 dBFS at its bin
//    regardless of window (2|X|/S1; DC and Nyquist without the factor 2).
//  - PSD, dBFS/Hz: 2|X|^2 / (fs * S2) — white noise of variance v reads
//    10*log10(v / (fs/2)) flat across the band.
//
// Single-channel; not thread-safe.
class SpectrumProcessor {
public:
    // fftSize: power of two >= 32. avgTimeConstSec: exponential averaging
    // time constant (0 = no averaging, every frame replaces the last).
    void configure(double sampleRateHz, int fftSize, WindowType window,
                   double avgTimeConstSec);

    // Push one channel of an interleaved buffer into the analysis ring.
    void feed(const float* interleaved, std::size_t frames, int channelCount,
              int channel);

    // Runs any due FFTs (one per hop of newly fed samples). Returns true if
    // at least one frame was computed since configure/last reset.
    bool compute();

    void resetPeak();

    int bins() const { return fftSize_ / 2 + 1; }
    double binHz() const { return fs_ / fftSize_; }

    // Fill out[bins()] with levels in dB. psd selects density scaling.
    // Returns false while no frame has been computed yet.
    bool readAverage(float* out, bool psd) const;
    bool readPeak(float* out) const;

private:
    void computeFrame();
    void fillDb(const std::vector<double>& power, float* out, bool psd) const;

    double fs_ = 48000.0;
    int fftSize_ = 8192;
    int hop_ = 2048;
    double avgAlpha_ = 1.0;
    Window window_;
    std::unique_ptr<RealFft> fft_;

    std::vector<float> ring_;   // mono sample history, size fftSize
    std::size_t ringPos_ = 0;
    std::size_t newSamples_ = 0;
    std::uint64_t totalFed_ = 0;

    std::vector<float> frame_;      // scratch: windowed samples
    std::vector<float> fftOut_;     // scratch: ordered FFT output
    std::vector<double> avgPower_;  // per-bin averaged |X|^2
    std::vector<double> peakPower_;
    bool hasFrame_ = false;
};

}  // namespace aa::dsp
