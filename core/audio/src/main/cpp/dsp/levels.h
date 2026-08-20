#pragma once
#include <cstddef>

namespace aa::dsp {

struct LevelStats {
    float rms = 0.0f;   // linear, 0..1 for full-scale float audio
    float peak = 0.0f;  // linear absolute peak
};

// Computes RMS and absolute peak of one channel of an interleaved float
// buffer. `frames` is the number of frames (not samples); `channel` selects
// which interleaved channel to measure.
LevelStats computeLevels(const float* interleaved, std::size_t frames,
                         int channelCount, int channel);

// 20*log10(linear), floored at -200 dB so silence never produces -inf/NaN.
float toDbfs(float linear);

}  // namespace aa::dsp
