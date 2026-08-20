# 003 — Audio Health hardware pass (Phase 0)

- Date: 2026-08-19
- Device: Samsung Galaxy S25 Ultra (SM-S938U1), Android 16
- App: commit `47357c8` (debug build)
- Setup: ~60 s runs on the Audio Health screen, quiet room; screenshots
  taken on-device at 21:19 (built-in mic) and 21:20 (iMM-6C on USB-C).

## Results

| Metric | Built-in mic (id 22) | Dayton iMM-6C (id 3159) |
| --- | --- | --- |
| Audio API / rate / channels | AAudio, 48 kHz, 2 ch | AAudio, 48 kHz, 2 ch |
| Frames per burst | 96 (2.0 ms) | 96 (2.0 ms) |
| Sharing / performance | Exclusive / Low latency | Exclusive / Low latency |
| MMAP | Yes | Yes |
| Input preset granted | **Unprocessed** | **Unprocessed** |
| Measured sample rate | 47999.926 Hz | 47999.804 Hz |
| Clock drift | **−1.5 ppm** | **−4.1 ppm** |
| Callback interval mean / p99 / max | 2.00 / 2.30 / 5.30 ms | 2.00 / 2.40 / 5.88 ms |
| XRuns | 0 (15 685 callbacks) | 0 (16 123 callbacks) |

## Findings

1. **Full measurement-grade path on both inputs**: exclusive MMAP low-latency
   streams with the Unprocessed preset granted — no hidden platform DSP.
2. **Clock-drift measurement is plausible and input-specific**: the two
   inputs converge to different, stable, single-digit-ppm values, consistent
   with reading each ADC's actual crystal. (Cross-check against a second
   independent reference is still open — this entry validates plumbing and
   plausibility, not absolute accuracy.)
3. **Scheduling is healthy**: p99 within 0.4 ms of the 2 ms burst cadence,
   occasional ~6 ms outlier, zero XRuns.
4. **Platform quirk found**: Samsung does not declare
   `PROPERTY_SUPPORT_AUDIO_SOURCE_UNPROCESSED` yet grants the preset
   per-stream. The UI was changed to warn on actual per-stream denial
   instead (commit `47357c8`).
5. The iMM-6C (mono capsule) is exposed as a 2-channel stream with
   identical channels; UI should collapse mono sources (open item).

## Verdict

Phase 0 engine instrumentation **passes** on reference hardware. Open
follow-ups: absolute drift cross-check (two-device chirp test, entry 008),
mono-source display.
