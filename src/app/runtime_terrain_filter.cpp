// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Area-filtered terrain for zoomed-out battlefield views.
#include "oa/app/runtime.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace oa::app {
namespace {

constexpr float kFilteredZoomLimit = 1.0F - 1.0e-4F;
constexpr uint32_t kFixedOne = 65536u;

/// Lists the map coordinate under each destination column (or row) plus one past the end.
///
/// Stepped with the terrain sampler's fixed-point DDA so every footprint
/// shares the nearest sampler's and the fog's tile boundaries.
///
/// @param[out] bounds `count` + 1 map coordinates
/// @param start map coordinate of the first destination pixel
/// @param count destination pixels
/// @param scale_fp zoom in 16.16, at least 1
void footprint_bounds(std::vector<uint32_t>& bounds, uint32_t start, int count, uint32_t scale_fp) {
    bounds.resize(static_cast<std::size_t>(count) + 1U);
    uint32_t position = start;
    uint32_t fraction = 0;
    for (auto& bound : bounds) {
        bound = position;
        fraction += kFixedOne;
        while (fraction >= scale_fp) {
            fraction -= scale_fp;
            ++position;
        }
    }
}

/// Averages every map pixel under each destination pixel's footprint.
///
/// Ground beyond the terrain counts as black, as the nearest sampler paints
/// it. Each visible map pixel is read once, so the cost is the source area in
/// view.
///
/// @param map terrain
/// @param palette palette the tiles' indices are shown in
/// @param source_x map column of the first destination pixel
/// @param source_y map row of the first destination pixel
/// @param dest_w destination width
/// @param dest_h destination height
/// @param zoom destination pixels per map pixel, below 1
/// @param[out] dest_rgb 3 bytes per destination pixel
/// @return false when the terrain's tile tables are malformed or name a missing tile
bool box_filter_terrain(
    const oa::formats::tnt::Map& map,
    const oa::PaletteBytes& palette,
    uint32_t source_x,
    uint32_t source_y,
    int dest_w,
    int dest_h,
    float zoom,
    uint8_t* dest_rgb
) {
    constexpr auto tile_edge = static_cast<uint32_t>(oa::formats::tnt::layout::tile_edge_pixels);
    constexpr auto tile_bytes = oa::formats::tnt::layout::tile_bytes;
    const auto grid_cells = static_cast<uint64_t>(map.tile_width) * map.tile_height;
    const auto tile_pixels = static_cast<uint64_t>(map.tile_count) * tile_bytes;
    if (grid_cells != map.tile_indices.size() || tile_pixels != map.tile_palette_indices.size())
        return false;
    const auto terrain_w = static_cast<uint64_t>(map.tile_width) * tile_edge;
    const auto terrain_h = static_cast<uint64_t>(map.tile_height) * tile_edge;
    auto scale_fp = static_cast<uint32_t>(std::lround(static_cast<double>(zoom) * kFixedOne));
    if (scale_fp == 0)
        scale_fp = 1;
    std::array<uint8_t, 256U * 3U> lut{};
    for (std::size_t index = 0; index < 256U; ++index)
        std::memcpy(&lut[index * 3U], &palette[index * oa::palette_entry_bytes], 3U);
    std::vector<uint32_t> columns;
    std::vector<uint32_t> rows;
    footprint_bounds(columns, source_x, dest_w, scale_fp);
    footprint_bounds(rows, source_y, dest_h, scale_fp);
    std::vector<uint32_t> sums(static_cast<std::size_t>(dest_w) * 3U);
    const uint16_t* const grid = map.tile_indices.data();
    const uint8_t* const tiles = map.tile_palette_indices.data();
    const auto column_end = static_cast<uint32_t>(
        std::min<uint64_t>(columns[static_cast<std::size_t>(dest_w)], terrain_w)
    );
    for (int dy = 0; dy < dest_h; ++dy) {
        const uint32_t row_begin = rows[static_cast<std::size_t>(dy)];
        const uint32_t row_end = rows[static_cast<std::size_t>(dy) + 1U];
        std::fill(sums.begin(), sums.end(), 0u);
        for (uint32_t sy = row_begin; sy < row_end && sy < terrain_h; ++sy) {
            const auto row_tiles = static_cast<std::size_t>(sy / tile_edge) * map.tile_width;
            const auto within_y = static_cast<std::size_t>(sy % tile_edge) * tile_edge;
            std::size_t column = 0;
            uint32_t next_column = columns[1];
            uint32_t* sum = sums.data();
            uint32_t sx = columns[0];
            while (sx < column_end) {
                const auto tile_index = grid[row_tiles + sx / tile_edge];
                if (tile_index >= map.tile_count)
                    return false;
                const uint32_t within_x = sx % tile_edge;
                const uint32_t run_end = std::min(column_end, sx - within_x + tile_edge);
                const uint8_t* source =
                    tiles + static_cast<std::size_t>(tile_index) * tile_bytes + within_y + within_x;
                for (; sx < run_end; ++sx, ++source) {
                    while (sx >= next_column) {
                        ++column;
                        sum += 3;
                        next_column = columns[column + 1U];
                    }
                    const uint8_t* rgb = &lut[static_cast<std::size_t>(*source) * 3U];
                    sum[0] += rgb[0];
                    sum[1] += rgb[1];
                    sum[2] += rgb[2];
                }
            }
        }
        const uint32_t row_count = row_end - row_begin;
        uint8_t* out =
            dest_rgb + static_cast<std::size_t>(dy) * static_cast<std::size_t>(dest_w) * 3U;
        const uint32_t* sum = sums.data();
        for (int dx = 0; dx < dest_w; ++dx, out += 3, sum += 3) {
            const auto index = static_cast<std::size_t>(dx);
            const uint32_t count = row_count * (columns[index + 1U] - columns[index]);
            out[0] = static_cast<uint8_t>((sum[0] + count / 2U) / count);
            out[1] = static_cast<uint8_t>((sum[1] + count / 2U) / count);
            out[2] = static_cast<uint8_t>((sum[2] + count / 2U) / count);
        }
    }
    return true;
}

} // namespace

