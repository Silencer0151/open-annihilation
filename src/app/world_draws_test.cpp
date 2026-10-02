// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The battlefield's thin lines drawn band by band (world_draws.hpp): lines
// of the frame (lasers, lightning) and selection lines of the model bridge,
// drawn 2 pixels thick, as the accelerated tier draws them in a scene the
// area pass reduces, cover each of the line's pixels and the pixels right
// of, below and below and right of it, across the edges of the bands and
// of the bridge's tiles, byte for byte the same on 1 to 8 bands, each band
// on a drawing thread of its own. Drawn 1 pixel thick they cover the line's
// own pixels, as the game always draws them.
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

} // namespace

int main() {
    test_thin_lines_in_bands();
    return oa::test::check_exit_status();
}
