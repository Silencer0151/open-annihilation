// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/model/model_library.hpp"
#include "oa/present/model/rgb_bridge.hpp"
#include "oa/present/blit.hpp"
#include "oa/present/polygon.hpp"
#include "oa/present/raster.hpp"
#include "oa/present/scene_grid.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>
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

// At a scale zoomed out and one zoomed in, every frame pixel is written back
// from the 8-bit pixel of the map pixel the scene grid shows there, the one
// the terrain fill shows there too, across a frame as wide as a large
// window's scene.
void test_written_on_the_scene_grid() {
    const oa::Palette palette = ramp_palette();
    constexpr int frame_width = 2400;
    constexpr int frame_height = 6;
    // The index an 8-bit pixel is drawn with: never the frame's own.
    const auto drawn = [](int x, int y) {
        return static_cast<uint8_t>(20 + (x * 7 + y * 3) % 200);
    };
    for (const float scale : {0.37F, 1.37F}) {
        Frame frame(frame_width, frame_height, palette.entries[3]);
        RgbBridge bridge;
        bridge_begin(
            bridge, frame.view(), {0, 0, frame_width - 1, frame_height - 1}, scale, palette
        );
        bridge_open(bridge, {0, 0, bridge.surface.width - 1, bridge.surface.height - 1});
        for (int y = 0; y < bridge.surface.height; ++y)
            for (int x = 0; x < bridge.surface.width; ++x)
                bridge.surface.pixels[static_cast<std::ptrdiff_t>(y) * bridge.surface.pitch + x] =
                    drawn(x, y);
        bridge_end(bridge);
        const uint32_t step = oa::present::scene_step(scale);
        int misplaced = 0;
        for (int y = 0; y < frame_height; ++y)
            for (int x = 0; x < frame_width; ++x) {
                const auto shown_x = static_cast<int>(oa::present::map_pixel_shown(x, step));
                const auto shown_y = static_cast<int>(oa::present::map_pixel_shown(y, step));
                misplaced +=
                    is_entry(frame.at(x, y), palette.entries[drawn(shown_x, shown_y)]) ? 0 : 1;
            }
        CHECK(misplaced == 0);
        if (misplaced != 0)
            std::fprintf(
                stderr,
                "scale %g: %d frame pixels written from another map pixel\n",
                static_cast<double>(scale),
                misplaced
            );
    }
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

// One step of a frame drawn through a bridge, worked out once so that it can
// be drawn whole and band by band.
struct BridgeStep {
    enum class Kind : uint8_t { open, polygon, line, blended, commit, scribble, sampled };

    // Rect32 is packed: aligned here, since the bridge takes its fields by reference.
    alignas(4) oa::Rect32 region{};
    Kind kind{};
    std::vector<oa::present::PolygonVertex> corners;
    uint8_t color{};
    int32_t x0{};
    int32_t y0{};
    int32_t x1{};
    int32_t y1{};
    uint32_t factor{1};
    /// Frame pixels written straight into the frame: offset and colour.
    std::vector<std::pair<std::size_t, oa::PaletteEntry>> pixels;
};

// Works out the steps of one frame: captures, polygons, lines and blended
// sprites in the bridge, write-backs, writes straight into the frame and
// sampled regions.
std::vector<BridgeStep>
frame_steps(Sequence& sequence, int32_t width, int32_t height, const Frame& frame) {
    std::vector<BridgeStep> steps;
    for (int i = 0; i < 24; ++i) {
        BridgeStep step;
        step.kind = static_cast<BridgeStep::Kind>(sequence.next(7));
        const auto x = static_cast<int32_t>(sequence.next(static_cast<uint32_t>(width + 40))) - 20;
        const auto y = static_cast<int32_t>(sequence.next(static_cast<uint32_t>(height + 40))) - 20;
        step.region = {
            x,
            y,
            x + static_cast<int32_t>(sequence.next(50)),
            y + static_cast<int32_t>(sequence.next(50))
        };
        step.color = static_cast<uint8_t>(sequence.next(OA_PALETTE_COLORS));
        step.factor = 1 + sequence.next(4);
        // Corners in any order, around the region and past it.
        const int corners = 3 + static_cast<int>(sequence.next(3));
        for (int corner = 0; corner < corners; ++corner)
            step.corners.push_back(
                {x - 10 + static_cast<int32_t>(sequence.next(70)),
                 y - 10 + static_cast<int32_t>(sequence.next(70))}
            );
        step.x0 = x;
        step.y0 = y;
        step.x1 = x + static_cast<int32_t>(sequence.next(60)) - 30;
        step.y1 = y + static_cast<int32_t>(sequence.next(60)) - 30;
        for (int pixel = 0; step.kind == BridgeStep::Kind::scribble && pixel < 40; ++pixel)
            step.pixels.push_back(
                {sequence.next(static_cast<uint32_t>(frame.width * frame.height)),
                 {static_cast<uint8_t>(sequence.next(256)),
                  static_cast<uint8_t>(sequence.next(256)),
                  static_cast<uint8_t>(sequence.next(256)),
                  0}}
            );
        steps.push_back(std::move(step));
    }
    return steps;
}

// Draws a frame's steps through the whole bridge (no band) or one band.
void draw_steps(
    RgbBridge& bridge,
    BridgeBand* band,
    SampledRegion& sampled,
    Frame& frame,
    const std::vector<BridgeStep>& steps
) {
    oa::Surface& surface = band != nullptr ? band->surface : bridge.surface;
    std::vector<uint8_t> shadow(36, 0);
    oa::Sprite sprite{};
    sprite.width = 6;
    sprite.height = 6;
    sprite.origin_x = 3;
    sprite.origin_y = 3;
    sprite.key = 1;
    sprite.data = shadow.data();
    for (const BridgeStep& step : steps) {
        switch (step.kind) {
        case BridgeStep::Kind::open:
            band != nullptr ? bridge_open(bridge, *band, step.region)
                            : bridge_open(bridge, step.region);
            break;
        case BridgeStep::Kind::polygon:
            oa::present::fill_polygon(
                &surface, step.corners.data(), static_cast<int32_t>(step.corners.size()), step.color
            );
            break;
        case BridgeStep::Kind::line:
            oa::present::draw_clipped_line(
                &surface, step.x0, step.y0, step.x1, step.y1, step.color
            );
            break;
        case BridgeStep::Kind::blended:
            oa::present::draw_sprite_blended(&surface, &sprite, step.x0, step.y0);
            break;
        case BridgeStep::Kind::commit:
            band != nullptr ? bridge_end(bridge, *band) : bridge_end(bridge);
            break;
        case BridgeStep::Kind::scribble:
            for (const auto& [offset, colour] : step.pixels) {
                const auto row =
                    static_cast<int32_t>(offset / static_cast<std::size_t>(frame.width));
                if (band != nullptr && (row < band->frame_first_row || row >= band->frame_end_row))
                    continue;
                uint8_t* pixel = frame.rgb.data() + offset * 3;
                pixel[0] = colour.r;
                pixel[1] = colour.g;
                pixel[2] = colour.b;
            }
            break;
        case BridgeStep::Kind::sampled: {
            band != nullptr ? bridge_end(bridge, *band) : bridge_end(bridge);
            if (band != nullptr)
                bridge_open_sampled(bridge, *band, sampled, step.region, step.factor);
            else
                bridge_open_sampled(bridge, sampled, step.region, step.factor);
            std::vector<oa::present::PolygonVertex> finer = step.corners;
            for (auto& corner : finer) {
                corner.x = (corner.x - sampled.region.x1) * static_cast<int32_t>(sampled.factor);
                corner.y = (corner.y - sampled.region.y1) * static_cast<int32_t>(sampled.factor);
            }
            oa::present::fill_polygon(
                &sampled.surface, finer.data(), static_cast<int32_t>(finer.size()), step.color
            );
            if (band != nullptr)
                bridge_end_sampled(bridge, *band, sampled);
            else
                bridge_end_sampled(bridge, sampled);
            break;
        }
        }
    }
}

// A frame drawn band by band through the bridge, each band drawing every
// step with its rows alone, comes out as the frame drawn whole, frame after
// frame with the capture copy kept, at every scale, whatever the number of
// bands; and the bands split the surface and the frame between them.
void test_bands_draw_the_whole() {
    struct Layout {
        int32_t width;
        int32_t height;
        oa::Rect32 area;
        float scale;
    };

    const Layout layouts[] = {
        {140, 230, {0, 0, 139, 229}, 1.0F},
        {150, 200, {0, 0, 149, 199}, 2.0F},
        {120, 150, {0, 0, 119, 149}, 0.5F},
        {130, 210, {0, 0, 129, 209}, 0.75F},
        {120, 260, {0, 0, 119, 259}, 1.1F},
        {120, 250, {0, 0, 119, 249}, 1.5F},
        {100, 140, {0, 0, 99, 139}, 0.3F},
        {110, 200, {4, 6, 105, 190}, 1.0F},
    };
    const oa::Palette palette = repeating_palette();
    ModelDisplay display;
    build_model_display(display, palette);
    oa::present::DisplayContext* previous = oa::present::display_context();
    oa::present::bind_display(&display.context);
    for (const Layout& layout : layouts) {
        for (const int32_t count : {2, 3, 4, 7}) {
            Sequence sequence{static_cast<uint32_t>(layout.width * 31 + count)};
            Frame whole(layout.width, layout.height, palette.entries[20]);
            scribble(whole, sequence, palette);
            Frame banded = whole;
            RgbBridge whole_bridge;
            RgbBridge banded_bridge;
            SampledRegion sampled;
            std::vector<BridgeBand> bands;
            bool all_equal = true;
            int32_t most_bands = 0;
            for (int frame = 0; frame < 6; ++frame) {
                bridge_begin(whole_bridge, whole.view(), layout.area, layout.scale, palette);
                bridge_begin(banded_bridge, banded.view(), layout.area, layout.scale, palette);
                const auto steps = frame_steps(
                    sequence, whole_bridge.surface.width, whole_bridge.surface.height, whole
                );
                draw_steps(whole_bridge, nullptr, sampled, whole, steps);
                const int32_t made = bridge_split(banded_bridge, count, bands);
                most_bands = std::max(most_bands, made);
                CHECK(made >= 1 && made <= count);
                CHECK(bands.front().first_row == 0 && bands.front().frame_first_row == 0);
                CHECK(bands.back().end_row == banded_bridge.surface.height);
                CHECK(bands.back().frame_end_row == layout.height);
                for (std::size_t index = 0; index + 1 < bands.size(); ++index) {
                    CHECK(bands[index].end_row == bands[index + 1].first_row);
                    CHECK(bands[index].end_row % bridge_tile_side == 0);
                    CHECK(bands[index].frame_end_row == bands[index + 1].frame_first_row);
                    CHECK(bands[index].frame_first_row < bands[index].frame_end_row);
                }
                for (BridgeBand& band : bands) {
                    draw_steps(banded_bridge, &band, sampled, banded, steps);
                    bridge_join_band(banded_bridge, band);
                }
                all_equal = all_equal && whole.rgb == banded.rgb;
            }
            CHECK(all_equal);
            if (!all_equal)
                std::fprintf(
                    stderr,
                    "scale %g, %d bands: banded frames differ\n",
                    static_cast<double>(layout.scale),
                    count
                );
            // At a scale of 1 every tile row can start a band.
            if (layout.scale == 1.0F)
                CHECK(most_bands == std::min(count, banded_bridge.tiles_y));
        }
    }
    oa::present::bind_display(previous);
}

// A band with colour memory of its own forgets the nearest entries it
// remembered once the bridge's colour lookup is built afresh for another
// palette, and looks colours up as the bridge does.
void test_band_colours_follow_the_palette() {
    const oa::Palette first = ramp_palette();
    oa::Palette second = ramp_palette();
    for (int i = 0; i < OA_PALETTE_COLORS; ++i)
        second.entries[i] = first.entries[OA_PALETTE_COLORS - 1 - i];
    Frame frame(64, 96, first.entries[3]);
    RgbBridge bridge;
    std::vector<BridgeBand> bands;
    bridge_begin(bridge, frame.view(), {0, 0, 63, 95}, 1.0F, first);
    CHECK(bridge_split(bridge, 3, bands) == 3);
    bridge_band_colours(bridge, bands[1]);
    // A colour outside both palettes, remembered by the band under the first.
    const uint8_t remembered = bridge_index(bridge, bands[1], 41, 214, 21);
    CHECK(remembered == bridge_index(bridge, 41, 214, 21));
    bridge_begin(bridge, frame.view(), {0, 0, 63, 95}, 1.0F, second);
    CHECK(bridge_split(bridge, 3, bands) == 3);
    bridge_band_colours(bridge, bands[1]);
    const uint8_t under_second = bridge_index(bridge, 41, 214, 21);
    CHECK(under_second != remembered);
    CHECK(bridge_index(bridge, bands[1], 41, 214, 21) == under_second);
}

} // namespace

int main() {
    test_index_lookup();
    test_draw_and_commit();
    test_blended_draw();
    test_scaled();
    test_written_on_the_scene_grid();
    test_nearest_remembered();
    test_sampled_untouched();
    test_sampled_coverage();
    test_sampled_scaled();
    test_copy_matches_fresh_capture();
    test_copy_maps_only_changes();
    test_copy_scaled_write_back();
    test_bands_draw_the_whole();
    test_band_colours_follow_the_palette();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("rgb bridge tests passed");
    return EXIT_SUCCESS;
}
