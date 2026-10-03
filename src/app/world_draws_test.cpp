// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The battlefield's thin lines drawn band by band (world_draws.hpp): lines
// of the frame (lasers, lightning) and selection lines of the model bridge,
// drawn 2 pixels thick, as the accelerated tier draws them in a scene the
// area pass reduces, cover each of the line's pixels and the pixels right
// of, below and below and right of it, across the edges of the bands and
// of the bridge's tiles, byte for byte the same on 1 to 8 bands, each band
// on a drawing thread of its own. Drawn 1 pixel thick they cover the line's
// own pixels, as the game always draws them. A feature's shadow frame,
// drawn or blended, mixes from the ground toward the game's shadow by the
// frame's shadow level and is not drawn at level 0; a frame's shadows are
// set from its zoom (set_frame_shadows).
#include "world_draws.hpp"

#include "oa/platform/job_pool.hpp"
#include "oa/present/model/model_library.hpp"
#include "oa/present/surface.hpp"
#include "oa/test/check.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

namespace model_render = oa::present::model;
namespace job_pool = oa::platform::job_pool;
using oa::app::WorldDrawKind;

/// The frame: eight rows of the bridge's tiles down, so that it splits into
/// as many as eight bands.
constexpr int32_t frame_width = 96;
constexpr int32_t frame_height = 8 * model_render::bridge_tile_side;
/// The most bands the frame is drawn in.
constexpr int32_t most_bands = 8;

/// The palette entry the frame is filled with, and the one selection lines
/// are drawn in.
constexpr uint8_t ground_index = 0;
constexpr uint8_t selection_index = 200;
/// The colour of the frame's lines.
constexpr std::array<uint8_t, 3> line_colour{0xF0, 0x20, 0x60};

/// A line from its first pixel to its last.
struct Line {
    int32_t x0{};
    int32_t y0{};
    int32_t x1{};
    int32_t y1{};
};

/// The lines: one down the whole frame, across every band's edge; one along
/// the last row of each tile row but the last, on every edge a band can
/// have; and two on the diagonal, across edges.
std::vector<Line> test_lines() {
    std::vector<Line> lines{{10, 0, 10, frame_height - 1}, {40, 0, 90, 50}, {30, 100, 90, 160}};
    for (int32_t row = model_render::bridge_tile_side - 1; row < frame_height - 1;
         row += model_render::bridge_tile_side)
        lines.push_back({20, row, 70, row});
    return lines;
}

/// Returns a line's pixels: a line down, along a row or on the diagonal
/// steps one pixel at a time from its first pixel to its last.
///
/// @param line the line
/// @return its pixels, as column and row
std::vector<std::array<int32_t, 2>> line_pixels(const Line& line) {
    const int32_t step_x = line.x1 > line.x0 ? 1 : (line.x1 < line.x0 ? -1 : 0);
    const int32_t step_y = line.y1 > line.y0 ? 1 : (line.y1 < line.y0 ? -1 : 0);
    std::vector<std::array<int32_t, 2>> pixels;
    for (int32_t x = line.x0, y = line.y0;; x += step_x, y += step_y) {
        pixels.push_back({x, y});
        if (x == line.x1 && y == line.y1)
            break;
    }
    return pixels;
}

/// A palette whose every entry differs in all three bytes.
oa::PaletteBytes test_palette() {
    oa::PaletteBytes palette{};
    for (std::size_t index = 0; index < oa::palette_color_count; ++index) {
        palette[index * oa::palette_entry_bytes] = static_cast<uint8_t>(index);
        palette[index * oa::palette_entry_bytes + 1] = static_cast<uint8_t>(index * 7U + 3U);
        palette[index * oa::palette_entry_bytes + 2] = static_cast<uint8_t>(255U - index);
    }
    return palette;
}

/// Returns a palette entry's colour.
///
/// @param palette the palette
/// @param index the entry
/// @return its red, green and blue
std::array<uint8_t, 3> entry_colour(const oa::PaletteBytes& palette, uint8_t index) {
    const std::size_t at = std::size_t{index} * oa::palette_entry_bytes;
    return {palette[at], palette[at + 1], palette[at + 2]};
}

