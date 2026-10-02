// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Minimal assertion helpers for the persistence tests.
#pragma once

#include <cstdio>
#include <cstdlib>

namespace oa::data::persist::test {
inline int failures = 0;
inline int checks = 0;

/// Counts a check and reports it on stderr when it fails.
///
/// @param condition the result of the check
/// @param expression the checked expression, as written
/// @param file source file of the check
/// @param line source line of the check
inline void check(bool condition, const char* expression, const char* file, int line) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
    }
}

/// Prints the count of checks and failures.
///
/// @param name the test's name, printed first
/// @return EXIT_SUCCESS when no check failed, else EXIT_FAILURE
inline int finish(const char* name) {
    std::printf("%s: %d checks, %d failures\n", name, checks, failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
} // namespace oa::data::persist::test

#define CHECK(expr)                                                                                \
    ::oa::data::persist::test::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
