# ADR-0003: DSP as a pure, host-testable C++ static library

- Status: Accepted
- Date: 2026-08-19

## Context

DSP correctness (FFT normalization, weighting filters, deconvolution) is
where measurement tools silently go wrong. Unit tests against analytically
known signals must run in seconds on a developer machine and in CI — not on
an Android device.

## Decision

`core/audio/src/main/cpp/dsp/` is a static library (`aa_dsp`) with **zero
Android/JNI/Oboe dependencies**. Its `CMakeLists.txt` configures standalone
on any host toolchain, where it also builds an assert-based test executable
registered with CTest (`scripts/host-dsp-tests.ps1` runs it; see
`docs/building.md`). The Android build consumes the same sources via
`add_subdirectory()`.

Test framework: plain asserts for now; adopting GoogleTest (or similar) is
deferred until the DSP surface grows (Phase 2, FFT work) — that adoption will
be its own ADR if it happens.

## Consequences

- Every DSP routine added must come with host tests against known signals
  (documented per-feature in `docs/validation/`).
- Nothing in `dsp/` may include Android headers; the engine adapts, not the
  DSP.

## FFT library (recorded here for Phase 2)

FFTW is GPL — incompatible with our Apache-2.0 distribution. The plan of
record is **pffft** (BSD-like FFTPACK license, single-file, SIMD-optimized,
NEON support), vendored into `dsp/third_party/` with its license header
intact and an entry added to `NOTICE`. KissFFT (BSD-3) is the fallback if
pffft's real-FFT constraints (N multiple of 32) prove awkward. Final choice
becomes an ADR when the FFT lands.
