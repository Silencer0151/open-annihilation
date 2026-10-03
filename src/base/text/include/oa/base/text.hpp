// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Copying and appending zero-terminated text in fixed-size character fields,
// without writing outside the field, and cutting text where it does not
// split a UTF-8 character. Nothing here allocates or throws.

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>

namespace oa::base::text {

/// Copies text into a field and fills the rest of the field with zero bytes.
///
/// The text's characters are copied up to its terminating zero or the end of
/// the field, whichever comes first. Text of `size` characters or more fills
/// the field and leaves it without a terminating zero.
///
/// @param field the first byte of the field
/// @param text zero-terminated text; no more than `size` characters of it
///        are read
/// @param size the size of the field, in bytes
inline void copy_padded(char* field, const char* text, std::size_t size) noexcept {
    std::size_t index = 0;
    for (; index < size && text[index] != '\0'; ++index)
        field[index] = text[index];
    for (; index < size; ++index)
        field[index] = '\0';
}

/// Copies as much of a text as fits into a field with a terminating zero
/// after it.
///
/// The bytes after the terminating zero keep what they held.
///
/// @param field the field; an empty one is left as it is
/// @param text the text
inline void copy_terminated(std::span<char> field, std::string_view text) noexcept {
    if (field.empty())
        return;
    const std::size_t count = std::min(text.size(), field.size() - 1);
    std::copy_n(text.data(), count, field.data());
    field[count] = '\0';
}

/// Adds as much of a text as fits to the end of the zero-terminated text a
/// field holds, with a terminating zero after it.
///
/// The bytes after the new terminating zero keep what they held.
///
/// @param field the field; one whose text has no terminating zero is left
///        as it is
/// @param text the text to add
inline void append_terminated(std::span<char> field, std::string_view text) noexcept {
    const auto end = std::find(field.begin(), field.end(), '\0');
    if (end == field.end())
        return;
    copy_terminated(field.subspan(static_cast<std::size_t>(end - field.begin())), text);
}

/// Gives how much of a text a limit keeps without cutting a UTF-8 character
/// in two.
///
/// Text no longer than the limit is kept whole. A longer text is cut at the
/// limit, or, when a well-formed UTF-8 sequence of two to four bytes (not
/// overlong, not a surrogate, not past U+10FFFF) starts before the limit
/// and ends after it, where that sequence starts. Bytes of an 8-bit code
/// page that form no such sequence are cut at the limit.
///
/// @param text the text
/// @param limit the most bytes kept
/// @return the bytes kept
[[nodiscard]] inline std::size_t
whole_characters(std::string_view text, std::size_t limit) noexcept {
    if (text.size() <= limit)
        return text.size();
    constexpr std::size_t longest_sequence = 4;
    const auto byte = [&text](std::size_t at) { return static_cast<unsigned char>(text[at]); };
    for (std::size_t back = 1; back < longest_sequence && back <= limit; ++back) {
        const std::size_t start = limit - back;
        const unsigned char lead = byte(start);
        std::size_t length = 0;
        unsigned long code_point = 0;
        unsigned long smallest = 0;
        if ((lead & 0xE0U) == 0xC0U) {
            length = 2;
            code_point = lead & 0x1FU;
            smallest = 0x80;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3;
            code_point = lead & 0x0FU;
            smallest = 0x800;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = longest_sequence;
            code_point = lead & 0x07U;
            smallest = 0x10000;
        } else if ((lead & 0xC0U) == 0x80U) {
            continue;
        } else {
            return limit;
        }
        if (start + length <= limit || start + length > text.size())
            return limit;
        for (std::size_t next = 1; next < length; ++next) {
            if ((byte(start + next) & 0xC0U) != 0x80U)
                return limit;
            code_point = (code_point << 6) | (byte(start + next) & 0x3FU);
        }
        const bool surrogate = code_point >= 0xD800 && code_point <= 0xDFFF;
        if (code_point < smallest || code_point > 0x10FFFF || surrogate)
            return limit;
        return start;
    }
    return limit;
}

} // namespace oa::base::text