void Runtime::refresh_filtered_terrain() {
    if (screen_ != Screen::match || !match_ || !selected_tnt_)
        return;
    const auto zoom = match_zoom();
    if (!(zoom > 0.0F) || zoom >= kFilteredZoomLimit)
        return;
    const auto& map = *selected_tnt_;
    const auto map_width = static_cast<int32_t>(map.tile_width * 32U);
    const auto map_height = static_cast<int32_t>(map.tile_height * 32U);
    const int dest_w = match_layout_.battlefield_width();
    const int dest_h = match_layout_.battlefield_height();
    if (dest_w <= 0 || dest_h <= 0)
        return;
    // The renderer clamps the camera the same way before it checks its cache.
    const auto camera_x = static_cast<uint32_t>(
        std::clamp(match_camera_x_, 0, std::max(0, map_width - visible_map_width()))
    );
    const auto camera_y = static_cast<uint32_t>(
        std::clamp(match_camera_z_, 0, std::max(0, map_height - visible_map_height()))
    );
    auto& cache = match_terrain_cache_;
    const auto pixels = static_cast<std::size_t>(dest_w) * static_cast<std::size_t>(dest_h) * 3U;
    const bool resized = cache.width != static_cast<uint32_t>(dest_w) ||
                         cache.height != static_cast<uint32_t>(dest_h) ||
                         cache.rgb.size() != pixels;
    if (resized) {
        cache.width = static_cast<uint32_t>(dest_w);
        cache.height = static_cast<uint32_t>(dest_h);
        cache.rgb.resize(pixels);
    }
    const auto current = [&](uint32_t cam_x, uint32_t cam_y, float cam_zoom) {
        return cam_x == camera_x && cam_y == camera_y && std::abs(cam_zoom - zoom) <= 1.0e-4F;
    };
    if (!resized &&
        current(terrain_filtered_cam_x_, terrain_filtered_cam_y_, terrain_filtered_zoom_) &&
        current(terrain_cache_cam_x_, terrain_cache_cam_y_, terrain_cache_zoom_))
        return;
    if (!box_filter_terrain(
            map, match_palette_, camera_x, camera_y, dest_w, dest_h, zoom, cache.rgb.data()
        ))
        return; // A malformed map is reported by the renderer's own fill.
    terrain_cache_cam_x_ = camera_x;
    terrain_cache_cam_y_ = camera_y;
    terrain_cache_zoom_ = zoom;
    terrain_filtered_cam_x_ = camera_x;
    terrain_filtered_cam_y_ = camera_y;
    terrain_filtered_zoom_ = zoom;
}

} // namespace oa::app
