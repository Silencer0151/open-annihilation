// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/model/model_library.hpp"
#include "oa/present/model/rgb_bridge.hpp"
#include "oa/present/blit.hpp"
#include "oa/present/polygon.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int failures = 0;

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x);             \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

using namespace oa::present::model;

// Entry i is (i, 255 - i, i / 2).
oa::Palette ramp_palette() {
    oa::Palette palette{};
    for (int i = 0; i < OA_PALETTE_COLORS; ++i)
        palette.entries[i] = {
            static_cast<uint8_t>(i), static_cast<uint8_t>(255 - i), static_cast<uint8_t>(i / 2), 0
        };
    return palette;
}

struct Frame {
    std::vector<uint8_t> rgb;
    int width;
    int height;

    Frame(int w, int h, const oa::PaletteEntry& fill)
        : rgb(static_cast<std::size_t>(w * h * 3)), width(w), height(h) {
        for (int i = 0; i < w * h; ++i) {
            rgb[static_cast<std::size_t>(i * 3)] = fill.r;
            rgb[static_cast<std::size_t>(i * 3 + 1)] = fill.g;
            rgb[static_cast<std::size_t>(i * 3 + 2)] = fill.b;
        }
    }

    RgbFrame view() { return {rgb.data(), width, height, width * 3}; }

    const uint8_t* at(int x, int y) const {
        return rgb.data() + static_cast<std::ptrdiff_t>((y * width + x) * 3);
    }
};

bool is_entry(const uint8_t* pixel, const oa::PaletteEntry& entry) {
    return pixel[0] == entry.r && pixel[1] == entry.g && pixel[2] == entry.b;
}

void test_index_lookup() {
    RgbBridge bridge;
    const oa::Palette palette = ramp_palette();
    Frame frame(8, 8, palette.entries[0]);
    bridge_begin(bridge, frame.view(), {0, 0, 7, 7}, 1.0F, palette);
    CHECK(bridge_index(bridge, 40, 215, 20) == 40);
    CHECK(bridge_index(bridge, 255, 0, 127) == 255);
    // Off-palette colours resolve to the nearest entry.
    CHECK(bridge_index(bridge, 41, 214, 21) == 41);
}

// Draws land only inside the opened region; untouched pixels keep their RGB.
void test_draw_and_commit() {
    const oa::Palette palette = ramp_palette();
    const oa::PaletteEntry odd{7, 9, 11, 0}; // not in the palette
    Frame frame(80, 60, odd);
    RgbBridge bridge;
    bridge_begin(bridge, frame.view(), {10, 5, 69, 54}, 1.0F, palette);
    CHECK(bridge.surface.width == 60);
    CHECK(bridge.surface.height == 50);
    bridge_open(bridge, {0, 0, 19, 19});
    const oa::present::PolygonVertex square[] = {{5, 5}, {31, 5}, {31, 31}, {5, 31}};
    oa::present::fill_polygon(&bridge.surface, square, 4, 100);
    bridge_end(bridge);
    CHECK(is_entry(frame.at(15, 10), palette.entries[100]));
    // The polygon filler leaves the clip's last row and column empty.
    CHECK(is_entry(frame.at(28, 23), palette.entries[100]));
    CHECK(is_entry(frame.at(29, 24), odd));
    CHECK(is_entry(frame.at(30, 25), odd));
    CHECK(is_entry(frame.at(14, 9), odd));
    CHECK(is_entry(frame.at(5, 5), odd));
}

// Blended draws read the captured indices of the frame underneath.
void test_blended_draw() {
    const oa::Palette palette = ramp_palette();
    ModelDisplay display;
    build_model_display(display, palette);
    oa::present::DisplayContext* previous = oa::present::display_context();
    oa::present::bind_display(&display.context);
    Frame frame(32, 32, palette.entries[200]);
    RgbBridge bridge;
    bridge_begin(bridge, frame.view(), {0, 0, 31, 31}, 1.0F, palette);
    bridge_open(bridge, {0, 0, 31, 31});
    std::vector<uint8_t> shadow(16, 0);
    oa::Sprite sprite{};
    sprite.width = 4;
    sprite.height = 4;
    sprite.key = 1;
    sprite.data = shadow.data();
    oa::present::draw_sprite_blended(&bridge.surface, &sprite, 10, 10);
    bridge_end(bridge);
    const uint8_t blended = display.alpha[0 * 256 + 200];
    CHECK(is_entry(frame.at(11, 11), palette.entries[blended]));
    CHECK(is_entry(frame.at(20, 20), palette.entries[200]));
    oa::present::bind_display(previous);
}

// With a scale of 2 every 8-bit pixel covers a 2x2 block of the frame.
void test_scaled() {
    const oa::Palette palette = ramp_palette();
    Frame frame(40, 40, palette.entries[3]);
    RgbBridge bridge;
    bridge_begin(bridge, frame.view(), {0, 0, 39, 39}, 2.0F, palette);
    CHECK(bridge.surface.width == 20);
    bridge_open(bridge, {0, 0, 19, 19});
    const oa::present::PolygonVertex square[] = {{4, 4}, {5, 4}, {5, 5}, {4, 5}};
    oa::present::fill_polygon(&bridge.surface, square, 4, 50);
    bridge_end(bridge);
    CHECK(is_entry(frame.at(8, 8), palette.entries[50]));
    CHECK(is_entry(frame.at(9, 9), palette.entries[50]));
    CHECK(is_entry(frame.at(10, 9), palette.entries[3]));
    CHECK(is_entry(frame.at(7, 8), palette.entries[3]));
}

} // namespace

int main() {
    test_index_lookup();
    test_draw_and_commit();
    test_blended_draw();
    test_scaled();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("rgb bridge tests passed");
    return EXIT_SUCCESS;
}
