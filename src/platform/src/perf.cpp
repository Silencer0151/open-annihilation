// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/perf.hpp"

#include <chrono>
#include <cstdint>

namespace oa::platform {

bool perf_device_available() noexcept {
    return false;
}

bool perf_read_event_counter(uint32_t, uint64_t*) noexcept {
    return false;
}

bool perf_write_event_command(uint32_t, uint32_t, uint32_t) noexcept {
    return false;
}

uint64_t perf_now_ns() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

} // namespace oa::platform
