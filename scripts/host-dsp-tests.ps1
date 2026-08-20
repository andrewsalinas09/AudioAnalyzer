# Builds and runs the host-side C++ DSP tests (core/audio/src/main/cpp/dsp).
# Uses the CMake/MinGW/Ninja toolchain bundled with JetBrains CLion by default;
# see docs/building.md for details and alternatives.
param(
    [string]$CLionBin = "C:\Program Files\JetBrains\CLion 2025.2.5\bin"
)

$ErrorActionPreference = "Stop"

$cmake = Join-Path $CLionBin "cmake\win\x64\bin\cmake.exe"
$ctest = Join-Path $CLionBin "cmake\win\x64\bin\ctest.exe"
$gxx = Join-Path $CLionBin "mingw\bin\g++.exe"
$gcc = Join-Path $CLionBin "mingw\bin\gcc.exe"
$ninja = Join-Path $CLionBin "ninja\win\x64\ninja.exe"
if (-not (Test-Path $ninja)) { $ninja = Join-Path $CLionBin "ninja\cygwin\x64\ninja.exe" }

foreach ($tool in @($cmake, $ctest, $gxx, $ninja)) {
    if (-not (Test-Path $tool)) {
        throw "Toolchain component not found: $tool  (pass -CLionBin or edit this script; see docs/building.md)"
    }
}

# MinGW's g++ spawns as/ld from PATH; make sure its bin dir is there.
$env:Path = (Join-Path $CLionBin "mingw\bin") + ";" + $env:Path

$repoRoot = Split-Path $PSScriptRoot -Parent
$src = Join-Path $repoRoot "core\audio\src\main\cpp\dsp"
$build = Join-Path $repoRoot "build\host-dsp"

& $cmake -S $src -B $build -G Ninja `
    -DCMAKE_MAKE_PROGRAM="$ninja" `
    -DCMAKE_C_COMPILER="$gcc" `
    -DCMAKE_CXX_COMPILER="$gxx" `
    -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

& $cmake --build $build
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Push-Location $build
try {
    & $ctest --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
} finally {
    Pop-Location
}

Write-Host "Host DSP tests passed." -ForegroundColor Green
