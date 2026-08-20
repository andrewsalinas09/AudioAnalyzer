# Building AudioAnalyzer

Goal of this page: someone with a clean machine can produce a working build
using only these instructions. If a build step, tool version, or path changes,
**update this file in the same commit**.

## Pinned toolchain

| Tool | Version | Pinned where |
| --- | --- | --- |
| JDK | 21 (Android Studio's bundled JBR) | `JAVA_HOME` at invocation |
| Gradle | 9.3.1 | `gradle/wrapper/gradle-wrapper.properties` |
| Android Gradle Plugin | 9.1.0 | `gradle/libs.versions.toml` |
| Kotlin | 2.2.10 | `gradle/libs.versions.toml` |
| compileSdk | 36 (minor 1) / targetSdk 36 / **minSdk 31** | module `build.gradle.kts` |
| NDK | 28.2.13676358 | `core/audio/build.gradle.kts` (`ndkVersion`) |
| CMake (Android build) | 3.31.6 | `core/audio/build.gradle.kts` (`externalNativeBuild`) |
| Oboe | 1.10.0 (prefab, from Google Maven) | `gradle/libs.versions.toml` |
| Compose BOM | 2026.02.01 | `gradle/libs.versions.toml` |

## From-scratch setup (Windows)

1. **Install Android Studio** (any recent version). It provides:
   - the SDK at `%LOCALAPPDATA%\Android\Sdk`
   - a JDK 21 at `C:\Program Files\Android\Android Studio\jbr`
2. **Install SDK packages** (Android Studio SDK Manager, or CLI):
   ```powershell
   & "$env:LOCALAPPDATA\Android\Sdk\cmdline-tools\latest\bin\sdkmanager.bat" `
       "platforms;android-36.1" "ndk;28.2.13676358" "cmake;3.31.6"
   ```
3. **Point the build at the SDK.** Create `local.properties` in the repo root
   (machine-local, gitignored):
   ```properties
   sdk.dir=C\:\\Users\\<you>\\AppData\\Local\\Android\\Sdk
   ```
4. **Build:**
   ```powershell
   $env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
   .\gradlew.bat :app:assembleDebug
   ```
   The APK lands in `app/build/outputs/apk/debug/`.

On Linux/macOS the same versions apply; paths follow the usual SDK locations
(`~/Android/Sdk`, `$ANDROID_HOME`).

## Tests

| What | Command | Runs on |
| --- | --- | --- |
| Calibration parser (Kotlin) | `.\gradlew.bat :core:calibration:test` | host JVM |
| DSP (C++) | `.\scripts\host-dsp-tests.ps1` | host, no device needed |
| Instrumented (future) | `.\gradlew.bat connectedDebugAndroidTest` | device |

### Host C++ DSP tests

`core/audio/src/main/cpp/dsp/` is a pure static library with no Android
dependencies, and its `CMakeLists.txt` is configurable standalone. The script
`scripts/host-dsp-tests.ps1` builds and runs the tests with a host toolchain.

It needs any host CMake + C++ compiler. On this project's reference machine we
use the toolchain bundled with JetBrains CLion (no separate install needed):

| Tool | Path (CLion 2025.2.5) |
| --- | --- |
| CMake 4.0.2 | `C:\Program Files\JetBrains\CLion 2025.2.5\bin\cmake\win\x64\bin\cmake.exe` |
| ctest | same directory as cmake |
| MinGW g++ 13.1 | `C:\Program Files\JetBrains\CLion 2025.2.5\bin\mingw\bin\g++.exe` |
| Ninja | `C:\Program Files\JetBrains\CLion 2025.2.5\bin\ninja\win\x64\ninja.exe` (or `...\ninja\cygwin\x64\`) |

The script auto-detects these; pass `-CLionBin <path>` if CLion lives
elsewhere, or edit the variables at the top. Any other host toolchain
(MSVC, LLVM, WSL gcc) also works with plain
`cmake -S core/audio/src/main/cpp/dsp -B <build> && cmake --build <build> && ctest`.

> Note: the *Android* build never uses CLion's CMake — Gradle uses the SDK's
> pinned CMake 3.31.6. The CLion toolchain is only for host-side DSP tests.

## Reference machine (for reproducing reported results)

Values recorded 2026-08-19; useful when a doc references "the reference
machine".

- Windows 11 Pro (build 26200), PowerShell 7
- SDK: `C:\Users\andre\AppData\Local\Android\Sdk`
- JDK: `C:\Program Files\Android\Android Studio\jbr` (OpenJDK 21.0.10)
- Build-tools installed: 36.0.0, 36.1.0, 37.0.0; platforms: android-34, android-36.1

## Known build gotchas

- **AGP 9 has built-in Kotlin support** — modules do *not* apply
  `org.jetbrains.kotlin.android`. Only the Compose compiler plugin
  (`org.jetbrains.kotlin.plugin.compose`) is applied where Compose is used.
- **Oboe via prefab** requires `buildFeatures { prefab = true }` and
  `-DANDROID_STL=c++_shared` (see `core/audio/build.gradle.kts`).
- `:core:calibration` is a pure JVM module; it must never gain Android
  dependencies (that would break host-side testing).
