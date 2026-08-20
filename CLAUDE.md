# AudioAnalyzer — project conventions

Android acoustic measurement tool ("mini REW"). Measurement fidelity first.
Apache-2.0. Read `docs/roadmap.md` for where the project is.

## Build & test

```powershell
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
.\gradlew.bat :app:assembleDebug          # APK
.\gradlew.bat :core:calibration:test      # Kotlin parser tests (host)
.\scripts\host-dsp-tests.ps1              # C++ DSP tests (host, CLion toolchain)
```

Toolchain versions/paths are pinned in `docs/building.md` — update it in the
same commit as any toolchain change.

## Hard rules

- **Docs are part of done.** Architecture decisions → new ADR in `docs/adr/`
  (immutable once accepted; supersede, don't edit). New/changed formats →
  `docs/formats/`. New measurement feature → entry in
  `docs/validation/README.md` ledger plus host tests.
- **`core/audio/src/main/cpp/dsp/` stays pure C++** — no Android/JNI/Oboe
  includes ever; it must configure standalone for host tests (ADR-0003).
- **Audio callback is real-time-safe**: no locks, no allocation, no JNI —
  SPSC ring + atomics only (ADR-0002).
- **JNI bridge is marshalling only**; the snapshot double[] layout is defined
  by `SnapshotField` in `AudioEngine.h` and mirrored in `EngineSnapshot.kt` —
  change both together.
- **`core:calibration` stays pure JVM** — no Android dependencies.
- **License hygiene**: dependencies must be Apache-2.0-compatible (no GPL —
  e.g. FFTW is off-limits); vendored code keeps its license header and gets a
  `NOTICE` entry.
- minSdk is 31: no `Build.VERSION` checks for API ≤ 31 (ADR-0004).
- AGP 9 built-in Kotlin: do NOT apply `org.jetbrains.kotlin.android`.
