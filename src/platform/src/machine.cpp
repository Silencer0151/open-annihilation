// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/machine.hpp"

#include "oa/platform/system.hpp"

#include <fstream>
#include <ios>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#if defined(__i386__) || defined(_M_IX86)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace oa::platform {

bool raspberry_pi_model(std::string_view model) noexcept {
    const std::string_view text = model.substr(0, model.find('\0'));
    return text.starts_with(raspberry_pi_model_prefix);
}

bool model_file_names_raspberry_pi(const std::filesystem::path& model_file) {
    std::ifstream input(model_file, std::ios::binary);
    if (!input)
        return false;
    std::string text(model_file_limit, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    const std::streamsize length = input.gcount();
    if (length <= 0)
        return false;
    text.resize(static_cast<std::size_t>(length));
    return raspberry_pi_model(text);
}

bool running_on_raspberry_pi() {
#if defined(__linux__)
    return model_file_names_raspberry_pi(device_tree_model_path);
#else
    return false;
#endif
}

bool windows_before_vista(uint32_t major_version) noexcept {
    return major_version < vista_major_version;
}

bool running_on_windows_before_vista() noexcept {
#if defined(_WIN32)
    // The system's own version, which a compatibility manifest does not
    // change, from the system library every Windows process has loaded.
    using VersionFunction = LONG(WINAPI*)(OSVERSIONINFOW*);
    // What the version reader returns when it read the version.
    constexpr LONG version_read = 0;
    const HMODULE system_library = GetModuleHandleW(L"ntdll.dll");
    const auto read_version =
        system_library != nullptr
            ? reinterpret_cast<VersionFunction>(
                  reinterpret_cast<void*>(GetProcAddress(system_library, "RtlGetVersion"))
              )
            : nullptr;
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof version;
    // A version that cannot be read counts as before Vista, which keeps the
    // processor drawing.
    if (read_version == nullptr || read_version(&version) != version_read)
        return true;
    return windows_before_vista(static_cast<uint32_t>(version.dwMajorVersion));
#else
    return false;
#endif
}

bool running_on_linux() noexcept {
#if defined(__linux__)
    return true;
#else
    return false;
#endif
}

bool light_machine(const MachineTraits& machine) noexcept {
    return machine.processors <= 1 || !machine.sse2 ||
           (machine.memory != 0 && machine.memory < light_machine_memory);
}

namespace {

/// Returns the machine's physical memory as the system reports it.
///
/// @return bytes, or 0 when the system does not say
uint64_t physical_memory() noexcept {
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    return GlobalMemoryStatusEx(&status) ? static_cast<uint64_t>(status.ullTotalPhys) : 0;
#elif defined(__APPLE__)
    uint64_t bytes = 0;
    std::size_t size = sizeof bytes;
    return sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0 ? bytes : 0;
#elif defined(__linux__)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page = sysconf(_SC_PAGESIZE);
    return pages > 0 && page > 0 ? static_cast<uint64_t>(pages) * static_cast<uint64_t>(page) : 0;
#else
    return 0;
#endif
}

/// Tells whether the processor runs SSE2 instructions.
///
/// @return the SSE2 bit of the processor's feature word on 32-bit x86; true elsewhere
bool runs_sse2() noexcept {
#if defined(__i386__) || defined(_M_IX86)
    // CPUID leaf 1 reports SSE2 in bit 26 of its EDX word.
    constexpr unsigned feature_leaf = 1;
    constexpr unsigned sse2_bit = 1U << 26;
#if defined(_MSC_VER)
    int words[4] = {};
    __cpuid(words, static_cast<int>(feature_leaf));
    return (static_cast<unsigned>(words[3]) & sse2_bit) != 0;
#else
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
    return __get_cpuid(feature_leaf, &eax, &ebx, &ecx, &edx) != 0 && (edx & sse2_bit) != 0;
#endif
#else
    return true;
#endif
}

} // namespace

MachineTraits read_machine_traits() noexcept {
    MachineTraits machine;
    machine.processors = processor_count();
    machine.memory = physical_memory();
    machine.sse2 = runs_sse2();
    return machine;
}

} // namespace oa::platform
