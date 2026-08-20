# Measurement validation

A measurement feature is not done when it renders — it is done when its
numbers have been checked against a known reference and the check is written
down here. Each feature gets a file `NNN-<topic>.md` recording: what was
tested, against which reference, with which setup (device, mic, versions),
the numbers obtained, and the acceptance criterion.

## Validation ledger

| # | Topic | Reference | Status |
| --- | --- | --- | --- |
| 001 | Level computation (RMS/peak/dBFS) | Analytic (full-scale sine = −3.01 dBFS RMS) | ✅ host test `dsp/tests/levels_test.cpp` |
| 002 | Calibration parsing | Real vendor files (UMIK-2, OmniMic V2, iMM-6C) | ✅ unit tests `core:calibration` |
| 003 | Engine instrumentation on hardware | S25 Ultra, built-in mic + iMM-6C ([report](003-audio-health-hardware-pass.md)) | ✅ 2026-08-19 (absolute drift cross-check → 008) |
| 004 | A/C/Z weighting filters | IEC 61672-1 tolerance tables ([report](004-weighting-filters.md)) | ✅ host test `weighting_test.cpp` |
| 005 | SPL absolute level | 94 dB calibrator / reference SLM | planned (Phase 1) |
| 006 | FFT magnitude & PSD normalization | Analytic sine + synthetic white noise ([report](006-spectrum-normalization.md)) | ✅ host test `spectrum_test.cpp` |
| 007 | Sweep → IR → group delay | Synthetic known system (all-pass/delay chains) | planned (Phase 4) |
| 008 | Cross-device sync & drift correction | Host round-trip ✅ ([report](008-syncframe-host-roundtrip.md)); two-device hardware pass planned (Phase 4) | partial |
| 009 | End-to-end vs REW | Same mic, same room, desktop REW comparison | planned (Phase 4) |
| 010 | Generator signal accuracy | Analyzed with the validated spectrum engine ([report](010-generator-signals.md)) | ✅ host test `generator_test.cpp` |

Host tests are the first line (they run in CI on every change); hardware
passes are recorded as dated reports because they depend on physical setup.
