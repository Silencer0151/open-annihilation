// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/world_renderer/world_fog.hpp"

#include "oa/present/palette_tables.hpp"

#include <algorithm>
#include <cstring>

namespace oa::present::world_renderer {
namespace {

enum class FogLayer { unmapped, unseen };

uint8_t& layer_mask(FogTile& tile, FogLayer layer) noexcept {
    return layer == FogLayer::unmapped ? tile.unmapped : tile.unseen;
}

void mark_cell_corners(FogGrid& grid, FogLayer layer, int32_t column, int32_t row) {
    const auto mark = [&](int32_t c, int32_t r, uint8_t bit) {
        if (c < 0 || r < 0 || c >= grid.width || r >= grid.height)
            return;
        layer_mask(
            grid.tiles
                [static_cast<std::size_t>(r) * static_cast<std::size_t>(grid.width) +
                 static_cast<std::size_t>(c)],
            layer
        ) |= bit;
    };
    mark(column, row, fog_corner_top_left);
    mark(column - 1, row, fog_corner_top_right);
    mark(column, row - 1, fog_corner_bottom_left);
    mark(column - 1, row - 1, fog_corner_bottom_right);
}

void extend_corner(FogTile& tile, bool unseen_layer, uint8_t from, uint8_t to) {
    if (unseen_layer && (tile.unseen & from) != 0)
        tile.unseen |= to;
    if ((tile.unmapped & from) != 0)
        tile.unmapped |= to;
}

int32_t first_cell(int32_t camera) noexcept {
    return camera % fog_cell_pixels < fog_cell_pixels / 2 ? camera / fog_cell_pixels - 1
                                                          : camera / fog_cell_pixels;
}

int32_t tile_offset(int32_t camera) noexcept {
    const auto half = fog_cell_pixels / 2;
    return (camera % fog_cell_pixels < half ? -half : half) - camera % fog_cell_pixels;
}

// First destination pixel of map pixel `map` (relative to the camera) under
// the terrain DDA, which shows map pixel floor(d * 65536 / zoom) at pixel d.
int32_t dest_edge(uint32_t zoom_fp, int32_t map) noexcept {
    return static_cast<int32_t>(
        (static_cast<int64_t>(map) * zoom_fp + (fog_zoom_one - 1)) / fog_zoom_one
    );
}

struct PixelSpan {
    int32_t begin{};
    int32_t end{};

    [[nodiscard]] bool empty() const noexcept { return begin >= end; }
};

// Destination pixels of map pixels [map_begin, map_end) clipped to the view.
PixelSpan dest_span(
    uint32_t zoom_fp, int32_t map_begin, int32_t map_end, int32_t clip_begin, int32_t clip_end
) noexcept {
    if (map_end <= 0)
        return {};
    return {
        std::max(dest_edge(zoom_fp, std::max(map_begin, 0)), clip_begin),
        std::min(dest_edge(zoom_fp, map_end), clip_end)
    };
}

struct FogPainter {
    Surface& surface;
    const FogView& view;
    const FogShading& shading;
    uint32_t zoom_fp;
    // View-relative clip of the destination rectangle against the surface.
    int32_t clip_x0, clip_x1, clip_y0, clip_y1;

    [[nodiscard]] uint8_t* pixel(int32_t x, int32_t y) noexcept {
        return surface.rgb.data() + (static_cast<std::size_t>(y + view.dest_y) * surface.width +
                                     static_cast<std::size_t>(x + view.dest_x)) *
                                        3U;
    }

    void fill(PixelSpan xs, PixelSpan ys, const std::array<uint8_t, 3>& rgb) noexcept {
        const bool flat = rgb[0] == rgb[1] && rgb[1] == rgb[2];
        for (int32_t y = ys.begin; y < ys.end; ++y) {
            auto* out = pixel(xs.begin, y);
            const auto count = static_cast<std::size_t>(xs.end - xs.begin);
            if (flat) {
                std::memset(out, rgb[0], count * 3U);
                continue;
            }
            for (std::size_t x = 0; x < count; ++x, out += 3) {
                out[0] = rgb[0];
                out[1] = rgb[1];
                out[2] = rgb[2];
            }
        }
    }

