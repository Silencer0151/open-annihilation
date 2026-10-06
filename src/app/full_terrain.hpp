// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Full tier's terrain on the graphics card: the rule that chooses, for a
// zoom, which levels of the terrain atlas are drawn, how each is sampled and
// blended and whether a target stands between the pages and the window;
// and the quads of the terrain tiles a view shows, appended to a card
// command list (card.hpp) from the atlas's pages (terrain_atlas.hpp). Pure:
// no SDL and no Runtime. runtime_full.cpp uploads the pages, builds each
// frame with these and runs it.
#pragma once

#include "oa/app/card.hpp"
#include "oa/present/gpu_world/terrain_atlas.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace oa::app::full_terrain {

/// One draw of the terrain from one level of the atlas.
struct TerrainPass {
    uint8_t level{}; ///< the atlas level drawn: 0 or 1
    card::Sampling sampling{card::Sampling::nearest};
    card::Blend blend{card::Blend::none};
    float alpha{1.0F}; ///< the vertex alpha of the quads, which the blend weighs the pass by
};

/// Most passes one zoom draws: the two levels that bracket a zoom below 1.
inline constexpr uint32_t most_terrain_passes = 2;

/// How the Full tier draws the terrain at a zoom, with no supersampling.
///
/// Zoomed out, below zoom 1, the two atlas levels that bracket the zoom are
/// blended: with t = log2(1 / zoom), level ceil(t) LINEAR, then the level
/// above it LINEAR over it at alpha 1 - (t - (ceil(t) - 1)); at a whole t
/// that level alone. So at zoom 0.5 level 1 alone is drawn, each of its
/// texels one window pixel, which is today's box filter where the camera
/// lies on an even map pixel; between 0.5 and 0.25 level 2 is blended under
/// level 1; and at 0.25 and below, which only the Full tier's zoom floor
/// (kMinFullBattlefieldZoom) reaches, level 2, the last tile level, is
/// drawn alone, reduced. At zoom 1 and at
/// every whole-number zoom above it level 0 is drawn NEAREST: 3.1c's pixels
/// exactly. At another zoom above 1 level 0 is drawn by the pixel-art
/// sampling mode where the renderer has it; elsewhere, SDL's software
/// renderer among them, it is drawn NEAREST into a target at the next whole
/// number and the target drawn LINEAR to the window by zoom over that
/// number, which is the Basic tier's sharp-bilinear.
struct TerrainDrawPlan {
    std::array<TerrainPass, most_terrain_passes> passes{}; ///< in the order drawn
    uint32_t pass_count{};
    /// The passes are drawn into a target at target_zoom, which is then
    /// drawn LINEAR to the window.
    bool through_target{};
    uint32_t target_zoom{1}; ///< window pixels per map pixel in the target; ceil(zoom)
};

/// The level the zoomed-out blend draws first between zoom 0.5 and 1, under
/// level 0, and alone at 0.5.
inline constexpr uint8_t far_level = 1;

/// Returns how the terrain is drawn at a zoom (TerrainDrawPlan).
///
/// @param zoom window pixels per map pixel, above 0
/// @param pixel_art_sampling the renderer draws card::Sampling::pixel_art
///        as its own mode, not as NEAREST
/// @return the plan
[[nodiscard]] TerrainDrawPlan plan_terrain_draw(float zoom, bool pixel_art_sampling) noexcept;

/// Where a view of the map lands in a target and at what scale.
struct TerrainView {
    int32_t camera_x{}; ///< map pixel at the view's left edge; below 0 left of the map
    int32_t camera_y{}; ///< map pixel at the view's top edge; below 0 above the map
    float origin_x{};   ///< target pixel the view's left edge lands on
    float origin_y{};   ///< target pixel the view's top edge lands on
    float scale{1.0F};  ///< target pixels per map pixel, above 0
    uint32_t width{};   ///< target pixels the view spans across
    uint32_t height{};  ///< target pixels the view spans down
    /// Every tile's edges on whole target pixels, each edge where its map
    /// pixel lands rounded to the nearest, so that the tiles of a zoom
    /// whose tiles span a fraction of a pixel meet on every renderer.
    bool whole_pixels{};
};

/// The tiles of the grid a view shows: columns first_column to end_column
/// less one, rows first_row to end_row less one; empty when the view lies
/// wholly before or past the map.
struct TileRange {
    uint32_t first_column{};
    uint32_t end_column{};
    uint32_t first_row{};
    uint32_t end_row{};
};

/// Returns the tiles a view shows: from the tile under the camera to the
/// tile under the view's far edge, within the grid; a camera before the
/// map starts at the grid's first tile.
///
/// @param atlas the map's atlas, whose grid is the map's tiles
/// @param view the view
/// @return the range; empty wholly before or past the map's edges
[[nodiscard]] TileRange
visible_tiles(const oa::present::gpu_world::TerrainAtlas& atlas, const TerrainView& view) noexcept;

/// Appends the quads of a view's tiles at a level to a frame, one batch for
/// each page the tiles read, in page order.
///
/// Each tile is a quad of tile_edge times the view's scale pixels a side at
/// origin + (tile - camera) * scale, textured by the tile's texels at the
/// level, its gutter ring left out, so that a LINEAR draw reads the gutter
/// and never a neighbour, every corner in the pass's colour (white at its
/// alpha). The tiles of one pass never overlap, so the quads are grouped by
/// page whatever their order in the grid: a view costs the card as many
/// calls as the atlas has pages, never more. The vertices are appended in
/// grid order, row by row, and each page's batch names one run of the
/// frame's indices. Tiles past the map's edge are not drawn; what the
/// clear left there, black, is what today's fill paints past the terrain.
///
/// @param[in,out] frame the frame
/// @param atlas the map's atlas
/// @param pages the executor's page for each page of the atlas, in order
/// @param view the view
/// @param pass the level, sampling, blend and alpha
/// @param target the target the quads are drawn into; none for the window
/// @param scissor the pixels of the target the draw may write; null for all
/// @return quads appended
uint32_t append_terrain_tiles(
    card::CardFrame& frame,
    const oa::present::gpu_world::TerrainAtlas& atlas,
    std::span<const card::PageHandle> pages,
    const TerrainView& view,
    const TerrainPass& pass,
    card::TargetHandle target,
    const card::Rect* scissor
);

} // namespace oa::app::full_terrain
