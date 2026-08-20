# ADR-0002: Native C++ audio core with Oboe

- Status: Accepted
- Date: 2026-08-19

## Context

This is a measurement tool first: we must be able to *quantify* the audio
path itself — actual sample clock rate vs nominal, callback jitter,
underruns, whether the low-latency (MMAP/exclusive) path is in use, and
whether the platform is applying processing to the input. We also need a
real-time-safe capture path feeding later DSP stages, and USB (UAC)
measurement microphones must work.

## Options considered

1. **Kotlin-only** (`AudioRecord`/`AudioTrack`): simplest build, but weak
   control over the stream configuration, no burst-level callback model, and
   JVM GC pauses in the capture path.
2. **Hybrid** (native I/O, Kotlin DSP): keeps timing fidelity but makes the
   DSP untestable off-device in C++ and pushes large buffers across JNI.
3. **C++ core with [Oboe](https://github.com/google/oboe)** (chosen):
   - `getTimestamp(CLOCK_MONOTONIC)` gives (frame position, monotonic time)
     pairs → measured ADC clock rate and drift in ppm.
   - XRun counts, frames-per-burst, performance/sharing mode, and
     `OboeExtensions::isMMapUsed()` are exposed.
   - Input preset control (`Unprocessed` etc.) and device selection.
   - Apache-2.0, maintained by Google, consumed as a prefab AAR
     (`com.google.oboe:oboe`).

## Decision

All audio I/O and DSP run in C++ (`core/audio/src/main/cpp/`), exposed to
Kotlin through a deliberately thin JNI bridge (`jni_bridge.cpp` is
marshalling only). One native library (`libaa_engine.so`) links the engine
plus the `aa_dsp` static library — a single JNI module keeps the build simple
while `dsp/` stays a separate, Android-free target (ADR-0003).

Real-time rules for the audio callback: no locks, no allocation, no JNI —
only the SPSC ring buffer and atomics. Everything else (snapshot, timestamp
regression, level computation) runs on caller threads.

Engine state crosses JNI as a flat `double[]` with an index enum defined in
`AudioEngine.h` and mirrored in `EngineSnapshot.kt` — crude but allocation-
free, and trivially extended.

## Consequences

- NDK/CMake in the build (versions pinned in `docs/building.md`).
- DSP correctness is testable on the host without an emulator.
- JNI surface must be kept mechanical; logic lives on one side or the other,
  never in the bridge.
