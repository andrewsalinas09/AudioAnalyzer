#pragma once
// Test assertion that is immune to NDEBUG: host tests build in Release mode,
// where <cassert>'s assert() compiles to nothing and silently passes empty
// tests. Always use CHECK() in test code, never assert().
#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr, "CHECK failed: %s  (%s:%d)\n", #cond,     \
                         __FILE__, __LINE__);                              \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)
