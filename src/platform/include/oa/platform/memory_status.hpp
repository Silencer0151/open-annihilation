// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Memory report of the developer statistics, sampled from the host's process
// memory counters.

#include <cstddef>
#include <cstdint>

namespace oa::platform {

// One sample of the process's memory, in bytes. Counters the host cannot
// report stay zero.
struct MemorySample {
    uint64_t mapped{};           // committed memory
    uint64_t code{};             // committed executable memory
    uint64_t working_set{};      // resident memory; 0 when the host has no working set
    uint64_t private_resident{}; // resident pages no other process maps
    uint64_t shared_resident{};
    uint64_t page_tables{};
    // The most committed memory and the largest working set the host has seen
    // since the process started; 0 when the host keeps no such peak.
    uint64_t peak_mapped{};
    uint64_t peak_working_set{};
};

// Report state kept between calls.
struct MemoryStatusReport {
    MemorySample sample{};
    uint64_t peak_mapped{};
    uint64_t peak_working_set{};
    int32_t refresh_countdown{-1}; // calls left before the next sample; negative samples at once
};

// Calls a sample serves: the report samples on its first call and on every
// tenth call after a sample.
inline constexpr int32_t memory_status_calls_per_sample = 10;

// Source of samples; false leaves the previous sample in place.
using MemorySampler = bool (*)(void* context, MemorySample* out);

/// Samples this process's memory counters from the host.
///
/// @param context unused; present to match MemorySampler
/// @param[out] out counters the host reports; the rest stay zero
/// @return false when the host cannot report any counters
bool sample_process_memory(void* context, MemorySample* out) noexcept;

/// Formats a value as decimal digits grouped in threes by commas ("1,234,567").
///
/// @param value value to format
/// @param[out] out text buffer
/// @param capacity bytes of out
/// @return the text length, or 0 when the text and its terminator do not fit
size_t format_grouped_decimal(uint64_t value, char* out, size_t capacity) noexcept;

/// Writes the memory report text, sampling when the countdown runs out.
///
/// The peaks are raised from the sample: from its current counters and from
/// the peaks the host keeps. The text is mapped memory and its
/// peak, then either the working-set breakdown or, when no working set was
/// ever seen, the code size.
///
/// @param[in,out] report state kept between calls
/// @param sampler source of samples
/// @param context value passed to sampler
/// @param[out] out text buffer
/// @param capacity bytes of out
/// @return the text length, or 0 when it does not fit
size_t format_memory_status(
    MemoryStatusReport& report, MemorySampler sampler, void* context, char* out, size_t capacity
) noexcept;

} // namespace oa::platform
