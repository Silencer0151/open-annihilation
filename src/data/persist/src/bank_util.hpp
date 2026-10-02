// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Small helpers shared by the persistence sources.
#pragma once

#include "oa/base/bytes.hpp"

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

// Little-endian fields of save files and banks.
using base::bytes::load_le16;
using base::bytes::load_le32;
using base::bytes::store_le16;
using base::bytes::store_le32;

} // namespace oa::data::persist::detail
