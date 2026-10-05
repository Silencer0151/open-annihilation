// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Typed text: the characters a line and a file's name take, a field's limits
// in bytes and characters kept by whole characters, and the erase key taking
// a whole character.

#include "oa/present/typed_text.hpp"
#include "oa/test/check.hpp"

#include <string>

namespace {

namespace present = oa::present;
using present::TypedCharacters;

/// U+4E2D and U+5B57 in UTF-8, two hanzi.
constexpr std::string_view middle = "\xE4\xB8\xAD";
constexpr std::string_view writing = "\xE5\xAD\x97";
/// The full-width colon, U+FF1A.
constexpr std::string_view wide_colon = "\xEF\xBC\x9A";

void takes_characters() {
    OA_CHECK(present::takes_typed_character(U'a', TypedCharacters::text));
    OA_CHECK(present::takes_typed_character(0x4E2D, TypedCharacters::file_name));
    OA_CHECK(present::takes_typed_character(0xFF1A, TypedCharacters::file_name));
    // Control characters, C1 included, go nowhere.
    for (const char32_t control : {char32_t{0x09}, char32_t{0x7F}, char32_t{0x85}})
        OA_CHECK(!present::takes_typed_character(control, TypedCharacters::text));
    // A line takes what a file's name refuses.
    OA_CHECK(present::takes_typed_character(U':', TypedCharacters::text));
    for (const char32_t reserved : std::u32string_view(U"\\/:*?\"<>|")) {
        OA_CHECK(!present::takes_typed_character(reserved, TypedCharacters::file_name));
        OA_CHECK(!present::takes_typed_character(reserved, TypedCharacters::ascii_file_name));
    }
    // A file's name in ASCII takes printable ASCII and nothing past it: no
    // Latin-1 letter, euro sign or hanzi.
    OA_CHECK(present::takes_typed_character(U'~', TypedCharacters::ascii_file_name));
    for (const char32_t beyond : {char32_t{0xFC}, char32_t{0x20AC}, char32_t{0x4E2D}})
        OA_CHECK(!present::takes_typed_character(beyond, TypedCharacters::ascii_file_name));
    // "Grüße " in UTF-8, a hanzi and a digit: the ASCII alone is taken.
    const std::string typed = "Gr\xC3\xBC\xC3\x9F\x65 " + std::string(middle) + "1";
    OA_CHECK(present::typed_characters(typed, TypedCharacters::ascii_file_name) == "Gre 1");
}

void reads_typed_characters() {
    OA_CHECK(present::character_count(std::string(middle) + "a" + std::string(writing)) == 3);
    // A lone byte that starts no sequence counts as one character.
    OA_CHECK(present::character_count("\xE9t") == 2);
    // Malformed bytes and refused characters are left out.
    OA_CHECK(
        present::typed_characters(
            std::string("a\xE9") + std::string(middle) + "\t:", TypedCharacters::file_name
        ) == "a" + std::string(middle)
    );
    OA_CHECK(
        present::typed_characters(std::string(wide_colon) + "?", TypedCharacters::file_name) ==
        wide_colon
    );
}

void keeps_limits() {
    // The bytes: a hanzi that would pass the limit is not cut, nor is the
    // rest taken.
    std::string text = "ab";
    OA_CHECK(
        present::take_typed_text(
            text, std::string(middle) + std::string(writing) + "c", TypedCharacters::text, {6, 0}
        ) == 3
    );
    OA_CHECK(text == "ab" + std::string(middle));
    // The characters: three, whatever their bytes.
    text.clear();
    OA_CHECK(
        present::take_typed_text(
            text, std::string(middle) + std::string(writing) + "xyz", TypedCharacters::text, {64, 3}
        ) == 7
    );
    OA_CHECK(text == std::string(middle) + std::string(writing) + "x");
}

void erases_whole_characters() {
    std::string text = "a" + std::string(middle) + std::string(writing);
    OA_CHECK(present::erase_last_character(text) && text == "a" + std::string(middle));
    OA_CHECK(present::erase_last_character(text) && text == "a");
    OA_CHECK(present::erase_last_character(text) && text.empty());
    OA_CHECK(!present::erase_last_character(text));
    // A code-page byte is one character.
    text = "caf\xE9";
    OA_CHECK(present::erase_last_character(text) && text == "caf");
}

} // namespace

int main() {
    takes_characters();
    reads_typed_characters();
    keeps_limits();
    erases_whole_characters();
    return oa::test::check_exit_status();
}
