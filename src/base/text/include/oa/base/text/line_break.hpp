// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Where a line of text may break into rows: at spaces, as Latin text breaks,
// and between any two Chinese, Japanese or Korean characters, which are
// written without spaces, except where the line-breaking rules of those
// languages (kinsoku) forbid it: no row starts with a closing mark, a comma
// or a full stop, and none ends with an opening mark. A Latin word or a
// number is never broken while there is any other place to break. Nothing
// here allocates or throws.

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace oa::base::text {

/// One character at the start of a text, as the line breaker reads it.
struct BreakCharacter {
    char32_t
        character{};     ///< the code point; a byte that starts no UTF-8 sequence stands for itself
    std::size_t bytes{}; ///< its bytes: 1 to 4; 0 for an empty text
};

/// Reads the character at the start of a text.
///
/// A well-formed UTF-8 sequence (not overlong, not a surrogate, not past
/// U+10FFFF) is one character; any other byte is a character of its own
/// whose code point is the byte's value.
///
/// @param text the text
/// @return the character and its bytes; no bytes for an empty text
[[nodiscard]] inline BreakCharacter break_character(std::string_view text) noexcept {
    if (text.empty())
        return {};
    const auto byte = [&text](std::size_t at) { return static_cast<unsigned char>(text[at]); };
    const unsigned char lead = byte(0);
    std::size_t length = 0;
    char32_t code_point = 0;
    char32_t smallest = 0;
    if (lead >= 0xC2U && lead <= 0xDFU) {
        length = 2;
        code_point = lead & 0x1FU;
        smallest = 0x80;
    } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3;
        code_point = lead & 0x0FU;
        smallest = 0x800;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        length = 4;
        code_point = lead & 0x07U;
        smallest = 0x10000;
    } else {
        return {lead, 1};
    }
    if (text.size() < length)
        return {lead, 1};
    for (std::size_t next = 1; next < length; ++next) {
        if ((byte(next) & 0xC0U) != 0x80U)
            return {lead, 1};
        code_point = (code_point << 6) | (byte(next) & 0x3FU);
    }
    if (code_point < smallest || code_point > 0x10FFFF ||
        (code_point >= 0xD800 && code_point <= 0xDFFF))
        return {lead, 1};
    return {code_point, length};
}

/// Tells whether a character is one of the scripts written without spaces
/// between words, so that a row may break before or after it: the CJK
/// ideographs, radicals, strokes and compatibility forms, kana, bopomofo,
/// Hangul, Yi, the CJK symbols and punctuation, the enclosed and
/// compatibility characters, and the full- and half-width forms.
///
/// @param character the code point
/// @return true for those characters
[[nodiscard]] constexpr bool is_wide_script(char32_t character) noexcept {
    return (character >= 0x1100 && character <= 0x115F) ||
           (character >= 0x2E80 && character <= 0x9FFF) ||
           (character >= 0xA000 && character <= 0xA4CF) ||
           (character >= 0xAC00 && character <= 0xD7A3) ||
           (character >= 0xF900 && character <= 0xFAFF) ||
           (character >= 0xFE30 && character <= 0xFE4F) ||
           (character >= 0xFF00 && character <= 0xFFEF) ||
           (character >= 0x20000 && character <= 0x3FFFF);
}

/// The characters no row starts with: closing brackets and quotes, commas,
/// full stops, colons, the question and exclamation marks, the iteration
/// and prolonged-sound marks, small kana and the ellipsis, in their CJK,
/// full-width and Latin forms.
inline constexpr std::array<char32_t, 75> row_start_forbidden{
    U')',  U']',  U'}',  U',',  U'.',  U':',  U';',  U'?',  U'!',  U'%',  U'»',  U'’',  U'”',
    U'…',  U'‥',  U'‰',  U'′',  U'″',  U'℃',  U'、', U'。', U'々', U'〉', U'》', U'」', U'』',
    U'】', U'〕', U'〗', U'〙', U'〛', U'〞', U'〟', U'〻', U'ぁ', U'ぃ', U'ぅ', U'ぇ', U'ぉ',
    U'っ', U'ゃ', U'ゅ', U'ょ', U'ゎ', U'゛', U'゜', U'ゝ', U'ゞ', U'ァ', U'ィ', U'ゥ', U'ェ',
    U'ォ', U'ッ', U'ャ', U'ュ', U'ョ', U'ヮ', U'ヵ', U'ヶ', U'・', U'ー', U'ヽ', U'ヾ', U'！',
    U'％', U'）', U'，', U'．', U'：', U'；', U'？', U'］', U'｝', U'｡',
};

/// The characters no row ends with: opening brackets and quotes, and the
/// currency signs written before a number, in their CJK, full-width and
/// Latin forms.
inline constexpr std::array<char32_t, 24> row_end_forbidden{
    U'(',  U'[',  U'{',  U'«',  U'‘',  U'“',  U'〈', U'《', U'「', U'『', U'【', U'〔',
    U'〖', U'〘', U'〚', U'〝', U'＃', U'＄', U'（', U'［', U'｛', U'｢',  U'￡', U'￥',
};

