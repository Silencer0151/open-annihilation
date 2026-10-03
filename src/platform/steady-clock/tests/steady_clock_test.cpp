// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The steady clock reads the performance counter, not the time of day: each
// reading lies between the counter's own readings just before and just after
// it. That holds for this module's clock and for a C++ library's own
// monotonic one alike.
#include "oa/test/check.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstdio>

namespace {

/// How many times the test reads the clock.
constexpr int clock_readings = 100;

/// Converts a performance counter reading to nanoseconds, rounded down.
///
/// @param counter the reading
/// @param frequency the counter's ticks per second
/// @return the reading in nanoseconds
int64_t counter_nanoseconds(int64_t counter, int64_t frequency) {
    constexpr int64_t nanoseconds_per_second = 1'000'000'000;
    return counter / frequency * nanoseconds_per_second +
           counter % frequency * nanoseconds_per_second / frequency;
}

/// Checks each reading of the steady clock against the counter's readings
/// around it, allowing a nanosecond for a clock that rounds to the nearest.
void test_steady_clock_reads_counter() {
    LARGE_INTEGER frequency{};
    OA_CHECK(QueryPerformanceFrequency(&frequency));
    if (frequency.QuadPart <= 0)
        return;
    for (int reading = 0; reading < clock_readings; ++reading) {
        LARGE_INTEGER before{};
        LARGE_INTEGER after{};
        QueryPerformanceCounter(&before);
        const auto since_start = std::chrono::steady_clock::now().time_since_epoch();
        QueryPerformanceCounter(&after);
        const int64_t steady =
            std::chrono::duration_cast<std::chrono::nanoseconds>(since_start).count();
        const int64_t earliest = counter_nanoseconds(before.QuadPart, frequency.QuadPart);
        const int64_t latest = counter_nanoseconds(after.QuadPart, frequency.QuadPart) + 1;
        OA_CHECK(steady >= earliest && steady <= latest);
        if (steady < earliest || steady > latest) {
            std::fprintf(
                stderr,
                "steady clock %lld ns, performance counter %lld to %lld ns\n",
                static_cast<long long>(steady),
                static_cast<long long>(earliest),
                static_cast<long long>(latest)
            );
            return;
        }
    }
}

} // namespace

int main() {
    test_steady_clock_reads_counter();
    const int status = oa::test::check_exit_status();
    if (status == 0)
        std::puts("steady clock checks passed");
    return status;
}
