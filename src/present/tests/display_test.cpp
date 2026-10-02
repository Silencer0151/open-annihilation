// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Display context, RenderSink boundary, sprite and row-RLE behaviour.

#include "oa/present/blit.hpp"
#include "oa/present/display.hpp"
#include "oa/present/pcx.hpp"
#include "oa/present/raster.hpp"
#include "oa/present/rle.hpp"
#include "oa/present/surface.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace {

using oa::Palette;
using oa::PaletteEntry;
using oa::Rect32;
using oa::RenderSink;
using oa::Sprite;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

struct SinkLog {
    int frames = 0;
    int modes = 0;
    int32_t mode_width = 0;
    int32_t mode_height = 0;
    int32_t mode_bits = 0;
    int32_t pitch = 0;
    std::vector<uint8_t> last_frame;
    Palette last_palette{};
};

void sink_present(
    void* user,
    const uint8_t* pixels,
    int32_t pitch,
    int32_t width,
    int32_t height,
    const Palette* palette
) {
    auto* log = static_cast<SinkLog*>(user);
    ++log->frames;
    log->pitch = pitch;
    log->last_frame.assign(pixels, pixels + static_cast<std::size_t>(pitch) * height);
    log->last_palette = *palette;
    (void)width;
}

int32_t sink_mode(void* user, int32_t width, int32_t height, int32_t bits) {
    auto* log = static_cast<SinkLog*>(user);
    ++log->modes;
    log->mode_width = width;
    log->mode_height = height;
    log->mode_bits = bits;
    return 1;
}

void test_surface_helpers() {
    auto buffer = oa::present::create_surface(5, 3);
    check(
        buffer.surface.pitch == 5 && buffer.surface.clip.x2 == 4 && buffer.surface.clip.y2 == 2,
        "create_surface clip"
    );
    check((buffer.surface.flags & OA_SURFACE_FLAG_MEMORY) != 0, "create_surface memory flag");
    buffer.pixels[1] = 7;
    Palette palette{};
    palette.entries[7] = PaletteEntry{10, 20, 30, 0};
    const auto rgb = oa::present::to_rgb(buffer.surface, palette);
    check(
        rgb.size() == 45 && rgb[3] == 10 && rgb[4] == 20 && rgb[5] == 30,
        "to_rgb expands through the palette"
    );
}

void test_display_sink() {
    SinkLog log;
    oa::present::DisplayContext display;
    display.sink = RenderSink{&log, nullptr, &sink_present, &sink_mode};
    display.width = 30;
    display.height = 20;
    display.palette.entries[9] = PaletteEntry{1, 2, 3, 0};
    oa::present::bind_display(&display);
    check(oa::present::init_display(display, false) == 1, "init_display");
    check(
        log.modes == 1 && log.mode_width == 30 && log.mode_height == 20 && log.mode_bits == 8,
        "display mode"
    );
    check(display.back_buffer.pitch == 32, "back buffer pitch rounds to four");
    check(
        oa::present::clear_surface(nullptr, 4) == 1 && display.back_pixels[0] == 4,
        "clear draw target"
    );
    oa::present::draw_clipped_line(nullptr, -5, 3, 40, 3, 9);
    check(
        display.back_pixels[3 * 32 + 0] == 9 && display.back_pixels[3 * 32 + 29] == 9,
        "line clipped to clip rect"
    );
    oa::present::draw_frame();
    check(
        log.frames == 1 && log.pitch == 32 && log.last_frame[3 * 32 + 5] == 9, "draw_frame presents"
    );
    check(log.last_palette.entries[9].b == 3, "device palette reaches the sink");

    oa::present::OffscreenSurface offscreen;
    oa::present::use_standard_offscreen(display, offscreen);
    check(
        display.width == 640 && display.height == 480 && offscreen.allocated,
        "standard offscreen size"
    );
    check(
        display.use_active_surface == 1 && display.active_surface == &offscreen.surface,
        "offscreen is active"
    );
    offscreen.pixels[640 * 2 + 3] = 77;
    oa::present::draw_frame();
    check(log.last_frame[640 * 2 + 3] == 77, "active surface composed into frame");
    oa::present::destroy_offscreen_surface(offscreen);
    check(display.use_active_surface == 0 && display.offscreen == nullptr, "offscreen destroyed");

    oa::present::DisplayModeList modes;
    oa::present::scan_display_modes(modes, 1280, 1024);
    check(modes.count == 4 && modes.modes[3].width == 1280, "large desktop adds 1280x1024");
    oa::present::scan_display_modes(modes, 1024, 768);
    check(modes.count == 3, "small desktop keeps the standard modes");
    oa::present::shutdown_display(display);
    oa::present::bind_display(nullptr);
}

