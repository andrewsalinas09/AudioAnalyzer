# ADR-0007: Acoustic sync frame instead of networked coordination

- Status: Accepted
- Date: 2026-08-19

## Context

Measurements should work with the emitter and analyzer on different devices
("phone on the couch, tablet at the listening position"), and multiple
capture devices should be usable across a room. Two devices have independent
ADC/DAC crystals: tens of ppm of clock drift over a 10 s sweep smears a
deconvolved impulse response at high frequencies, and there is no shared
t = 0.

## Options considered

1. **Networked coordination** (Wi-Fi discovery, master/slave triggering):
   heavy scope — protocol, pairing UI, result merging — and *still* doesn't
   solve sample-clock drift, only coarse triggering.
2. **Acoustic sync frame** (chosen): put the synchronization into the signal
   itself, like REW's acoustic timing reference, extended with a postamble
   for drift estimation.

## Decision

Every generated measurement signal is wrapped in a fixed frame:

```
[ preamble chirp | guard | payload (sweep, tone, …) | guard | postamble chirp ]
```

A capture device detects the preamble by matched filter (cross-correlation)
for sub-millisecond t₀ alignment, and compares the *apparent* preamble→
postamble interval against the nominal one to estimate clock-rate ratio,
then resamples before analysis. No cables, no network, no pairing: any
device running the app in capture mode can analyze any other device's
emission. Full signal spec: [../formats/sync-frame.md](../formats/sync-frame.md).

Networked *convenience* features (remote start, result collection) remain
possible later — they layer on top and were deliberately cut from v1.

## Consequences

- The frame layout is a compatibility surface from day one; versioned in the
  spec doc so third parties (or future us) can implement detection.
- Emitter and analyzer on the *same* device get timing sync for free through
  the same path (and the loopback case validates the implementation).
- Detection robustness (distance, noise floor, reverberance) needs its own
  validation entry when implemented (Phase 3/4).
