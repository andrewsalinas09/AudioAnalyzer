# AudioAnalyzer

An open-source (Apache-2.0) **acoustic measurement tool for Android** — think
"mini [REW](https://www.roomeqwizard.com/)" on a phone. Measurement fidelity
comes first: the app instruments its own audio path (sample-clock drift,
callback jitter, underruns, input processing) so you always know whether a
result can be trusted.

> Status: **Phase 0** — project skeleton, native audio engine, and the
> Audio Health diagnostics screen. See [docs/roadmap.md](docs/roadmap.md).

## Planned feature set

- **SPL meter** — dBFS / dB SPL, A/C/Z weighting, Fast/Slow/Impulse time
  weighting, Leq, LN percentiles, level logging
- **RTA** — FFT magnitude / PSD / phase, octave smoothing (1/1 … 1/48),
  spectrogram / waterfall
- **Signal generator** — sine, sweeps, pink/white noise, warble, multitone,
  and a *sync frame* (chirp preamble/postamble) so any second device across
  the room can time-align and drift-correct without cables or networking
- **Impulse response** — exponential-sweep (Farina) deconvolution: magnitude,
  phase, group delay, RT60, ETC, C50/C80, waterfall
- **Calibration files** — miniDSP UMIK-1/2, Dayton iMM-6/6C, OmniMic V2
  dialects ([format docs](docs/formats/calibration-files.md))
- **USB measurement mics** — UAC devices (UMIK-2, iMM-6C, …) via USB-C,
  alongside the built-in mic with raw/UNPROCESSED input
- **Audio Health** — measured sample rate vs nominal (ppm), callback-interval
  statistics, XRun counts, MMAP/exclusive status, input-preset verification

## Architecture (short version)

| Layer | Tech | Where |
| --- | --- | --- |
| UI | Jetpack Compose (plots hand-drawn on Canvas) | `app/` |
| Audio I/O | C++ / [Oboe](https://github.com/google/oboe) (AAudio), real-time-safe callback | `core/audio/src/main/cpp/engine/` |
| DSP | Pure C++ static lib, host-testable, no Android deps | `core/audio/src/main/cpp/dsp/` |
| Calibration | Pure Kotlin/JVM module, host-testable | `core/calibration/` |

Every architectural decision is recorded as an ADR in
[docs/adr/](docs/adr/README.md). Measurement features are verified against
known references — see [docs/validation/](docs/validation/README.md).

## Building

Full, from-scratch instructions (exact toolchain versions and paths) live in
**[docs/building.md](docs/building.md)**. Short version:

```
# Windows (PowerShell), from the repo root
.\gradlew.bat :app:assembleDebug        # build the APK
.\gradlew.bat :core:calibration:test    # host-side parser tests
.\scripts\host-dsp-tests.ps1            # host-side C++ DSP tests
```

## License

[Apache License 2.0](LICENSE). See also [NOTICE](NOTICE).
