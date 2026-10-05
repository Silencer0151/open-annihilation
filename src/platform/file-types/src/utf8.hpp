// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The UTF-8 tests and spelling the registrations share.
#pragma once

#include <stdint.h>

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

namespace oa::platform::file_types::detail {

/// Returns a path's UTF-8 spelling.
///
/// @param path the path
/// @return its UTF-8 spelling: on Windows converted from the system's UTF-16, elsewhere the
///         path's own bytes, which may not be UTF-8; empty when it cannot be converted
inline std::string utf8_of(const std::filesystem::path& path) {
    try {
        const std::u8string text = path.u8string();
        return {text.begin(), text.end()};
    } catch (const std::exception&) {
        return {};
    }
}

/// Reports whether `text` is well-formed UTF-8: no overlong form, no surrogate, nothing above
/// U+10FFFF.
///
/// @param text the bytes
/// @return true when every sequence is well formed
inline bool is_utf8(std::string_view text) {
    constexpr uint8_t continuation_mask = 0xC0;
    constexpr uint8_t continuation_bits = 0x80;
    std::size_t at = 0;
    while (at < text.size()) {
        const auto lead = static_cast<uint8_t>(text[at]);
        std::size_t length = 0;
        uint32_t value = 0;
        uint32_t least = 0;
        if (lead < 0x80) {
            ++at;
            continue;
        }
        if ((lead & 0xE0) == 0xC0) {
            length = 2;
            value = lead & 0x1Fu;
            least = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            value = lead & 0x0Fu;
            least = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4;
            value = lead & 0x07u;
            least = 0x10000;
        } else {
            return false;
        }
        if (text.size() - at < length)
            return false;
        for (std::size_t next = 1; next < length; ++next) {
            const auto byte = static_cast<uint8_t>(text[at + next]);
            if ((byte & continuation_mask) != continuation_bits)
                return false;
            value = (value << 6) | (byte & 0x3Fu);
        }
        constexpr uint32_t first_surrogate = 0xD800;
        constexpr uint32_t last_surrogate = 0xDFFF;
        constexpr uint32_t last_code_point = 0x10FFFF;
        if (value < least || value > last_code_point ||
            (value >= first_surrogate && value <= last_surrogate))
            return false;
        at += length;
    }
    return true;
}

/// Reports whether `text` holds a control character: below U+0020, or U+007F.
///
/// @param text the bytes
/// @return true when one is there
inline bool holds_control_character(std::string_view text) {
    constexpr uint8_t first_printable = 0x20;
    constexpr uint8_t delete_character = 0x7F;
    for (const char character : text) {
        const auto byte = static_cast<uint8_t>(character);
        if (byte < first_printable || byte == delete_character)
            return true;
    }
    return false;
}

} // namespace oa::platform::file_types::detail
