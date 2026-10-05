// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Text a player types into a field: the characters a field takes from what
// the platform sends, and the limits a field keeps in bytes and characters.
// An input method's composition is shown after the typed text until it is
// committed; while it is open, the keys it uses act on it and not on the
// field.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace oa::present {

/// Which characters a field takes.
enum class TypedCharacters : uint8_t {
    text,            ///< every character but the control characters
    file_name,       ///< text, less the characters no file's name may hold
    ascii_file_name, ///< a file's name in printable ASCII alone
};

/// The most a field holds.
struct TypedLimits {
    std::size_t bytes{};      ///< UTF-8 bytes
    std::size_t characters{}; ///< characters; 0 for no limit but the bytes
};

/// Tells whether a field takes a character.
///
/// No field takes a control character (U+0000-U+001F, U+007F-U+009F) or a
/// character past U+10FFFF. A file's name also leaves out \ / : * ? " < > |,
/// which some system's file names cannot hold; the full-width colon and
/// question mark (U+FF1A, U+FF1F) are ordinary characters. A file's name in
/// ASCII takes nothing past U+007E either.
///
/// @param character the character
/// @param kind which characters the field takes
/// @return true when the field takes it
[[nodiscard]] bool takes_typed_character(char32_t character, TypedCharacters kind) noexcept;

/// Counts the characters of UTF-8 text.
///
/// @param text UTF-8 text; each byte that starts no well-formed sequence
///        counts as one character
/// @return the characters
[[nodiscard]] std::size_t character_count(std::string_view text) noexcept;

/// Gives the characters of typed text a field takes.
///
/// @param typed UTF-8, as the platform sends it
/// @param kind which characters the field takes
/// @return the characters taken, in order, as UTF-8; bytes that start no
///         well-formed sequence and characters the field refuses are left
///         out
[[nodiscard]] std::string typed_characters(std::string_view typed, TypedCharacters kind);

/// Adds typed text to a field's text, whole characters at a time, while the
/// text stays within the field's limits.
///
/// The characters the field refuses are left out; the first character that
/// would take the text past a limit, and the rest after it, are not taken.
///
/// @param[in,out] text the field's text, UTF-8
/// @param typed UTF-8, as the platform sends it
/// @param kind which characters the field takes
/// @param limits the most the field holds
/// @return the bytes added
std::size_t take_typed_text(
    std::string& text, std::string_view typed, TypedCharacters kind, TypedLimits limits
);

/// Removes the last character of a field's text, for the erase key.
///
/// @param[in,out] text the field's text: UTF-8, or game text, whose bytes
///        outside a well-formed sequence are each one character
/// @return true when a character was removed; false for an empty text
bool erase_last_character(std::string& text) noexcept;

} // namespace oa::present
