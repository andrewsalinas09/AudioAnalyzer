#pragma once
#include <cstddef>
#include <vector>

#include "syncframe.h"

namespace aa::dsp {

// Linear-interpolation resample by ratio r: output sample i reads input at
// position i*r (drift correction: r = measured clockRatio).
std::vector<float> resampleLinear(const std::vector<float>& x, double ratio);

// FFT-based cross-correlation: returns c[k] = sum_i x[k+i]*h[i] for
// k = 0 .. x.size()-h.size() (same quantity the direct detector computes,
// but O(N log N) so it works on multi-second captures).
std::vector<float> fftXcorr(const std::vector<float>& x,
                            const std::vector<float>& h);

// Sync-frame detection on long captures, using fftXcorr. Same semantics as
// detectSyncFrame (syncframe.h).
SyncDetection detectSyncFrameFft(const std::vector<float>& x, double fs,
                                 const SyncFrameSpec& spec,
                                 std::size_t payloadSamples);

// Regularized frequency-domain deconvolution:
//   IR = IFFT( X(f) * conj(S(f)) / (|S(f)|^2 + eps) )
// where S is the reference sweep and X the capture. eps is a fraction of
// max|S|^2, which behaves like the matched inverse filter in-band and
// suppresses noise blow-up out-of-band. The result is normalized so that
// deconvolving the reference against itself yields a unit impulse at t = 0.
// irLength caps the returned IR (samples).
std::vector<float> deconvolve(const std::vector<float>& capture,
                              const std::vector<float>& reference,
                              std::size_t irLength,
                              double epsRel = 1e-4);

}  // namespace aa::dsp