void test_cursor_overlay() {
    SinkLog log;
    oa::present::DisplayContext display;
    display.sink = RenderSink{&log, nullptr, &sink_present, &sink_mode};
    display.width = 16;
    display.height = 16;
    oa::present::bind_display(&display);
    oa::present::init_display(display, false);
    auto cursor = oa::present::create_sprite(2, 2);
    cursor.pixels = {5, 0, 5, 5};
    cursor.sprite.data = cursor.pixels.data();
    cursor.sprite.key = 0;
    auto backing = oa::present::create_surface(8, 8);
    display.back_pixels[4 * 16 + 4] = 3;
    display.cursor = oa::present::CursorOverlay{4, 4, &cursor.sprite, 0, 0, &backing.surface, 1, 1};
    oa::present::draw_frame();
    check(
        log.last_frame[4 * 16 + 4] == 5 && log.last_frame[4 * 16 + 5] == 0,
        "cursor keyed over frame"
    );
    check(backing.pixels[0] == 3, "pixels under the cursor saved");
    // set_cursor_overlay_visible writes CursorOverlay::visible, which gates the overlay.
    oa::present::set_cursor_overlay_visible(0);
    display.back_pixels[4 * 16 + 4] = 3;
    oa::present::draw_frame();
    check(
        display.cursor.visible == 0 && log.last_frame[4 * 16 + 4] == 3, "hidden cursor not drawn"
    );
    oa::present::set_cursor_overlay_visible(1);
    oa::present::draw_frame();
    check(
        display.cursor.visible == 1 && log.last_frame[4 * 16 + 4] == 5, "shown cursor drawn again"
    );
    oa::present::bind_display(nullptr);
    oa::present::set_cursor_overlay_visible(1);
}

void test_rle_round_trip() {
    std::mt19937 rng(1234);
    for (int n = 0; n < 200; ++n) {
        const auto w = static_cast<uint16_t>(1 + rng() % 70);
        const auto h = static_cast<uint16_t>(1 + rng() % 9);
        auto raw = oa::present::create_sprite(w, h);
        raw.sprite.key = static_cast<uint8_t>(rng() % 3);
        for (std::size_t i = 0; i < raw.pixels.size();) {
            const std::size_t run = 1 + rng() % 150;
            const bool repeat = rng() % 2 == 0;
            const auto value = static_cast<uint8_t>(rng() % 5);
            for (std::size_t k = 0; k < run && i < raw.pixels.size(); ++k, ++i) {
                raw.pixels[i] = repeat ? value : static_cast<uint8_t>(rng() % 5);
            }
        }
        oa::present::RleEncoder encoder;
        const int32_t size = oa::present::encode_rle_sprite(encoder, nullptr, raw.sprite);
        std::vector<uint8_t> stream(static_cast<std::size_t>(size));
        check(
            oa::present::encode_rle_sprite(encoder, stream.data(), raw.sprite) == size,
            "measure matches encode"
        );
        Sprite encoded = raw.sprite;
        encoded.encoding = OA_SPRITE_ROW_RLE;
        encoded.data = stream.data();
        auto a = oa::present::create_surface(64, 24);
        auto b = oa::present::create_surface(64, 24);
        std::fill(a.pixels.begin(), a.pixels.end(), uint8_t{200});
        std::fill(b.pixels.begin(), b.pixels.end(), uint8_t{200});
        a.surface.clip = Rect32{3, 2, 50, 20};
        b.surface.clip = a.surface.clip;
        const auto x = static_cast<int32_t>(rng() % 60) - 10;
        const auto y = static_cast<int32_t>(rng() % 24) - 4;
        oa::present::draw_sprite(&a.surface, &raw.sprite, x, y);
        oa::present::draw_sprite(&b.surface, &encoded, x, y);
        if (a.pixels != b.pixels) {
            check(false, "row-RLE sprite draws like its raw source");
            break;
        }
    }
}

