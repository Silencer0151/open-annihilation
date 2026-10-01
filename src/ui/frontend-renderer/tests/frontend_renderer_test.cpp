// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend_renderer.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

namespace {

int failures = 0;

// Whether render times compare the drawing code. A build with the address
// sanitizer checks every memory access, which slows the grayed buttons'
// shade-table reads far more than the normal buttons' copies, so its times
// are not compared.
#if defined(__SANITIZE_ADDRESS__)
constexpr bool render_times_compared = false;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr bool render_times_compared = false;
#else
constexpr bool render_times_compared = true;
#endif
#else
constexpr bool render_times_compared = true;
#endif

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

oa::ui::gui_layout::Gadget
button(std::string name, int16_t x, int16_t y, int16_t width, int16_t height) {
    oa::ui::gui_layout::Gadget result;
    result.common.type = oa::ui::gui_layout::GadgetType::button;
    result.common.name = std::move(name);
    result.common.x = x;
    result.common.y = y;
    result.common.width = width;
    result.common.height = height;
    result.common.active = 1;
    result.fields = oa::ui::gui_layout::ButtonFields{};
    return result;
}

uint8_t red_at(const oa::ui::frontend_renderer::Surface& surface, std::size_t x, std::size_t y) {
    return surface.rgb[(y * surface.width + x) * 3];
}

// A grayed-out art button shows the frame two past its normal one, or the
// last frame of a shorter sequence, darkened through shade table row 12.
void test_grayed_art_frame() {
    oa::ui::frontend_renderer::ScreenResources resources;
    resources.background.width = 4;
    resources.background.height = 4;
    resources.background.rgb.assign(4U * 4U * 3U, 99);
    for (std::size_t index = 0; index < 256; ++index)
        resources.gui_palette[index * 4] = static_cast<uint8_t>(index);
    // The screen's bitmap carries the palette the shade table indexes.
    resources.background.palette = resources.gui_palette;
    resources.layout.gadgets.push_back(button("ART", 1, 1, 1, 1));
    oa::formats::gaf::Sequence sequence;
    sequence.name = "ART";
    for (uint8_t value : {10, 11, 12, 13}) {
        oa::formats::gaf::Frame frame;
        frame.width = 1;
        frame.height = 1;
        frame.pixels = {value};
        frame.coverage = {1};
        sequence.frames.push_back(frame);
    }
    resources.sprites.sequences.push_back(sequence);
    resources.shade_table.resize(32U * 256U);
    for (std::size_t row = 0; row < 32; ++row)
        for (std::size_t index = 0; index < 256; ++index)
            resources.shade_table[row * 256 + index] = static_cast<uint8_t>(index);
    resources.shade_table[12U * 256U + 12U] = 50;
    const oa::ui::frontend_renderer::ButtonPresentation grayed{
        "ART",
        oa::ui::frontend_renderer::ButtonCondition::disabled,
        std::nullopt,
        std::nullopt,
        std::nullopt
    };
    CHECK(red_at(oa::ui::frontend_renderer::render_screen(resources, {&grayed, 1}), 1, 1) == 50);
    resources.sprites.sequences.back().frames.resize(2);
    CHECK(red_at(oa::ui::frontend_renderer::render_screen(resources, {&grayed, 1}), 1, 1) == 11);
    // Drawn in the GUI palette alone, the grayed frame is darkened in the game
    // palette, and left undarkened without one.
    resources.sprites.sequences.back().frames.push_back(
        resources.sprites.sequences.back().frames[0]
    );
    resources.sprites.sequences.back().frames.back().pixels = {12};
    resources.background.palette.reset();
    resources.game_palette = resources.gui_palette;
    CHECK(red_at(oa::ui::frontend_renderer::render_screen(resources, {&grayed, 1}), 1, 1) == 50);
    resources.game_palette.reset();
    CHECK(red_at(oa::ui::frontend_renderer::render_screen(resources, {&grayed, 1}), 1, 1) == 12);
}

// A centred caption underlines its quick key's glyph on the row below the
// text, in GUI palette entry 2, or 0 while pressed, and not while grayed;
// the focused record gets the focus marker's six rings, lit through the
// light table, corners twice, inside the root.
void test_quick_key_and_focus() {
    namespace renderer = oa::ui::frontend_renderer;
    constexpr uint8_t ground = 99;
    constexpr uint8_t glyph_colour = 7;
    constexpr uint8_t underline_entry = 2;
    constexpr uint8_t button_fill = 20;
    constexpr uint8_t grayed_fill = 19;
    renderer::ScreenResources resources;
    resources.background.width = 60;
    resources.background.height = 30;
    resources.background.rgb.assign(60U * 30U * 3U, 0);
    for (std::size_t pixel = 0; pixel < 60U * 30U; ++pixel)
        resources.background.rgb[pixel * 3] = ground;
    for (std::size_t index = 0; index < 256; ++index)
        resources.gui_palette[index * 4] = static_cast<uint8_t>(index);
    resources.background.palette = resources.gui_palette;
    // Every light level adds itself to the entry, so a pixel lit twice shows it.
    resources.light_table.resize(32U * 256U);
    for (std::size_t level = 0; level < 32; ++level)
        for (std::size_t index = 0; index < 256; ++index)
            resources.light_table[level * 256 + index] = static_cast<uint8_t>(index + level);
    // Glyphs 3 pixels wide and 4 high: the text is 'I' + 2 = 6 high.
    oa::formats::fnt::Glyph glyph;
    glyph.width = 3;
    glyph.height = 4;
    glyph.pixels.assign(12, glyph_colour);
    glyph.coverage.assign(12, 1);
    for (const char character : {'I', 'Y', 'e', 's'})
        resources.font.glyphs[static_cast<unsigned char>(character)] = glyph;
    oa::ui::gui_layout::Gadget root;
    root.common.type = oa::ui::gui_layout::GadgetType::panel;
    root.common.name = "ROOT";
    root.common.y = 2;
    root.common.width = 60;
    root.common.height = 26;
    root.common.active = 1;
    auto yes = button("YES", 10, 5, 20, 10);
    yes.common.attributes = 2; // centred caption
    std::get<oa::ui::gui_layout::ButtonFields>(yes.fields).text = "Yes";
    resources.layout.gadgets = {root, yes};
    renderer::ButtonPresentation state{
        "YES", renderer::ButtonCondition::normal, std::nullopt, std::nullopt, std::nullopt
    };
    state.quick_key = 'Y';
    // "Yes" is 9 wide: x = 10 + (20 - 1 - 9) / 2 + 1 = 16,
    // y = 5 + (10 - 1 - 6) / 2 = 6, so the underline lies on row 6 + 6 - 1.
    auto drawn = renderer::render_screen(resources, {&state, 1});
    CHECK(red_at(drawn, 16, 11) == underline_entry && red_at(drawn, 18, 11) == underline_entry);
    CHECK(red_at(drawn, 15, 11) == button_fill && red_at(drawn, 19, 11) == button_fill);
    CHECK(red_at(drawn, 16, 10) == button_fill);
    state.quick_key = 's';
    drawn = renderer::render_screen(resources, {&state, 1});
    CHECK(red_at(drawn, 22, 11) == underline_entry && red_at(drawn, 16, 11) == button_fill);
    // A key the caption does not hold in that case underlines nothing.
    state.quick_key = 'y';
    drawn = renderer::render_screen(resources, {&state, 1});
    CHECK(red_at(drawn, 16, 11) == button_fill);
    state.quick_key = 'Y';
    state.condition = renderer::ButtonCondition::pressed;
    drawn = renderer::render_screen(resources, {&state, 1});
    // Pressed, the caption moves a pixel right and down with its underline.
    CHECK(red_at(drawn, 17, 12) == 0 && red_at(drawn, 19, 12) == 0);
    state.condition = renderer::ButtonCondition::disabled;
    drawn = renderer::render_screen(resources, {&state, 1});
    CHECK(red_at(drawn, 16, 11) == grayed_fill);
    CHECK(red_at(drawn, 15, 4) == ground);

    state.condition = renderer::ButtonCondition::normal;
    state.focused = true;
    drawn = renderer::render_screen(resources, {&state, 1});
    // Ring 1 (level 31) lies a pixel outside the record, ring 2 (28) two.
    CHECK(red_at(drawn, 15, 4) == ground + 31);
    CHECK(red_at(drawn, 9, 10) == ground + 31);
    CHECK(red_at(drawn, 9, 4) == ground + 2 * 31);
    CHECK(red_at(drawn, 15, 3) == ground + 28);
    CHECK(red_at(drawn, 8, 3) == ground + 2 * 28);
    // Ring 3 (24) lies on the root's top row; ring 4 above it is left out.
    CHECK(red_at(drawn, 15, 2) == ground + 24);
    CHECK(red_at(drawn, 15, 1) == ground);
    // Rings 4 to 6 are lit where the root holds them.
    CHECK(red_at(drawn, 6, 10) == ground + 19);
    CHECK(red_at(drawn, 5, 10) == ground + 13);
    CHECK(red_at(drawn, 4, 10) == ground + 6);
    CHECK(red_at(drawn, 3, 10) == ground);
}

// A caption is placed within the button's inclusive rectangle, whose far
// edges are a pixel inside its width and height: centred, it starts
// (width - 1 - text width) / 2 + 1 across and (height - 1 - text height) / 2
// down, halves rounded toward zero; right-aligned, it ends 3 pixels before
// the far edge; left-aligned, it starts 3 pixels in, centred down.
void test_caption_placement() {
    namespace renderer = oa::ui::frontend_renderer;
    constexpr uint8_t glyph_colour = 7;
    constexpr uint8_t button_fill = 20;
    constexpr int16_t origin = 5;
    renderer::ScreenResources resources;
    resources.background.width = 40;
    resources.background.height = 40;
    resources.background.rgb.assign(40U * 40U * 3U, 0);
    for (std::size_t index = 0; index < 256; ++index)
        resources.gui_palette[index * 4] = static_cast<uint8_t>(index);
    resources.background.palette = resources.gui_palette;
    // Glyphs 3 pixels wide and 4 high: "AA" is 6 wide and the text is 6 high.
    oa::formats::fnt::Glyph glyph;
    glyph.width = 3;
    glyph.height = 4;
    glyph.pixels.assign(12, glyph_colour);
    glyph.coverage.assign(12, 1);
    for (const char character : {'I', 'A'})
        resources.font.glyphs[static_cast<unsigned char>(character)] = glyph;
    const renderer::ButtonPresentation state{
        "CAPTION", renderer::ButtonCondition::normal, std::nullopt, std::nullopt, std::nullopt
    };
    // Returns whether the caption's top-left glyph pixel lies at (x, y), with
    // the button's fill left of it and above it.
    const auto caption_at = [&](int16_t width, int16_t height, uint32_t attributes, int x, int y) {
        auto caption = button("CAPTION", origin, origin, width, height);
        caption.common.attributes = static_cast<int32_t>(attributes);
        std::get<oa::ui::gui_layout::ButtonFields>(caption.fields).text = "AA";
        resources.layout.gadgets = {caption};
        const auto drawn = renderer::render_screen(resources, {&state, 1});
        const auto ux = static_cast<std::size_t>(x);
        const auto uy = static_cast<std::size_t>(y);
        return red_at(drawn, ux, uy) == glyph_colour && red_at(drawn, ux - 1, uy) == button_fill &&
               red_at(drawn, ux, uy - 1) == button_fill;
    };
    constexpr uint32_t left = 1U;
    constexpr uint32_t centred = 2U;
    constexpr uint32_t right = 4U;
    // Even differences (20 - 6): x = 5 + (19 - 6) / 2 + 1 = 12, y = 5 + 6 = 11.
    CHECK(caption_at(20, 20, centred, 12, 11));
    // Odd differences (21 - 6): x = 5 + (20 - 6) / 2 + 1 = 13, y = 5 + 7 = 12.
    CHECK(caption_at(21, 21, centred, 13, 12));
    // Right-aligned: x = 5 + 19 - 6 - 3 = 15, or 5 + 20 - 6 - 3 = 16.
    CHECK(caption_at(20, 20, right, 15, 11));
    CHECK(caption_at(21, 21, right, 16, 12));
    // Left-aligned: x = 5 + 3 whatever the width.
    CHECK(caption_at(20, 20, left, 8, 11));
    CHECK(caption_at(21, 21, left, 8, 12));
}

// Six grayed-out 64x64 art buttons drawn in all 256 colours of a palette
// whose colours all differ, as a builder's page of empty build slots is:
// every pixel is darkened to the shade table's colour for its own, and the
// screen costs at most twice what it costs with the buttons drawn normally.
void test_grayed_art_cost() {
    namespace renderer = oa::ui::frontend_renderer;
    constexpr int screen_width = 640;
    constexpr int screen_height = 480;
    constexpr int button_side = 64;
    constexpr int button_count = 6;
    constexpr std::size_t shade_row = 12;
    renderer::ScreenResources resources;
    resources.background.width = screen_width;
    resources.background.height = screen_height;
    resources.background.rgb.assign(std::size_t{screen_width} * screen_height * 3U, 0);
    for (std::size_t index = 0; index < 256; ++index) {
        resources.gui_palette[index * 4] = static_cast<uint8_t>(index);
        resources.gui_palette[index * 4 + 1] = static_cast<uint8_t>(index * 37U);
        resources.gui_palette[index * 4 + 2] = static_cast<uint8_t>(index * 101U);
    }
    resources.background.palette = resources.gui_palette;
    resources.shade_table.resize(32U * 256U);
    for (std::size_t row = 0; row < 32; ++row)
        for (std::size_t index = 0; index < 256; ++index)
            resources.shade_table[row * 256 + index] = static_cast<uint8_t>(255U - index);
    oa::formats::gaf::Frame frame;
    frame.width = button_side;
    frame.height = button_side;
    frame.pixels.resize(std::size_t{button_side} * button_side);
    frame.coverage.assign(frame.pixels.size(), 1);
    for (std::size_t pixel = 0; pixel < frame.pixels.size(); ++pixel)
        frame.pixels[pixel] = static_cast<uint8_t>(pixel * 7U);
    std::vector<std::string> names;
    for (int slot = 0; slot < button_count; ++slot)
        names.push_back("SLOT" + std::to_string(slot));
    std::vector<renderer::ButtonPresentation> grayed;
    std::vector<renderer::ButtonPresentation> normal;
    for (int slot = 0; slot < button_count; ++slot) {
        auto gadget = button(
            names[static_cast<std::size_t>(slot)],
            static_cast<int16_t>(10 + slot * (button_side + 10)),
            20,
            button_side,
            button_side
        );
        std::get<oa::ui::gui_layout::ButtonFields>(gadget.fields).text.clear();
        resources.layout.gadgets.push_back(gadget);
        grayed.push_back(
            {names[static_cast<std::size_t>(slot)],
             renderer::ButtonCondition::disabled,
             std::nullopt,
             std::nullopt,
             std::nullopt}
        );
        normal.push_back(
            {names[static_cast<std::size_t>(slot)],
             renderer::ButtonCondition::normal,
             std::nullopt,
             std::nullopt,
             std::nullopt}
        );
    }
    // Each button draws the art sequence of its own name: the same frame
    // three times, so the grayed-out frame two past the normal one is it too.
    for (const auto& name : names) {
        oa::formats::gaf::Sequence sequence;
        sequence.name = name;
        sequence.frames.assign(3, frame);
        resources.sprites.sequences.push_back(sequence);
    }

    const auto shaded = renderer::render_screen(resources, grayed);
    bool every_pixel_shaded = true;
    for (int slot = 0; slot < button_count; ++slot)
        for (int y = 0; y < button_side; ++y)
            for (int x = 0; x < button_side; ++x) {
                const auto source = frame.pixels[static_cast<std::size_t>(y * button_side + x)];
                const std::size_t row = source >= 0x80 ? shade_row - 1 : shade_row;
                const auto darkened = resources.shade_table[row * 256 + source];
                const auto at = (static_cast<std::size_t>(20 + y) * screen_width +
                                 static_cast<std::size_t>(10 + slot * (button_side + 10) + x)) *
                                3U;
                for (std::size_t channel = 0; channel < 3; ++channel)
                    if (shaded.rgb[at + channel] != resources.gui_palette[darkened * 4U + channel])
                        every_pixel_shaded = false;
            }
    CHECK(every_pixel_shaded);

    // The quickest of several interleaved renders of each, so a busy
    // machine's pauses count against neither.
    constexpr int rounds = 7;
    auto quickest_grayed = std::chrono::steady_clock::duration::max();
    auto quickest_normal = std::chrono::steady_clock::duration::max();
    for (int round = 0; round < rounds; ++round) {
        auto start = std::chrono::steady_clock::now();
        const auto grayed_surface = renderer::render_screen(resources, grayed);
        quickest_grayed = std::min(quickest_grayed, std::chrono::steady_clock::now() - start);
        start = std::chrono::steady_clock::now();
        const auto normal_surface = renderer::render_screen(resources, normal);
        quickest_normal = std::min(quickest_normal, std::chrono::steady_clock::now() - start);
        CHECK(grayed_surface.rgb.size() == normal_surface.rgb.size());
    }
    if constexpr (render_times_compared) {
        CHECK(quickest_grayed <= 2 * quickest_normal);
        if (quickest_grayed > 2 * quickest_normal)
            std::fprintf(
                stderr,
                "grayed buttons took %lld us, normal ones %lld us\n",
                static_cast<long long>(
                    std::chrono::duration_cast<std::chrono::microseconds>(quickest_grayed).count()
                ),
                static_cast<long long>(
                    std::chrono::duration_cast<std::chrono::microseconds>(quickest_normal).count()
                )
            );
    }
}

// A colour blended over a rectangle: each channel mixed by the opacity in
// 256ths and rounded to the nearest, clipped to the image; no opacity
// leaves the pixels, a whole one paints the colour.
void test_blend_rect() {
    using oa::ui::frontend_renderer::blend_opaque;
    using oa::ui::frontend_renderer::blend_rect;
    oa::ui::frontend_renderer::Surface surface;
    surface.width = 4;
    surface.height = 3;
    surface.rgb.assign(4U * 3U * 3U, 200);
    const auto at = [&](int x, int y, int channel) {
        return surface.rgb[(static_cast<std::size_t>(y) * surface.width + x) * 3 + channel];
    };
    // Three quarters black over 200: (200 * 64 + 128) / 256 = 50.
    blend_rect(surface, 1, 1, 2, 1, {0, 0, 0}, 192);
    CHECK(at(1, 1, 0) == 50 && at(2, 1, 1) == 50 && at(2, 1, 2) == 50);
    CHECK(at(0, 1, 0) == 200 && at(3, 1, 0) == 200 && at(1, 0, 0) == 200 && at(1, 2, 0) == 200);
    // Half of each channel's colour over 50: (50 * 128 + c * 128 + 128) / 256.
    blend_rect(surface, 1, 1, 1, 1, {255, 100, 0}, 128);
    CHECK(at(1, 1, 0) == 153 && at(1, 1, 1) == 75 && at(1, 1, 2) == 25);
    // No opacity leaves the pixels; a whole one, or more, paints the colour.
    const auto kept = surface.rgb;
    blend_rect(surface, 0, 0, 4, 3, {9, 9, 9}, 0);
    CHECK(surface.rgb == kept);
    blend_rect(surface, 3, 2, 1, 1, {7, 8, 9}, blend_opaque);
    CHECK(at(3, 2, 0) == 7 && at(3, 2, 1) == 8 && at(3, 2, 2) == 9);
    blend_rect(surface, 0, 2, 1, 1, {1, 2, 3}, blend_opaque + 100);
    CHECK(at(0, 2, 0) == 1 && at(0, 2, 1) == 2 && at(0, 2, 2) == 3);
    // Clipped at every edge; nothing outside the image, an empty
    // rectangle or an image whose pixels do not fill its size changes.
    blend_rect(surface, -3, -3, 4, 4, {0, 0, 0}, blend_opaque);
    CHECK(at(0, 0, 0) == 0 && at(1, 0, 0) == 200 && at(0, 1, 0) == 200);
    blend_rect(surface, 3, 1, 10, 10, {0, 0, 0}, blend_opaque);
    CHECK(at(3, 1, 0) == 0 && at(3, 2, 0) == 0 && at(2, 2, 0) == 200);
    const auto clipped = surface.rgb;
    blend_rect(surface, 4, 0, 2, 2, {0, 0, 0}, blend_opaque);
    blend_rect(surface, 0, 3, 2, 2, {0, 0, 0}, blend_opaque);
    blend_rect(surface, 1, 1, 0, 2, {0, 0, 0}, blend_opaque);
    blend_rect(surface, 1, 1, 2, -1, {0, 0, 0}, blend_opaque);
    CHECK(surface.rgb == clipped);
    surface.rgb.assign(3, 200);
    blend_rect(surface, 0, 0, 1, 1, {0, 0, 0}, blend_opaque);
    CHECK(surface.rgb[0] == 200);
}

// The panel under a shade_below panel: entries below 0x80 read shade row
// 8 (level -0x18), entries 0x80 to 0xFF the row before, as 3.1c's
// rectangle shade indexes the row as signed bytes.
void test_shade_panel_below() {
    using oa::ui::frontend_renderer::shade_below_level;
    using oa::ui::frontend_renderer::shade_panel_below;
    CHECK(shade_below_level == -0x18);
    // Entry e is (e, 255 - e, 7): every colour is its own entry.
    oa::PaletteBytes palette{};
    for (std::size_t entry = 0; entry < oa::palette_color_count; ++entry) {
        palette[entry * oa::palette_entry_bytes] = static_cast<uint8_t>(entry);
        palette[entry * oa::palette_entry_bytes + 1] = static_cast<uint8_t>(255 - entry);
        palette[entry * oa::palette_entry_bytes + 2] = 7;
    }
    // Row 8 takes entry e to e + 1, row 7 to e + 2, every other row to 0.
    std::vector<uint8_t> shade(32U * 256U, 0);
    for (std::size_t entry = 0; entry < 256; ++entry) {
        shade[8U * 256U + entry] = static_cast<uint8_t>(entry + 1);
        shade[7U * 256U + entry] = static_cast<uint8_t>(entry + 2);
    }
    const std::vector<uint8_t> entries{0x00, 0x10, 0x7f, 0x80, 0x90, 0xff};
    oa::ui::frontend_renderer::Surface surface;
    surface.width = static_cast<uint32_t>(entries.size());
    surface.height = 2;
    surface.rgb.resize(static_cast<std::size_t>(surface.width) * surface.height * 3U);
    const auto paint = [&](std::size_t pixel, uint8_t entry) {
        std::copy_n(&palette[entry * oa::palette_entry_bytes], 3, &surface.rgb[pixel * 3U]);
    };
    const auto entry_at = [&](std::size_t pixel) {
        const auto* rgb = &surface.rgb[pixel * 3U];
        return rgb[2] == 7 && rgb[0] == 255 - rgb[1] ? static_cast<int>(rgb[0]) : -1;
    };
    const auto fill = [&] {
        for (std::size_t row = 0; row < surface.height; ++row)
            for (std::size_t column = 0; column < entries.size(); ++column)
                paint(row * surface.width + column, entries[column]);
    };
    fill();
    shade_panel_below(surface, 0, 0, 6, 1, palette, shade.data());
    CHECK(entry_at(0) == 0x01);
    CHECK(entry_at(1) == 0x11);
    CHECK(entry_at(2) == 0x80);
    CHECK(entry_at(3) == 0x82);
    CHECK(entry_at(4) == 0x92);
    CHECK(entry_at(5) == 0x01);
    for (std::size_t column = 0; column < entries.size(); ++column)
        CHECK(entry_at(surface.width + column) == entries[column]);
    // Clipped to the image; pixels of the kept colour stay.
    fill();
    shade_panel_below(
        surface, 3, -1, 10, 10, palette, shade.data(), &palette[0x90 * oa::palette_entry_bytes]
    );
    for (std::size_t row = 0; row < surface.height; ++row) {
        const std::size_t first = row * surface.width;
        CHECK(entry_at(first) == 0x00 && entry_at(first + 2) == 0x7f);
        CHECK(entry_at(first + 3) == 0x82 && entry_at(first + 4) == 0x90);
        CHECK(entry_at(first + 5) == 0x01);
    }
    // Without a shade table, or with an empty rectangle, nothing changes.
    fill();
    const auto kept = surface.rgb;
    shade_panel_below(surface, 0, 0, 6, 2, palette, nullptr);
    shade_panel_below(surface, 0, 0, 0, 2, palette, shade.data());
    shade_panel_below(surface, 6, 0, 2, 2, palette, shade.data());
    CHECK(surface.rgb == kept);
}

} // namespace

