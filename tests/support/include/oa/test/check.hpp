// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// OA_CHECK, the check a test program makes in place of assert(). A failed
// check prints its file, line and expression and is counted, and the program
// carries on, so one run shows every broken case; main() returns
// oa::test::check_exit_status(), which is non-zero when any check failed.
// Unlike assert(), it is kept in every build type, NDEBUG or not.
#pragma once

#include <cstdio>

namespace oa::test {

/// Returns the number of checks that have failed so far in this program.
///
/// @return a reference to the count OA_CHECK increments
inline int& failed_checks() {
    static int count = 0;
    return count;
}

/// Prints a failed check's file, line and expression, and counts it.
///
/// @param file source file of the check
/// @param line line of the check
/// @param expression the expression that was false, as written
inline void report_failed_check(const char* file, int line, const char* expression) {
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
    ++failed_checks();
}

/// Returns the exit status of a test program from its checks so far,
/// printing how many failed when any did.
///
/// @return 0 when every check passed, 1 otherwise
inline int check_exit_status() {
    if (failed_checks() == 0)
        return 0;
    std::fprintf(stderr, "%d check(s) failed\n", failed_checks());
    return 1;
}

} // namespace oa::test

#define OA_CHECK(condition)                                                                        \
    do {                                                                                           \
        if (!(condition))                                                                          \
            ::oa::test::report_failed_check(__FILE__, __LINE__, #condition);                       \
    } while (false)
