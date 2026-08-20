# Android build entry point. The dsp/ subdirectory is a pure, Android-free
# static library that can also be configured standalone for host-side tests
# (see scripts/host-dsp-tests.ps1 and docs/building.md).
cmake_minimum_required(VERSION 3.22)
project(audioanalyzer_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(dsp)

find_package(oboe REQUIRED CONFIG)

add_library(aa_engine SHARED
        engine/AudioEngine.cpp
        engine/jni_bridge.cpp
)
target_compile_options(aa_engine PRIVATE -Wall -Wextra)
target_link_libraries(aa_engine PRIVATE aa_dsp oboe::oboe log android)
