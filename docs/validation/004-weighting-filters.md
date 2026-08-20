# 004 — A/C/Z frequency-weighting filters vs IEC 61672-1

- Date: 2026-08-19
- Method: analytic frequency response of the designed biquad cascades
  (no signals), checked against the IEC 61672-1 nominal table at the
  standard third-octave frequencies. Automated in
  `core/audio/src/main/cpp/dsp/tests/weighting_test.cpp`, runs on every
  host-test invocation.

## Design

Analog prototypes (poles 20.6 / 107.7 / 737.9 / 12194.2 Hz) discretized by
bilinear transform with per-pole prewarping; the prewarp amount of the
12.2 kHz pole pair is bisected at design time so the digital response equals
the exact analog value at 8 kHz. Gain normalized to 0 dB at 1 kHz
(`weighting.cpp`).

## Measured deviation from nominal (A-weighting)

| Freq | 44.1 kHz | 48 kHz | 96 kHz |
| --- | --- | --- | --- |
| 20 Hz – 4 kHz | ≤ ±0.15 | ≤ ±0.15 | ≤ ±0.15 |
| 5 kHz | +0.22 | +0.20 | +0.09 |
| 6.3 kHz | +0.15 | +0.12 | +0.02 |
| 8 kHz | −0.05 | −0.05 | −0.05 |
| 10 kHz | −0.54 | −0.43 | −0.09 |
| 12.5 kHz | −2.01 | −1.57 | −0.27 |
| 16 kHz | −6.90 | −5.16 | −0.95 |

C-weighting behaves identically at the high end (same pole pair) and is
within ±0.1 dB below 8 kHz. Z is exactly flat (empty cascade).

## Verdict

- **20 Hz – 10 kHz: within IEC 61672-1 class 1 acceptance limits** at all
  three rates (our own test bounds are much tighter than class 1 below
  8 kHz).
- 12.5–16 kHz at 44.1/48 kHz: response reads low (bilinear compression
  toward Nyquist). The standard's *lower* acceptance limits in this region
  are strongly relaxed (−∞ at 20 kHz for class 2, double-digit dB for
  class 1), so the deviations remain inside the standard, but they are
  systematic — broadband A/C SPL of very HF-rich signals will read slightly
  low at 48 kHz. Capturing at 96 kHz reduces the 16 kHz error to −0.95 dB.
- Bounds are enforced by the host test; regressions fail CI.
