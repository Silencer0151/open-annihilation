// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Small helpers shared by the persistence sources.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace oa::data::persist::detail {

inline char lower_ascii(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}

// Case-insensitive ASCII equality, matching the game's name lookups.
inline bool equal_nocase(const char* a, const char* b) {
    if (a == nullptr || b == nullptr)
        return false;
    for (;; ++a, ++b) {
        if (lower_ascii(*a) != lower_ascii(*b))
            return false;
        if (*a == '\0')
            return true;
    }
}

inline char* duplicate(const char* text) {
    const std::size_t length = std::strlen(text) + 1;
    auto* copy = static_cast<char*>(std::malloc(length));
    if (copy != nullptr)
        std::memcpy(copy, text, length);
    return copy;
}

inline uint32_t load_le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline uint16_t load_le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline void store_le32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

inline void store_le16(uint8_t* p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
}

} // namespace oa::data::persist::detail
