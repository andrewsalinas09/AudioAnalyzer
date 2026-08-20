# 008 — Sync frame: host round-trip (render → detect → drift estimate)

- Date: 2026-08-19
- Method: host tests in `core/audio/src/main/cpp/dsp/tests/syncframe_test.cpp`
  (run on every host-test invocation). This validates the v0 frame design
  (docs/formats/sync-frame.md, ADR-0007) in software; the two-device
  hardware pass remains open (this ledger row stays half-done until then).

## What is verified

Frame: 100 ms 1→4 kHz chirp markers, 250 ms guards, 1 s exponential-sweep
payload, at 48 kHz.

1. **Clean capture, known offset**: matched-filter detection finds the
   preamble start within **±0.5 samples** (~10 µs) via parabolic
   sub-sample interpolation; normalized correlation peaks ≈ 1.0.
2. **Noisy capture** (marker-to-noise ≈ 10 dB, full-band white noise):
   detection still within ±1 sample.
3. **Clock drift**: capture resampled by 500 ppm (linear interpolation);
   the preamble→postamble spacing recovers the clock ratio within
   **20 ppm** — an order of magnitude tighter than typical device crystal
   error, and enough to keep a 10 s sweep aligned to ~1 sample.

## Notes / open items

- Detector uses direct O(N·M) correlation — fine offline; the on-device
  Phase 4 capture path should switch to FFT-based correlation for long
  recordings.
- Hardware pass (phone speaker → second device across a room: detection
  robustness vs distance/reverb, real two-crystal drift) is the remaining
  half of this entry, planned with Phase 4.
