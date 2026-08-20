#pragma once
#include <cstddef>
#include <vector>

namespace aa::dsp {

// Broadband room-acoustics metrics from an impulse response.
// Times are NaN when the corresponding Schroeder fit is not possible
// (decay too short / too noisy).
struct IrMetrics {
    double peakSample = 0.0;   // position of |ir| maximum
    double peakDb = 0.0;       // 20*log10(|peak|)
    double edtSec = 0.0;       // fit 0 .. -10 dB, x6
    double t20Sec = 0.0;       // fit -5 .. -25 dB, x3
    double t30Sec = 0.0;       // fit -5 .. -35 dB, x2
    double c50Db = 0.0;        // early/late energy ratio at 50 ms
    double c80Db = 0.0;
};

IrMetrics analyzeIr(const std::vector<float>& ir, double fs);

// Energy-time curve for display: out[n] = dB of the max |ir| in each of n
// equal buckets across the IR, normalized so the global peak is 0 dB.
void etcTrace(const std::vector<float>& ir, float* out, std::size_t n);

// Frequency response of the time-windowed IR. The window is a half-Hann
// rise over preSec before the peak and a taper to windowSec after it; the
// peak is placed at a known offset so the reported group delay is excess
// group delay (bulk time-of-flight removed).
//
// magDb/gdMs must hold fftSize/2 + 1 entries; bin k is at k*fs/fftSize Hz.
void irFrequencyResponse(const std::vector<float>& ir, double fs,
                         int fftSize, double preSec, double windowSec,
                         float* magDb, float* gdMs);

// Aligns x to ref for coherent averaging: cross-correlates a window around
// ref's peak over lags within +-maxShift of the coarse (peak-to-peak) offset,
// refines to sub-sample precision (parabolic), and returns x shifted by the
// fractional lag (linear interpolation), same length as ref.
std::vector<float> alignTo(const std::vector<float>& ref,
                           const std::vector<float>& x, int maxShift);

}  // namespace aa::dsp