    // Graying remaps the ground through the gray table.
    void gray(PixelSpan xs, PixelSpan ys) noexcept {
        for (int32_t y = ys.begin; y < ys.end; ++y) {
            auto* out = pixel(xs.begin, y);
            for (int32_t x = xs.begin; x < xs.end; ++x, out += 3) {
                const auto level = (static_cast<unsigned>(out[0]) + out[1] + out[2]) / 3u;
                const auto& rgb = shading.gray_levels[level];
                out[0] = rgb[0];
                out[1] = rgb[1];
                out[2] = rgb[2];
            }
        }
    }

    // Dithering clears every other map pixel to palette index 0.
    void dither(int32_t map_x, int32_t map_z, PixelSpan xs, PixelSpan ys) noexcept {
        if (((static_cast<uint32_t>(view.camera_x + map_x) +
              static_cast<uint32_t>(view.camera_z + map_z)) &
             1u) == 0)
            fill(xs, ys, shading.dither_rgb);
    }

    // Grays or dithers the map pixels [map_x0, map_x1) x [map_z0, map_z1).
    void shade(int32_t map_x0, int32_t map_x1, int32_t map_z0, int32_t map_z1) noexcept {
        if (!shading.dithered) {
            gray(
                dest_span(zoom_fp, map_x0, map_x1, clip_x0, clip_x1),
                dest_span(zoom_fp, map_z0, map_z1, clip_y0, clip_y1)
            );
            return;
        }
        for (int32_t mz = std::max(map_z0, 0); mz < map_z1; ++mz) {
            const auto ys = dest_span(zoom_fp, mz, mz + 1, clip_y0, clip_y1);
            if (ys.empty())
                continue;
            for (int32_t mx = std::max(map_x0, 0); mx < map_x1; ++mx) {
                const auto xs = dest_span(zoom_fp, mx, mx + 1, clip_x0, clip_x1);
                if (!xs.empty())
                    dither(mx, mz, xs, ys);
            }
        }
    }

