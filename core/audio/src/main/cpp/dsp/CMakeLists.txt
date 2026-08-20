# aa_dsp: pure C++ signal processing. No Android, JNI, or Oboe dependencies —
# everything here must stay host-buildable so DSP correctness is testable
# without a device (ADR-0003).
#
# Standalone configure (host tests):  cmake -S <this dir> -B <build dir>
# As part of the Android build it is pulled in via add_subdirectory().
cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED AA_PARENT_PROJECT AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(AA_DSP_STANDALONE TRUE)
endif()

project(aa_dsp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(aa_dsp STATIC
        levels.cpp
        weighting.cpp
        spl.cpp
        fft.cpp
        window.cpp
        spectrum.cpp
        third_party/pffft/pffft.c
)
target_include_directories(aa_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(AA_DSP_STANDALONE)
    enable_testing()
    foreach(t levels weighting spl spectrum)
        add_executable(${t}_test tests/${t}_test.cpp)
        target_link_libraries(${t}_test PRIVATE aa_dsp)
        add_test(NAME ${t} COMMAND ${t}_test)
    endforeach()
endif()
