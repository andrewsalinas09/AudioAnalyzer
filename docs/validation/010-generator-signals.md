# 010 — Generator signal accuracy

- Date: 2026-08-19
- Method: host tests in `core/audio/src/main/cpp/dsp/tests/generator_test.cpp`,
  analyzed with the (independently validated, entry 006) spectrum engine.

## What is verified

1. **Sine**: a −6.02 dBFS 1 kHz tone measures −6.02 dBFS ± 0.1 at 1 kHz
   ± 1 bin on the flat-top spectrum (level and frequency accuracy).
2. **Pink noise** (Paul Kellet filter): PSD slope measured 100 Hz → 1 kHz
   → 10 kHz is 10 dB/decade ± 1.5 dB per decade.
3. **Exponential (Farina) sweep**: exact sample count, raised-cosine faded
   endpoints (< 1e-3 residual), amplitude bounded by the requested level.
4. **Linear sweep**: length and amplitude bounds.

## Not yet verified (device-level, later entries)

- Absolute acoustic output level (depends on the device's speaker/DAC path).
- Full-duplex behavior (generator + analyzer simultaneously) — exercised
  manually on the S25 Ultra, to be captured in the Phase 4 loopback entry.
