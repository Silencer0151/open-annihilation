// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The system's memory sampled for the memory guard. It lives apart from the
// developer statistics' report so that a program that never samples it links
// none of it.

#include "oa/platform/memory_status.hpp"

#include <cstdio>
#include <limits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__unix__)
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace oa::platform {
namespace {

/// Bytes of the kB a Linux status file counts in.
constexpr uint64_t kibibyte = 1024;

/// Removes the spaces and tabs at the start of a text.
///
/// @param text the text
/// @return the text from its first character that is neither
std::string_view skip_blanks(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    return text;
}

/// Removes the spaces, tabs and carriage returns at the end of a text.
///
/// @param text the text
/// @return the text up to its last character that is none of them
std::string_view trim_end(std::string_view text) noexcept {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

#if defined(__APPLE__) || defined(__unix__)
/// Reports this process's hard page faults since it started.
///
/// @param[out] out takes the count and its flag when the system gives it
void sample_hard_faults(SystemMemorySample& out) noexcept {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_majflt < 0)
        return;
    out.hard_faults = static_cast<uint64_t>(usage.ru_majflt);
    out.hard_faults_known = true;
}
#endif

#if defined(__APPLE__)
/// The largest value of `kern.memorystatus_level`, a percentage.
constexpr unsigned free_level_whole = 100;

/// Reads the share of physical memory the system counts as free, as a
/// figure in bytes.
///
/// @param physical installed physical memory, in bytes; above 0
/// @param[out] out takes the figure and its flag when the system gives the
///     share
void read_free_memory(uint64_t physical, SystemMemorySample& out) noexcept {
    unsigned level = 0;
    std::size_t size = sizeof level;
    if (sysctlbyname("kern.memorystatus_level", &level, &size, nullptr, 0) != 0 ||
        size != sizeof level || level > free_level_whole)
        return;
    // A whole percentage of physical memory, rounded down, split so that no
    // product can overflow.
    out.available = physical / free_level_whole * level +
                    physical % free_level_whole * level / free_level_whole;
    out.available_known = true;
}

/// The levels of `kern.memorystatus_vm_pressure_level`.
constexpr int pressure_level_normal = 1;
constexpr int pressure_level_warning = 2;
constexpr int pressure_level_critical = 4;

/// Reads the system's memory pressure level.
///
/// @return the level, or MemoryPressure::unknown when it cannot be read or is
///     not one the engine knows
MemoryPressure read_memory_pressure() noexcept {
    int level = 0;
    std::size_t size = sizeof level;
    if (sysctlbyname("kern.memorystatus_vm_pressure_level", &level, &size, nullptr, 0) != 0 ||
        size != sizeof level)
        return MemoryPressure::unknown;
    switch (level) {
    case pressure_level_normal:
        return MemoryPressure::normal;
    case pressure_level_warning:
        return MemoryPressure::warning;
    case pressure_level_critical:
        return MemoryPressure::critical;
    default:
        return MemoryPressure::unknown;
    }
}
#endif

#if defined(__linux__)
/// The most bytes of a status file that are read; the fields the guard reads
/// come well before it.
constexpr std::size_t status_text_limit = 16384;

/// The text of a small status file, read once.
struct StatusText {
    char bytes[status_text_limit]{};
    std::size_t length{};

    /// Returns the bytes read.
    ///
    /// @return the text, empty when the file could not be read
    std::string_view text() const noexcept { return {bytes, length}; }
};

/// Reads at most status_text_limit bytes of a status file.
///
/// @param path the file
/// @param[out] out the text read; empty when the file cannot be opened
void read_status_text(const char* path, StatusText& out) noexcept {
    out.length = 0;
    std::FILE* file = std::fopen(path, "r");
    if (file == nullptr)
        return;
    out.length = std::fread(out.bytes, 1, sizeof out.bytes, file);
    std::fclose(file);
}
#endif

} // namespace

std::optional<uint64_t> proc_memory_field(std::string_view text, std::string_view field) noexcept {
    if (field.empty())
        return std::nullopt;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.size() <= field.size() || !line.starts_with(field) || line[field.size()] != ':')
            continue;
        std::string_view rest = skip_blanks(line.substr(field.size() + 1));
        uint64_t kibibytes = 0;
        std::size_t digits = 0;
        constexpr uint64_t largest = std::numeric_limits<uint64_t>::max();
        while (digits < rest.size() && rest[digits] >= '0' && rest[digits] <= '9') {
            const auto digit = static_cast<uint64_t>(rest[digits] - '0');
            if (kibibytes > (largest - digit) / 10)
                return std::nullopt;
            kibibytes = kibibytes * 10 + digit;
            ++digits;
        }
        if (digits == 0 || trim_end(skip_blanks(rest.substr(digits))) != "kB" ||
            kibibytes > largest / kibibyte)
            return std::nullopt;
        return kibibytes * kibibyte;
    }
    return std::nullopt;
}

std::optional<uint64_t> committed_from_proc_status(std::string_view text) noexcept {
    const std::optional<uint64_t> anonymous = proc_memory_field(text, "RssAnon");
    if (!anonymous)
        return std::nullopt;
    const uint64_t swapped = proc_memory_field(text, "VmSwap").value_or(0);
    if (swapped > std::numeric_limits<uint64_t>::max() - *anonymous)
        return std::nullopt;
    return *anonymous + swapped;
}

bool sample_system_memory(SystemMemorySample* out) noexcept {
    if (out == nullptr)
        return false;
    *out = {};
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    if (GlobalMemoryStatusEx(&status)) {
        out->physical = status.ullTotalPhys;
        out->available = status.ullAvailPhys;
        out->available_known = true;
    }
    // Private bytes: the memory the process has committed for itself.
    MemorySample process{};
    if (sample_process_memory(nullptr, &process)) {
        out->committed = process.mapped;
        out->committed_known = true;
    }
#elif defined(__APPLE__)
    uint64_t physical = 0;
    std::size_t size = sizeof physical;
    if (sysctlbyname("hw.memsize", &physical, &size, nullptr, 0) == 0 && size == sizeof physical)
        out->physical = physical;
    if (out->physical != 0)
        read_free_memory(out->physical, *out);
    out->pressure = read_memory_pressure();
    // The physical footprint: private memory, resident, compressed or paged out.
    MemorySample process{};
    if (sample_process_memory(nullptr, &process)) {
        out->committed = process.mapped;
        out->committed_known = true;
    }
    sample_hard_faults(*out);
#elif defined(__linux__)
    StatusText text;
    read_status_text("/proc/meminfo", text);
    out->physical = proc_memory_field(text.text(), "MemTotal").value_or(0);
    if (const std::optional<uint64_t> available = proc_memory_field(text.text(), "MemAvailable")) {
        out->available = *available;
        out->available_known = true;
    }
    read_status_text("/proc/self/status", text);
    if (const std::optional<uint64_t> committed = committed_from_proc_status(text.text())) {
        out->committed = *committed;
        out->committed_known = true;
    }
    sample_hard_faults(*out);
#elif defined(__unix__)
#if defined(_SC_PHYS_PAGES)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page > 0)
        out->physical = static_cast<uint64_t>(pages) * static_cast<uint64_t>(page);
#endif
    sample_hard_faults(*out);
#endif
    return out->physical != 0 || out->available_known || out->committed_known ||
           out->hard_faults_known || out->pressure != MemoryPressure::unknown;
}

} // namespace oa::platform
