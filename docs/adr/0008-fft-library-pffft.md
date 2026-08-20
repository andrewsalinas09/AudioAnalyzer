# ADR-0008: FFT library — vendored PFFFT

- Status: Accepted
- Date: 2026-08-19
- Executes the plan recorded in ADR-0003's "FFT library" note.

## Context

Phase 2 (RTA) needs a fast single-precision real FFT on ARM. FFTW is GPL —
excluded by license policy. Candidates: PFFFT (FFTPACK license, BSD-style),
KissFFT (BSD-3), pocketfft.

## Decision

Vendor the original two-file **PFFFT** (Julien Pommier,
bitbucket.org/jpommier/pffft) into
`core/audio/src/main/cpp/dsp/third_party/pffft/`, license header intact,
NOTICE entry added. Reasons: NEON-vectorized on ARM (with scalar fallback),
tiny (one .c + one .h), stable for a decade, and its constraint (N a multiple
of 32) is irrelevant since we use power-of-two sizes.

The raw API (aligned buffers, unordered/ordered transforms) is wrapped in
`dsp/fft.h` (`RealFft`), which owns the aligned allocations and always uses
the ordered output layout. Only the wrapper is used by our DSP code, so the
library is swappable behind one class.

## Consequences

- `aa_dsp` gains a C source; the dsp CMake project declares `LANGUAGES C CXX`.
- Correctness of the wrapper (output layout, scaling) is pinned by host tests
  against a naive DFT (`spectrum_test.cpp`).
- Updates to the vendored files must preserve the license header and be noted
  in NOTICE.
