// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Fog-of-war edge grid and gray shade table. The battlefield is covered by a
// grid of 32-pixel tiles offset half a sight cell from the sight grid, so each
// tile's four corners sit on four sight-cell centres. Each tile holds two
// four-bit corner masks: one for never-mapped corners (drawn with the black
// FOG.GAF tiles) and one for corners outside current line of sight (drawn by
// graying the terrain through the gray FOG.GAF tiles).

#include "oa/present/surface.h"
#include "oa/sim/visibility_state.hpp"
#include "oa/present/world_renderer.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace oa::present::world_renderer {

inline constexpr int32_t fog_cell_pixels = 32;
// Corner bits of a fog tile mask.
inline constexpr uint8_t fog_corner_top_left = 0x01;
inline constexpr uint8_t fog_corner_top_right = 0x02;
inline constexpr uint8_t fog_corner_bottom_left = 0x04;
inline constexpr uint8_t fog_corner_bottom_right = 0x08;
// Every corner fogged: the whole tile is filled instead of masked.
inline constexpr uint8_t fog_mask_full = 0x0f;
// FOG.GAF holds four variants of each tile set, chosen per tile.
inline constexpr int32_t fog_tile_variants = 4;
// 16.16 zoom of an unscaled view.
inline constexpr uint32_t fog_zoom_one = 65536u;

struct FogOptions {
    bool line_of_sight{}; // gray tiles are built only with LOS enabled
    bool mapping{true};   // without mapping every cell counts as mapped
};

struct FogTile {
    uint8_t unmapped{}; // black FOG.GAF mask
    uint8_t unseen{};   // gray FOG.GAF mask
};

struct FogGrid {
    int32_t width{}; // tiles
    int32_t height{};
    // Sight cell whose centre is the top-left corner of tile (0, 0).
    int32_t first_cell_x{};
    int32_t first_cell_z{};
    // Screen offset of tile (0, 0) from the camera origin (-31..0).
    int32_t offset_x{};
    int32_t offset_z{};
    // Tile-set variant phase: variant = (row + column + phase) & 3.
    int32_t variant_phase{};
    std::vector<FogTile> tiles{};

    /// Returns the tile at a grid column and row.
    ///
    /// @param column tile column, 0..width-1
    /// @param row tile row, 0..height-1
    /// @return the tile; the position is not checked
    [[nodiscard]] const FogTile& at(int32_t column, int32_t row) const noexcept {
        return tiles
            [static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(column)];
    }
};

// One 32x32 FOG.GAF tile: the palette index of each texel and whether the
// texel covers the ground at all.
struct FogTileArt {
    std::array<uint8_t, fog_cell_pixels * fog_cell_pixels> index{};
    std::array<uint8_t, fog_cell_pixels * fog_cell_pixels> opaque{};
};

// The FOG.GAF tile sets: black tiles for never-mapped corners and gray tiles
// for corners outside line of sight, four variants of each, one tile per
// partial corner mask 1..14.
struct FogTileSet {
    enum Bank : uint8_t { black = 0, gray = 1 };

    static constexpr std::size_t tiles_per_bank =
        static_cast<std::size_t>(fog_tile_variants) * (fog_mask_full - 1);

    std::vector<FogTileArt> art; // empty until loaded

    /// Returns whether both tile banks are loaded.
    [[nodiscard]] bool loaded() const noexcept { return art.size() == 2 * tiles_per_bank; }

    /// Returns the tile art for a bank, variant and partial corner mask.
    ///
    /// @param bank black or gray tiles
    /// @param variant tile-set variant, 0..3
    /// @param mask partial corner mask, 1..14
    /// @return the tile art; the arguments are not checked
    [[nodiscard]] FogTileArt& at(Bank bank, int32_t variant, uint8_t mask) {
        return art
            [static_cast<std::size_t>(bank) * tiles_per_bank +
             static_cast<std::size_t>(variant) * (fog_mask_full - 1) + (mask - 1U)];
    }