/// Returns a frame filled with the ground's colour.
///
/// @param palette the palette
/// @return the frame's RGB pixels
std::vector<uint8_t> ground_frame(const oa::PaletteBytes& palette) {
    const auto ground = entry_colour(palette, ground_index);
    std::vector<uint8_t> rgb(std::size_t{frame_width} * frame_height * 3U);
    for (std::size_t pixel = 0; pixel < rgb.size() / 3U; ++pixel)
        for (std::size_t channel = 0; channel < 3; ++channel)
            rgb[pixel * 3U + channel] = ground[channel];
    return rgb;
}

/// Returns the frame the lines must give: the ground, and the colour on
/// each line's pixels and, drawn thick, on those pixels moved right and
/// down by up to one less than the thickness.
///
/// @param palette the palette
/// @param colour the lines' colour
/// @param thickness pixels across and down each is drawn
/// @return the frame's RGB pixels
std::vector<uint8_t> expected_frame(
    const oa::PaletteBytes& palette, const std::array<uint8_t, 3>& colour, int32_t thickness
) {
    auto rgb = ground_frame(palette);
    for (const Line& line : test_lines())
        for (const auto& [x, y] : line_pixels(line))
            for (int32_t down = 0; down < thickness; ++down)
                for (int32_t across = 0; across < thickness; ++across) {
                    const int32_t column = x + across;
                    const int32_t row = y + down;
                    if (column < 0 || row < 0 || column >= frame_width || row >= frame_height)
                        continue;
                    const std::size_t at = (static_cast<std::size_t>(row) * frame_width +
                                            static_cast<std::size_t>(column)) *
                                           3U;
                    for (std::size_t channel = 0; channel < 3; ++channel)
                        rgb[at + channel] = colour[channel];
                }
    return rgb;
}

/// Draws the lines into a frame of the ground in bands, each band on a
/// drawing thread of its own, through draw_world_band.
///
/// @param palette the palette
/// @param kind WorldDrawKind::line or WorldDrawKind::selection_line
/// @param thickness the frame's line_thickness and bridge_line_thickness
/// @param bands the bands wanted
/// @return the frame's RGB pixels
std::vector<uint8_t>
draw_lines(const oa::PaletteBytes& palette, WorldDrawKind kind, int32_t thickness, int32_t bands) {
    auto rgb = ground_frame(palette);
    model_render::ModelDisplay display;
    model_render::build_model_display(display, oa::present::palette_from_bytes(palette));
    model_render::RgbBridge bridge;
    const model_render::RgbFrame frame{rgb.data(), frame_width, frame_height, frame_width * 3};
    model_render::bridge_begin(
        bridge, frame, {0, 0, frame_width - 1, frame_height - 1}, 1.0F, display.palette
    );
    oa::app::WorldDrawList list;
    for (const Line& line : test_lines()) {
        list.lines.push_back({line.x0, line.y0, line.x1, line.y1, line_colour, selection_index});
        oa::app::add_world_draw(list, kind, list.lines.size() - 1);
    }
    oa::app::add_world_draw(list, WorldDrawKind::commit_always, 0);
    oa::app::WorldFrameDraw draw;
    draw.target = {
        rgb.data(), frame_width, frame_height, 0, 0, frame_width, frame_height, 0, frame_height
    };
    draw.palette = &palette;
    draw.bridge = &bridge;
    draw.display = &display;
    draw.line_thickness = thickness;
    draw.bridge_line_thickness = thickness;
    std::vector<model_render::BridgeBand> split;
    const int32_t made = model_render::bridge_split(bridge, bands, split);
    OA_CHECK(made == bands);
    for (int32_t band = 1; band < made; ++band)
        model_render::bridge_band_colours(bridge, split[static_cast<std::size_t>(band)]);
    std::vector<model_render::ModelRenderer> renderers(static_cast<std::size_t>(made));
    std::vector<model_render::SupersampleScratch> supersample(static_cast<std::size_t>(made));
    std::vector<std::vector<oa::formats::objects3d::FixedVector3>> points(
        static_cast<std::size_t>(made)
    );
    std::array<bool, most_bands> failed{};
    const auto pool = std::make_unique<job_pool::Pool>(static_cast<uint32_t>(made));
    job_pool::run_bands(
        made > 1 ? pool.get() : nullptr, static_cast<uint32_t>(made), [&](uint32_t band) noexcept {
            try {
                oa::app::draw_world_band(
                    list, draw, split[band], renderers[band], supersample[band], points[band]
                );
            } catch (...) {
                failed[band] = true;
            }
        }
    );
    for (auto& band : split)
        model_render::bridge_join_band(bridge, band);
    for (const bool band_failed : failed)
        OA_CHECK(!band_failed);
    return rgb;
}

