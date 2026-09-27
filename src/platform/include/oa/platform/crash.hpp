// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Host forms of the faults the console's DebugBreak command forces.

#include <cstddef>

namespace oa::platform {

// Block size of the heap exhaustion test.
inline constexpr std::size_t crash_block_bytes = 0x2000000u;
// Most the heap exhaustion test holds, 2 GiB.
inline constexpr std::size_t crash_heap_ceiling = 0x80000000u;

/// Exhausts the heap, then runs the installed out-of-memory handler.
///
/// Takes crash_block_bytes blocks from the host heap until it refuses one or
/// crash_heap_ceiling bytes are held, so a host with more memory still runs
/// out. Aborts if the handler returns.
///
/// @param tag diagnostic tag given to each block
[[noreturn]] void exhaust_heap(const char* tag) noexcept;

/// Raises the integer divide-by-zero fault.
[[noreturn]] void raise_divide_fault() noexcept;

/// Breaks into an attached debugger; without one the trap ends the process.
void break_into_debugger() noexcept;

} // namespace oa::platform