    /// Returns the tile art for a bank, variant and partial corner mask.
    ///
    /// @param bank black or gray tiles
    /// @param variant tile-set variant, 0..3
    /// @param mask partial corner mask, 1..14
    /// @return the tile art; the arguments are not checked
    [[nodiscard]] const FogTileArt& at(Bank bank, int32_t variant, uint8_t mask) const {
        return art
            [static_cast<std::size_t>(bank) * tiles_per_bank +
             static_cast<std::size_t>(variant) * (fog_mask_full - 1) + (mask - 1U)];
    }
};

// Colours the fog draws with on an RGB world surface.
struct FogShading {
    // RGB of the gray-table entry for each (r+g+b)/3 level of a grayed pixel.
    std::array<std::array<uint8_t, 3>, OA_PALETTE_COLORS> gray_levels{};
    // RGB of each palette index, for the black tile texels.
    std::array<std::array<uint8_t, 3>, OA_PALETTE_COLORS> palette_rgb{};
    // Solid fill of a tile whose four corners were never mapped (UI colour 0).
    std::array<uint8_t, 3> unmapped_rgb{};
    // Dithered fog clears every other grayed pixel to palette index 0 instead.
    std::array<uint8_t, 3> dither_rgb{};
    bool dithered{};
};

// The zoomed map view the fog is drawn over: destination rectangle on the
// surface, the map pixel under its top-left corner and the 16.16 zoom of the
// terrain DDA (destination pixel d shows map pixel floor(d * 65536 / zoom)).
struct FogView {
    int32_t dest_x{};
    int32_t dest_y{};
    int32_t dest_width{};
    int32_t dest_height{};
    int32_t camera_x{};
    int32_t camera_z{};
    uint32_t zoom_fp{fog_zoom_one};
};

/// Returns the map pixels shown by `count` destination pixels at a zoom.
///
/// @param zoom_fp 16.16 zoom; 0 is treated as 1
/// @param count destination pixels
/// @return (count - 1) * 65536 / zoom_fp + 1, or 0 for no pixels
[[nodiscard]] int32_t fog_map_span(uint32_t zoom_fp, int32_t count) noexcept;

/// Builds the fog tile masks for a view whose top-left map pixel is the camera.
///
/// Corners off the map are left clear, then the masks of the tiles straddling
/// the map border are copied outward so the border shows no fog edge. The
/// straddling tile is found from the border cell for any view size and zoom;
/// 3.1c copies the corners of the first and last-but-one tile, which straddle
/// the border only when the view is a whole number of tiles wide.
///
/// @param sight the viewer's sight grid; the viewer is `sight.viewpoint_player`
/// @param coverage the viewer's per-cell count of units seeing it
/// @param options line-of-sight and mapping rules
/// @param camera_x camera map-pixel X
/// @param camera_z camera map-pixel Z (screen row)
/// @param view_width view width in map pixels
/// @param view_height view height in map pixels
/// @return the fog grid
[[nodiscard]] FogGrid build_fog_grid(
    const sim::visibility_state::PlayerSightGrid& sight,
    std::span<const uint8_t> coverage,
    FogOptions options,
    int32_t camera_x,
    int32_t camera_z,
    int32_t view_width,
    int32_t view_height
);

/// Draws the fog grid over the view.
///
/// Wholly never-mapped tiles are filled, wholly unseen tiles grayed (or
/// dithered), and partial tiles drawn through their FOG.GAF masks, gray before
/// black.
///
/// @param[in,out] surface RGB world surface
/// @param view destination rectangle, camera and zoom
/// @param grid fog grid from build_fog_grid
/// @param tiles FOG.GAF tile art; without it partial tiles are skipped
/// @param shading gray levels, palette and fill colours
void draw_fog_grid(
    Surface& surface,
    const FogView& view,
    const FogGrid& grid,
    const FogTileSet& tiles,
    const FogShading& shading
);

/// Finds the nearest palette entry to each gray level among entries of similar brightness.
///
/// This is the search the game's gray table makes for an entry whose
/// (r+g+b)/3 is L.
///
/// @param palette the game palette
/// @param[out] levels palette index for each level (L, L, L)
void build_gray_levels(const Palette& palette, std::array<uint8_t, OA_PALETTE_COLORS>& levels);

} // namespace oa::present::world_renderer