/// Checks the lines of each kind at each thickness on 1 to most_bands bands.
void test_thin_lines_in_bands() {
    const auto palette = test_palette();
    for (const auto kind : {WorldDrawKind::line, WorldDrawKind::selection_line}) {
        const auto colour =
            kind == WorldDrawKind::line ? line_colour : entry_colour(palette, selection_index);
        for (const int32_t thickness : {1, 2}) {
            const auto expected = expected_frame(palette, colour, thickness);
            for (int32_t bands = 1; bands <= most_bands; ++bands) {
                const bool same = draw_lines(palette, kind, thickness, bands) == expected;
                if (!same)
                    std::fprintf(
                        stderr,
                        "%s lines %d thick on %d bands differ from their pixels\n",
                        kind == WorldDrawKind::line ? "frame" : "selection",
                        static_cast<int>(thickness),
                        static_cast<int>(bands)
                    );
                OA_CHECK(same);
            }
        }
    }
}

/// Returns a frame's pixel.
///
/// @param rgb the frame's RGB pixels
/// @param x column
/// @param y row
/// @return its red, green and blue
std::array<uint8_t, 3> pixel_at(const std::vector<uint8_t>& rgb, int32_t x, int32_t y) {
    const std::size_t at =
        (static_cast<std::size_t>(y) * frame_width + static_cast<std::size_t>(x)) * 3U;
    return {rgb[at], rgb[at + 1], rgb[at + 2]};
}

/// Draws a 4x4 GAF frame of one colour at (20, 20) into a frame of the
/// ground, as a sprite or blended, a feature's shadow or not, from a list
/// at a shadow level, on one band.
///
/// @param palette the palette
/// @param kind WorldDrawKind::sprite or WorldDrawKind::blended_sprite
/// @param shadow the frame is a feature's shadow
/// @param level the list's shadow level
/// @return the frame's RGB pixels
std::vector<uint8_t>
draw_square(const oa::PaletteBytes& palette, WorldDrawKind kind, bool shadow, uint32_t level) {
    auto rgb = ground_frame(palette);
    model_render::ModelDisplay display;
    model_render::build_model_display(display, oa::present::palette_from_bytes(palette));
    model_render::RgbBridge bridge;
    const model_render::RgbFrame frame{rgb.data(), frame_width, frame_height, frame_width * 3};
    model_render::bridge_begin(
        bridge, frame, {0, 0, frame_width - 1, frame_height - 1}, 1.0F, display.palette
    );
    oa::formats::gaf::RenderedFrame square;
    square.width = 4;
    square.height = 4;
    square.pixels.assign(16, selection_index);
    square.coverage.assign(16, 1);
    oa::app::WorldDrawList list;
    list.shadow_level = level;
    list.sprites.push_back({&square, {20, 20}, shadow});
    oa::app::add_world_draw(list, kind, 0);
    oa::app::WorldFrameDraw draw;
    draw.target = {
        rgb.data(), frame_width, frame_height, 0, 0, frame_width, frame_height, 0, frame_height
    };
    draw.palette = &palette;
    draw.bridge = &bridge;
    draw.display = &display;
    std::vector<model_render::BridgeBand> split;
    model_render::bridge_split(bridge, 1, split);
    model_render::ModelRenderer renderer;
    model_render::SupersampleScratch supersample;
    std::vector<oa::formats::objects3d::FixedVector3> points;
    oa::app::draw_world_band(list, draw, split.front(), renderer, supersample, points);
    model_render::bridge_join_band(bridge, split.front());
    return rgb;
}