void test_text() {
    // Height 2, baseline 0, first char 'A', one glyph of width 3.
    std::vector<uint8_t> font = {2, 0, 0, 'A', 6, 0, 3, 0xA8, 0x00};
    auto surface = oa::present::create_surface(8, 4);
    check(oa::present::measure_text_width(font.data(), "AA\nA") == 6, "measure stops at newline");
    oa::present::draw_font_text(surface.pixels.data(), 8, font.data(), "A", 1, 1, 9, 2, 0);
    check(
        surface.pixels[1 * 8 + 1] == 9 && surface.pixels[1 * 8 + 2] == 2 &&
            surface.pixels[1 * 8 + 3] == 9,
        "font bits pick fg and bg"
    );
    check(
        surface.pixels[2 * 8 + 1] == 2 && surface.pixels[2 * 8 + 2] == 9,
        "font bits continue across rows"
    );
}

// Height 3, baseline 1, first char 'A'; 'A' is 2 wide, 'B' 3 wide, 'C' has
// no glyph.
std::vector<uint8_t> sample_font() {
    std::vector<uint8_t> font = {3,    0,    1, 'A', 10, 0, 17, 0,    0,    0,   2,
                                 0xAA, 0x80, 0, 0,   0,  0, 3,  0xFF, 0xFF, 0x80};
    return font;
}

void test_text_colors() {
    oa::present::DisplayContext display;
    oa::present::bind_display(&display);
    oa::present::set_text_colors(4, 5);
    check(display.text_color == 4 && display.text_background == 5, "text colours set");
    oa::present::set_text_colors(oa::present::text_color_keep, 6);
    check(display.text_color == 4 && display.text_background == 6, "-1 keeps the text colour");
    oa::present::set_text_colors(7, oa::present::text_color_keep);
    check(display.text_color == 7 && display.text_background == 6, "-1 keeps the background");
    oa::present::set_text_transparent(0xFE);
    check(
        oa::present::text_transparent() == 0xFE && display.text_transparent == 0xFE,
        "transparent text value"
    );
    oa::present::bind_display(nullptr);
}

void test_draw_text() {
    const auto font = sample_font();
    oa::present::DisplayContext display;
    display.font = font.data();
    display.text_color = 9;
    display.text_background = 2;
    display.text_transparent = 2;
    oa::present::bind_display(&display);
    auto surface = oa::present::create_surface(16, 8);
    oa::present::draw_text(&surface.surface, "AB", 1, 2, oa::present::text_width_unbounded);
    // Baseline 1: rows 1..3. 'A' rows are 10 / 10 / 10 and 'B' 111 / 111 / 111.
    check(
        surface.pixels[1 * 16 + 1] == 9 && surface.pixels[1 * 16 + 2] == 0,
        "set bits take the text colour"
    );
    check(
        surface.pixels[3 * 16 + 5] == 9 && surface.pixels[4 * 16 + 1] == 0,
        "glyph rows follow the baseline"
    );

    std::fill(surface.pixels.begin(), surface.pixels.end(), uint8_t{0});
    oa::present::draw_text(&surface.surface, "ABAB", 1, 2, 7);
    check(
        surface.pixels[1 * 16 + 6] == 9 && surface.pixels[1 * 16 + 8] == 0,
        "trimmed to the widest fitting prefix"
    );

    std::fill(surface.pixels.begin(), surface.pixels.end(), uint8_t{0});
    display.text_transparent = 0xFE;
    oa::present::draw_text(&surface.surface, "CA", 1, 2, 2);
    check(
        surface.pixels[1 * 16 + 1] == 9 && surface.pixels[1 * 16 + 2] == 2,
        "glyphless characters keep their place"
    );

    std::fill(surface.pixels.begin(), surface.pixels.end(), uint8_t{0});
    surface.surface.clip = Rect32{1, 2, 5, 5};
    oa::present::draw_text(&surface.surface, "AB", 1, 2, oa::present::text_width_unbounded);
    check(surface.pixels[1 * 16 + 1] == 0, "the box reaches x + width and is not inside the clip");
    surface.surface.clip = Rect32{1, 2, 6, 5};
    oa::present::draw_text(&surface.surface, "AB", 1, 2, oa::present::text_width_unbounded);
    check(
        surface.pixels[1 * 16 + 1] == 9, "a box of x..x+width, y..y+height inside the clip draws"
    );
    std::fill(surface.pixels.begin(), surface.pixels.end(), uint8_t{0});
    surface.surface.clip = Rect32{1, 2, 6, 4};
    oa::present::draw_text(&surface.surface, "AB", 1, 2, oa::present::text_width_unbounded);
    check(surface.pixels[1 * 16 + 1] == 0, "the box is one row taller than the font");
    oa::present::bind_display(nullptr);
}

