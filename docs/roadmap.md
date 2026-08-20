# Roadmap

Phases build strictly on each other; a phase is done when its features have
validation entries (see [validation/](validation/README.md)) and its
decisions have ADRs.

## Phase 0 — Skeleton + Audio Health ✅ (2026-08-19)

- Multi-module Gradle project (`app`, `core:audio` with C++/Oboe engine,
  `core:calibration`), pinned toolchain ([building.md](building.md)).
- Native input engine: real-time-safe capture, callback-interval histogram,
  XRun counter, hardware-timestamp regression → measured sample rate & drift
  (ppm).
- **Audio Health screen**: device picker (USB mics first), input preset
  selection with UNPROCESSED verification, stream config, live levels,
  clock & callback statistics.
- Calibration parser for all three dialects with real-file unit tests.
- Docs baseline: ADRs 0001–0007, format specs, this roadmap.

## Phase 1 — Level engine (SPL meter) — largely ✅ (2026-08-19)

Done: SPL meter (weightings, detectors, Leq/LN), calibration import/UI with
raw-header display, manual trim, SPL time log with chart + CSV export.
Open: WAV file import; calibrator-based absolute-SPL validation (entry 005).

Original scope:

- dBFS and dB SPL via calibration sensitivity + manual trim.
- IEC 61672 A/C/Z weighting biquads, validated against the standard's
  tolerance tables (validation entry required).
- Fast / Slow / Impulse time weightings; Leq, LAmax/LAmin, LN percentiles
  (L10/L90); SPL-vs-time logging with CSV export.
- Calibration settings UI: file import (SAF), raw header + first rows shown
  verbatim beside the parsed values.
- WAV file import: the analysis pipeline accepts a file source as an
  alternative to the live engine (also the test path for everything above).

## Phase 2 — RTA + spectrogram — in progress

Done 2026-08-19: pffft vendored (ADR-0008), spectrum engine with validated
amplitude/PSD normalization (entry 006), RTA screen (log-f plot, octave
smoothing, peak hold, tap cursor, per-bin cal correction).
Open: spectrogram/waterfall, linear averaging mode.

Original scope:

- FFT (vendored pffft or KissFFT — final ADR then), windows (Hann,
  flat-top, rectangular), correct magnitude/PSD normalization (dBFS per bin
  vs dBFS/√Hz, documented and validated against synthetic noise).
- Phase display; 1/1…1/48 octave smoothing; exponential/linear/peak-hold
  averaging.
- The shared plot component (log-f axes, cursors, touch zoom) — hand-drawn
  Canvas per ADR-0005.
- Spectrogram/waterfall.

## Phase 3 — Signal generator + sync frame — largely ✅ (2026-08-19)

Done: output engine (RT-safe synth callback + pre-rendered one-shots), sine /
white / pink / exp & lin sweeps, level control, output device selection, sync
frame render + matched-filter detector with drift estimation (host-verified,
entries 008/010), Gen tab with sweep progress.
Open: multitone/warble, loopback round-trip latency test, output level checks.

Original scope:

- Sine (dithered), exponential/linear sweeps, white/pink noise, warble,
  multitone; output level control with dBFS calibration.
- The sync frame per [formats/sync-frame.md](formats/sync-frame.md);
  spec frozen (v1) when this ships.
- Loopback round-trip latency test (validates detection + timing end to end).

## Phase 4 — Impulse response — core ✅ (2026-08-19)

Done: regularized deconvolution, FFT-based sync detection + drift-correcting
resample, ETC, Schroeder RT60 (EDT/T20/T30), C50/C80, windowed magnitude +
excess group delay — all verified against a synthetic room (entry 007). IR
tab: one-tap measure (capture + sync-framed sweep, full duplex), results
card, ETC/magnitude/group-delay plots.
Open: IR export (WAV), mag/GD smoothing + cursor, octave-band RT60, CSD
waterfall, REW cross-check (entry 009), two-device measurement UX.

Original scope:

- Farina exponential-sweep deconvolution → IR.
- Preamble/postamble drift correction (multi-device across the room).
- Derived views: magnitude, phase, group delay + excess group delay
  (minimum-phase via Hilbert), ETC, Schroeder integration → RT60
  (T20/T30/EDT), C50/C80, CSD waterfall.
- IR export as WAV; measurement export in REW-compatible text.

## Phase 5 — Distortion & dual-channel

- THD / THD+N, harmonic overlays, stepped-sine distortion vs level.
- Coherence and true transfer function with a reference channel (USB audio
  interface).
- Measurement session storage (Room DB) with full Audio Health metadata
  embedded per measurement.

## Deliberately out of scope (for now)

- Networked multi-device coordination (remote trigger/collection) — layers
  on the sync frame later (ADR-0007).
- iOS, desktop.