/// Tells whether a row may start with a character.
///
/// @param character the code point
/// @return false for the characters row_start_forbidden lists
[[nodiscard]] constexpr bool may_start_row(char32_t character) noexcept {
    return std::find(row_start_forbidden.begin(), row_start_forbidden.end(), character) ==
           row_start_forbidden.end();
}

/// Tells whether a row may end with a character.
///
/// @param character the code point
/// @return false for the characters row_end_forbidden lists
[[nodiscard]] constexpr bool may_end_row(char32_t character) noexcept {
    return std::find(row_end_forbidden.begin(), row_end_forbidden.end(), character) ==
           row_end_forbidden.end();
}

/// Tells whether a row may break between two characters with no space
/// between them: one of them is_wide_script, the first may end a row and
/// the second may start one.
///
/// @param before the character before the break
/// @param after the character after it
/// @return true where the row may break
[[nodiscard]] constexpr bool may_break_between(char32_t before, char32_t after) noexcept {
    if (before == U' ' || after == U' ')
        return false;
    return (is_wide_script(before) || is_wide_script(after)) && may_end_row(before) &&
           may_start_row(after);
}

/// Gives how much of a text's start a buffer of `most` bytes holds in whole
/// characters, so that a cut there leaves no UTF-8 sequence in part.
///
/// @param text the text
/// @param most the most bytes
/// @return the bytes of the whole characters that fit, `most` or fewer; the
///         text's size when it fits
[[nodiscard]] inline std::size_t
whole_character_bytes(std::string_view text, std::size_t most) noexcept {
    if (text.size() <= most)
        return text.size();
    std::size_t kept = 0;
    while (kept < most) {
        const BreakCharacter read = break_character(text.substr(kept));
        if (kept + read.bytes > most)
            break;
        kept += read.bytes;
    }
    return kept;
}

/// Gives a text's width in columns, as a fixed-pitch estimate lays it: two
/// for a character of the scripts written without spaces (is_wide_script),
/// which are drawn about twice as wide as a Latin letter, one for any other.
///
/// @param text the text, UTF-8
/// @return the columns
[[nodiscard]] inline std::size_t text_columns(std::string_view text) noexcept {
    std::size_t columns = 0;
    for (std::size_t at = 0; at < text.size();) {
        const BreakCharacter read = break_character(text.substr(at));
        columns += is_wide_script(read.character) ? 2 : 1;
        at += read.bytes;
    }
    return columns;
}

/// Tells whether a text holds a character of the scripts written without
/// spaces (is_wide_script).
///
/// @param text the text
/// @return true when one of its characters is_wide_script
[[nodiscard]] inline bool has_wide_script(std::string_view text) noexcept {
    for (std::size_t at = 0; at < text.size();) {
        const BreakCharacter read = break_character(text.substr(at));
        if (is_wide_script(read.character))
            return true;
        at += read.bytes;
    }
    return false;
}

/// Where the first row of a text ends.
struct RowBreak {
    /// the row's bytes from the text's start, the spaces it was broken at
    /// left out
    std::size_t bytes{};
    /// where the next row starts, after those spaces; the text's size when
    /// the row is the last
    std::size_t next{};
};

/// Finds where the first row of a text ends, as wide as a test lets it be.
///
/// The row takes characters while `fits` accepts it, and always its first
/// character. When the next character does not fit, the row breaks at the
/// last place before it where it may: before a space or after one, or
/// between two characters may_break_between allows. Without one, in a
/// word wider than the row, it breaks before the character that does not
/// fit; where either of the two is_wide_script, one character earlier when
/// that character may not start a row or the one before it may not end
/// one. Spaces at the break belong to neither row. A text without wide
/// characters breaks at its spaces alone.
///
/// @param text the text, from the row's start; its line breaks are not
///        read as breaks, so the caller splits the text at them first
/// @param fits called with a start of the text; true when that start fits
///        in the row
/// @return the row's bytes and where the next row starts
template <class Fits>
[[nodiscard]] RowBreak first_row(std::string_view text, Fits&& fits) {
    std::size_t opportunity = 0;
    char32_t previous = 0;
    std::size_t previous_at = 0;
    std::size_t end = text.size();
    for (std::size_t at = 0; at < text.size();) {
        const BreakCharacter read = break_character(text.substr(at));
        if (at > 0) {
            if (read.character == U' ' || previous == U' ' ||
                may_break_between(previous, read.character))
                opportunity = at;
            if (!fits(text.substr(0, at + read.bytes))) {
                end = opportunity;
                // A word wider than the row breaks before the character
                // that does not fit; in the wide scripts, one earlier when
                // that would start the next row with a closing mark or end
                // this one with an opening mark.
                const bool wide = is_wide_script(read.character) || is_wide_script(previous);
                if (opportunity == 0)
                    end = wide && previous_at > 0 &&
                                  (!may_start_row(read.character) || !may_end_row(previous))
                              ? previous_at
                              : at;
                break;
            }
        }
        previous = read.character;
        previous_at = at;
        at += read.bytes;
    }
    RowBreak row{end, end};
    while (row.bytes > 1 && text[row.bytes - 1] == ' ')
        --row.bytes;
    while (row.next < text.size() && text[row.next] == ' ')
        ++row.next;
    return row;
}

} // namespace oa::base::text
