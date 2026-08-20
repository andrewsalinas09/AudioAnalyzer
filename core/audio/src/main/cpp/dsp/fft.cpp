#include "fft.h"

#include <cassert>
#include <cstring>

#include "third_party/pffft/pffft.h"

namespace aa::dsp {

RealFft::RealFft(int n) : n_(n) {
    setup_ = pffft_new_setup(n, PFFFT_REAL);
    assert(setup_ != nullptr && "invalid FFT size for PFFFT (needs 2^k >= 32)");
    alignedIn_ = static_cast<float*>(pffft_aligned_malloc(sizeof(float) * n));
    alignedOut_ = static_cast<float*>(pffft_aligned_malloc(sizeof(float) * n));
    work_ = static_cast<float*>(pffft_aligned_malloc(sizeof(float) * n));
}

RealFft::~RealFft() {
    pffft_destroy_setup(setup_);
    pffft_aligned_free(alignedIn_);
    pffft_aligned_free(alignedOut_);
    pffft_aligned_free(work_);
}

void RealFft::forward(const float* in, float* out) {
    std::memcpy(alignedIn_, in, sizeof(float) * static_cast<std::size_t>(n_));
    pffft_transform_ordered(setup_, alignedIn_, alignedOut_, work_,
                            PFFFT_FORWARD);
    std::memcpy(out, alignedOut_, sizeof(float) * static_cast<std::size_t>(n_));
}

}  // namespace aa::dsp
