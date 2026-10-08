// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The font stack: the fonts found beside the program, the sizes that match
// the game's fonts, the fallback chain, text that is not UTF-8, the limits,
// and, with --pixels, a short mixed line drawn pixel for pixel as the pinned
// FreeType draws it. --show TEXT prints a line as it is drawn.

#include "oa/platform/text_font.hpp"
#include "oa/test/check.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>

namespace {

namespace text_font = oa::platform::text_font;
using text_font::Face;
using text_font::Rendering;
using text_font::Style;
using text_font::Weight;

/// The FreeType release whose drawing the pixel check pins.
constexpr std::string_view pinned_freetype = "2.14.3";

/// A style of the stack.
Style style_of(int32_t pixel_size, Weight weight, Rendering rendering = Rendering::mono) {
    Style style;
    style.pixel_size = pixel_size;
    style.weight = weight;
    style.rendering = rendering;
    return style;
}

/// The rows of a line that hold ink: the first and one past the last.
struct InkRows {
    int32_t first{};
    int32_t end{};
};

InkRows ink_rows(const text_font::Coverage& line) {
    InkRows rows{line.height, 0};
    for (int32_t row = 0; row < line.height; ++row)
        for (int32_t column = 0; column < line.width; ++column)
            if (line.alpha[static_cast<std::size_t>(row * line.width + column)] != 0) {
                rows.first = std::min(rows.first, row);
                rows.end = std::max(rows.end, row + 1);
            }
    return rows;
}

/// Rows of ink a text stands in a style.
int32_t ink_height(text_font::FontStack& stack, std::string_view text, const Style& style) {
    const auto line = stack.draw(text, style);
    OA_CHECK(line.has_value());
    if (!line)
        return 0;
    const auto rows = ink_rows(*line);
    return std::max(rows.end - rows.first, 0);
}

/// A line as text: '#' for a covered pixel, '.' for an empty one, a row a line.
std::string picture(const text_font::Coverage& line) {
    std::string out;
    for (int32_t row = 0; row < line.height; ++row) {
        for (int32_t column = 0; column < line.width; ++column)
            out.push_back(
                line.alpha[static_cast<std::size_t>(row * line.width + column)] != 0 ? '#' : '.'
            );
        out.push_back('\n');
    }
    return out;
}

/// Opens the stack beside this program, where the build copies the fonts.
std::unique_ptr<text_font::FontStack> open_beside() {
    const auto directory = text_font::bundled_font_directory();
    OA_CHECK(directory.filename() == "fonts");
    auto stack = text_font::FontStack::open(directory);
    OA_CHECK(stack != nullptr);
    return stack;
}

void decodes_only_utf8() {
    const auto mixed = text_font::decode_utf8("A\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x9A\x80");
    OA_CHECK(mixed.has_value());
    if (mixed)
        OA_CHECK((*mixed == std::vector<char32_t>{U'A', 0xE9, 0x4E2D, 0x1F680}));
    OA_CHECK(text_font::decode_utf8("")->empty());
    // A lone continuation byte, a truncated sequence, an overlong slash, a
    // surrogate, a value past U+10FFFF and a lead byte no sequence has.
    for (std::string_view bad :
         {"\x80",
          "A\xE4\xB8",
          "\xC0\xAF",
          "\xE0\x80\xAF",
          "\xED\xA0\x80",
          "\xF4\x90\x80\x80",
          "\xFF",
          "\xC3("})
        OA_CHECK(!text_font::decode_utf8(bad));
}

void refuses_what_it_cannot_draw() {
    OA_CHECK(text_font::FontStack::open("no such folder") == nullptr);
    auto stack = open_beside();
    if (!stack)
        return;
    const auto bold = style_of(14, Weight::bold);
    OA_CHECK(!stack->draw("A\xE4\xB8", bold));
    OA_CHECK(!stack->draw("\xC0\xAF", bold));
    OA_CHECK(!stack->layout("\xED\xA0\x80", bold));
    OA_CHECK(!stack->draw(std::string(text_font::max_text_bytes + 1, 'A'), bold));
    OA_CHECK(stack->draw(std::string(text_font::max_text_bytes, 'A'), bold).has_value());
    OA_CHECK(!stack->draw("A", style_of(0, Weight::bold)));
    OA_CHECK(!stack->draw("A", style_of(text_font::max_pixel_size + 1, Weight::bold)));
    OA_CHECK(!stack->metrics(style_of(0, Weight::bold)));
    OA_CHECK(!stack->face_metrics(Face::dejavu_sans_bold, style_of(0, Weight::bold)));
    auto spaced = bold;
    spaced.letter_spacing = text_font::max_letter_spacing + 1;
    OA_CHECK(!stack->draw("A", spaced));
    spaced.letter_spacing = -1;
    OA_CHECK(!stack->draw("A", spaced));
    // An empty line, and one of characters that draw nothing, are as tall as
    // the style and take no room.
    for (std::string_view nothing :
         {std::string_view(""), std::string_view("\x01\xE2\x80\x8B\xEF\xB8\x8F")}) {
        const auto line = stack->draw(nothing, bold);
        OA_CHECK(line.has_value());
        if (line)
            OA_CHECK(line->width == 0 && line->advance == 0 && line->height > 0);
    }
    // The longest line is cut off at max_line_width.
    const auto widest =
        stack->draw(std::string(text_font::max_text_bytes, 'W'), style_of(64, Weight::bold));
    OA_CHECK(
        widest && widest->width == text_font::max_line_width && widest->advance > widest->width
    );
}

void falls_back_through_the_chain() {
    auto stack = open_beside();
    if (!stack)
        return;
    const auto bold = text_font::fallback_chain(Weight::bold);
    const auto regular = text_font::fallback_chain(Weight::regular);
    OA_CHECK(bold.size() == 4 && regular.size() == 3);
    OA_CHECK(
        bold[0] == Face::dejavu_sans_bold && bold[1] == Face::dejavu_sans &&
        bold[2] == Face::noto_sans_cjk && bold[3] == Face::noto_emoji
    );
    OA_CHECK(
        regular[0] == Face::dejavu_sans && regular[1] == Face::noto_sans_cjk &&
        regular[2] == Face::noto_emoji
    );
    OA_CHECK(stack->face_for(U'A', Weight::bold) == bold.front());
    OA_CHECK(stack->face_for(U'A', Weight::regular) == regular.front());
    OA_CHECK(stack->face_for(U'A', Weight::bold) == Face::dejavu_sans_bold);
    OA_CHECK(stack->face_for(U'A', Weight::regular) == Face::dejavu_sans);
    OA_CHECK(stack->face_for(0x0416, Weight::bold) == Face::dejavu_sans_bold); // Cyrillic Zhe
    // A mathematical letter DejaVu Sans Bold lacks comes from DejaVu Sans.
    OA_CHECK(stack->face_for(0x1D5A0, Weight::bold) == Face::dejavu_sans);
    OA_CHECK(stack->face_for(0x4E2D, Weight::bold) == Face::noto_sans_cjk);    // Chinese
    OA_CHECK(stack->face_for(0x3042, Weight::regular) == Face::noto_sans_cjk); // hiragana
    OA_CHECK(stack->face_for(0xD55C, Weight::bold) == Face::noto_sans_cjk);    // Hangul
    OA_CHECK(stack->face_for(0x1F680, Weight::bold) == Face::noto_emoji);      // rocket
    OA_CHECK(stack->face_for(0x1F680, Weight::regular) == Face::noto_emoji);
    // A character no font has draws the chain's first font's box.
    OA_CHECK(stack->face_for(0x10FFFD, Weight::bold) == Face::dejavu_sans_bold);
    OA_CHECK(stack->face_for(0x10FFFD, Weight::regular) == Face::dejavu_sans);
    OA_CHECK(!stack->face_for(0x200D, Weight::bold));
    OA_CHECK(!stack->face_for(U'\n', Weight::bold));

    const auto placed =
        stack->layout("A\xE4\xB8\xAD\xE2\x80\x8D\xF0\x9F\x9A\x80", style_of(14, Weight::bold));
    OA_CHECK(placed && placed->size() == 3);
    if (placed && placed->size() == 3) {
        OA_CHECK((*placed)[0].face == Face::dejavu_sans_bold && (*placed)[0].pen == 0);
        OA_CHECK(
            (*placed)[1].face == Face::noto_sans_cjk && (*placed)[1].pen == (*placed)[0].advance
        );
        OA_CHECK((*placed)[2].face == Face::noto_emoji);
        OA_CHECK((*placed)[2].pen == (*placed)[1].pen + (*placed)[1].advance);
    }
}

/// The line's rows are the greatest rows of the faces its weight looks in.
void chain_rows_match(text_font::FontStack& stack, const Style& style) {
    const auto line = stack.metrics(style);
    OA_CHECK(line.has_value());
    int32_t ascent = 0;
    int32_t descent = 0;
    for (const Face face : text_font::fallback_chain(style.weight)) {
        const auto measured = stack.face_metrics(face, style);
        OA_CHECK(measured.has_value());
        if (!measured)
            return;
        ascent = std::max(ascent, measured->ascent);
        descent = std::max(descent, measured->descent);
    }
    if (line)
        OA_CHECK(line->ascent == ascent && line->descent == descent);
}

void matches_the_game_fonts_sizes() {
    auto stack = open_beside();
    if (!stack)
        return;
    OA_CHECK(text_font::message_log_pixel_size == 14);
    OA_CHECK(text_font::status_readout_pixel_size == 11);
    OA_CHECK(text_font::label_pixel_size == 11);
    OA_CHECK(text_font::least_cjk_language_pixel_size == 12);
    // Next to hattfont12, the message log's font: capitals 10 rows and an
    // x-height of 8, from DejaVu Sans Bold at 14 px.
    const auto log = style_of(text_font::message_log_pixel_size, Weight::bold);
    OA_CHECK(ink_height(*stack, "H", log) == 10);
    OA_CHECK(ink_height(*stack, "x", log) == 8);
    // Next to CONSOLE.FNT, the chat line's and the labels' font: an x-height
    // of 6, from DejaVu Sans at 11 px.
    const auto label = style_of(text_font::label_pixel_size, Weight::regular);
    OA_CHECK(ink_height(*stack, "x", label) == 6);
    OA_CHECK(ink_height(*stack, "H", label) == 8);
    // CJK at 12 px beside the 14-px log font: ideographs 11 or 12 rows.
    OA_CHECK(text_font::related_pixel_size(14) == 12);
    OA_CHECK(text_font::related_pixel_size(11) == 11);
    OA_CHECK(text_font::related_pixel_size(28) == 24);
    OA_CHECK(text_font::related_pixel_size(1) == 1);
    const int32_t ideograph = ink_height(*stack, "\xE4\xB8\xAD\xE5\x9C\x8B", log);
    OA_CHECK(ideograph >= 11 && ideograph <= 12);
    // The line fits every font: it is at least as tall as the log's 14 rows.
    const auto bold_sans = stack->face_metrics(Face::dejavu_sans_bold, log);
    const auto cjk = stack->face_metrics(Face::noto_sans_cjk, log);
    const auto emoji = stack->face_metrics(Face::noto_emoji, log);
    const auto regular_sans = stack->face_metrics(Face::dejavu_sans, label);
    // Rows as FreeType 2.14.3 reports them: the bold sans at 14 px, the CJK
    // and emoji faces at 12 px beside it, and the regular sans at 11 px.
    OA_CHECK(bold_sans && bold_sans->pixel_size == 14);
    OA_CHECK(bold_sans && bold_sans->ascent == 13 && bold_sans->descent == 4);
    OA_CHECK(cjk && cjk->pixel_size == 12 && cjk->ascent == 14 && cjk->descent == 4);
    OA_CHECK(emoji && emoji->pixel_size == 12 && emoji->ascent == 12 && emoji->descent == 3);
    OA_CHECK(regular_sans && regular_sans->pixel_size == 11);
    OA_CHECK(regular_sans && regular_sans->ascent == 11 && regular_sans->descent == 3);
    // A regular line still measures the bold face when asked for it.
    OA_CHECK(
        stack->face_metrics(Face::dejavu_sans_bold, label) &&
        stack->face_metrics(Face::dejavu_sans_bold, label)->pixel_size ==
            text_font::label_pixel_size
    );
    chain_rows_match(*stack, log);
    chain_rows_match(*stack, label);
    auto cjk_floor = style_of(text_font::label_pixel_size, Weight::bold);
    cjk_floor.least_cjk_pixel_size = text_font::least_cjk_language_pixel_size;
    const auto held_cjk = stack->face_metrics(Face::noto_sans_cjk, cjk_floor);
    const auto held_emoji = stack->face_metrics(Face::noto_emoji, cjk_floor);
    OA_CHECK(held_cjk && held_cjk->pixel_size == 12);
    OA_CHECK(
        held_emoji &&
        held_emoji->pixel_size == text_font::related_pixel_size(text_font::label_pixel_size)
    );
    chain_rows_match(*stack, cjk_floor);
    const auto line = stack->metrics(log);
    OA_CHECK(line.has_value());
    // The line is the CJK face's rows: 14 above the baseline and 4 below.
    if (line)
        OA_CHECK(line->ascent == 14 && line->descent == 4);
    // A drawn line is at least the style's rows, its baseline at the ascent.
    const auto drawn = stack->draw("Hg", log);
    OA_CHECK(
        drawn && line && drawn->baseline == line->ascent &&
        drawn->height == line->ascent + line->descent
    );
    // H's foot sits on the row above the baseline's.
    const auto capital = stack->draw("H", log);
    OA_CHECK(capital && ink_rows(*capital).end == capital->baseline);
}

void draws_mono_and_antialiased() {
    auto stack = open_beside();
    if (!stack)
        return;
    const auto mono = stack->draw("Aa\xE4\xB8\xAD", style_of(14, Weight::bold));
    OA_CHECK(mono.has_value());
    if (mono)
        OA_CHECK(std::all_of(mono->alpha.begin(), mono->alpha.end(), [](uint8_t a) {
            return a == 0 || a == 255;
        }));
    const auto smooth =
        stack->draw("Aa\xE4\xB8\xAD", style_of(14, Weight::bold, Rendering::antialiased));
    OA_CHECK(smooth.has_value());
    if (smooth)
        OA_CHECK(std::any_of(smooth->alpha.begin(), smooth->alpha.end(), [](uint8_t a) {
            return a > 0 && a < 255;
        }));
    // Letter spacing moves every character after the first by its pixels.
    auto spaced = style_of(14, Weight::bold);
    spaced.letter_spacing = 1;
    const auto plain = stack->draw("HHH", style_of(14, Weight::bold));
    const auto wider = stack->draw("HHH", spaced);
    OA_CHECK(plain && wider && wider->advance == plain->advance + 3);
    // Bold is wider than regular, and a larger size wider than a smaller.
    const auto regular = stack->draw("HHH", style_of(14, Weight::regular));
    const auto larger = stack->draw("HHH", style_of(28, Weight::bold));
    OA_CHECK(
        plain && regular && larger && regular->advance < plain->advance &&
        larger->advance > plain->advance
    );
    // Drawn glyphs are kept for the lines that draw them again.
    const std::size_t kept = stack->cached_glyphs();
    OA_CHECK(kept > 0 && kept <= text_font::FontStack::kept_glyphs);
    const auto again = stack->draw("HHH", style_of(14, Weight::bold));
    OA_CHECK(stack->cached_glyphs() == kept);
    OA_CHECK(again && plain && again->alpha == plain->alpha);
}

void keeps_the_glyphs_used_last() {
    auto stack = open_beside();
    if (!stack)
        return;
    const auto style = style_of(8, Weight::bold);
    // Lines of a hundred new hanzi each, and an A in every one, until more
    // glyphs than the store keeps have been drawn.
    char32_t next = 0x4E00;
    while (stack->drawn_glyphs() <= text_font::FontStack::kept_glyphs + 200) {
        std::string line = "A";
        for (int count = 0; count < 100; ++count) {
            const char32_t character = next++;
            line += static_cast<char>(0xE0 | (character >> 12));
            line += static_cast<char>(0x80 | ((character >> 6) & 0x3F));
            line += static_cast<char>(0x80 | (character & 0x3F));
        }
        OA_CHECK(stack->draw(line, style).has_value());
    }
    // The store stays within its bound and forgets a glyph at a time,
    // never starting again.
    OA_CHECK(stack->cached_glyphs() <= text_font::FontStack::kept_glyphs);
    OA_CHECK(stack->cached_glyphs() > text_font::FontStack::kept_glyphs - 101);
    // The A, used in every line, is kept; the first hanzi, used longest
    // ago, is drawn again.
    const std::size_t drawn = stack->drawn_glyphs();
    OA_CHECK(stack->draw("A", style).has_value());
    OA_CHECK(stack->drawn_glyphs() == drawn);
    OA_CHECK(stack->draw("\xE4\xB8\x80", style).has_value());
    OA_CHECK(stack->drawn_glyphs() == drawn + 1);
}

void holds_ideographs_to_a_least_size() {
    auto stack = open_beside();
    if (!stack)
        return;
    const auto small = style_of(8, Weight::bold);
    auto floored = small;
    floored.least_cjk_pixel_size = 12;
    // An ideograph at 8 px is drawn at 12 px; Latin letters keep their
    // size, in a line grown to hold the ideographs.
    const auto cjk_face = stack->face_metrics(Face::noto_sans_cjk, floored);
    const auto sans_face = stack->face_metrics(Face::dejavu_sans_bold, floored);
    const auto emoji_face = stack->face_metrics(Face::noto_emoji, floored);
    OA_CHECK(cjk_face && cjk_face->pixel_size == 12);
    OA_CHECK(sans_face && sans_face->pixel_size == 8);
    OA_CHECK(emoji_face && emoji_face->pixel_size == text_font::related_pixel_size(8));
    const auto plain = stack->draw("\xE4\xB8\xAD", small);
    const auto held = stack->draw("\xE4\xB8\xAD", floored);
    OA_CHECK(plain && held && plain->advance < 12 && held->advance >= 12);
    const auto latin = stack->draw("H", small);
    const auto latin_held = stack->draw("H", floored);
    OA_CHECK(
        latin && latin_held && latin->advance == latin_held->advance &&
        latin->width == latin_held->width && latin_held->height > latin->height
    );
    auto beyond = small;
    beyond.least_cjk_pixel_size = text_font::max_pixel_size + 1;
    OA_CHECK(!stack->draw("H", beyond));
}

/// "Ab", a Chinese character and the rocket emoji, bold at 14 px, mono:
/// the line as FreeType 2.14.3 draws it with the pinned fonts.
constexpr std::string_view mixed_line = "Ab\xE4\xB8\xAD\xF0\x9F\x9A\x80";
constexpr std::string_view mixed_picture = "................................................\n"
                                           "................................................\n"
                                           "................................................\n"
                                           "............##............##....................\n"
                                           "....###.....##............##...............###..\n"
                                           "....###.....##........##########.........##..#..\n"
                                           "...##.##....##.###....#...##..##........#.#..#..\n"
                                           "...##.##....###..##...#...##..##.......#...##...\n"
                                           "..##...##...##....##..#...##..##......#..##.#...\n"
                                           "..##...##...##....##..#...##..##...###...###....\n"
                                           "..#######...##....##..##########..#..#.#..#.....\n"
                                           ".##.....##..##....##..#...##..##...###..##......\n"
                                           ".##.....##..###..##.......##.......#######......\n"
                                           "##.......##.##.###........##.......#.####.......\n"
                                           "..........................##.......####.#.......\n"
                                           ".......................................#........\n"
                                           "................................................\n"
                                           "................................................\n";

int check_pixels() {
    if (text_font::freetype_version() != pinned_freetype) {
        std::cout << "platform-text-font --pixels: skipped, FreeType "
                  << text_font::freetype_version() << " is not the pinned " << pinned_freetype
                  << '\n';
        return 77;
    }
    auto stack = open_beside();
    if (!stack)
        return oa::test::check_exit_status();
    const auto line = stack->draw(mixed_line, style_of(14, Weight::bold));
    OA_CHECK(line.has_value());
    if (line) {
        const std::string drawn = picture(*line);
        OA_CHECK(drawn == mixed_picture);
        if (drawn != mixed_picture)
            std::cout << "drawn:\n" << drawn;
    }
    return oa::test::check_exit_status();
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--show") {
        auto stack = open_beside();
        const auto line = stack ? stack->draw(argv[2], style_of(14, Weight::bold)) : std::nullopt;
        if (line)
            std::cout << line->width << 'x' << line->height << " baseline " << line->baseline
                      << " origin " << line->origin << " advance " << line->advance << '\n'
                      << picture(*line);
        return line ? 0 : 1;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--pixels")
        return check_pixels();
    decodes_only_utf8();
    refuses_what_it_cannot_draw();
    falls_back_through_the_chain();
    matches_the_game_fonts_sizes();
    draws_mono_and_antialiased();
    keeps_the_glyphs_used_last();
    holds_ideographs_to_a_least_size();
    return oa::test::check_exit_status();
}