    // Draws the opaque texels of a tile whose top-left map pixel is (map_x0, map_z0).
    template <typename Texel>
    void masked(const FogTileArt& art, int32_t map_x0, int32_t map_z0, Texel&& texel) noexcept {
        for (int32_t ty = 0; ty < fog_cell_pixels; ++ty) {
            const auto mz = map_z0 + ty;
            if (mz < 0)
                continue;
            const auto ys = dest_span(zoom_fp, mz, mz + 1, clip_y0, clip_y1);
            if (ys.empty())
                continue;
            const auto* row = &art.opaque[static_cast<std::size_t>(ty) * fog_cell_pixels];
            const auto* index = &art.index[static_cast<std::size_t>(ty) * fog_cell_pixels];
            for (int32_t tx = 0; tx < fog_cell_pixels; ++tx) {
                if (row[tx] == 0)
                    continue;
                const auto mx = map_x0 + tx;
                if (mx < 0)
                    continue;
                const auto xs = dest_span(zoom_fp, mx, mx + 1, clip_x0, clip_x1);
                if (!xs.empty())
                    texel(index[tx], mx, mz, xs, ys);
            }
        }
    }
};

} // namespace

int32_t fog_map_span(uint32_t zoom_fp, int32_t count) noexcept {
    if (count <= 0)
        return 0;
    if (zoom_fp == 0)
        zoom_fp = 1;
    return static_cast<int32_t>(static_cast<int64_t>(count - 1) * fog_zoom_one / zoom_fp) + 1;
}

FogGrid build_fog_grid(
    const sim::visibility_state::PlayerSightGrid& sight,
    std::span<const uint8_t> coverage,
    FogOptions options,
    int32_t camera_x,
    int32_t camera_z,
    int32_t view_width,
    int32_t view_height
) {
    FogGrid grid;
    grid.width = (view_width + fog_cell_pixels - 1) / fog_cell_pixels + 2;
    grid.height = (view_height + fog_cell_pixels - 1) / fog_cell_pixels + 2;
    if (grid.width < 2 || grid.height < 2)
        return {};
    grid.first_cell_x = first_cell(camera_x);
    grid.first_cell_z = first_cell(camera_z);
    grid.offset_x = tile_offset(camera_x);
    grid.offset_z = tile_offset(camera_z);
    grid.variant_phase = (camera_x + fog_cell_pixels / 2) / fog_cell_pixels +
                         (camera_z + fog_cell_pixels / 2) / fog_cell_pixels;
    grid.tiles.assign(
        static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height), FogTile{}
    );
    const auto viewer_bit = static_cast<uint16_t>(1u << (sight.viewpoint_player & 0x1fu));
    const auto cells = static_cast<std::size_t>(sight.width > 0 ? sight.width : 0) *
                       static_cast<std::size_t>(sight.height > 0 ? sight.height : 0);
    for (int32_t row = 0; row < grid.height; ++row) {
        const auto z = grid.first_cell_z + row;
        for (int32_t column = 0; column < grid.width; ++column) {
            const auto x = grid.first_cell_x + column;
            if (x < 0 || z < 0 || x >= sight.width || z >= sight.height)
                continue;
            const auto index = static_cast<std::size_t>(z) * static_cast<std::size_t>(sight.width) +
                               static_cast<std::size_t>(x);
            if (index >= cells)
                continue;
            const bool in_sight = index < coverage.size() && coverage[index] != 0;
            if (!in_sight && options.line_of_sight)
                mark_cell_corners(grid, FogLayer::unseen, column, row);
            const bool mapped = !options.mapping || (index < sight.player_bits.size() &&
                                                     (sight.player_bits[index] & viewer_bit) != 0);
            if (!mapped)
                mark_cell_corners(grid, FogLayer::unmapped, column, row);
        }
    }
    // The game copies the corners of the first tile and the last-but-one
    // tile, which straddle the border when the view is a whole number of
    // tiles wide; here the straddling tile is found from the border cell so
    // any view size and zoom keeps a clean border.
    const bool unseen_layer = options.line_of_sight;
    const auto tile = [&](int32_t column, int32_t row) -> FogTile& {
        return grid.tiles
            [static_cast<std::size_t>(row) * static_cast<std::size_t>(grid.width) +
             static_cast<std::size_t>(column)];
    };
    const auto top_row = -1 - grid.first_cell_z; // top corners off the map
    if (top_row >= 0 && top_row < grid.height)
        for (int32_t column = 0; column < grid.width; ++column) {
            auto& edge = tile(column, top_row);
            extend_corner(edge, unseen_layer, fog_corner_bottom_left, fog_corner_top_left);
            extend_corner(edge, unseen_layer, fog_corner_bottom_right, fog_corner_top_right);
        }
    const auto bottom_row = sight.height - 1 - grid.first_cell_z; // bottom corners off the map
    if (bottom_row >= 0 && bottom_row < grid.height)
        for (int32_t column = 0; column < grid.width; ++column) {
            auto& edge = tile(column, bottom_row);
            extend_corner(edge, unseen_layer, fog_corner_top_left, fog_corner_bottom_left);
            extend_corner(edge, unseen_layer, fog_corner_top_right, fog_corner_bottom_right);
        }
    const auto left_column = -1 - grid.first_cell_x;
    if (left_column >= 0 && left_column < grid.width)
        for (int32_t row = 0; row < grid.height; ++row) {
            auto& edge = tile(left_column, row);
            extend_corner(edge, unseen_layer, fog_corner_bottom_right, fog_corner_bottom_left);
            extend_corner(edge, unseen_layer, fog_corner_top_right, fog_corner_top_left);
        }
    const auto right_column = sight.width - 1 - grid.first_cell_x;
    if (right_column >= 0 && right_column < grid.width)
        for (int32_t row = 0; row < grid.height; ++row) {
            auto& edge = tile(right_column, row);
            extend_corner(edge, unseen_layer, fog_corner_bottom_left, fog_corner_bottom_right);
            extend_corner(edge, unseen_layer, fog_corner_top_left, fog_corner_top_right);
        }
    return grid;
}

