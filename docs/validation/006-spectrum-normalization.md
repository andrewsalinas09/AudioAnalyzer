# 006 — FFT spectrum & PSD normalization

- Date: 2026-08-19
- Method: host tests in `core/audio/src/main/cpp/dsp/tests/spectrum_test.cpp`
  (run on every host-test invocation).

## What is verified

1. **FFT wrapper vs naive DFT** (N = 64, deterministic noise): PFFFT's
   ordered real output layout (DC, Nyquist, then re/im pairs) matches a
   direct DFT to 1e-3 — pins the vendored library's conventions (ADR-0008).
2. **Amplitude spectrum**: a full-scale bin-centered sine reads
   **0 dBFS ± 0.05** at its bin for rectangular, Hann, and flat-top windows
   (2|X|/S1 normalization). Flat-top additionally verified at the worst-case
   half-bin offset: 0 dBFS ± 0.1 (that is what flat-top is for; Hann scallops
   up to −1.4 dB between bins by design).
3. **PSD**: white noise of variance 1/3 reads 10·log10((1/3)/(fs/2)) =
   −48.57 dBFS/Hz; measured −48.61/−48.62 across windows (2|X|²/(fs·S2)
   normalization, no doubling at DC/Nyquist).
4. **Averaging & peak hold**: exponential power-domain averaging decays after
   the signal stops; the peak trace holds until explicitly reset.

## Display-side (not yet independently validated)

- Fractional-octave smoothing (power-domain, prefix-sum) and per-bin
  microphone calibration correction happen in Kotlin (`RtaMath.kt`);
  they get a validation entry when the loopback rig exists (entry 009's
  REW comparison will cover both).
