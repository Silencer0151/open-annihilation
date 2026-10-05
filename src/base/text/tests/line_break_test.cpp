// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// first_row with a width of one column a byte: Latin text breaks at its
// spaces alone; Chinese breaks between any two characters, and never puts
// a closing mark at a row's start or an opening mark at its end; mixed
// text keeps its Latin words and numbers whole; the characters the
// breaker reads from bytes that are not UTF-8; and cuts at whole
// characters and widths in columns.

#include "oa/base/text/line_break.hpp"
#include "oa/test/check.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

using oa::base::text::break_character;
using oa::base::text::first_row;
using oa::base::text::has_wide_script;
using oa::base::text::may_break_between;
using oa::base::text::text_columns;
using oa::base::text::whole_character_bytes;

/// Breaks a text into rows of at most `width` columns, each character as
/// wide as the given columns: 1 for ASCII, 2 for any other.
///
/// @param text the text
/// @param width the room, in columns
/// @return the rows
std::vector<std::string> rows(std::string_view text, std::size_t width) {
    const auto columns = [](std::string_view row) {
        std::size_t count = 0;
        for (std::size_t at = 0; at < row.size();) {
            const auto read = break_character(row.substr(at));
            count += read.character < 0x80 ? 1 : 2;
            at += read.bytes;
        }
        return count;
    };
    std::vector<std::string> found;
    while (!text.empty()) {
        const auto row =
            first_row(text, [&](std::string_view start) { return columns(start) <= width; });
        found.emplace_back(text.substr(0, row.bytes));
        text.remove_prefix(row.next);
    }
    return found;
}

void latin_breaks_at_spaces() {
    OA_CHECK(
        (rows("the quick brown fox", 10) == std::vector<std::string>{"the quick", "brown fox"})
    );
    // A space that does not fit ends the row; the spaces belong to neither.
    OA_CHECK((rows("abcde   fgh", 5) == std::vector<std::string>{"abcde", "fgh"}));
    // A word wider than the row breaks before the letter that does not fit,
    // a comma there included.
    OA_CHECK((rows("abcdef,gh", 6) == std::vector<std::string>{"abcdef", ",gh"}));
    OA_CHECK((rows("ok", 10) == std::vector<std::string>{"ok"}));
    OA_CHECK(rows("", 10).empty());
}

void chinese_breaks_between_characters() {
    // 指挥官已经阵亡 at 6 columns: three characters a row.
    OA_CHECK((rows("指挥官已经阵亡", 6) == std::vector<std::string>{"指挥官", "已经阵", "亡"}));
    // A full-width comma never starts a row: the character before it goes
    // down with it.
    OA_CHECK((rows("建造完成，单位", 8) == std::vector<std::string>{"建造完", "成，单位"}));
    // An opening bracket never ends a row: it goes down with what it opens.
    OA_CHECK((rows("前往（基地", 6) == std::vector<std::string>{"前往", "（基地"}));
    // Full stop and closing quote stay at the end of the row they close.
    OA_CHECK((rows("胜利。「好」", 6) == std::vector<std::string>{"胜利。", "「好」"}));
}

void mixed_text_keeps_words_whole() {
    // A space early in the row does not stop the row filling with hanzi.
    OA_CHECK(
        (rows("ARM 指挥官已经阵亡", 10) == std::vector<std::string>{"ARM 指挥官", "已经阵亡"})
    );
    // A Latin word and a number go whole to the next row.
    OA_CHECK((rows("金属1000单位", 6) == std::vector<std::string>{"金属", "1000单", "位"}));
    OA_CHECK((rows("建造Commander", 10) == std::vector<std::string>{"建造", "Commander"}));
}

void reads_characters() {
    OA_CHECK(break_character("\xE4\xB8\xAD").character == U'中');
    OA_CHECK(break_character("\xE4\xB8\xAD").bytes == 3);
    // A byte that starts no sequence, and a cut sequence, are one character each.
    OA_CHECK(break_character("\xE9").character == 0xE9);
    OA_CHECK(break_character("\xE4\xB8").bytes == 1);
    OA_CHECK(break_character("").bytes == 0);
    OA_CHECK(has_wide_script("Arm \xE4\xB8\xAD"));
    OA_CHECK(!has_wide_script("Arm caf\xC3\xA9"));
    OA_CHECK(may_break_between(U'中', U'A'));
    OA_CHECK(!may_break_between(U'A', U'B'));
    OA_CHECK(!may_break_between(U'中', U'，'));
    OA_CHECK(!may_break_between(U'（', U'中'));
    // Bytes of the game's 8-bit code page break as Latin text does.
    OA_CHECK((rows("caf\xE9 cr\xE8me", 6) == std::vector<std::string>{"caf\xE9", "cr\xE8me"}));
}

void cuts_and_measures_whole_characters() {
    // 指挥官 is 9 bytes: 7 bytes hold two characters, never part of one.
    OA_CHECK(whole_character_bytes("指挥官", 7) == 6);
    OA_CHECK(whole_character_bytes("指挥官", 9) == 9);
    OA_CHECK(whole_character_bytes("Arm 指挥官", 5) == 4);
    OA_CHECK(whole_character_bytes("abc", 2) == 2);
    // Hanzi and full-width marks take two columns, Latin letters one.
    OA_CHECK(text_columns("Arm 指挥官。") == 12);
    OA_CHECK(text_columns("caf\xC3\xA9") == 4);
}

} // namespace

int main() {
    latin_breaks_at_spaces();
    chinese_breaks_between_characters();
    mixed_text_keeps_words_whole();
    reads_characters();
    cuts_and_measures_whole_characters();
    return oa::test::check_exit_status();
}
