// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// require() passes a decoded value through and turns an error into an
// exception whose text names the file, the message, the code and the offset.

#include "oa/ui/decoded.hpp"

#include <cstdio>
#include <string>

namespace {

using oa::base::bytes::DecodeCode;
using oa::base::bytes::DecodeError;
using oa::base::bytes::Decoded;

int failures = 0;

/// Records a failed check.
///
/// @param line source line of the check
/// @param what text of the failed condition
void report_failure(int line, const char* what) {
    std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, what);
    ++failures;
}

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            report_failure(__LINE__, #condition);                                                  \
        }                                                                                          \
    } while (false)

} // namespace

int main() {
    CHECK(oa::ui::decoded::require(Decoded<int>(7), "seven.bin") == 7);
    try {
        (void)oa::ui::decoded::require(
            Decoded<int>(DecodeError{DecodeCode::truncated, 42, "record is cut short"}), "units.bin"
        );
        CHECK(false);
    } catch (const std::runtime_error& error) {
        CHECK(std::string(error.what()) == "units.bin: record is cut short (truncated at byte 42)");
    }
    CHECK(
        oa::ui::decoded::describe(DecodeError{DecodeCode::not_found, 0, nullptr}, "x") ==
        "x: cannot be decoded (not found at byte 0)"
    );
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