void test_fill_clipped_rect() {
    auto surface = oa::present::create_surface(8, 6);
    surface.surface.clip = Rect32{2, 1, 5, 4};
    check(
        oa::present::fill_clipped_rect(&surface.surface, Rect32{0, 0, 3, 7}, 7),
        "overlapping rectangle filled"
    );
    check(surface.pixels[1 * 8 + 2] == 7 && surface.pixels[4 * 8 + 3] == 7, "fill inside the clip");
    check(
        surface.pixels[0 * 8 + 2] == 0 && surface.pixels[1 * 8 + 1] == 0 &&
            surface.pixels[1 * 8 + 4] == 0,
        "fill stops at the clip and the rectangle"
    );
    check(
        !oa::present::fill_clipped_rect(&surface.surface, Rect32{6, 0, 7, 5}, 3),
        "rectangle outside the clip"
    );
    check(
        !oa::present::fill_clipped_rect(&surface.surface, Rect32{4, 2, 3, 3}, 3),
        "inverted rectangle"
    );

    check(!oa::present::fill_clipped_rect(nullptr, Rect32{0, 0, 1, 1}, 3), "no display to lock");
    oa::present::DisplayContext display;
    display.width = 8;
    display.height = 4;
    oa::present::bind_display(&display);
    oa::present::init_display(display, false);
    check(
        oa::present::fill_clipped_rect(nullptr, Rect32{-3, 1, 1, 9}, 5),
        "null target fills the locked surface"
    );
    check(
        display.back_pixels[1 * 8 + 0] == 5 && display.back_pixels[3 * 8 + 1] == 5 &&
            display.back_pixels[1 * 8 + 2] == 0,
        "locked surface clipped fill"
    );
    oa::present::bind_display(nullptr);
}

