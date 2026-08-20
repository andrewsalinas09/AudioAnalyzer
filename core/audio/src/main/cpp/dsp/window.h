#pragma once
#include <cstddef>
#include <vector>

namespace aa::dsp {

enum class WindowType : int {
    Rectangular = 0,
    Hann = 1,
    // 5-term flat-top (SFT5, HP/Stanford coefficients): near-zero scalloping
    // loss, ideal for amplitude-accurate sine measurements.
    FlatTop = 2,
};

struct Window {
    std::vector<float> coeff;
    double s1 = 0;  // sum of coefficients      (amplitude normalization)
    double s2 = 0;  // sum of squared coeffs    (power/PSD normalization)

    // Equivalent noise bandwidth in bins: N * S2 / S1^2.
    double enbwBins() const {
        return static_cast<double>(coeff.size()) * s2 / (s1 * s1);
    }
};

Window makeWindow(WindowType type, std::size_t n);

}  // namespace aa::dsp
