// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The C++ library's steady and system clocks, for a C++ run-time library
// built without a monotonic clock, whose steady clock reads the time of day.
// On Windows XP the time of day advances in steps of 15.625 ms, and on every
// Windows it jumps when the clock is set, so it cannot time the engine's
// frames and ticks. A program linked with this file reads the steady clock
// from the performance counter instead, as other C++ run-time libraries for
// Windows do. The system clock is defined here too, reading the time of day
// as before, because the library defines both in one object, which would
// otherwise be linked beside these definitions. Built only for MinGW; with a
// C++ library whose steady clock is monotonic, this file defines nothing.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>

#if defined(__GLIBCXX__) && !defined(_GLIBCXX_USE_CLOCK_MONOTONIC)

namespace {

/// Nanoseconds in a second.
constexpr int64_t nanoseconds_per_second = 1'000'000'000;
/// Nanoseconds in one interval of the system's time of day.
constexpr int64_t nanoseconds_per_time_interval = 100;
/// The system's time of day counts 100 ns intervals from 1 January 1601; the
/// system clock counts from 1 January 1970, this many intervals later.
constexpr int64_t time_intervals_before_1970 = 116'444'736'000'000'000;

/// The performance counter's ticks per second, 0 until first read. It is
/// fixed while the system runs, so threads that read it at once store the
/// same value.
constinit std::atomic<int64_t> counter_frequency{0};

/// Returns the performance counter's ticks per second.
///
/// @return the frequency, which Windows XP and later never report as 0
int64_t performance_counter_frequency() noexcept {
    int64_t frequency = counter_frequency.load(std::memory_order_relaxed);
    if (frequency == 0) {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        frequency = value.QuadPart;
        counter_frequency.store(frequency, std::memory_order_relaxed);
    }
    return frequency;
}

} // namespace

/// Reads the performance counter, in nanoseconds since the system started.
///
/// Whole seconds and the rest of the count are converted apart, so the
/// result is exact to the nanosecond, rounded down, and every product fits
/// in 64 bits at any counter frequency below 9.2 GHz.
///
/// @return the time on the performance counter
std::chrono::steady_clock::time_point std::chrono::steady_clock::now() noexcept {
    const int64_t frequency = performance_counter_frequency();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const int64_t seconds = counter.QuadPart / frequency;
    const int64_t rest = counter.QuadPart % frequency;
    return time_point(
        duration(seconds * nanoseconds_per_second + rest * nanoseconds_per_second / frequency)
    );
}

/// Reads the time of day, in nanoseconds since 1 January 1970.
///
/// @return the time of day, in steps of 100 ns
std::chrono::system_clock::time_point std::chrono::system_clock::now() noexcept {
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    const int64_t intervals = static_cast<int64_t>(
        (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime
    );
    return time_point(
        duration((intervals - time_intervals_before_1970) * nanoseconds_per_time_interval)
    );
}

#endif
