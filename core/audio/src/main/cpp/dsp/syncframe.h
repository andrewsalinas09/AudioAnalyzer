#pragma once
#include <cstddef>
#include <vector>

namespace aa::dsp {

// Acoustic sync frame (docs/formats/sync-frame.md, v0; ADR-0007):
//   [ preamble up-chirp | guard | payload | guard | postamble down-chirp ]
// The postamble is the time-reversed preamble, so the two markers do not
// correlate with each other's reflections.
struct SyncFrameSpec {
    double chirpF1Hz = 1000.0;
    double chirpF2Hz = 4000.0;
    double chirpSec = 0.100;
    double guardSec = 0.250;
    double fadeSec = 0.005;

    std::size_t chirpSamples(double fs) const {
        return static_cast<std::size_t>(chirpSec * fs);
    }
    std::size_t guardSamples(double fs) const {
        return static_cast<std::size_t>(guardSec * fs);
    }
};

// Linear up-chirp (or its time reverse) with raised-cosine fades, unit
// amplitude. This is both the emitted marker and the matched-filter template.
std::vector<float> renderSyncChirp(double fs, const SyncFrameSpec& spec,
                                   bool reversed);

// Wraps a payload in the full frame at the given marker amplitude.
std::vector<float> wrapWithSyncFrame(const std::vector<float>& payload,
                                     double fs, const SyncFrameSpec& spec,
                                     double markerAmplitude);

struct SyncDetection {
    bool found = false;
    // Sub-sample positions (start of each marker) in the analyzed buffer.
    double preambleStart = 0.0;
    double postambleStart = 0.0;
    // Measured / nominal marker spacing: capture-clock over emit-clock rate
    // ratio. 1.0 means the clocks agree.
    double clockRatio = 1.0;
    double preamblePeak = 0.0;   // normalized correlation peak, 0..1
    double postamblePeak = 0.0;
};

// Finds preamble and postamble by normalized cross-correlation and derives
// the clock-rate ratio from their spacing. payloadSamples is the payload
// length in emitter samples (defines the nominal marker spacing).
//
// Direct O(N*M) correlation — fine for host tests and offline analysis;
// switch to FFT-based correlation for long on-device captures (Phase 4).
SyncDetection detectSyncFrame(const float* x, std::size_t n, double fs,
                              const SyncFrameSpec& spec,
                              std::size_t payloadSamples);

}  // namespace aa::dsp
