// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/crash.hpp"

#include "oa/platform/allocator.hpp"

#include <csignal>
#include <cstdlib>
#include <new>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace oa::platform {

void exhaust_heap(const char* tag) noexcept {
    for (std::size_t held = 0; held < crash_heap_ceiling; held += crash_block_bytes)
        if (heap_alloc(crash_block_bytes, tag) == nullptr)
            break;
    if (const std::new_handler handler = std::get_new_handler(); handler != nullptr)
        handler();
    std::abort();
}

void raise_divide_fault() noexcept {
#ifdef _WIN32
    RaiseException(EXCEPTION_INT_DIVIDE_BY_ZERO, 0, 0, nullptr);
#else
    std::raise(SIGFPE);
#endif
    std::abort();
}

void break_into_debugger() noexcept {
#ifdef _WIN32
    DebugBreak();
#else
    std::raise(SIGTRAP);
#endif
}

} // namespace oa::platform
