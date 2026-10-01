// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/model/model_library.hpp"
#include "oa/present/model/rgb_bridge.hpp"
#include "oa/present/blit.hpp"
#include "oa/present/polygon.hpp"

#include <cstdint>
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

// A small linear congruential sequence, so that the bookkeeping tests draw
// the same frames on every platform.
struct Sequence {
    uint32_t state{};

    uint32_t next(uint32_t bound) {
        state = state * 1664525U + 1013904223U;
        return (state >> 8) % bound;
    }
};

// Writes a few frame pixels as a sprite drawn straight into the frame
// would: palette colours, and colours outside the palette.
void scribble(Frame& frame, Sequence& sequence, const oa::Palette& palette) {
    for (int i = 0; i < 40; ++i) {
        uint8_t* pixel =
            frame.rgb.data() + static_cast<std::ptrdiff_t>(
                                   sequence.next(static_cast<uint32_t>(frame.width * frame.height))
                               ) * 3;
        if (sequence.next(2) == 0) {
            const oa::PaletteEntry& entry = palette.entries[sequence.next(OA_PALETTE_COLORS)];
            pixel[0] = entry.r;
            pixel[1] = entry.g;
            pixel[2] = entry.b;
        } else {
            pixel[0] = static_cast<uint8_t>(sequence.next(256));
            pixel[1] = static_cast<uint8_t>(sequence.next(256));
            pixel[2] = static_cast<uint8_t>(sequence.next(256));
        }
    }
}

// The palette of ramp_palette with its upper half repeating its lower half,
// so that colours have more than one entry.
oa::Palette repeating_palette() {
    oa::Palette palette = ramp_palette();
    for (int i = OA_PALETTE_COLORS / 2; i < OA_PALETTE_COLORS; ++i)
        palette.entries[i] = palette.entries[i - OA_PALETTE_COLORS / 2];
    return palette;
}

// A bridge kept from one capture to the next captures the same indices,
// and writes back the same frame, as a bridge that maps every pixel afresh,
// whatever was drawn into the frame between captures, at every scale and
// with the rectangle inside the frame or past its edges.
void test_copy_matches_fresh_capture() {
    struct Layout {
        oa::Rect32 area;
        float scale;
    };

    const Layout layouts[] = {
        {{0, 0, 69, 49}, 1.0F},
        {{6, 4, 61, 45}, 1.0F},
        {{-5, -3, 74, 52}, 1.0F},
        {{0, 0, 69, 49}, 2.0F},
        {{0, 0, 69, 49}, 0.5F},
        {{3, 2, 66, 47}, 1.5F},
        {{0, 0, 69, 49}, 0.75F},
    };
    const oa::Palette palette = repeating_palette();
    for (const Layout& layout : layouts) {
        Sequence sequence{static_cast<uint32_t>(layout.area.x1 * 7 + layout.scale * 100)};
        Frame kept_frame(70, 50, palette.entries[20]);
        scribble(kept_frame, sequence, palette);
        Frame fresh_frame = kept_frame;
        RgbBridge kept;
        bool all_equal = true;
        for (int step = 0; step < 60; ++step) {
            // Every few steps a new frame starts, as each drawn frame does.
            if (step % 7 == 0)
                bridge_begin(kept, kept_frame.view(), layout.area, layout.scale, palette);
            RgbBridge fresh;
            bridge_begin(fresh, fresh_frame.view(), layout.area, layout.scale, palette);
            const int32_t width = kept.surface.width;
            const int32_t height = kept.surface.height;
            const auto x = static_cast<int32_t>(sequence.next(static_cast<uint32_t>(width)));
            const auto y = static_cast<int32_t>(sequence.next(static_cast<uint32_t>(height)));
            const oa::Rect32 region{
                x - 20, y - 15, x + static_cast<int32_t>(sequence.next(40)), y + 12
            };
            bridge_open(kept, region);
            bridge_open(fresh, region);
            const oa::Rect32& clip = kept.surface.clip;
            for (int32_t row = clip.y1; row <= clip.y2; ++row)
                for (int32_t column = clip.x1; column <= clip.x2; ++column)
                    all_equal =
                        all_equal && kept.surface.pixels[row * kept.surface.pitch + column] ==
                                         fresh.surface.pixels[row * fresh.surface.pitch + column];
            // Draws: some pixels of the clip take new indices, some the
            // index they hold.
            for (int i = 0; i < 300 && clip.x1 <= clip.x2 && clip.y1 <= clip.y2; ++i) {
                const auto column =
                    clip.x1 + static_cast<int32_t>(
                                  sequence.next(static_cast<uint32_t>(clip.x2 - clip.x1 + 1))
                              );
                const auto row =
                    clip.y1 + static_cast<int32_t>(
                                  sequence.next(static_cast<uint32_t>(clip.y2 - clip.y1 + 1))
                              );
                const auto value = static_cast<uint8_t>(sequence.next(OA_PALETTE_COLORS));
                kept.surface.pixels[row * kept.surface.pitch + column] = value;
                fresh.surface.pixels[row * fresh.surface.pitch + column] = value;
            }
            bridge_end(kept);
            bridge_end(fresh);
            all_equal = all_equal && kept_frame.rgb == fresh_frame.rgb;
            Sequence copy = sequence;
            scribble(kept_frame, sequence, palette);
            scribble(fresh_frame, copy, palette);
        }
        CHECK(all_equal);
    }
}

