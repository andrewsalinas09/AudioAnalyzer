#include "levels.h"

#include <cmath>

namespace aa::dsp {

LevelStats computeLevels(const float* interleaved, std::size_t frames,
                         int channelCount, int channel) {
    LevelStats out;
    if (interleaved == nullptr || frames == 0 || channelCount <= 0 ||
        channel < 0 || channel >= channelCount) {
        return out;
    }
    double sumSquares = 0.0;
    float peak = 0.0f;
    for (std::size_t i = 0; i < frames; ++i) {
        const float s = interleaved[i * static_cast<std::size_t>(channelCount) +
                                    static_cast<std::size_t>(channel)];
        sumSquares += static_cast<double>(s) * static_cast<double>(s);
        const float a = std::fabs(s);
        if (a > peak) peak = a;
    }
    out.rms = static_cast<float>(std::sqrt(sumSquares / static_cast<double>(frames)));
    out.peak = peak;
    return out;
}

float toDbfs(float linear) {
    constexpr float kFloor = 1e-10f;  // -200 dB
    if (linear < kFloor) linear = kFloor;
    return 20.0f * std::log10(linear);
}

}  // namespace aa::dsp
