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

// Colours outside the palette resolve to the same entry however often they
// are looked up, and whatever other colours took their slot in between.
void test_nearest_remembered() {
    RgbBridge bridge;
    const oa::Palette palette = ramp_palette();
    Frame frame(8, 8, palette.entries[0]);
    bridge_begin(bridge, frame.view(), {0, 0, 7, 7}, 1.0F, palette);
    const uint8_t first = bridge_index(bridge, 41, 214, 21);
    for (int r = 0; r < 256; ++r)
        for (int b = 0; b < 256; b += 3)
            (void)bridge_index(bridge, static_cast<uint8_t>(r), 7, static_cast<uint8_t>(b));
    CHECK(bridge_index(bridge, 41, 214, 21) == first);
    CHECK(first == 41);
}

// A sampled region starts as the captured indices under it, and with nothing
// drawn leaves the frame as it was, colours outside the palette included.
void test_sampled_untouched() {
    const oa::Palette palette = ramp_palette();
    const oa::PaletteEntry odd{7, 9, 11, 0};
    Frame frame(20, 20, odd);
    const std::vector<uint8_t> before = frame.rgb;
    RgbBridge bridge;
    bridge_begin(bridge, frame.view(), {0, 0, 19, 19}, 1.0F, palette);
    SampledRegion sampled;
    bridge_open_sampled(bridge, sampled, {-3, 2, 5, 6}, 4);
    CHECK(sampled.region.x1 == 0);
    CHECK(sampled.region.x2 == 5);
    CHECK(sampled.surface.width == 24);
    CHECK(sampled.surface.height == 20);
    const uint8_t under = bridge_index(bridge, odd.r, odd.g, odd.b);
    CHECK(sampled.surface.pixels[0] == under);
    CHECK(sampled.surface.pixels[24 * 20 - 1] == under);
    bridge_end_sampled(bridge, sampled);
    CHECK(frame.rgb == before);
    // A region off the surface covers nothing and writes nothing.
    bridge_open_sampled(bridge, sampled, {30, 30, 40, 40}, 4);
    CHECK(sampled.region.x1 > sampled.region.x2);
    bridge_end_sampled(bridge, sampled);
    CHECK(frame.rgb == before);
}

// A bridge pixel drawn all over in one index takes that palette colour; one
// drawn in part takes the average of its samples, those not drawn counting
// as the frame's own colour, rounded to nearest.
void test_sampled_coverage() {
    const oa::Palette palette = ramp_palette();
    const oa::PaletteEntry odd{7, 9, 11, 0};
    Frame frame(20, 20, odd);
    RgbBridge bridge;
    bridge_begin(bridge, frame.view(), {0, 0, 19, 19}, 1.0F, palette);
    SampledRegion sampled;
    bridge_open_sampled(bridge, sampled, {2, 2, 9, 9}, 4);
    const auto sample = [&](int x, int y) -> uint8_t& {
        return sampled.surface.pixels[y * sampled.surface.pitch + x];
    };
    // Bridge pixel (2, 2) in full; (3, 2) in its left half; (4, 2) in one
    // sample of 50 and one of 60.
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 6; ++x)
            sample(x, y) = 100;
    sample(8, 0) = 50;
    sample(11, 3) = 60;
    bridge_end_sampled(bridge, sampled);
    CHECK(is_entry(frame.at(2, 2), palette.entries[100]));
    const oa::PaletteEntry& ink = palette.entries[100];
    CHECK(frame.at(3, 2)[0] == (ink.r * 8 + odd.r * 8 + 8) / 16);
    CHECK(frame.at(3, 2)[1] == (ink.g * 8 + odd.g * 8 + 8) / 16);
    CHECK(frame.at(3, 2)[2] == (ink.b * 8 + odd.b * 8 + 8) / 16);
    const oa::PaletteEntry& a = palette.entries[50];
    const oa::PaletteEntry& b = palette.entries[60];
    CHECK(frame.at(4, 2)[0] == (a.r + b.r + odd.r * 14 + 8) / 16);
    CHECK(frame.at(4, 2)[1] == (a.g + b.g + odd.g * 14 + 8) / 16);
    CHECK(is_entry(frame.at(5, 2), odd));
    CHECK(is_entry(frame.at(2, 3), odd));
}

// With a scale of 2 each frame pixel of a bridge pixel blends with its own
// colour, and the frame pixels map to bridge pixels as bridge_end maps them.
void test_sampled_scaled() {
    const oa::Palette palette = ramp_palette();
    Frame frame(40, 40, palette.entries[3]);
    frame.rgb[static_cast<std::size_t>((9 * 40 + 9) * 3)] = 200; // one pixel off the palette
    RgbBridge bridge;
    bridge_begin(bridge, frame.view(), {0, 0, 39, 39}, 2.0F, palette);
    SampledRegion sampled;
    bridge_open_sampled(bridge, sampled, {4, 4, 4, 4}, 2);
    CHECK(sampled.surface.width == 2);
    sampled.surface.pixels[0] = 50;
    sampled.surface.pixels[1] = 50;
    bridge_end_sampled(bridge, sampled);
    const oa::PaletteEntry& ink = palette.entries[50];
    const oa::PaletteEntry& ground = palette.entries[3];
    CHECK(frame.at(8, 8)[1] == (ink.g * 2 + ground.g * 2 + 2) / 4);
    CHECK(frame.at(9, 8)[1] == frame.at(8, 8)[1]);
    CHECK(frame.at(9, 9)[0] == (ink.r * 2 + 200 * 2 + 2) / 4);
    CHECK(is_entry(frame.at(10, 9), ground));
    CHECK(is_entry(frame.at(7, 8), ground));
}

} // namespace

int main() {
    test_index_lookup();
    test_draw_and_commit();
    test_blended_draw();
    test_scaled();
    test_nearest_remembered();
    test_sampled_untouched();
    test_sampled_coverage();
    test_sampled_scaled();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("rgb bridge tests passed");
    return EXIT_SUCCESS;
}