/// A feature's shadow frame, drawn as a sprite or blended through the
/// alpha table: at the full level as the game draws it; lighter, each
/// covered pixel mixed from the ground toward that by the level; at 0 not
/// at all. A frame that is not a shadow draws the same at any level.
void test_feature_shadows() {
    const auto palette = test_palette();
    const auto ground = entry_colour(palette, ground_index);
    model_render::ModelDisplay display;
    model_render::build_model_display(display, oa::present::palette_from_bytes(palette));
    const auto blend_index = display.alpha[std::size_t{selection_index} * 256U + ground_index];
    for (const auto kind : {WorldDrawKind::sprite, WorldDrawKind::blended_sprite}) {
        const auto shadowed = kind == WorldDrawKind::sprite ? entry_colour(palette, selection_index)
                                                            : entry_colour(palette, blend_index);
        const auto full = draw_square(palette, kind, true, model_render::shadow_full_level);
        const auto plain = draw_square(palette, kind, false, model_render::shadow_full_level);
        OA_CHECK(full == plain);
        OA_CHECK(pixel_at(full, 21, 21) == shadowed);
        OA_CHECK(pixel_at(full, 19, 21) == ground);
        for (const uint32_t level : {1U, 16U, 32U, 63U}) {
            const auto faded = draw_square(palette, kind, true, level);
            std::array<uint8_t, 3> expected{};
            for (std::size_t channel = 0; channel < 3; ++channel)
                expected[channel] =
                    model_render::fade_shadow_channel(ground[channel], shadowed[channel], level);
            OA_CHECK(pixel_at(faded, 20, 20) == expected);
            OA_CHECK(pixel_at(faded, 23, 23) == expected);
            OA_CHECK(pixel_at(faded, 24, 23) == ground);
            OA_CHECK(draw_square(palette, kind, false, level) == plain);
        }
        OA_CHECK(draw_square(palette, kind, true, 0) == ground_frame(palette));
        OA_CHECK(draw_square(palette, kind, false, 0) == plain);
    }
}

/// A frame's shadows set from its zoom: the game's own at zoom 1 and
/// closer, through the faded table between, and none from a quarter out,
/// where the renderer's shadow option is cleared for the frame alone.
void test_frame_shadows() {
    const auto palette = test_palette();
    model_render::ModelDisplay display;
    model_render::build_model_display(display, oa::present::palette_from_bytes(palette));
    model_render::ShadowTable table;
    oa::app::WorldDrawList list;
    model_render::ModelRenderer renderer;
    constexpr uint16_t game_flags =
        model_render::graphics_shadows | model_render::graphics_vehicle_shadows;
    std::vector<uint8_t> pixels{5, 5, 0xff, 9};
    oa::Sprite shadow_sprite{};
    shadow_sprite.width = 2;
    shadow_sprite.height = 2;
    shadow_sprite.key = 0xff;
    shadow_sprite.data = pixels.data();
    for (const float zoom : {1.0F, 2.0F, 4.0F}) {
        renderer.graphics_flags = game_flags;
        oa::app::set_frame_shadows(list, renderer, table, display, &shadow_sprite, zoom);
        OA_CHECK(list.shadow_level == model_render::shadow_full_level);
        OA_CHECK(oa::app::shadows_drawn(list));
        OA_CHECK(renderer.shadow_table == nullptr);
        OA_CHECK(renderer.graphics_flags == game_flags);
    }
    for (const float zoom : {0.75F, 0.5F, 1.0F / 3.0F, 0.26F}) {
        renderer.graphics_flags = game_flags;
        oa::app::set_frame_shadows(list, renderer, table, display, &shadow_sprite, zoom);
        OA_CHECK(list.shadow_level > 0 && list.shadow_level < model_render::shadow_full_level);
        OA_CHECK(renderer.shadow_table != nullptr);
        OA_CHECK(renderer.graphics_flags == game_flags);
        if (renderer.shadow_table == nullptr)
            continue;
        // The silhouettes' row and the shadow sprite's colours are faded,
        // and others are the display's.
        bool faded = false;
        for (const std::size_t colour : {0U, 5U, 9U})
            for (std::size_t under = 0; under < 256; ++under)
                faded = faded || renderer.shadow_table[colour * 256U + under] !=
                                     display.alpha[colour * 256U + under];
        OA_CHECK(faded);
        bool kept = true;
        for (std::size_t under = 0; under < 256; ++under)
            kept = kept &&
                   renderer.shadow_table[6U * 256U + under] == display.alpha[6U * 256U + under];
        OA_CHECK(kept);
    }
    for (const float zoom : {0.25F, 0.2F, 1.0F / 6.0F}) {
        renderer.graphics_flags = game_flags;
        oa::app::set_frame_shadows(list, renderer, table, display, &shadow_sprite, zoom);
        OA_CHECK(list.shadow_level == 0);
        OA_CHECK(!oa::app::shadows_drawn(list));
        OA_CHECK(renderer.shadow_table == nullptr);
        OA_CHECK(renderer.graphics_flags == model_render::graphics_vehicle_shadows);
    }
}

} // namespace

int main() {
    test_thin_lines_in_bands();
    test_feature_shadows();
    test_frame_shadows();
    return oa::test::check_exit_status();
}
