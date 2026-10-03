// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend_renderer/gadget_draw.hpp"
#include "oa/ui/frontend_renderer/game_text.hpp"

#include "oa/present/display.hpp"
#include "oa/present/palette_tables.hpp"
#include "oa/present/rle.hpp"
#include "oa/present/surface.hpp"
#include "oa/present/game_text.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        ++failures;
    }
}

uint8_t at(const oa::present::SurfaceBuffer& buffer, int x, int y) {
    return buffer.pixels[static_cast<std::size_t>(y * buffer.surface.pitch + x)];
}

std::string rows(const oa::present::SurfaceBuffer& buffer) {
    std::string out;
    for (int y = 0; y < buffer.surface.height; ++y) {
        for (int x = 0; x < buffer.surface.width; ++x)
            out += static_cast<char>('0' + at(buffer, x, y));
        out += '\n';
    }
    return out;
}

// A GAF font whose first sequence has glyphs 'A' (2x2 row-RLE, key 0,
// hotspot (0,1)), ' ' (3 wide) and 'B' (1x1 raw).
struct TestFont {
    oa::present::GafSprites gaf;
    std::vector<uint8_t> a_pixels{5, 0, 5, 6};
    std::vector<uint8_t> a_stream;
    std::vector<uint8_t> b_pixels{7};
};

void build_font(TestFont& font) {
    constexpr int glyph_count = 128;
    oa::Sprite a{};
    a.width = 2;
    a.height = 2;
    a.origin_y = 1;
    a.data = font.a_pixels.data();
    oa::present::RleEncoder encoder;
    font.a_stream.resize(
        static_cast<std::size_t>(oa::present::encode_rle_sprite(encoder, nullptr, a))
    );
    oa::present::encode_rle_sprite(encoder, font.a_stream.data(), a);
    a.encoding = OA_SPRITE_ROW_RLE;
    a.data = font.a_stream.data();
    oa::Sprite space{};
    space.width = 3;
    space.height = 1;
    oa::Sprite b{};
    b.width = 1;
    b.height = 1;
    b.key = 0xFF;
    b.data = font.b_pixels.data();
    font.gaf.sprites = {a, space, b};
    font.gaf.slots.assign(glyph_count, oa::present::GafFrameSlot{});
    font.gaf.slots['A'].frame = &font.gaf.sprites[0];
    font.gaf.slots[' '].frame = &font.gaf.sprites[1];
    font.gaf.slots['B'].frame = &font.gaf.sprites[2];
    oa::present::GafSequence glyphs{};
    glyphs.frame_count = glyph_count;
    glyphs.frames = font.gaf.slots.data();
    font.gaf.sequences = {glyphs};
}

void put_le(std::vector<uint8_t>& bytes, std::size_t at, uint32_t value, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i)
        bytes[at + i] = static_cast<uint8_t>(value >> (8 * i));
}

// One sequence of glyphs 0..'I', each a raw 1x1 frame with hotspot y 3,
// except 'I', which is 1x5.
std::vector<uint8_t> font_file() {
    constexpr std::size_t glyphs = 'I' + 1;
    constexpr std::size_t sequence = 0x10;
    constexpr std::size_t slots = sequence + 0x28;
    constexpr std::size_t headers = slots + glyphs * 8;
    constexpr std::size_t pixels = headers + glyphs * 0x18;
    std::vector<uint8_t> bytes(pixels + glyphs * 5, 0);
    put_le(bytes, 0, 0x00010100, 4);
    put_le(bytes, 4, 1, 4);
    put_le(bytes, 0x0C, sequence, 4);
    put_le(bytes, sequence, glyphs, 2);
    for (std::size_t glyph = 0; glyph < glyphs; ++glyph) {
        const std::size_t header = headers + glyph * 0x18;
        put_le(bytes, slots + glyph * 8, static_cast<uint32_t>(header), 4);
        put_le(bytes, header, 1, 2);
        put_le(bytes, header + 2, glyph == 'I' ? 5 : 1, 2);
        put_le(bytes, header + 6, 3, 2);
        put_le(bytes, header + 0x10, static_cast<uint32_t>(pixels + glyph * 5), 4);
    }
    return bytes;
}

