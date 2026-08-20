# 007 — Impulse-response pipeline vs a synthetic room

- Date: 2026-08-19
- Method: host test `core/audio/src/main/cpp/dsp/tests/ir_test.cpp` — the
  complete measurement pipeline run against a constructed room whose answer
  is known exactly. Runs on every host-test invocation.

Synthetic room: direct sound at 40 ms (0.5), discrete echo at 100 ms (0.1),
exponentially decaying diffuse tail with **RT60 = 0.400 s**. Excitation:
3 s exponential sweep 50 Hz – 16 kHz.

## Measured vs truth

| Quantity | Truth | Recovered |
| --- | --- | --- |
| Direct-sound position | 1920 samples | 1920.0 (±1.5 asserted) |
| Echo/direct amplitude | 0.200 | 0.205 |
| EDT | 0.400 s | 0.406 s |
| T20 | 0.400 s | 0.401 s |
| T30 | 0.400 s | 0.403 s |
| Flat system magnitude (200 Hz–10 kHz) | 0 dB | within ±0.5 dB |
| Flat system excess group delay | 0 ms | within ±0.5 ms |
| Clock drift (300 ppm resampled capture) | −300 ppm | −297 ppm |

Stage 3 runs the full chain — sync-framed sweep → room convolution → leading
silence → additive noise → 300 ppm clock drift — then FFT-based sync
detection, drift-correcting resample, payload extraction, regularized
deconvolution, and metrics. Direct sound recovered within 1 ms of the
expected position; T20 within 0.01 s of design.

## Notes

- The acoustic sync reference absorbs propagation delay by design: the IR
  peak lands at the analysis lead offset, not at lead + time-of-flight
  (absolute time-of-flight is not recoverable from a one-microphone acoustic
  sync, and is not needed for room acoustics).
- Test-design lesson recorded: a synthetic room whose Schroeder curve is
  dominated by a discrete echo step (weak diffuse tail) defeats *any* RT60
  estimator; the tail must carry the reverberant energy, as in real rooms.
- Remaining for this ledger row's hardware half: real-room measurement
  cross-checked against REW (entry 009).