int main() {
    using oa::ui::frontend_renderer::staged_caption;
    assert(staged_caption("Easy|Medium|Hard", 0) == "Easy");
    assert(staged_caption("Easy|Medium|Hard", 1) == "Medium");
    assert(staged_caption("Easy|Medium|Hard", 2) == "Hard");
    assert(staged_caption("Easy|Medium|Hard", 3).empty());
    assert(staged_caption("Select Map", 2) == "Select Map");
    assert(staged_caption("", 1).empty());

    oa::ui::frontend_renderer::MainMenuResources resources;
    resources.background.width = 8;
    resources.background.height = 8;
    resources.background.rgb.assign(8U * 8U * 3U, 99);
    for (std::size_t index = 0; index < 256; ++index) {
        resources.gui_palette[index * 4] = static_cast<uint8_t>(index);
    }
    resources.layout.gadgets.push_back(button("PLAIN", 1, 1, 5, 5));

    const auto raised = oa::ui::frontend_renderer::render_main_menu(resources);
    assert(red_at(raised, 1, 1) == 17);
    assert(red_at(raised, 5, 5) == 0);
    assert(red_at(raised, 3, 3) == 20);

    const oa::ui::frontend_renderer::ButtonPresentation pressed{
        "plain",
        oa::ui::frontend_renderer::ButtonCondition::pressed,
        std::nullopt,
        std::nullopt,
        std::nullopt
    };
    const auto sunken = oa::ui::frontend_renderer::render_main_menu(resources, {&pressed, 1});
    assert(red_at(sunken, 1, 1) == 0);
    assert(red_at(sunken, 5, 5) == 17);

    // A button under the pointer draws exactly as it does without it.
    const oa::ui::frontend_renderer::ButtonPresentation hovered{
        "PLAIN",
        oa::ui::frontend_renderer::ButtonCondition::hovered,
        std::nullopt,
        std::nullopt,
        std::nullopt
    };
    CHECK(oa::ui::frontend_renderer::render_main_menu(resources, {&hovered, 1}).rgb == raised.rgb);

    const oa::ui::frontend_renderer::ButtonPresentation disabled{
        "PLAIN",
        oa::ui::frontend_renderer::ButtonCondition::disabled,
        std::nullopt,
        std::nullopt,
        std::nullopt
    };
    const auto grayed = oa::ui::frontend_renderer::render_main_menu(resources, {&disabled, 1});
    assert(red_at(grayed, 1, 1) == 0);
    assert(red_at(grayed, 5, 5) == 19);
    assert(red_at(grayed, 3, 3) == 19);

    oa::formats::gaf::Sequence sequence;
    sequence.name = "PLAIN";
    oa::formats::gaf::Frame frame;
    frame.width = 2;
    frame.height = 1;
    frame.transparency_index = 7;
    frame.pixels = {7, 42};
    frame.coverage = {1, 0};
    sequence.frames.push_back(frame);
    resources.sprites.sequences.push_back(sequence);
    const auto sprite = oa::ui::frontend_renderer::render_main_menu(resources);
    // A covered literal equal to the transparency index must still draw, and
    // an uncovered non-transparent value must not draw.
    assert(red_at(sprite, 1, 1) == 7);
    assert(red_at(sprite, 2, 1) == 99);

    oa::ui::frontend_renderer::ScreenResources controls;
    controls.background.width = 24;
    controls.background.height = 16;
    controls.background.rgb.assign(24U * 16U * 3U, 99);
    for (std::size_t index = 0; index < 256; ++index)
        controls.gui_palette[index * 4] = static_cast<uint8_t>(index);
    oa::formats::fnt::Glyph glyph;
    glyph.width = 2;
    glyph.height = 2;
    glyph.origin_y = -1;
    glyph.pixels.assign(4, 42);
    glyph.coverage.assign(4, 1);
    controls.font.glyphs['A'] = glyph;
    controls.font.glyphs['I'] = glyph;
    controls.light_table.resize(32U * 256U);
    for (std::size_t level = 0; level < 32; ++level)
        for (std::size_t index = 0; index < 256; ++index)
            controls.light_table[level * 256 + index] = static_cast<uint8_t>(index);
    controls.light_table[0x1eU * 256U + 42U] = 77;

    oa::ui::gui_layout::Gadget label;
    label.common.type = oa::ui::gui_layout::GadgetType::label;
    label.common.name = "DESCRIPTION";
    label.common.x = 1;
    label.common.y = 1;
    label.common.width = 2; // clips the second A completely
    label.common.height = 3;
    label.common.attributes = 1;
    label.common.active = 1;
    label.fields = oa::ui::gui_layout::LabelFields{"", "AA", ""};
    controls.layout.gadgets.push_back(label);

    oa::ui::gui_layout::Gadget list;
    list.common.type = oa::ui::gui_layout::GadgetType::list_box;
    list.common.name = "MAPNAMES";
    list.common.x = 8;
    list.common.y = 1;
    list.common.width = 8;
    list.common.height = 13;
    list.common.attributes = 1;
    list.common.active = 1;
    list.fields = oa::ui::gui_layout::ListBoxFields{0};
    controls.layout.gadgets.push_back(list);
    const std::vector<std::string> names{"A", "AA"};
    const oa::ui::frontend_renderer::ListPresentation list_data{"mapnames", names, 0, 1};
    const auto controls_rendered =
        oa::ui::frontend_renderer::render_screen(controls, {}, {&list_data, 1});
    assert(red_at(controls_rendered, 1, 2) == 42);
    assert(red_at(controls_rendered, 3, 2) == 99); // label clip

    // A single-line label authored with no height is limited by width only.
    auto flat_controls = controls;
    flat_controls.layout.gadgets[0].common.height = 0;
    const auto flat_rendered =
        oa::ui::frontend_renderer::render_screen(flat_controls, {}, {&list_data, 1});
    assert(red_at(flat_rendered, 1, 2) == 42);
    assert(red_at(flat_rendered, 3, 2) == 99);
    assert(red_at(controls_rendered, 10, 4) == 42);
    // line_height is glyph-I height+2; default item spacing adds one.
    assert(red_at(controls_rendered, 10, 9) == 77); // selected row lit at level 0x1E

    // The list drawing stops when the remaining gadget height falls below one font
    // line, even if another row's top would still lie inside the rectangle.
    auto short_controls = controls;
    auto& short_list = short_controls.layout.gadgets[1];
    short_list.common.height = 8; // line height 4, item height 5: one row only
    const auto short_rendered =
        oa::ui::frontend_renderer::render_screen(short_controls, {}, {&list_data, 1});
    CHECK(red_at(short_rendered, 10, 4) == 42);
    CHECK(red_at(short_rendered, 10, 9) == 99);

    // A button or label left with an authored foreground colour (a merged
    // sub-panel's records keep theirs) draws lit through that light-table row,
    // as 3.1c draws it with a colour table; colour 0 draws it plain.
    oa::formats::gaf::Sequence transport;
    transport.name = "CDPLAY";
    oa::formats::gaf::Frame transport_frame;
    transport_frame.width = 1;
    transport_frame.height = 1;
    transport_frame.pixels = {60};
    transport_frame.coverage = {1};
    transport.frames.push_back(transport_frame);
    controls.sprites.sequences.push_back(transport);
    controls.light_table[5U * 256U + 60U] = 120;
    auto lit_button = button("CDPLAY", 20, 10, 1, 1);
    lit_button.common.foreground_color = 5;
    controls.layout.gadgets.push_back(lit_button);
    const auto lit_button_rendered = oa::ui::frontend_renderer::render_screen(controls);
    CHECK(red_at(lit_button_rendered, 20, 10) == 120);
    controls.layout.gadgets.back().common.foreground_color = 0;
    const auto plain_button_rendered = oa::ui::frontend_renderer::render_screen(controls);
    CHECK(red_at(plain_button_rendered, 20, 10) == 60);

    controls.light_table[5U * 256U + 42U] = 150;
    oa::ui::gui_layout::Gadget lit_label;
    lit_label.common.type = oa::ui::gui_layout::GadgetType::label;
    lit_label.common.name = "TRACKNUM";
    lit_label.common.x = 20;
    lit_label.common.y = 12;
    lit_label.common.width = 2;
    lit_label.common.height = 3;
    lit_label.common.attributes = 1;
    lit_label.common.foreground_color = 5;
    lit_label.common.active = 1;
    lit_label.fields = oa::ui::gui_layout::LabelFields{"", "I", ""};
    controls.layout.gadgets.push_back(lit_label);
    const auto lit_label_rendered = oa::ui::frontend_renderer::render_screen(controls);
    CHECK(red_at(lit_label_rendered, 20, 13) == 150);

    oa::ui::gui_layout::Gadget runtime_image;
    runtime_image.common.type = oa::ui::gui_layout::GadgetType::hot_surface;
    runtime_image.common.name = "Color0";
    runtime_image.common.x = 20;
    runtime_image.common.y = 1;
    runtime_image.common.active = 1;
    controls.layout.gadgets.push_back(runtime_image);
    oa::formats::gaf::Sequence logos;
    logos.name = "32xlogos";
    oa::formats::gaf::Frame logo;
    logo.width = 1;
    logo.height = 1;
    logo.pixels = {55};
    logo.coverage = {1};
    logos.frames.push_back(logo);
    controls.global_sprites.sequences.push_back(logos);
    const oa::ui::frontend_renderer::ButtonPresentation image_state{
        "Color0",
        oa::ui::frontend_renderer::ButtonCondition::normal,
        0,
        std::nullopt,
        oa::ui::frontend_renderer::SpriteOverride{
            oa::ui::frontend_renderer::SpriteArchive::global, "32xlogos"
        }
    };
    const auto image_rendered =
        oa::ui::frontend_renderer::render_screen(controls, {&image_state, 1});
    assert(red_at(image_rendered, 20, 1) == 55);
    // Without a frame the hot surface is left to whoever draws it.
    auto unframed_state = image_state;
    unframed_state.gaf_frame.reset();
    const auto unframed = oa::ui::frontend_renderer::render_screen(controls, {&unframed_state, 1});
    assert(red_at(unframed, 20, 1) != 55);

    oa::ui::frontend_renderer::ScreenResources setup_screen = controls;
    setup_screen.background.width = 80;
    setup_screen.background.height = 40;
    setup_screen.background.rgb.assign(80U * 40U * 3U, 99);
    oa::ui::gui_layout::Gadget color_patch;
    color_patch.common.type = oa::ui::gui_layout::GadgetType::hot_surface;
    color_patch.common.name = "Color1";
    color_patch.common.x = 8;
    color_patch.common.y = 8;
    color_patch.common.width = 20;
    color_patch.common.height = 20;
    color_patch.common.active = 1;
    setup_screen.layout.gadgets = {color_patch};
    oa::formats::gaf::Frame logo32;
    logo32.width = 32;
    logo32.height = 32;
    logo32.pixels.assign(32U * 32U, 77);
    logo32.coverage.assign(32U * 32U, 1);
    logos.frames[0] = std::move(logo32);
    setup_screen.global_sprites.sequences.back() = logos;
    const oa::ui::frontend_renderer::ButtonPresentation patch_state{
        "Color1",
        oa::ui::frontend_renderer::ButtonCondition::normal,
        0,
        std::nullopt,
        oa::ui::frontend_renderer::SpriteOverride{
            oa::ui::frontend_renderer::SpriteArchive::global, "32xlogos"
        }
    };
    const auto patch_rendered =
        oa::ui::frontend_renderer::render_screen(setup_screen, {&patch_state, 1});
    assert(red_at(patch_rendered, 8, 8) == 77);
    assert(red_at(patch_rendered, 27, 27) == 77);
    assert(red_at(patch_rendered, 28, 8) == 99);
    assert(red_at(patch_rendered, 8, 28) == 99);

    oa::Image circuit;
    circuit.width = oa::ui::frontend_renderer::menu_spark_width;
    circuit.height = oa::ui::frontend_renderer::menu_spark_height;
    const auto pixels = static_cast<std::size_t>(circuit.width) * circuit.height;
    circuit.rgb.assign(pixels * 3U, 0);
    circuit.indices.assign(pixels, 0);
    circuit.palette.emplace();
    (*circuit.palette)[oa::ui::frontend_renderer::menu_spark_pixel * 4] = 250;
    (*circuit.palette)[oa::ui::frontend_renderer::menu_spark_pixel * 4 + 1] = 250;
    (*circuit.palette)[oa::ui::frontend_renderer::menu_spark_pixel * 4 + 2] = 250;
    for (int x = 0; x < oa::ui::frontend_renderer::menu_spark_width; ++x)
        circuit.indices[10U * circuit.width + static_cast<std::size_t>(x)] = 0xdf;
    oa::ui::frontend_renderer::Surface spark_surface;
    spark_surface.width = circuit.width;
    spark_surface.height = circuit.height;
    spark_surface.rgb = circuit.rgb;
    oa::ui::frontend_renderer::MenuSparks sparks;
    oa::ui::frontend_renderer::reset_menu_sparks(sparks, circuit);
    assert(sparks.sparks.size() == oa::ui::frontend_renderer::menu_spark_count);
    bool drew = false;
    for (int step = 0; step < 400 && !drew; ++step) {
        spark_surface.rgb.assign(pixels * 3U, 0);
        oa::ui::frontend_renderer::step_menu_sparks(sparks, spark_surface, circuit);
        for (const auto& spark : sparks.sparks) {
            if (spark.active == 0)
                continue;
            assert(spark.x >= 0 && spark.x < oa::ui::frontend_renderer::menu_spark_width);
            assert(spark.y >= 0 && spark.y < oa::ui::frontend_renderer::menu_spark_height);
            if (spark.y == 10) {
                const auto offset = (static_cast<std::size_t>(spark.y) * spark_surface.width +
                                     static_cast<std::size_t>(spark.x)) *
                                    3U;
                if (spark_surface.rgb[offset] == 250)
                    drew = true;
            }
        }
    }
    assert(drew);

    // A spark spawned on a vertical course moves down from an even row and
    // up from an odd one; a horizontal one right from an even column.
    oa::Image traces = circuit;
    traces.indices.assign(pixels, 0xdf);
    oa::ui::frontend_renderer::MenuSparks born;
    oa::ui::frontend_renderer::reset_menu_sparks(born, traces);
    oa::ui::frontend_renderer::step_menu_sparks(born, spark_surface, traces);
    int vertical = 0;
    for (const auto& spark : born.sparks) {
        assert(spark.active == 1);
        if (spark.dx == 0) {
            assert(spark.dy == ((spark.y & 1) == 0 ? 3 : -3));
            ++vertical;
        } else {
            assert(spark.dx == ((spark.x & 1) == 0 ? 3 : -3));
        }
    }
    assert(vertical > 0);

    test_grayed_art_frame();
    test_grayed_art_cost();
    test_quick_key_and_focus();
    test_caption_placement();
    test_blend_rect();
    test_shade_panel_below();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
