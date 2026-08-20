#pragma once
#include "biquad.h"

namespace aa::dsp {

enum class Weighting : int {
    Z = 0,  // no weighting (flat)
    A = 1,  // IEC 61672-1 A-weighting
    C = 2,  // IEC 61672-1 C-weighting
};

// Designs the frequency-weighting filter for the given sample rate as a
// biquad cascade (empty cascade for Z). The analog prototypes from
// IEC 61672-1 are discretized with the bilinear transform and the overall
// gain is normalized to exactly 0 dB at 1 kHz. See the host tests for the
// verification against the standard's table values.
BiquadCascade designWeighting(Weighting type, double sampleRateHz);

}  // namespace aa::dsp
