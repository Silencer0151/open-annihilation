// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Each double operation rounds to a 53-bit significand on the program's
// first thread and on a thread the engine starts.

#include "oa/base/float_precision.hpp"
#include "oa/base/threads.hpp"

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

/// 1 + 2^-53 + 2^-53 in one expression. Rounded to a double at each step the
/// first sum is a tie that rounds to 1, and so is the second; kept to a
/// 64-bit significand between the steps it is 1 + 2^-52.
double sum_of_two_half_steps() {
    volatile double one = 1.0;
    volatile double half_step = 0x1p-53;
    const double first = one;
    const double step = half_step;
    return first + step + step;
}

/// What the started thread found.
struct ThreadResult {
    bool rounds_to_double{};
    double sum{};
};

void measure(void* argument) {
    auto* result = static_cast<ThreadResult*>(argument);
    result->rounds_to_double = oa::base::float_precision::rounds_to_double();
    result->sum = sum_of_two_half_steps();
}

} // namespace

int main() {
    check(
        oa::base::float_precision::rounds_to_double(), "the first thread starts at double precision"
    );
    check(sum_of_two_half_steps() == 1.0, "the first thread rounds each step to a double");

    oa::base::float_precision::use_double_precision();
    check(
        oa::base::float_precision::rounds_to_double(), "use_double_precision keeps double precision"
    );

    ThreadResult result{};
    oa::base::threads::Thread thread{};
    check(oa::base::threads::start_thread(thread, measure, &result), "a thread starts");
    oa::base::threads::join_thread(thread);
    check(result.rounds_to_double, "a started thread runs at double precision");
    check(result.sum == 1.0, "a started thread rounds each step to a double");

    if (failures != 0)
        return 1;
    std::puts("base-float-precision: ok");
    return 0;
}
