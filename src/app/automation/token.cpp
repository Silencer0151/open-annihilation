// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The automation endpoint's token (token.hpp).
#include "token.hpp"

#include <array>
#include <cstdint>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace oa::app::automation {
namespace {

constexpr size_t kTokenBytes = token_bits / 8;

/// Fills a buffer from the system's generator of random bytes.
///
/// @param[out] bytes the buffer
/// @return false when the generator could not be read
bool system_random(std::array<uint8_t, kTokenBytes>& bytes) {
#ifdef _WIN32
    // The system's generator (RtlGenRandom), which every Windows from XP on
    // exports from advapi32.dll by this name.
    using GenerateRandom = BOOLEAN(WINAPI*)(PVOID, ULONG);
    HMODULE library = LoadLibraryW(L"advapi32.dll");
    if (library == nullptr)
        return false;
    const auto generate = reinterpret_cast<GenerateRandom>(
        reinterpret_cast<void*>(GetProcAddress(library, "SystemFunction036"))
    );
    const bool filled =
        generate != nullptr && generate(bytes.data(), static_cast<ULONG>(bytes.size())) != FALSE;
    FreeLibrary(library);
    return filled;
#else
    const int device = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (device < 0)
        return false;
    size_t filled = 0;
    while (filled < bytes.size()) {
        const ssize_t got = read(device, bytes.data() + filled, bytes.size() - filled);
        if (got <= 0)
            break;
        filled += static_cast<size_t>(got);
    }
    close(device);
    return filled == bytes.size();
#endif
}

} // namespace

std::optional<std::string> make_token() {
    std::array<uint8_t, kTokenBytes> bytes{};
    if (!system_random(bytes))
        return std::nullopt;
    static constexpr char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(bytes.size() * 2);
    for (const uint8_t byte : bytes) {
        token += hex[byte >> 4];
        token += hex[byte & 0xFU];
    }
    return token;
}

bool token_matches(std::string_view given, std::string_view expected) noexcept {
    if (given.size() != expected.size())
        return false;
    uint8_t difference = 0;
    for (size_t at = 0; at < expected.size(); ++at)
        difference |= static_cast<uint8_t>(given[at] ^ expected[at]);
    return difference == 0;
}

} // namespace oa::app::automation