void test_gui_font() {
    namespace draw = oa::ui::frontend_renderer;
    const auto file = font_file();
    oa::present::GafSprites font;
    require(draw::load_gui_font(file, font) == oa::present::GafStatus::ok, "gui font: relocates");
    const auto* glyphs = &font.sequences.front();
    require(
        oa::present::gaf_frame(glyphs, 'A')->origin_y == -2 &&
            oa::present::gaf_frame(glyphs, 'I')->origin_y == -2 &&
            oa::present::gaf_frame(glyphs, 0)->origin_y == -2,
        "gui font: every hotspot moves up by the height of 'I'"
    );
    oa::present::GafSprites broken;
    require(
        draw::load_gui_font(std::span(file).first(0x20), broken) != oa::present::GafStatus::ok,
        "gui font: a truncated file is rejected"
    );
}

void test_gadget_text() {
    namespace draw = oa::ui::frontend_renderer;
    TestFont font;
    build_font(font);
    auto buffer = oa::present::create_surface(12, 3);
    std::fill(buffer.pixels.begin(), buffer.pixels.end(), uint8_t{9});
    draw::draw_gadget_text(
        &buffer.surface,
        &font.gaf,
        "A B\x01"
        "AC",
        1,
        2,
        draw::gadget_text_unbounded,
        0
    );
    // Glyphs sit at the pen less their hotspot; the space advances 3 without
    // drawing, the control byte and the glyphless 'C' are skipped.
    require(
        at(buffer, 1, 1) == 5 && at(buffer, 2, 1) == 9 && at(buffer, 1, 2) == 5 &&
            at(buffer, 2, 2) == 6,
        "gadget text: 'A' at the pen less its hotspot, key pixels skipped"
    );
    require(at(buffer, 3, 1) == 9 && at(buffer, 5, 1) == 9, "gadget text: the space draws nothing");
    require(at(buffer, 6, 2) == 7, "gadget text: 'B' after the space's advance");
    require(
        at(buffer, 7, 1) == 5 && at(buffer, 8, 2) == 6, "gadget text: the control byte is skipped"
    );
    require(
        at(buffer, 9, 1) == 9 && at(buffer, 9, 2) == 9,
        "gadget text: a byte without a glyph draws nothing"
    );

    // A width budget stops before the first glyph wider than what is left.
    std::fill(buffer.pixels.begin(), buffer.pixels.end(), uint8_t{9});
    draw::draw_gadget_text(&buffer.surface, &font.gaf, "A B", 0, 1, 5, 0);
    require(at(buffer, 0, 0) == 5 && at(buffer, 5, 1) == 9, "gadget text: the budget stops at 'B'");

    // A light level draws through that row of the light table.
    oa::present::DisplayContext display{};
    display.flags = oa::present::display_flag_light_table;
    oa::present::load_light_table(display);
    for (int row = 0; row < oa::present::ramp_table_rows; ++row)
        for (int index = 0; index < 256; ++index)
            display.light_table[row * 256 + index] = static_cast<uint8_t>(index + row);
    oa::present::bind_display(&display);
    std::fill(buffer.pixels.begin(), buffer.pixels.end(), uint8_t{9});
    draw::draw_gadget_text(&buffer.surface, &font.gaf, "A", 0, 1, draw::gadget_text_unbounded, 30);
    require(
        at(buffer, 0, 0) == 35 && at(buffer, 1, 0) == 9 && at(buffer, 1, 1) == 36,
        "gadget text: a light level remaps the glyph through its row"
    );
    oa::present::bind_display(nullptr);
    oa::present::free_light_table(display);
}

// What the game-text hooks answer: the settings, and whether a line is drawn.
struct GameTextAnswers {
    oa::present::TextSettings settings{};
    bool draws{true};
    std::vector<uint8_t> palette{};
    int32_t size{}; ///< the size the last line was drawn at, in percent
};

/// A line of solid characters, 3 pixels wide and 2 rows above the baseline.
std::shared_ptr<const oa::present::TextMask> solid_line(std::string_view text) {
    auto mask = std::make_shared<oa::present::TextMask>();
    int32_t pen = 0;
    for (std::size_t at = 0; at < text.size();) {
        const auto sequence = oa::present::utf8_sequence(text.substr(at));
        at += sequence.bytes != 0 ? sequence.bytes : 1;
        pen += 3;
        mask->character_ends.push_back(pen);
    }
    mask->width = pen;
    mask->height = 2;
    mask->baseline = 2;
    mask->advance = pen;
    mask->alpha.assign(static_cast<std::size_t>(pen) * 2U, 255);
    return mask;
}

