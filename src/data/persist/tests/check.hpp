// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Minimal assertion helpers for the persistence tests.
#pragma once

#include <cstdio>
#include <cstdlib>

namespace oa::data::persist::test {
inline int failures = 0;
inline int checks = 0;

inline void check(bool condition, const char* expression, const char* file, int line) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
    }
}

inline int finish(const char* name) {
    std::printf("%s: %d checks, %d failures\n", name, checks, failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
} // namespace oa::data::persist::test

#define CHECK(expr)                                                                                \
    ::oa::data::persist::test::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
