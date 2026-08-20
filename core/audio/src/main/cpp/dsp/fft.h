#pragma once
#include <cstddef>

struct PFFFT_Setup;  // from third_party/pffft

namespace aa::dsp {

// RAII wrapper around PFFFT's real forward transform with ordered output.
// Hides the aligned-allocation requirements and the raw C API so the FFT
// backend is swappable behind this class (ADR-0008).
//
// Ordered real-FFT output layout for size n (n floats):
//   out[0]        = X[0].re      (DC)
//   out[1]        = X[n/2].re    (Nyquist)
//   out[2k],[2k+1] = X[k].re, X[k].im   for k = 1 .. n/2-1
class RealFft {
public:
    // n must be a power of two >= 32 (PFFFT: multiple of 32).
    explicit RealFft(int n);
    ~RealFft();
    RealFft(const RealFft&) = delete;
    RealFft& operator=(const RealFft&) = delete;

    int size() const { return n_; }

    // in: n samples; out: n floats in the ordered layout above.
    // Neither needs special alignment (copied through aligned scratch).
    void forward(const float* in, float* out);

private:
    int n_;
    PFFFT_Setup* setup_;
    float* alignedIn_;
    float* alignedOut_;
    float* work_;
};

}  // namespace aa::dsp