void test_gadget_text_game_runs() {
    namespace draw = oa::ui::frontend_renderer;
    TestFont font;
    build_font(font);
    GameTextAnswers answers;
    answers.settings.style = {false, false, false, false, oa::present::default_text_size};
    answers.settings.utf8 = true;
    // Entry 4 is hattfont12's colour, which the modern runs are drawn in.
    answers.palette.assign(8 * 4, 0);
    answers.palette[4 * 4] = 195;
    answers.palette[4 * 4 + 1] = 195;
    answers.palette[4 * 4 + 2] = 155;
    oa::present::GameTextHooks hooks{};
    hooks.context = &answers;
    hooks.settings = [](void* context) { return static_cast<GameTextAnswers*>(context)->settings; };
    hooks.draw = [](void* context,
                    std::string_view text,
                    oa::present::TextFace,
                    int32_t,
                    int32_t size) -> std::shared_ptr<const oa::present::TextMask> {
        auto& answers = *static_cast<GameTextAnswers*>(context);
        answers.size = size;
        return answers.draws ? solid_line(text) : nullptr;
    };
    hooks.palette = [](void* context) -> std::span<const uint8_t> {
        return static_cast<GameTextAnswers*>(context)->palette;
    };
    oa::present::set_game_text_hooks(hooks);
    const std::string sun = "\xE6\x97\xA5";
    const std::string text = "A" + sun + "B";
    auto buffer = oa::present::create_surface(12, 4);
    std::fill(buffer.pixels.begin(), buffer.pixels.end(), uint8_t{9});
    draw::draw_gadget_text(
        &buffer.surface, &font.gaf, text.c_str(), 1, 2, draw::gadget_text_unbounded, 0
    );
    // The font has no 'I': the baseline is the pen row, and the run stands
    // on it after 'A'.
    require(
        at(buffer, 3, 0) == 4 && at(buffer, 5, 1) == 4 && at(buffer, 3, 2) == 9,
        "gadget text: the font's missing character is drawn in the modern fonts after 'A'"
    );
    require(at(buffer, 6, 2) == 7, "gadget text: 'B' follows the run's width");
    require(
        draw::measure_gadget_text(&font.gaf, text.c_str()) == 2 + 3 + 1,
        "gadget text: the run measures as drawn"
    );
    // A width budget stops before a run wider than what is left.
    std::fill(buffer.pixels.begin(), buffer.pixels.end(), uint8_t{9});
    draw::draw_gadget_text(&buffer.surface, &font.gaf, text.c_str(), 1, 2, 4, 0);
    require(
        at(buffer, 3, 0) == 9 && at(buffer, 6, 2) == 9, "gadget text: the budget stops at the run"
    );
    // A run the modern fonts cannot draw is the code page's '?', which the
    // font has no glyph for.
    answers.draws = false;
    std::fill(buffer.pixels.begin(), buffer.pixels.end(), uint8_t{9});
    draw::draw_gadget_text(
        &buffer.surface, &font.gaf, text.c_str(), 1, 2, draw::gadget_text_unbounded, 0
    );
    require(
        at(buffer, 3, 0) == 9 && at(buffer, 3, 2) == 7,
        "gadget text: a run the modern fonts cannot draw takes no room"
    );
    // Game text is drawn whole in the modern fonts while the settings choose
    // them; interface text keeps the font.
    answers.draws = true;
    answers.settings.style.modern_fonts = true;
    std::fill(buffer.pixels.begin(), buffer.pixels.end(), uint8_t{9});
    draw::draw_gadget_text(
        &buffer.surface, &font.gaf, "AB", 1, 2, draw::gadget_text_unbounded, 0, true
    );
    require(
        at(buffer, 1, 0) == 4 && at(buffer, 6, 1) == 4 && at(buffer, 1, 2) == 9,
        "gadget text: game text is drawn in the modern fonts"
    );
    require(
        answers.size == oa::present::default_text_size,
        "gadget text: game text is drawn at the text size"
    );
    // Gadgets are laid out for the game's fonts: game text larger than them
    // is drawn at their size, on the same baseline; smaller is drawn smaller.
    answers.settings.style.size = oa::present::highest_text_size;
    std::fill(buffer.pixels.begin(), buffer.pixels.end(), uint8_t{9});
    draw::draw_gadget_text(
        &buffer.surface, &font.gaf, "AB", 1, 2, draw::gadget_text_unbounded, 0, true
    );
    require(
        answers.size == oa::present::game_font_text_size && at(buffer, 1, 0) == 4 &&
            at(buffer, 1, 2) == 9,
        "gadget text: game text keeps to the game fonts' size"
    );
    answers.settings.style.size = oa::present::lowest_text_size;
    draw::draw_gadget_text(
        &buffer.surface, &font.gaf, "AB", 1, 2, draw::gadget_text_unbounded, 0, true
    );
    require(
        answers.size == oa::present::lowest_text_size, "gadget text: smaller text is drawn smaller"
    );
    require(
        draw::screen_text_size({true, "AB", oa::present::highest_text_size}) ==
                oa::present::game_font_text_size &&
            draw::screen_text_size({true, "AB", oa::present::lowest_text_size}) ==
                oa::present::lowest_text_size,
        "gadget text: the screens hold text to the game fonts' size"
    );
    // A label's missing character is drawn at the font's own size, whatever
    // the setting.
    draw::draw_gadget_text(
        &buffer.surface, &font.gaf, text.c_str(), 1, 2, draw::gadget_text_unbounded, 0
    );
    require(
        answers.size == oa::present::game_font_text_size,
        "gadget text: a missing character keeps the font's size"
    );
    answers.settings.style.size = oa::present::default_text_size;
    std::fill(buffer.pixels.begin(), buffer.pixels.end(), uint8_t{9});
    draw::draw_gadget_text(&buffer.surface, &font.gaf, "AB", 1, 2, draw::gadget_text_unbounded, 0);
    require(at(buffer, 1, 2) == 5 && at(buffer, 3, 2) == 7, "gadget text: a label keeps the font");
    oa::present::set_game_text_hooks({});
}

} // namespace

