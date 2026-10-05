// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/memory_status.hpp"

#include <algorithm>
#include <cstdio>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#if __has_include(<libproc.h>)
#include <libproc.h>
#define OA_HAVE_LIBPROC 1
#else
#include <atomic>
#endif
#include <malloc/malloc.h>
#include <mach/mach.h>
#include <mach/task_info.h>
#include <sys/resource.h>
#include <unistd.h>
#elif defined(__linux__)
#include <malloc.h>
#include <unistd.h>
#endif

// Under the address sanitizer, malloc is the sanitizer's own, which counts
// what it holds.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define OA_HEAP_USE_FROM_SANITIZER 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(OA_HEAP_USE_FROM_SANITIZER)
#define OA_HEAP_USE_FROM_SANITIZER 1
#endif
#ifdef OA_HEAP_USE_FROM_SANITIZER
#include <sanitizer/allocator_interface.h>
#endif

namespace oa::platform {
namespace {

// Bytes of the kB the Linux status file counts in.
[[maybe_unused]] constexpr uint64_t kibibyte = 1024;

// The most heaps of the process sample_host_heap_use walks on Windows; a
// process with more reports no heap use rather than part of it.
[[maybe_unused]] constexpr unsigned long max_walked_heaps = 256;

// Widest grouped 64-bit value: 20 digits and 6 commas.
constexpr size_t grouped_capacity = 27;

struct Grouped {
    char text[grouped_capacity]{};
};

Grouped grouped(uint64_t value) noexcept {
    Grouped result;
    (void)format_grouped_decimal(value, result.text, sizeof result.text);
    return result;
}

// Records an snprintf result at out+length; false once the text no longer fits.
bool advance(int written, size_t capacity, size_t& length) noexcept {
    if (written < 0 || static_cast<size_t>(written) >= capacity - length)
        return false;
    length += static_cast<size_t>(written);
    return true;
}

} // namespace

size_t format_grouped_decimal(uint64_t value, char* out, size_t capacity) noexcept {
    char reversed[grouped_capacity]{};
    size_t length = 0;
    int32_t digits = 0;
    do {
        reversed[length++] = static_cast<char>('0' + value % 10);
        value /= 10;
        ++digits;
        if (digits % 3 == 0 && value != 0)
            reversed[length++] = ',';
    } while (value != 0);
    if (out == nullptr || length + 1 > capacity)
        return 0;
    for (size_t index = 0; index < length; ++index)
        out[index] = reversed[length - 1 - index];
    out[length] = '\0';
    return length;
}

size_t format_memory_status(
    MemoryStatusReport& report, MemorySampler sampler, void* context, char* out, size_t capacity
) noexcept {
    if (report.refresh_countdown < 0 || --report.refresh_countdown < 1) {
        MemorySample sample{};
        if (sampler != nullptr && sampler(context, &sample))
            report.sample = sample;
        report.refresh_countdown = memory_status_calls_per_sample;
    }
    const MemorySample& sample = report.sample;
    report.peak_working_set =
        std::max({report.peak_working_set, sample.working_set, sample.peak_working_set});
    report.peak_mapped = std::max({report.peak_mapped, sample.mapped, sample.peak_mapped});
    if (out == nullptr || capacity == 0)
        return 0;
    out[0] = '\0';
    size_t length = 0;
    bool fits = advance(
        std::snprintf(
            out,
            capacity,
            "\r\nMapped now:          %13s\r\nMapped peak:         %13s\r\n",
            grouped(sample.mapped).text,
            grouped(report.peak_mapped).text
        ),
        capacity,
        length
    );
    if (report.peak_working_set == 0) {
        if (fits && sample.code != 0)
            fits = advance(
                std::snprintf(
                    out + length,
                    capacity - length,
                    "Code size:           %13s\r\n",
                    grouped(sample.code).text
                ),
                capacity,
                length
            );
    } else if (fits) {
        fits = advance(
            std::snprintf(
                out + length,
                capacity - length,
                "Resident now:        %13s\r\nResident peak:       %13s\r\n"
                "Resident private:    %13s\r\nResident shared:     %13s\r\n"
                "Page table size:     %13s\r\n",
                grouped(sample.working_set).text,
                grouped(report.peak_working_set).text,
                grouped(sample.private_resident).text,
                grouped(sample.shared_resident).text,
                grouped(sample.page_tables).text
            ),
            capacity,
            length
        );
    }
    if (!fits) {
        out[0] = '\0';
        return 0;
    }
    return length;
}

bool sample_process_memory(void*, MemorySample* out) noexcept {
    if (out == nullptr)
        return false;
    *out = {};
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof counters;
    if (!GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof counters
        ))
        return false;
    out->mapped = counters.PrivateUsage;
    out->working_set = counters.WorkingSetSize;
    out->peak_mapped = counters.PeakPagefileUsage;
    out->peak_working_set = counters.PeakWorkingSetSize;
    return true;
#elif defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) !=
        KERN_SUCCESS)
        return false;
    out->mapped = info.phys_footprint;
    out->working_set = info.resident_size;
    out->private_resident = info.internal;
    out->shared_resident = info.external;
    out->peak_working_set = info.resident_size_peak;
