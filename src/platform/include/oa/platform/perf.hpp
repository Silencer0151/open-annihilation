// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// CPU event counters of the developer statistics. No supported host provides
// the event-counter device, so it is always reported absent; a monotonic high
// resolution counter serves native profiling.

#include <cstdint>

namespace oa::platform {

/// Reports whether the event-counter device is available.
///
/// @return false: the device is never available on supported hosts
[[nodiscard]] bool perf_device_available() noexcept;
/// Reads a CPU event counter.
///
/// @param counter counter number
/// @param[out] value counter value; not written
/// @return false: there is no event-counter device to read
[[nodiscard]] bool perf_read_event_counter(uint32_t counter, uint64_t* value) noexcept;
/// Sends a command to the event-counter device.
///
/// @param command command number
/// @param low low word of the command argument
/// @param high high word of the command argument
/// @return false: there is no event-counter device to command
[[nodiscard]] bool perf_write_event_command(uint32_t command, uint32_t low, uint32_t high) noexcept;
/// Returns a monotonic time for native profiling.
///
/// @return nanoseconds since an arbitrary origin
[[nodiscard]] uint64_t perf_now_ns() noexcept;

} // namespace oa::platform