int main() {
    namespace draw = oa::ui::frontend_renderer;
    {
        auto buffer = oa::present::create_surface(6, 6);
        draw::fill_box_raised(&buffer.surface, oa::Rect32{0, 0, 5, 5}, 1, 2, 3);
        const std::string expected = "111111\n"
                                     "111112\n"
                                     "113322\n"
                                     "113322\n"
                                     "112222\n"
                                     "122222\n";
        require(rows(buffer) == expected, "raised box: two-pixel bevel over the fill");
    }

    {
        auto buffer = oa::present::create_surface(6, 6);
        draw::fill_box_sunken(&buffer.surface, oa::Rect32{0, 0, 5, 5}, 1, 2, 3);
        require(
            at(buffer, 0, 0) == 2 && at(buffer, 5, 5) == 1 && at(buffer, 2, 2) == 3,
            "sunken box swaps the edge colours"
        );
    }
    {
        auto buffer = oa::present::create_surface(5, 5);
        draw::fill_frame_raised(&buffer.surface, oa::Rect32{0, 0, 4, 4}, 1, 2, 3);
        const std::string expected = "11111\n"
                                     "13332\n"
                                     "13332\n"
                                     "13332\n"
                                     "12222\n";
        require(rows(buffer) == expected, "raised frame: one-pixel edges");
        draw::fill_frame_sunken(&buffer.surface, oa::Rect32{0, 0, 4, 4}, 1, 2, 3);
        require(
            at(buffer, 0, 0) == 2 && at(buffer, 4, 4) == 1, "sunken frame swaps the edge colours"
        );
    }
    {
        auto buffer = oa::present::create_surface(4, 4);
        const oa::ui::gui_layout::GadgetRect rect{0, 0, 3, 3};
        draw::draw_value_marker(&buffer.surface, rect, 1, 7, 1);
        require(at(buffer, 3, 0) == 7 && at(buffer, 0, 3) == 0, "attribute 1 draws the top edge");
        draw::draw_value_marker(&buffer.surface, rect, 2, 5, 1);
        require(at(buffer, 0, 3) == 5, "attribute 2 draws the left edge");
        draw::draw_value_marker(&buffer.surface, rect, 4, 6, 0);
        require(at(buffer, 2, 2) == 0, "flag bit 0 clear draws nothing");
        draw::draw_value_marker(&buffer.surface, rect, 4, 6, 1);
        require(at(buffer, 2, 2) == 6, "attribute 4 draws the diagonal");
    }
    test_gadget_text();
    test_gadget_text_game_runs();
    test_gui_font();
    return failures == 0 ? 0 : 1;
}
