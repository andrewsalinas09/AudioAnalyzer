# Sync frame specification (v0 — draft)

Status: **v0 implemented** (`dsp/syncframe.h/.cpp`, host-verified —
validation entry 008); frozen as v1 when the Phase 4 hardware pass confirms
the parameters. Decision record:
[ADR-0007](../adr/0007-sync-frame-for-multi-device.md).

## Purpose

Wrap every emitted measurement signal so that any capture device — the same
phone, or another device across the room — can (a) find t₀ with
sub-millisecond precision and (b) estimate the emitter/capturer clock-rate
ratio, with no cable or network connection.

## Frame layout

```
[ preamble | guard | payload | guard | postamble ]
```

| Segment | Content (v0 values, subject to freeze) |
| --- | --- |
| preamble | linear up-chirp, 1 kHz → 4 kHz, 100 ms, raised-cosine faded (5 ms) |
| guard | 250 ms silence |
| payload | the actual measurement signal (exponential sweep, tone, noise…) |
| guard | 250 ms silence |
| postamble | time-reversed copy of the preamble chirp (down-chirp) |

Rationale for the choices to validate before freezing:

- A chirp's autocorrelation is sharp, and matched filtering is robust at low
  SNR and in reverberant rooms.
- The 1–4 kHz band avoids both small-speaker LF rolloff and HF directivity /
  absorption problems at distance.
- Using a *down*-chirp for the postamble makes the two markers mutually
  non-correlating, so late room reflections of the preamble can't be
  mistaken for the postamble.

## Capture-side processing

1. **Detection**: cross-correlate the recording with the known preamble
   (matched filter); the correlation peak is t₀. Same for the postamble.
   Peak interpolation (parabolic, over the correlation magnitude) gives
   sub-sample timing.
2. **Drift estimate**: nominal marker spacing `D_nom` (in emitter samples) is
   known from the frame parameters; the measured spacing `D_meas` (in
   capturer samples) gives the clock-rate ratio `r = D_meas / D_nom`.
3. **Correction**: resample the payload region by `1/r` before analysis.
   At 48 kHz, 30 ppm over a 10 s sweep is ~14 samples of accumulated slip —
   uncorrected, this visibly smears an impulse response above ~5 kHz.
4. The capture device's own ADC-vs-CLOCK_MONOTONIC drift (measured
   continuously by the engine's timestamp regression) is reported alongside,
   so a saved measurement records both numbers.

## Compatibility

- The frame parameters (chirp band/length, guard lengths, fade shape) will be
  versioned; a capture device must know which frame version it is listening
  for. v0 is not a compatibility promise — the freeze happens when Phase 3
  ships and this doc drops its "draft" marker.
