// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Copying and appending zero-terminated text in fixed-size character fields,
// without writing outside the field. Nothing here allocates or throws.

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

} // namespace oa::base::text
