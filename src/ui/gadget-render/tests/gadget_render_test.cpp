// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Gadget draws on synthetic surfaces, art and display tables; every expected
// pixel is worked out by hand from the game's drawing rules.
#include "oa/ui/gadget_render.hpp"

#include "oa/ui/gui_input/gadget_panel.hpp"
#include "oa/ui/gui_layout/gui_gadget.hpp"
#include "oa/present/display.hpp"
#include "oa/present/raster.hpp"
#include "oa/present/surface.hpp"
#include "oa/present/game_text.hpp"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace render = oa::ui::gadget_render;
namespace layout = oa::ui::gui_layout;
namespace field = oa::ui::gui_layout::field;
namespace present = oa::present;
using oa::ui::gui_input::GadgetOwner;
using oa::ui::gui_input::GadgetPanel;

int failures = 0;

void require(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

// ---- Synthetic art ----

struct TestSequence {
    std::string name;
    std::vector<std::unique_ptr<present::SpriteBuffer>> frames;
};

struct TestFile {
    std::string path;
    std::vector<TestSequence> sequences;
};

struct TestArt {
    std::vector<TestFile*> files;
};

std::unique_ptr<present::SpriteBuffer> solid(uint16_t width, uint16_t height, uint8_t color) {
    auto sprite = std::make_unique<present::SpriteBuffer>(present::create_sprite(width, height));
    sprite->pixels.assign(sprite->pixels.size(), color);
    sprite->sprite.data = sprite->pixels.data();
    sprite->sprite.key = 0xFF;
    return sprite;
}

bool same_name(const std::string& a, const char* b) {
    if (a.size() != std::strlen(b))
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

render::GadgetArt make_art(TestArt& art) {
    render::GadgetArt result;
    result.context = &art;
    result.load_gaf = [](void* context, const char* path) -> const void* {
        for (TestFile* file : static_cast<TestArt*>(context)->files)
            if (file->path == path)
                return file;
        return nullptr;
    };
    result.find_sequence = [](void*, const void* file, const char* name) -> const void* {
        for (const auto& sequence : static_cast<const TestFile*>(file)->sequences)
            if (same_name(sequence.name, name))
                return &sequence;
        return nullptr;
    };
    result.first_sequence = [](void*, const void* file) -> const void* {
        const auto* gaf = static_cast<const TestFile*>(file);
        return gaf->sequences.empty() ? nullptr : &gaf->sequences.front();
    };
    result.frame = [](void*, const void* sequence, int32_t index) -> oa::Sprite* {
        const auto& frames = static_cast<const TestSequence*>(sequence)->frames;
        if (index < 0 || static_cast<size_t>(index) >= frames.size() ||
            !frames[static_cast<size_t>(index)])
            return nullptr;
        return &frames[static_cast<size_t>(index)]->sprite;
    };
    result.frame_count = [](void*, const void* sequence) {
        return static_cast<int32_t>(static_cast<const TestSequence*>(sequence)->frames.size());
    };
    return result;
}

// A GAF font: 'A' 2x3, 'B' 3x3, space 1x1 and the reference 'I' 1x4.
TestFile make_font(uint8_t a, uint8_t b) {
    TestFile font;
    font.sequences.push_back({"glyphs", {}});
    auto& glyphs = font.sequences.front().frames;
    glyphs.resize(128);
    glyphs['A'] = solid(2, 3, a);
    glyphs['B'] = solid(3, 3, b);
    glyphs[' '] = solid(1, 1, 0x3F);
    glyphs['I'] = solid(1, 4, 0x07);
    return font;
}

// ---- Display with synthetic tables: light row r maps i to i + r, shade
// row r maps i to i + 2r, gray maps i to i ^ 0x80. ----

struct TestDisplay {
    present::DisplayContext context;
    present::SurfaceBuffer screen;
    std::vector<uint8_t> light;
    std::vector<uint8_t> shade;
    std::vector<uint8_t> gray;
};

uint8_t lit(uint8_t pixel, int32_t row) {
    return static_cast<uint8_t>(pixel + row);
}

uint8_t shaded(uint8_t pixel, int32_t row) {
    return static_cast<uint8_t>(pixel + 2 * row);
}

uint8_t grayed(uint8_t pixel) {
    return static_cast<uint8_t>(pixel ^ 0x80);
}

// shade_rect_level on one pixel: shade rows for negative levels, signed index.
uint8_t level_of(uint8_t pixel, int32_t level) {
    const int32_t row = level < 0 ? level + 0x20 : level;
    const int32_t used = pixel >= 0x80 ? row - 1 : row;
    return level < 0 ? shaded(pixel, used) : lit(pixel, used);
}

void bind_display(TestDisplay& display, int32_t width, int32_t height, uint8_t fill) {
    display.screen = present::create_surface(width, height);
    display.screen.pixels.assign(display.screen.pixels.size(), fill);
    display.screen.surface.pixels = display.screen.pixels.data();
    display.light.resize(32 * 256);
    display.shade.resize(32 * 256);
    display.gray.resize(256);
    for (int32_t row = 0; row < 32; ++row)
        for (int32_t index = 0; index < 256; ++index) {
            display.light[static_cast<size_t>(row * 256 + index)] =
                lit(static_cast<uint8_t>(index), row);
            display.shade[static_cast<size_t>(row * 256 + index)] =
                shaded(static_cast<uint8_t>(index), row);
        }
    for (int32_t index = 0; index < 256; ++index)
        display.gray[static_cast<size_t>(index)] = grayed(static_cast<uint8_t>(index));
    display.context.back_buffer = display.screen.surface;
    display.context.width = width;
    display.context.height = height;
    display.context.light_table = display.light.data();
    display.context.shade_table = display.shade.data();
    display.context.gray_table = display.gray.data();
    display.context.flags = present::display_flag_light_table | present::display_flag_shade_table |
                            present::display_flag_gray_table;
    present::bind_display(&display.context);
}

// ---- Panels ----

constexpr uint8_t kColorBase = 0xA0; // colour map entry i is kColorBase + i

struct TestPanel {
    std::unique_ptr<GadgetPanel> panel = std::make_unique<GadgetPanel>();
    present::SurfaceBuffer face;
};

void make_panel(TestPanel& test, render::GadgetRenderer& renderer, int16_t width, int16_t height) {
    GadgetPanel& panel = *test.panel;
    oa::ui::gui_input::init_gadget_panel(panel);
    render::bind_renderer(panel, renderer);
    for (size_t i = 0; i < panel.colors.size(); ++i)
        panel.colors[i] = static_cast<uint8_t>(kColorBase + i);
    panel.owner = std::make_unique<GadgetOwner>();
    auto& root = panel.owner->table.records[0];
    layout::set_record_i16(root, field::width, width);
    layout::set_record_i16(root, field::height, height);
    layout::set_record_i16(root, field::record_count, 0);
    test.face = present::create_surface(width, height);
    root.refs.surface = &test.face.surface;
}

int32_t add_record(
    GadgetPanel& panel,
    uint8_t type,
    int16_t x,
    int16_t y,
    int16_t width,
    int16_t height,
    const char* name
) {
    const int32_t index = layout::add_gadget(panel.owner->records(), type);
    auto& record = panel.owner->table.records[static_cast<size_t>(index)];
    layout::set_record_i16(record, field::x, x);
    layout::set_record_i16(record, field::y, y);
    layout::set_record_i16(record, field::width, width);
    layout::set_record_i16(record, field::height, height);
    layout::set_record_string(record, field::name, name, field::name_bytes);
    return index;
}

layout::GadgetRecord& record(GadgetPanel& panel, int32_t index) {
    return panel.owner->table.records[static_cast<size_t>(index)];
}

uint8_t at(const present::SurfaceBuffer& buffer, int32_t x, int32_t y) {
    return buffer.surface.pixels[static_cast<size_t>(y * buffer.surface.pitch + x)];
}

uint8_t color(size_t slot) {
    return static_cast<uint8_t>(kColorBase + slot);
}

// ---- Tests ----

void test_text(render::GadgetRenderer& renderer, TestFile& font) {
    TestPanel test;
    make_panel(test, renderer, 12, 24);
    GadgetPanel& panel = *test.panel;
    panel.gaf_fonts[0] = &font;
    panel.active_gaf_font = &font;
    require(render::text_width(renderer, panel, "AB A") == 8, "text width sums glyph widths");
    require(render::text_height(renderer, panel) == 6, "text height is glyph I plus two");
    render::draw_text(renderer, panel, &test.face.surface, "AB A", 1, 1, -1, 0);
    require(at(test.face, 1, 1) == 5 && at(test.face, 2, 3) == 5, "first A drawn at the pen");
    require(at(test.face, 3, 2) == 6 && at(test.face, 5, 3) == 6, "B follows A");
    require(at(test.face, 6, 1) == 0, "space advances without drawing");
    require(at(test.face, 7, 1) == 5 && at(test.face, 9, 1) == 0, "second A after the space");

    TestPanel bounded;
    make_panel(bounded, renderer, 12, 6);
    bounded.panel->active_gaf_font = &font;
    render::draw_text(
        renderer,
        *bounded.panel,
        &bounded.face.surface,
        "\x01"
        "AB",
        0,
        0,
        4,
        0
    );
    require(at(bounded.face, 0, 0) == 5, "control byte skipped, A drawn");
    require(at(bounded.face, 2, 0) == 0, "B wider than the remaining width stops the run");

    char text[] = "AA AA A";
    const int32_t end =
        render::draw_wrapped_text(renderer, panel, &test.face.surface, text, 0, 0, 5, 100, 0);
    require(end == 24, "three wrapped lines advance by text height plus two");
    require(
        at(test.face, 0, 8) == 5 && at(test.face, 0, 16) == 5, "wrapped lines start at the left"
    );
    require(std::strcmp(text, "AA AA A") == 0, "wrapping restores the text");

    TestPanel short_panel;
    make_panel(short_panel, renderer, 12, 24);
    short_panel.panel->active_gaf_font = &font;
    char again[] = "AA AA A";
    const int32_t cut = render::draw_wrapped_text(
        renderer, *short_panel.panel, &short_panel.face.surface, again, 0, 0, 5, 10, 0
    );
    require(cut == 16 && at(short_panel.face, 0, 16) == 0, "the height budget ends the wrap");

    require(
        panel.host.text_width(panel.host.context, &font, "AB") == 5,
        "host text width uses the given font"
    );
    require(
        panel.host.line_height(panel.host.context, &font) == 6,
        "host line height uses the given font"
    );
}

// Each run the system-text hook drew, and the width it answers.
// What the game-text hooks answer: the settings and the palette.
struct GameTextAnswers {
    present::TextSettings settings{};
    std::vector<uint8_t> palette{};
};

void test_text_game_runs(render::GadgetRenderer& renderer, TestFile& font) {
    TestPanel test;
    make_panel(test, renderer, 12, 24);
    GadgetPanel& panel = *test.panel;
    panel.active_gaf_font = &font;
    GameTextAnswers answers;
    answers.settings.style = {false, false, false, false, present::default_text_size};
    answers.settings.utf8 = true;
    // Entry 4 is hattfont12's colour, which the modern runs are drawn in.
    answers.palette.assign(8 * 4, 0);
    answers.palette[4 * 4] = 195;
    answers.palette[4 * 4 + 1] = 195;
    answers.palette[4 * 4 + 2] = 155;
    present::GameTextHooks hooks{};
    hooks.context = &answers;
    hooks.settings = [](void* context) { return static_cast<GameTextAnswers*>(context)->settings; };
    // Each character a solid block 3 pixels wide, 2 rows above the baseline.
    hooks.draw = [](
                     void*, std::string_view text, present::TextFace, int32_t, int32_t
                 ) -> std::shared_ptr<const present::TextMask> {
        auto mask = std::make_shared<present::TextMask>();
        int32_t pen = 0;
        for (std::size_t at = 0; at < text.size();) {
            const auto sequence = present::utf8_sequence(text.substr(at));
            at += sequence.bytes != 0 ? sequence.bytes : 1;
            pen += 3;
            mask->character_ends.push_back(pen);
        }
        mask->width = mask->advance = pen;
        mask->height = mask->baseline = 2;
        mask->alpha.assign(static_cast<std::size_t>(pen) * 2U, 255);
        return mask;
    };
    hooks.palette = [](void* context) -> std::span<const uint8_t> {
        return static_cast<GameTextAnswers*>(context)->palette;
    };
    present::set_game_text_hooks(hooks);
    const std::string sun = "\xE6\x97\xA5";
    const std::string text = "A" + sun + "B";
    require(
        render::text_width(renderer, panel, text.c_str()) == 8,
        "a modern run is as wide as the modern fonts draw it"
    );
    render::draw_text(renderer, panel, &test.face.surface, text.c_str(), 1, 1, -1, 0);
    // The baseline is the 'I' glyph's height below the pen.
    require(
        at(test.face, 3, 3) == 4 && at(test.face, 5, 4) == 4 && at(test.face, 3, 2) == 0,
        "the run stands on the font's baseline after A"
    );
    require(at(test.face, 7, 2) == 6, "B follows the run");
    // A width budget stops before a run wider than what is left.
    TestPanel bounded;
    make_panel(bounded, renderer, 12, 6);
    bounded.panel->active_gaf_font = &font;
    render::draw_text(renderer, *bounded.panel, &bounded.face.surface, text.c_str(), 0, 0, 4, 0);
    require(at(bounded.face, 2, 2) == 0, "the budget stops at the run");
    // Interface text keeps the game's font even while the settings draw
    // game text in the modern fonts.
    answers.settings.style.modern_fonts = true;
    require(render::text_width(renderer, panel, "AB") == 5, "a label keeps the font");
    // Without hooks the bytes are the font's, which has no glyphs for them.
    present::set_game_text_hooks({});
    require(
        render::text_width(renderer, panel, text.c_str()) == 5, "without hooks the run is glyphless"
    );
}

void test_shade_level(TestDisplay&) {
    auto buffer = present::create_surface(4, 1);
    buffer.pixels = {0x10, 0x90, 0x7F, 0xFF};
    buffer.surface.pixels = buffer.pixels.data();
    oa::Rect32 rect{0, 0, 3, 0};
    require(present::shade_rect_level(&buffer.surface, &rect, 3) == 1, "light pass succeeds");
    require(
        buffer.pixels[0] == 0x13 && buffer.pixels[2] == 0x82, "low pixels read their light row"
    );
    require(
        buffer.pixels[1] == 0x92 && buffer.pixels[3] == 0x01, "high pixels read the previous row"
    );
    buffer.pixels = {0x10, 0x90, 0, 0};
    rect = {0, 0, 1, 0};
    present::shade_rect_level(&buffer.surface, &rect, -0x14);
    require(
        buffer.pixels[0] == 0x28 && buffer.pixels[1] == 0xA6, "shade row 12, high pixels row 11"
    );
    buffer.pixels = {0x10, 0x90, 0, 0};
    present::shade_rect_level(&buffer.surface, &rect, -100);
    require(buffer.pixels[0] == 0x10 && buffer.pixels[1] == 0x90, "levels clamp to shade row 0");
    rect = {-5, 0, 10, 0};
    buffer.pixels = {0, 0, 0, 0};
    present::shade_rect_level(&buffer.surface, &rect, 100);
    require(
        rect.x1 == 0 && rect.x2 == 3 && buffer.pixels[3] == 0x1F,
        "rectangle clipped in place, level 0x1F"
    );
}

void test_button(render::GadgetRenderer& renderer, TestFile& font) {
    TestPanel test;
    make_panel(test, renderer, 14, 10);
    GadgetPanel& panel = *test.panel;
    panel.gaf_fonts[0] = &font;
    panel.active_gaf_font = &font;
    const int32_t index = add_record(panel, layout::gadget_type::button, 2, 1, 12, 8, "BTN");
    layout::set_record_u32(record(panel, index), field::attributes, layout::attribute::centered);
    layout::copy_record_cstring(record(panel, index), field::text, "B");
    render::draw_button(renderer, panel, index);
    require(
        at(test.face, 2, 1) == color(render::color_slot::light_edge),
        "released: light top-left edge"
    );
    require(
        at(test.face, 13, 8) == color(render::color_slot::dark_edge),
        "released: dark bottom-right edge"
    );
    require(at(test.face, 10, 3) == color(render::color_slot::face), "released: face fill");
    require(
        at(test.face, 7, 1) == 6 && at(test.face, 9, 3) == 6,
        "caption centred from the fitted width"
    );
    require(panel.owner->redraw == 1, "a button draw asks for a blit");

    layout::set_record_i16(record(panel, index), field::button_status, 1);
    render::draw_button(renderer, panel, index);
    require(
        at(test.face, 2, 1) == color(render::color_slot::dark_edge), "pressed: dark top-left edge"
    );
    require(
        at(test.face, 13, 8) == color(render::color_slot::light_edge),
        "pressed: light bottom-right edge"
    );
    require(
        at(test.face, 8, 2) == 6 && at(test.face, 7, 1) == color(render::color_slot::dark_edge),
        "pressed caption moves one pixel right and down"
    );

    layout::set_record_i16(record(panel, index), field::button_status, 0);
    record(panel, index).bytes[field::button_flags] = 1;
    render::draw_button(renderer, panel, index);
    require(at(test.face, 10, 5) == color(render::color_slot::grayed_face), "grayed: gray fill");
    require(
        at(test.face, 13, 8) == color(render::color_slot::grayed_face),
        "grayed: gray bottom-right edge"
    );

    record(panel, index).bytes[field::button_flags] = 0;
    layout::copy_record_cstring(record(panel, index), field::text, "AB");
    record(panel, index).bytes[field::button_quick_key] = 'B';
    render::draw_button(renderer, panel, index);
    require(
        at(test.face, 6, 1) == 5 && at(test.face, 8, 1) == 6,
        "quick key splits the caption in place"
    );
    require(
        at(test.face, 8, 6) == color(render::color_slot::underline) &&
            at(test.face, 10, 6) == color(render::color_slot::underline),
        "quick key underlined one row above the line height"
    );
    require(
        at(test.face, 7, 6) == color(render::color_slot::face), "underline covers the key only"
    );

    layout::copy_record_cstring(record(panel, index), field::text, "ABAB");
    render::draw_button(renderer, panel, index);
    require(
        layout::record_string(record(panel, index), field::text) == "AB",
        "captions are cut to the width - 6"
    );
}

void test_button_art(render::GadgetRenderer& renderer, TestFile& font) {
    TestPanel test;
    make_panel(test, renderer, 14, 10);
    GadgetPanel& panel = *test.panel;
    panel.gaf_fonts[0] = &font;
    panel.active_gaf_font = &font;
    TestSequence art{"BTN", {}};
    for (uint8_t frame = 0; frame < 6; ++frame)
        art.frames.push_back(solid(10, 8, static_cast<uint8_t>(0x10 + frame)));
    const int32_t index = add_record(panel, layout::gadget_type::button, 2, 1, 10, 8, "BTN");
    record(panel, index).refs.sprite = &art;
    render::draw_button(renderer, panel, index);
    require(at(test.face, 2, 1) == 0x10 && at(test.face, 11, 8) == 0x10, "released: base frame");
    layout::set_record_i16(record(panel, index), field::button_status, 1);
    render::draw_button(renderer, panel, index);
    require(at(test.face, 5, 5) == 0x11, "pressed: base frame plus status");
    layout::set_record_i16(record(panel, index), field::button_status, 0);
    record(panel, index).bytes[field::button_flags] = 1;
    render::draw_button(renderer, panel, index);
    require(
        at(test.face, 5, 5) == level_of(grayed(0x12), -0x14), "grayed: frame two, grayed and shaded"
    );
    require(at(test.face, 0, 0) == 0, "shade stays inside the button");
    layout::set_record_u32(record(panel, index), field::attributes, 0x80);
    render::draw_button(renderer, panel, index);
    require(at(test.face, 5, 5) == 0x12, "checkbox art: grayed frame left unshaded");
}

void test_label(render::GadgetRenderer& renderer, TestFile& font, TestFile& small) {
    TestPanel test;
    make_panel(test, renderer, 20, 10);
    GadgetPanel& panel = *test.panel;
    panel.gaf_fonts[0] = &font;
    panel.gaf_fonts[1] = &small;
    panel.active_gaf_font = &font;
    const int32_t index = add_record(panel, layout::gadget_type::label, 1, 1, 12, 6, "LBL");
    layout::set_record_u32(record(panel, index), field::attributes, layout::attribute::centered);
    layout::copy_record_cstring(record(panel, index), field::text, "AB");
    record(panel, index).bytes[field::label_quick_key] = 'B';
    render::draw_label(renderer, panel, index);
    require(
        at(test.face, 5, 1) == 0x21 && at(test.face, 7, 1) == 0x22,
        "labels draw in font slot 1, centred"
    );
    require(
        at(test.face, 3, 6) == color(render::color_slot::underline) &&
            at(test.face, 5, 6) == color(render::color_slot::underline) && at(test.face, 6, 6) == 0,
        "quick-key underline measured from the rectangle's left edge"
    );
    require(panel.active_gaf_font == &font, "font slot 0 is active again");

    layout::set_record_i16(record(panel, index), field::x, -1);
    render::draw_label(renderer, panel, index);
    require(
        layout::record_i16(record(panel, index), field::x) == 7,
        "x = -1 centres the label on the root"
    );

    layout::set_record_u32(record(panel, index), field::color_background, 3);
    record(panel, index).bytes[field::label_flags] = 1;
    render::draw_label(renderer, panel, index);
    require(
        at(test.face, 18, 6) == level_of(grayed(color(3)), -0x14),
        "flagged label is grayed and shaded"
    );
}

void test_text_list(render::GadgetRenderer& renderer, TestFile& font) {
    TestPanel test;
    make_panel(test, renderer, 16, 20);
    GadgetPanel& panel = *test.panel;
    panel.gaf_fonts[0] = &font;
    panel.active_gaf_font = &font;
    static const char lines[] = "&GHd\0AB\0A";
    const int32_t index = add_record(panel, layout::gadget_type::list_box, 0, 0, 16, 20, "LIST");
    auto& list = record(panel, index);
    layout::set_record_u32(
        list, field::attributes, layout::attribute::text_list | layout::attribute::horizontal
    );
    layout::set_record_i16(list, field::list_count, 3);
    layout::set_record_i16(list, field::list_selected, 1);
    list.refs.lines = lines;
    list.refs.lines_size = sizeof(lines);
    render::draw_list(renderer, panel, index);
    uint8_t header = 0;
    for (int32_t level = -0x13; level >= -0x16; --level)
        header = level_of(header, level);
    require(at(test.face, 10, 4) == header, "header row shaded four times");
    require(at(test.face, 2, 9) == level_of(5, 0x1E), "selected row text lit by 0x1E");
    require(at(test.face, 10, 9) == level_of(header, 0x1E), "rows overlap by one line");
    require(at(test.face, 10, 12) == level_of(0, 0x1E), "selected row background lit");
    require(at(test.face, 2, 16) == 5 && at(test.face, 2, 19) == 0, "third row drawn plain");
    require(at(test.face, 1, 4) == 0, "rows start two pixels in");
}

void test_scroll_bar(render::GadgetRenderer& renderer, TestFile& font) {
    TestPanel test;
    make_panel(test, renderer, 20, 16);
    GadgetPanel& panel = *test.panel;
    panel.active_gaf_font = &font;

    struct Caption {
        std::string text;
        int32_t x = 0;
        int32_t y = 0;
        int32_t color = 0;
    } caption;

    renderer.text_context = &caption;
    renderer.fnt_text = [](void* context,
                           oa::Surface*,
                           const char* text,
                           int32_t x,
                           int32_t y,
                           int32_t,
                           int32_t text_color) {
        auto& seen = *static_cast<Caption*>(context);
        seen = {text, x, y, text_color};
    };
    const int32_t index = add_record(panel, layout::gadget_type::scroll_bar, 1, 1, 6, 12, "BAR");
    auto& bar = record(panel, index);
    layout::set_record_i16(bar, field::scroll_knob, 2);
    layout::set_record_i16(bar, field::scroll_knob_size, 3);
    layout::set_record_u32(bar, field::attributes, layout::attribute::right_aligned);
    render::draw_scroll_bar(renderer, panel, index);
    require(
        at(test.face, 1, 1) == color(render::color_slot::dark_edge), "track frame: dark top-left"
    );
    require(
        at(test.face, 7, 13) == color(render::color_slot::light_edge), "track frame spans width + 1"
    );
    require(
        at(test.face, 2, 5) == color(render::color_slot::light_edge) &&
            at(test.face, 6, 8) == color(render::color_slot::dark_edge),
        "knob frame sunken at its position"
    );
    require(at(test.face, 4, 6) == color(render::color_slot::face), "knob filled");
    require(
        caption.text == "2" && caption.x == 9 && caption.y == 5 &&
            caption.color == color(render::color_slot::scroll_value),
        "value caption right of the bar"
    );
    layout::set_record_i32(bar, field::scroll_thickness, 100);
    render::draw_scroll_bar(renderer, panel, index);
    require(caption.text == "66", "scaled value truncates knob * scale / (width - knob)");
    layout::set_record_i32(bar, field::scroll_locked, 1);
    render::draw_scroll_bar(renderer, panel, index);
    require(
        at(test.face, 4, 6) == level_of(grayed(color(render::color_slot::face)), -0x14),
        "locked bar shaded"
    );
    renderer.fnt_text = nullptr;
    renderer.text_context = nullptr;
}

void test_slider_art(render::GadgetRenderer& renderer) {
    TestPanel test;
    make_panel(test, renderer, 10, 24);
    GadgetPanel& panel = *test.panel;
    TestSequence art{"SLIDERS", {}};
    art.frames.push_back(solid(4, 2, 0x30)); // start
    art.frames.push_back(solid(4, 3, 0x31)); // track
    art.frames.push_back(solid(4, 2, 0x32)); // end
    art.frames.push_back(solid(2, 1, 0x33)); // knob start
    art.frames.push_back(solid(2, 2, 0x34)); // knob middle
    art.frames.push_back(solid(2, 1, 0x35)); // knob end
    const int32_t index = add_record(panel, layout::gadget_type::scroll_bar, 1, 0, 4, 20, "BAR");
    auto& bar = record(panel, index);
    bar.refs.scroll_ticks = &art;
    layout::set_record_i16(bar, field::scroll_knob, 2);
    layout::set_record_i16(bar, field::scroll_knob_size, 6);
    render::draw_scroll_bar(renderer, panel, index);
    require(
        at(test.face, 1, 0) == 0x30 && at(test.face, 1, 2) == 0x31, "start cap then track tiles"
    );
    require(
        at(test.face, 1, 16) == 0x31 && at(test.face, 1, 18) == 0x32,
        "track stops where the end cap fits"
    );
    require(
        at(test.face, 2, 5) == 0x33 && at(test.face, 1, 5) == 0x31,
        "knob centred on the track at knob + 3"
    );
    require(at(test.face, 2, 6) == 0x34 && at(test.face, 2, 9) == 0x34, "knob middle tiles");
    require(
        at(test.face, 2, 10) == 0x35 && at(test.face, 2, 11) == 0x31, "knob end at the knob bottom"
    );
}

void test_progress(render::GadgetRenderer& renderer, TestDisplay& display) {
    TestPanel test;
    make_panel(test, renderer, 20, 10);
    GadgetPanel& panel = *test.panel;
    const int32_t index = add_record(panel, layout::gadget_type::progress, 0, 0, 14, 6, "BAR");
    auto& bar = record(panel, index);
    layout::set_record_i32(bar, field::progress_value, 5);
    layout::set_record_i32(bar, field::progress_scale, 10);
    layout::set_record_u32(bar, field::color_foreground, 0x33);
    layout::set_record_u32(bar, field::color_background, 0x44);
    std::fill(display.screen.pixels.begin(), display.screen.pixels.end(), uint8_t{0});
    render::draw_progress(renderer, panel, index);
    require(
        test.face.surface.pixels == display.screen.pixels.data(),
        "the face descriptor is replaced by the display target"
    );
    require(
        at(display.screen, 0, 0) == color(render::color_slot::dark_edge), "bar frame on the screen"
    );
    require(
        at(display.screen, 2, 2) == 0x33 && at(display.screen, 7, 4) == 0x33,
        "filled part: 5/10 of 10"
    );
    require(at(display.screen, 8, 3) == 0x44 && at(display.screen, 12, 4) == 0x44, "empty part");
    require(test.face.pixels[0] == 0, "the face pixels are left alone");

    // Without a face the draw falls back to the context backdrop, still lands
    // on the display target and keeps the backdrop's descriptor.
    record(panel, 0).refs.surface = nullptr;
    const present::SurfaceBuffer backdrop = present::create_surface(20, 10);
    panel.backdrop = &backdrop.surface;
    std::fill(display.screen.pixels.begin(), display.screen.pixels.end(), uint8_t{0});
    render::draw_progress(renderer, panel, index);
    require(
        at(display.screen, 0, 0) == color(render::color_slot::dark_edge) &&
            at(display.screen, 2, 2) == 0x33 && at(display.screen, 8, 3) == 0x44,
        "the backdrop fallback draws the bar on the screen"
    );
    require(
        backdrop.surface.pixels == backdrop.pixels.data() && backdrop.pixels[0] == 0,
        "the backdrop descriptor and pixels are left alone"
    );
    panel.backdrop = nullptr;
}

void test_text_box(render::GadgetRenderer& renderer, TestFile& font) {
    TestPanel test;
    make_panel(test, renderer, 12, 8);
    GadgetPanel& panel = *test.panel;
    panel.active_gaf_font = &font;
    panel.gaf_fonts[0] = &font;
    const int32_t index = add_record(panel, layout::gadget_type::text_box, 0, 0, 12, 8, "EDIT");
    layout::set_record_u32(record(panel, index), field::attributes, layout::attribute::horizontal);
    layout::copy_record_cstring(record(panel, index), field::text, "AB");
    panel.captured = index;
    panel.caret = 1;
    render::draw_text_box(renderer, panel, index);
    require(at(test.face, 0, 0) == color(render::color_slot::dark_edge), "flat box filled dark");
    require(at(test.face, 1, 4) == 5 && at(test.face, 3, 4) == 6, "text three pixels down");
    require(
        at(test.face, 2, 4) == color(render::color_slot::caret) &&
            at(test.face, 2, 7) == color(render::color_slot::caret),
        "caret after the first character"
    );
}

void test_image(render::GadgetRenderer& renderer) {
    TestPanel test;
    make_panel(test, renderer, 6, 6);
    GadgetPanel& panel = *test.panel;
    // Row-RLE frame: per row a u16 length, then a literal run of two bytes.
    std::vector<uint8_t> stream = {3, 0, 4, 0x10, 0x10, 3, 0, 4, 0x10, 0x10};
    oa::Sprite frame{};
    frame.width = 2;
    frame.height = 2;
    frame.encoding = OA_SPRITE_ROW_RLE;
    frame.data = stream.data();
    const int32_t index = add_record(panel, layout::gadget_type::image, 1, 1, 2, 2, "IMG");
    record(panel, index).refs.image = &frame;
    render::draw_image(renderer, panel, index);
    require(
        at(test.face, 1, 1) == 0x10 && at(test.face, 2, 2) == 0x10 && at(test.face, 3, 3) == 0,
        "image frame at the record"
    );
    layout::set_record_i32(record(panel, index), field::flash_level, 3);
    render::draw_image(renderer, panel, index);
    require(at(test.face, 1, 1) == lit(0x10, 3), "flashing image lit by its level");
    layout::set_record_i32(record(panel, index), field::flash_level, 0);
    record(panel, index).bytes[field::image_flags] = 1;
    render::draw_image(renderer, panel, index);
    require(at(test.face, 2, 2) == shaded(0x10, 4), "flagged image shaded by -0x1C");

    const int32_t hot = add_record(panel, layout::gadget_type::hot_surface, 3, 3, 2, 2, "HOT");
    render::draw_hot_image(renderer, panel, hot);
    require(
        at(test.face, 4, 4) == color(render::color_slot::empty_image),
        "hot surface without art is filled"
    );
}

void test_skin(render::GadgetRenderer& renderer) {
    TestPanel test;
    make_panel(test, renderer, 10, 7);
    GadgetPanel& panel = *test.panel;
    TestSequence skin{"Skin", {}};
    for (uint8_t frame = 0; frame < 9; ++frame)
        skin.frames.push_back(solid(3, 2, static_cast<uint8_t>(0x50 + frame)));
    render::draw_skin(renderer, panel, 0, &skin);
    require(
        at(test.face, 0, 0) == 0x50 && at(test.face, 4, 0) == 0x51 && at(test.face, 6, 0) == 0x51,
        "top row: corner then edges"
    );
    require(
        at(test.face, 8, 0) == 0x52 && at(test.face, 9, 1) == 0x52,
        "last tile pulled back to the edge"
    );
    require(
        at(test.face, 0, 2) == 0x53 && at(test.face, 0, 4) == 0x53 && at(test.face, 9, 3) == 0x55,
        "middle rows"
    );
    require(
        at(test.face, 0, 5) == 0x56 && at(test.face, 5, 6) == 0x57 && at(test.face, 9, 6) == 0x58,
        "bottom row pulled up to the edge"
    );
}

void test_panel(
    render::GadgetRenderer& renderer, TestDisplay& display, TestArt& art, TestFile& font
) {
    std::fill(display.screen.pixels.begin(), display.screen.pixels.end(), uint8_t{0x77});
    TestFile common;
    common.path = "COMMON";
    common.sequences.push_back({"BackTile", {}});
    for (uint8_t frame = 0; frame < 9; ++frame) {
        auto tile = solid(3, 2, static_cast<uint8_t>(0x50 + frame));
        tile->sprite.origin_x = 1;
        tile->sprite.origin_y = 1;
        common.sequences.back().frames.push_back(std::move(tile));
    }
    common.sequences.back().frames[4]->pixels[1] = 0xFF; // one transparent pixel in the centre tile
    common.sequences.push_back({"BUTTONS0", {}});
    for (uint8_t frame = 0; frame < 8; ++frame)
        common.sequences.back().frames.push_back(
            frame < 4 ? solid(6, 4, static_cast<uint8_t>(0x60 + frame))
                      : solid(5, 3, static_cast<uint8_t>(0x60 + frame))
        );
    art.files.push_back(&common);

    TestPanel test;
    make_panel(test, renderer, 10, 7);
    GadgetPanel& panel = *test.panel;
    panel.owner->table.records[0].refs.surface = nullptr;
    panel.list_skin = &common;
    panel.gaf_fonts[0] = &font;
    panel.active_gaf_font = &font;
    const int32_t button = add_record(panel, layout::gadget_type::button, 1, 1, 5, 3, "OK");
    record(panel, button).bytes[field::active] = 1;

    const uint32_t flags =
        oa::ui::gui_input::panel_flag::first_draw | oa::ui::gui_input::panel_flag::centre;
    require(render::draw_panel(renderer, panel, flags) == 1, "first draw succeeds");
    const auto& root = panel.owner->table.records[0];
    require(
        layout::record_i16(root, field::x) == 15 && layout::record_i16(root, field::y) == 11,
        "centred on the 40x30 display"
    );
    require(
        common.sequences[0].frames[0]->sprite.origin_x == 0, "bound skin frames lose their origin"
    );
    require(
        record(panel, button).bytes[field::button_frame_base] == 4, "closest BUTTONS0 size group"
    );
    require(
        layout::record_i16(record(panel, button), field::width) == 5,
        "button takes its frame's size"
    );
    oa::Surface* face = render::panel_face(panel);
    require(face != nullptr && face->width == 10 && face->height == 7, "face sized to the root");
    const auto face_at = [&](int32_t x, int32_t y) { return face->pixels[y * face->pitch + x]; };
    require(face_at(0, 0) == 0x50 && face_at(9, 6) == 0x58, "face skinned with BackTile");
    require(face_at(4, 4) == 0x77, "a transparent skin pixel shows the captured screen");
    require(face_at(7, 2) == 0x55, "the pulled-back right tile covers the tile before it");
    require(face_at(1, 1) == 0x64 && face_at(5, 3) == 0x64, "button drawn from its frame");

    render::blit_panel_stack(panel, &display.screen.surface, nullptr);
    require(
        at(display.screen, 15, 11) == 0x50 && at(display.screen, 16, 12) == 0x64,
        "face blitted at the root"
    );
    require(at(display.screen, 14, 11) == 0x77, "screen outside the panel kept");
    require(panel.owner->redraw == 0, "blit clears the redraw request");
    display.screen.pixels[static_cast<size_t>(11 * 40 + 15)] = 0x01;
    render::blit_panel_stack(panel, &display.screen.surface, nullptr);
    require(at(display.screen, 15, 11) == 0x01, "a clean panel is not blitted");
    const oa::Rect32 region{0, 0, 15, 11};
    render::blit_panel_stack(panel, &display.screen.surface, &region);
    require(at(display.screen, 15, 11) == 0x50, "an overlapping region forces the blit");

    render::draw_panel(renderer, panel, render::draw_flag::release);
    require(
        at(display.screen, 15, 11) == 0x77 && at(display.screen, 16, 12) == 0x77,
        "release restores the screen under the panel"
    );
    require(render::panel_face(panel) == nullptr, "release frees the face");
    art.files.pop_back();
}

void test_focus_and_fonts(render::GadgetRenderer& renderer, TestArt& art) {
    TestPanel test;
    make_panel(test, renderer, 10, 10);
    GadgetPanel& panel = *test.panel;
    const int32_t index = add_record(panel, layout::gadget_type::button, 3, 3, 3, 3, "B");
    render::draw_request(renderer, panel, oa::ui::gui_input::GadgetDraw::focus_outline, index);
    require(
        at(test.face, 4, 2) == lit(0, 0x1F) && at(test.face, 4, 1) == lit(0, 0x1C),
        "focus rings lit outward"
    );
    require(at(test.face, 2, 2) == lit(lit(0, 0x1F), 0x1F), "ring corners are lit by both edges");

    TestFile hatt = make_font(5, 6);
    hatt.path = "hattfont12.GAF";
    art.files.push_back(&hatt);
    render::load_gui_font(renderer, panel, "hattfont12", 0);
    require(
        panel.gaf_fonts[0] == &hatt && panel.active_gaf_font == &hatt,
        "font loaded into slot 0 and active"
    );
    require(
        hatt.sequences[0].frames['A']->sprite.origin_y == -4,
        "glyph origins lifted by the height of I"
    );
    art.files.pop_back();

    TestFile common;
    common.path = "commongui.GAF";
    art.files.push_back(&common);
    render::load_common_gaf(renderer, panel, "commongui");
    require(panel.list_skin == &common, "the common GUI GAF is loaded from the GAF directory");
    render::load_common_gaf(renderer, panel, "missing");
    require(panel.list_skin == &common, "a missing file keeps the loaded one");
    art.files.pop_back();
}

} // namespace

int main() {
    TestDisplay display;
    bind_display(display, 40, 30, 0);
    TestArt art;
    render::GadgetRenderer renderer;
    renderer.art = make_art(art);
    TestFile font = make_font(5, 6);
    TestFile small = make_font(0x21, 0x22);

    test_text(renderer, font);
    test_text_game_runs(renderer, font);
    test_shade_level(display);
    test_button(renderer, font);
    test_button_art(renderer, font);
    test_label(renderer, font, small);
    test_text_list(renderer, font);
    test_scroll_bar(renderer, font);
    test_slider_art(renderer);
    test_progress(renderer, display);
    test_text_box(renderer, font);
    test_image(renderer);
    test_skin(renderer);
    test_panel(renderer, display, art, font);
    test_focus_and_fonts(renderer, art);
    present::bind_display(nullptr);
    if (failures == 0)
        std::cout << "gadget render: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