#ifdef OA_HAVE_LIBPROC
    rusage_info_v4 usage{};
    if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t*>(&usage)) == 0)
        out->peak_mapped = usage.ri_lifetime_max_phys_footprint;
#else
    // Without the process usage library the system gives no lifetime peak, so the peak is the
    // largest footprint this process has sampled so far: it can miss a peak between two
    // samples, never overstate one.
    static std::atomic<uint64_t> largest_sampled{};
    uint64_t largest = largest_sampled.load(std::memory_order_relaxed);
    while (largest < info.phys_footprint &&
           !largest_sampled.compare_exchange_weak(
               largest, info.phys_footprint, std::memory_order_relaxed
           )) {
    }
    out->peak_mapped = std::max(largest, static_cast<uint64_t>(info.phys_footprint));
#endif
    return true;
#elif defined(__linux__)
    // /proc/self/statm: total, resident, shared and text sizes in pages.
    std::FILE* statm = std::fopen("/proc/self/statm", "r");
    if (statm == nullptr)
        return false;
    unsigned long long total = 0, resident = 0, shared = 0, text = 0;
    const int fields = std::fscanf(statm, "%llu %llu %llu %llu", &total, &resident, &shared, &text);
    std::fclose(statm);
    if (fields != 4)
        return false;
    const long page = sysconf(_SC_PAGESIZE);
    const uint64_t page_bytes = page > 0 ? static_cast<uint64_t>(page) : 4096U;
    out->mapped = total * page_bytes;
    out->code = text * page_bytes;
    out->working_set = resident * page_bytes;
    out->shared_resident = shared * page_bytes;
    out->private_resident = resident > shared ? (resident - shared) * page_bytes : 0;
    // /proc/self/status: the largest resident set, "VmHWM:" in kB.
    if (std::FILE* status = std::fopen("/proc/self/status", "r")) {
        char line[128];
        unsigned long long peak_kib = 0;
        while (std::fgets(line, sizeof line, status) != nullptr)
            if (std::sscanf(line, "VmHWM: %llu", &peak_kib) == 1) {
                out->peak_working_set = peak_kib * kibibyte;
                break;
            }
        std::fclose(status);
    }
    return true;
#else
    return false;
#endif
}

bool sample_host_heap_use(HostHeapUse* out) noexcept {
    if (out == nullptr)
        return false;
    *out = {};
#if defined(OA_HEAP_USE_FROM_SANITIZER)
    out->bytes = __sanitizer_get_current_allocated_bytes();
    return true;
#elif defined(_WIN32)
    // The heap malloc and operator new allocate from is one of the process's
    // heaps; each is locked while it is walked, so that no other thread
    // changes it meanwhile.
    HANDLE heaps[max_walked_heaps];
    const DWORD heap_count = GetProcessHeaps(max_walked_heaps, heaps);
    if (heap_count == 0 || heap_count > max_walked_heaps)
        return false;
    bool walked = true;
    for (DWORD index = 0; index < heap_count && walked; ++index) {
        if (!HeapLock(heaps[index]))
            continue;
        PROCESS_HEAP_ENTRY entry{};
        while (HeapWalk(heaps[index], &entry))
            if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0) {
                out->bytes += entry.cbData;
                ++out->blocks;
            }
        // A walk ends at the heap's last entry, or stops short on an error.
        walked = GetLastError() == ERROR_NO_MORE_ITEMS;
        HeapUnlock(heaps[index]);
    }
    if (!walked)
        *out = {};
    return walked;
#elif defined(__APPLE__)
    malloc_statistics_t statistics{};
    malloc_zone_statistics(nullptr, &statistics);
    out->bytes = statistics.size_in_use;
    out->blocks = statistics.blocks_in_use;
    return true;
#elif defined(__GLIBC__)
    // The bytes of the arenas' chunks in use and of the blocks mapped on
    // their own.
#if __GLIBC_PREREQ(2, 33)
    const struct mallinfo2 info = mallinfo2();
    out->bytes = uint64_t{info.uordblks} + uint64_t{info.hblkhd};
#else
    const struct mallinfo info = mallinfo();
    out->bytes = uint64_t{static_cast<unsigned int>(info.uordblks)} +
                 uint64_t{static_cast<unsigned int>(info.hblkhd)};
#endif
    return true;
#else
    return false;
#endif
}

} // namespace oa::platform