void draw_fog_grid(
    Surface& surface,
    const FogView& view,
    const FogGrid& grid,
    const FogTileSet& tiles,
    const FogShading& shading
) {
    if (grid.tiles.empty() || view.dest_width <= 0 || view.dest_height <= 0)
        return;
    const auto zoom_fp = view.zoom_fp == 0 ? fog_zoom_one : view.zoom_fp;
    FogPainter painter{
        surface,
        view,
        shading,
        zoom_fp,
        std::max(view.dest_x, 0) - view.dest_x,
        std::min(view.dest_x + view.dest_width, static_cast<int32_t>(surface.width)) - view.dest_x,
        std::max(view.dest_y, 0) - view.dest_y,
        std::min(view.dest_y + view.dest_height, static_cast<int32_t>(surface.height)) - view.dest_y
    };
    if (painter.clip_x0 >= painter.clip_x1 || painter.clip_y0 >= painter.clip_y1)
        return;
    const auto span_x = fog_map_span(zoom_fp, view.dest_width);
    const auto span_z = fog_map_span(zoom_fp, view.dest_height);
    const bool have_art = tiles.loaded();
    for (int32_t row = 0; row < grid.height; ++row) {
        const auto map_z0 = grid.offset_z + row * fog_cell_pixels;
        if (map_z0 >= span_z || map_z0 + fog_cell_pixels <= 0)
            continue;
        const auto ys =
            dest_span(zoom_fp, map_z0, map_z0 + fog_cell_pixels, painter.clip_y0, painter.clip_y1);
        if (ys.empty())
            continue;
        for (int32_t column = 0; column < grid.width; ++column) {
            const auto& tile = grid.at(column, row);
            if (tile.unmapped == 0 && tile.unseen == 0)
                continue;
            const auto map_x0 = grid.offset_x + column * fog_cell_pixels;
            if (map_x0 >= span_x || map_x0 + fog_cell_pixels <= 0)
                continue;
            const auto xs = dest_span(
                zoom_fp, map_x0, map_x0 + fog_cell_pixels, painter.clip_x0, painter.clip_x1
            );
            if (xs.empty())
                continue;
            if (tile.unmapped == fog_mask_full) {
                painter.fill(xs, ys, shading.unmapped_rgb);
                continue;
            }
            const auto variant = (row + column + grid.variant_phase) & (fog_tile_variants - 1);
            if (tile.unseen == fog_mask_full) {
                painter.shade(map_x0, map_x0 + fog_cell_pixels, map_z0, map_z0 + fog_cell_pixels);
            } else if (tile.unseen != 0 && have_art) {
                painter.masked(
                    tiles.at(FogTileSet::gray, variant, tile.unseen),
                    map_x0,
                    map_z0,
                    [&](uint8_t, int32_t mx, int32_t mz, PixelSpan tx, PixelSpan ty) {
                        if (shading.dithered)
                            painter.dither(mx, mz, tx, ty);
                        else
                            painter.gray(tx, ty);
                    }
                );
            }
            if (tile.unmapped != 0 && have_art)
                painter.masked(
                    tiles.at(FogTileSet::black, variant, tile.unmapped),
                    map_x0,
                    map_z0,
                    [&](uint8_t index, int32_t, int32_t, PixelSpan tx, PixelSpan ty) {
                        painter.fill(tx, ty, shading.palette_rgb[index]);
                    }
                );
        }
    }
}

void build_gray_levels(const Palette& palette, std::array<uint8_t, OA_PALETTE_COLORS>& levels) {
    int32_t brightness[OA_PALETTE_COLORS];
    uint8_t order[OA_PALETTE_COLORS];
    ::oa::present::sort_palette_by_brightness(palette, brightness, order);
    for (std::size_t level = 0; level < levels.size(); ++level) {
        const auto gray = static_cast<uint8_t>(level);
        levels[level] =
            ::oa::present::find_nearest_sorted_color(palette, brightness, order, gray, gray, gray);
    }
}

} // namespace oa::present::world_renderer