void test_sprite_gray() {
    uint8_t gray[256];
    for (int i = 0; i < 256; ++i) {
        gray[i] = static_cast<uint8_t>(255 - i);
    }
    oa::present::DisplayContext display;
    display.gray_table = gray;
    oa::present::bind_display(&display);
    auto raw = oa::present::create_sprite(2, 2);
    raw.pixels = {1, 0, 0, 1};
    raw.sprite.data = raw.pixels.data();
    raw.sprite.origin_x = 1;
    auto surface = oa::present::create_surface(4, 3);
    std::fill(surface.pixels.begin(), surface.pixels.end(), uint8_t{10});
    oa::present::draw_sprite_gray(&surface.surface, &raw.sprite, 2, 1);
    check(surface.pixels[1 * 4 + 1] == 10, "nothing grays without the gray table flag");
    display.flags = oa::present::display_flag_gray_table;
    oa::present::draw_sprite_gray(&surface.surface, &raw.sprite, 2, 1);
    check(
        surface.pixels[1 * 4 + 1] == 245 && surface.pixels[1 * 4 + 2] == 10 &&
            surface.pixels[2 * 4 + 2] == 245,
        "pixels under non-key sprite pixels gray"
    );

    Sprite encoded = raw.sprite;
    encoded.encoding = OA_SPRITE_ROW_RLE;
    std::fill(surface.pixels.begin(), surface.pixels.end(), uint8_t{10});
    oa::present::draw_sprite_gray(&surface.surface, &encoded, 2, 1);
    check(surface.pixels[1 * 4 + 1] == 10, "row-RLE sprites are skipped");

    Sprite* children[] = {&raw.sprite, &encoded};
    Sprite composite{};
    composite.child_count = 2;
    composite.data = children;
    oa::present::draw_sprite_gray(&surface.surface, &composite, 1, 0);
    check(
        surface.pixels[0 * 4 + 0] == 245 && surface.pixels[1 * 4 + 1] == 245 &&
            surface.pixels[0 * 4 + 1] == 10,
        "composite grays its raw children"
    );
    composite.encoding = OA_SPRITE_ROW_RLE;
    std::fill(surface.pixels.begin(), surface.pixels.end(), uint8_t{10});
    oa::present::draw_sprite_gray(&surface.surface, &composite, 1, 0);
    check(surface.pixels[0 * 4 + 0] == 10, "a composite marked row-RLE is skipped whole");
    oa::present::bind_display(nullptr);
}

void test_surface_rows_stream() {
    auto source = oa::present::create_surface(3, 2);
    source.pixels = {1, 2, 3, 4, 5, 6};
    source.surface.pixels = source.pixels.data();
    oa::present::MemoryWriter writer;
    auto out = oa::present::memory_writer_stream(writer);
    check(oa::present::write_surface_rows(out, source.surface), "surface rows written");
    check(
        writer.bytes.size() == 14 && writer.bytes[0] == 3 && writer.bytes[4] == 2,
        "surface row header"
    );
    oa::present::MemoryReader reader{writer.bytes};
    auto in = oa::present::memory_reader_stream(reader);
    oa::present::SurfaceBuffer loaded;
    check(
        oa::present::read_surface_rows(in, loaded) && loaded.pixels == source.pixels,
        "surface rows round trip"
    );
    oa::present::MemoryReader truncated{std::span(writer.bytes).first(10)};
    auto short_in = oa::present::memory_reader_stream(truncated);
    check(!oa::present::read_surface_rows(short_in, loaded), "short surface rows rejected");
}

void test_start_display() {
    using namespace oa::present;
    DisplayContext display{};
    oa::Surface previous{};
    display.offscreen = &previous;
    display.use_active_surface = 1;
    display.startup_request = 0x3f2;
    check(start_display(display) == 1, "session display starts");
    // start_display shifts request bits 1..9 and adds the session bit.
    check(display.flags == 0x7e5, "request 3f2 becomes flags 7e5");
    check(
        display.offscreen == nullptr && display.use_active_surface == 0,
        "startup clears offsets 98 and dc"
    );
    check(
        display.alpha_table && display.shade_table && display.light_table && display.gray_table &&
            display.blue_table,
        "session allocates all five tables"
    );
    shutdown_display(display);

    uint8_t unused{};
    display = DisplayContext{};
    display.shade_table = &unused;
    display.light_table = &unused;
    check(start_display(display) == 1 && display.flags == 1, "zero request keeps only session");
    check(
        display.shade_table == nullptr && display.light_table == nullptr,
        "unrequested shade and light pointers are cleared"
    );
    shutdown_display(display);
}

} // namespace

int main() {
    test_start_display();
    test_surface_rows_stream();
    test_surface_helpers();
    test_display_sink();
    test_cursor_overlay();
    test_rle_round_trip();
    test_text();
    test_text_colors();
    test_draw_text();
    test_fill_clipped_rect();
    test_sprite_gray();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("display tests passed");
    return 0;
}