// A capture maps again only the frame pixels whose colour changed; pixels
// written back at a scale of 1 are already up to date, and a new frame of
// the same layout keeps the copy.
void test_copy_maps_only_changes() {
    const oa::Palette palette = ramp_palette();
    Frame frame(64, 64, palette.entries[9]);
    RgbBridge bridge;
    bridge_begin(bridge, frame.view(), {0, 0, 63, 63}, 1.0F, palette);
    const uint64_t at_start = bridge.copy.mapped_pixels;
    bridge_open(bridge, {0, 0, 63, 63});
    CHECK(bridge.copy.mapped_pixels - at_start == 64 * 64);
    bridge_end(bridge);
    bridge_open(bridge, {0, 0, 63, 63});
    CHECK(bridge.copy.mapped_pixels - at_start == 64 * 64);
    // Five pixels drawn and written back.
    for (int i = 0; i < 5; ++i)
        bridge.surface.pixels[i * 70] = 77;
    bridge_end(bridge);
    bridge_open(bridge, {0, 0, 63, 63});
    CHECK(bridge.copy.mapped_pixels - at_start == 64 * 64);
    CHECK(bridge.surface.pixels[70] == 77);
    bridge_end(bridge);
    // Three pixels drawn straight into the frame, one outside the palette.
    frame.rgb[0] = 1;
    frame.rgb[static_cast<std::size_t>((10 * 64 + 10) * 3)] = 200;
    frame.rgb[static_cast<std::size_t>((63 * 64 + 63) * 3 + 2)] = 3;
    bridge_begin(bridge, frame.view(), {0, 0, 63, 63}, 1.0F, palette);
    bridge_open(bridge, {0, 0, 63, 63});
    CHECK(bridge.copy.mapped_pixels - at_start == 64 * 64 + 3);
    CHECK(bridge.surface.pixels[10 * 64 + 10] == bridge_index(bridge, 200, 255 - 9, 9 / 2));
    bridge_end(bridge);
    // A region captured as samples maps nothing that has not changed.
    SampledRegion sampled;
    bridge_open_sampled(bridge, sampled, {0, 0, 63, 63}, 2);
    CHECK(bridge.copy.mapped_pixels - at_start == 64 * 64 + 3);
    CHECK(
        sampled.surface.pixels[0] ==
        bridge_index(bridge, frame.at(0, 0)[0], frame.at(0, 0)[1], frame.at(0, 0)[2])
    );
    // Another palette maps every pixel again.
    oa::Palette other = palette;
    other.entries[0] = {1, 2, 3, 0};
    bridge_begin(bridge, frame.view(), {0, 0, 63, 63}, 1.0F, other);
    bridge_open(bridge, {0, 0, 63, 63});
    CHECK(bridge.copy.mapped_pixels - at_start == 2 * 64 * 64 + 3);
    bridge_end(bridge);
}

// At a scale of 2 a pixel written back is mapped again at the next capture,
// from the frame pixel it captures from.
void test_copy_scaled_write_back() {
    const oa::Palette palette = ramp_palette();
    Frame frame(40, 40, palette.entries[3]);
    RgbBridge bridge;
    bridge_begin(bridge, frame.view(), {0, 0, 39, 39}, 2.0F, palette);
    bridge_open(bridge, {0, 0, 19, 19});
    const uint64_t captured = bridge.copy.mapped_pixels;
    CHECK(captured == 20 * 20);
    bridge.surface.pixels[5 * bridge.surface.pitch + 5] = 50;
    bridge_end(bridge);
    bridge_open(bridge, {0, 0, 19, 19});
    CHECK(bridge.copy.mapped_pixels == captured + 1);
    CHECK(bridge.surface.pixels[5 * bridge.surface.pitch + 5] == 50);
    bridge_end(bridge);
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
    test_copy_matches_fresh_capture();
    test_copy_maps_only_changes();
    test_copy_scaled_write_back();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("rgb bridge tests passed");
    return EXIT_SUCCESS;
}
