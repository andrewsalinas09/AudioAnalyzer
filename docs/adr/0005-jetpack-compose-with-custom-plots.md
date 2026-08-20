# ADR-0005: Jetpack Compose UI with hand-drawn plots

- Status: Accepted
- Date: 2026-08-19

## Context

The app needs ordinary screens (settings, device pickers, measurement
management) *and* demanding real-time visualizations: an RTA redrawing at
display rate, log-frequency axes, octave smoothing, cursors, spectrograms and
waterfalls.

## Options considered

1. **View system + a charting library** (MPAndroidChart etc.): charting
   libraries are built for business charts; none handle log-frequency axes,
   dB scaling, per-frame updates, and measurement cursors without being
   fought constantly.
2. **Compose + a Compose charting library**: same mismatch, younger
   libraries.
3. **Compose for chrome, custom plot rendering** (chosen): REW-class tools
   always own their plot renderer; there is no shortcut.

## Decision

Jetpack Compose (Material 3) for all UI. Plots are drawn by hand on Compose
`Canvas`; if profiling shows Canvas can't sustain the spectrogram/waterfall,
that surface moves to `GLSurfaceView`/AGSL interop behind the same composable
API (that change would be a new ADR).

## Consequences

- We own axis math, tick placement, and hit-testing — built once as a shared
  plot component when the RTA lands (Phase 2), then reused by every feature.
- No charting dependencies to license-check or fight.
