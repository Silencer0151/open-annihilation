// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/typed_text.hpp"

#include "oa/present/game_text.hpp"

#include <string_view>

namespace oa::present {

namespace {

/// The first character after the C0 control characters: the space.
constexpr char32_t first_printable = 0x20;
/// DEL, the first of the control characters that run on through C1.
constexpr char32_t first_high_control = 0x7F;
/// The last C1 control character.
constexpr char32_t last_high_control = 0x9F;
/// The last character Unicode defines.
constexpr char32_t last_character = 0x10FFFF;
/// The characters no file's name may hold on some system.
constexpr std::string_view file_name_reserved = "\\/:*?\"<>|";

/// One character at the start of UTF-8 text.
struct Character {
    std::size_t bytes{}; ///< its bytes; 1 for a byte that starts no sequence
    char32_t code_point{};
    bool well_formed{}; ///< ASCII or a well-formed sequence
};

/// Reads the character at the start of UTF-8 text.
///
/// @param text the text, not empty
/// @return the character
Character first_character(std::string_view text) noexcept {
    const auto lead = static_cast<unsigned char>(text.front());
    if (lead < first_high_control + 1)
        return {1, lead, true};
    if (const auto sequence = utf8_sequence(text); sequence.bytes != 0)
        return {sequence.bytes, sequence.code_point, true};
    return {1, lead, false};
}

} // namespace

bool takes_typed_character(char32_t character, TypedCharacters kind) noexcept {
    if (character < first_printable ||
        (character >= first_high_control && character <= last_high_control) ||
        character > last_character)
        return false;
    if (kind == TypedCharacters::text)
        return true;
    if (kind == TypedCharacters::ascii_file_name && character >= first_high_control)
        return false;
    return character >= first_high_control ||
           file_name_reserved.find(static_cast<char>(character)) == std::string_view::npos;
}

std::size_t character_count(std::string_view text) noexcept {
    std::size_t count = 0;
    for (std::size_t at = 0; at < text.size(); ++count)
        at += first_character(text.substr(at)).bytes;
    return count;
}

std::string typed_characters(std::string_view typed, TypedCharacters kind) {
    std::string taken;
    for (std::size_t at = 0; at < typed.size();) {
        const auto character = first_character(typed.substr(at));
        if (character.well_formed && takes_typed_character(character.code_point, kind))
            taken.append(typed.substr(at, character.bytes));
        at += character.bytes;
    }
    return taken;
}

std::size_t take_typed_text(
    std::string& text, std::string_view typed, TypedCharacters kind, TypedLimits limits
) {
    const std::size_t before = text.size();
    std::size_t characters = character_count(text);
    const std::string taken = typed_characters(typed, kind);
    for (std::size_t at = 0; at < taken.size();) {
        const std::size_t bytes = first_character(std::string_view(taken).substr(at)).bytes;
        if (text.size() + bytes > limits.bytes ||
            (limits.characters != 0 && characters + 1 > limits.characters))
            break;
        text.append(taken, at, bytes);
        ++characters;
        at += bytes;
    }
    return text.size() - before;
}

bool erase_last_character(std::string& text) noexcept {
    if (text.empty())
        return false;
    text.erase(last_character_start(text));
    return true;
}

} // namespace oa::present
