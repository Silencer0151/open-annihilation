// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Full tier's fog of war on the graphics card: the fog grid the
// processor builds each frame (world_fog.hpp), whose 32-pixel tiles sit half
// a sight cell off the terrain's tiles so that each tile's corners lie on
// four cell centres, drawn as passes of quads whose corner alphas are 1 at
// a fogged corner and 0 elsewhere, interpolated across each tile, in place
// of the FOG.GAF mask shapes:
//
// - the greyed pass over cells that are mapped but out of sight, which
//   draws the terrain under each such tile again from the greyed terrain
//   pages (the atlas built with a palette of the gray table's entries),
//   blended over the colour terrain by the corner alphas, so a tile whose
//   four corners are out of sight shows the gray table's colours exactly
//   and a tile at an edge ramps from colour to grey;
// - its dithered form, a solid quad of the dither colour at half the alpha,
//   the even tone the dither averages to, drawn over the objects;
// - the black pass over cells never mapped, a solid quad per tile.
//
// Beside the fog, the alphas of the quads that stand in for the two
// painters that shade the world beneath them, the kill board's shade and
// light levels among them (level_alpha).
//
// Pure: no SDL and no Runtime. The presentation chooses the terrain level,
// the targets and the colours, and runs the frame.
#pragma once

#include "oa/app/card.hpp"
#include "oa/present/gpu_world/terrain_atlas.hpp"
#include "oa/present/world_renderer/world_fog.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace oa::app::full_fog {

/// Where the fog grid lands in a target: the map pixel under the view's
/// top-left corner, which the grid was built for, the target pixel that map
/// pixel lands on, and the target pixels a map pixel spans.
struct FogPlacement {
    int32_t camera_x{}; ///< map pixel at the view's left edge
    int32_t camera_z{}; ///< map pixel at the view's top edge
    float origin_x{};   ///< target pixel the camera's map pixel lands on
    float origin_y{};
    float scale{1.0F}; ///< target pixels per map pixel, above 0
};

/// One level of the greyed terrain pages the greyed pass draws from, and
/// the share of the pass it carries: the terrain's own levels and shares at
/// the zoom, so that a tile wholly out of sight is filtered as the colour
/// terrain under it is.
struct GreyedLevel {
    uint8_t level{}; ///< the atlas level, below tile_level_count
    card::Sampling sampling{card::Sampling::nearest};
    float share{1.0F}; ///< the level's share of the greyed picture; the shares sum to 1
};

/// Most levels a greyed pass draws: the two that bracket a zoom below 1.
inline constexpr uint32_t most_greyed_levels = 2;

/// Corners of a fog tile, in the order the alphas are given: top-left,
/// top-right, bottom-left, bottom-right.
inline constexpr uint32_t fog_tile_corners = 4;

/// Rows of the display's shade and light tables, each scaling a colour.
inline constexpr int32_t table_rows = 32;
/// What one row of the shade table scales a colour by: row r scales by
/// r times this, so row 15 is about 1, unlit.
inline constexpr float shade_row_step = 0.06875F;
/// What one row of the light table adds to a colour's scale: row r scales
/// by 1 + r times this.
inline constexpr float light_row_step = 1.0F / 30.0F;

/// How a painter's shade or light level is drawn on the card: a quad of
/// the colour at the alpha, blended as the blend says, over the world.
struct LevelQuad {
    card::Colour colour{}; ///< black or white, at the alpha
    card::Blend blend{card::Blend::alpha};
};

/// Returns the quad that stands in for a shade or light level of the
/// display's tables over the world (the levels of shade_rect_level): a
/// negative level selects shade row 0x20 + level, clamped at 0, which
/// scales the colour by the row times shade_row_step, so the quad is black
/// by alpha at one less the scale, and nothing at a scale of 1 or more; a
/// level of 0 or more selects light row `level`, clamped at table_rows - 1,
/// which scales by 1 + row times light_row_step, so the quad is white added
/// at one less the scale's reciprocal, which lifts a colour toward white
/// and saturates as the table does.
///
/// @param level the level, shade below 0 and light from 0
/// @return the quad; alpha 0 where the level changes nothing
[[nodiscard]] LevelQuad level_quad(int32_t level) noexcept;

/// Returns the alpha at a fog tile's four corners from a corner mask: 1
/// where the corner's bit is set, 0 elsewhere.
///
/// @param mask the tile's corner mask (fog_corner_top_left and the rest)
/// @return the alphas, top-left, top-right, bottom-left, bottom-right
[[nodiscard]] std::array<float, fog_tile_corners> corner_alphas(uint8_t mask) noexcept;

/// Returns the alpha a level's pass draws a corner at, so that the levels
/// drawn one over another, each over what the earlier ones left, give the
/// corner's alpha of their weighted picture over the colour terrain:
/// alpha * share / (1 - alpha * later), where `later` is the sum of the
/// shares of the levels drawn after this one, and 0 where those cover the
/// corner wholly. Exact at the corners; across a tile the card interpolates
/// each pass's alphas on its own.
///
/// @param alpha the corner's alpha, 0 to 1
/// @param share the level's share of the greyed picture, 0 to 1
/// @param later the shares of the levels drawn after it, summing with `share` to at most 1
/// @return the pass's alpha at the corner, 0 to 1
[[nodiscard]] float pass_alpha(float alpha, float share, float later) noexcept;

/// Appends the greyed pass over the cells mapped but out of sight: for each
/// fog tile with any corner out of sight, the terrain under it from the
/// greyed pages, blended by card::Blend::alpha with the corner alphas
/// interpolated across the tile. A fog tile covers a quarter of each of four
/// terrain tiles, so it is drawn as four quads, each the quarter of a
/// terrain tile with the alphas the tile's corners interpolate to at its
/// corners; a terrain tile whose four fog tiles are wholly out of sight is
/// drawn once, whole, at alpha 1. A fog tile wholly never mapped, which the
/// black pass covers, adds nothing. Each level of `levels` is one pass over
/// the same quads, in order, at the alphas pass_alpha gives; one batch for
/// each run of quads on one page. Quads past the map's edge are left out,
/// as the terrain's are.
///
/// @param[in,out] frame the frame
/// @param grid the fog grid, built for the placement's camera
/// @param atlas the map's atlas, whose grid and slots the greyed pages share
/// @param greyed_pages the executor's greyed page for each page of the atlas, in order
/// @param levels the levels drawn, in order, with their shares
/// @param placement where the grid lands in the target
/// @param target the target the quads are drawn into; none for the window
/// @param scissor the pixels of the target the draw may write; null for all
/// @return quads appended, every level counted
uint32_t append_unseen_terrain(
    card::CardFrame& frame,
    const oa::present::world_renderer::FogGrid& grid,
    const oa::present::gpu_world::TerrainAtlas& atlas,
    std::span<const card::PageHandle> greyed_pages,
    std::span<const GreyedLevel> levels,
    const FogPlacement& placement,
    card::TargetHandle target,
    const card::Rect* scissor
);

/// Appends the dithered form of the greyed pass: for each fog tile with any
/// corner out of sight and not wholly never mapped, a solid quad of the
/// dither colour, blended by card::Blend::alpha at half the corner alphas,
/// the even tone the dither's every-other pixel averages to. Tiles wholly
/// out of sight that follow one another in a row are one quad. Unlike the
/// greyed pass it goes over the objects, as the black pass does, since the
/// processor dithers them with the ground.
///
/// @param[in,out] frame the frame
/// @param grid the fog grid, built for the placement's camera
/// @param dither the dither colour, its alpha ignored
/// @param placement where the grid lands in the target
/// @param target the target the quads are drawn into; none for the window
/// @param scissor the pixels of the target the draw may write; null for all
/// @return quads appended
uint32_t append_unseen_dither(
    card::CardFrame& frame,
    const oa::present::world_renderer::FogGrid& grid,
    const card::Colour& dither,
    const FogPlacement& placement,
    card::TargetHandle target,
    const card::Rect* scissor
);

/// Appends the black pass over the cells never mapped: for each fog tile
/// with any corner never mapped, a solid quad of the colour, blended by
/// card::Blend::alpha at the corner alphas. Tiles wholly never mapped that
/// follow one another in a row are one quad.
///
/// @param[in,out] frame the frame
/// @param grid the fog grid, built for the placement's camera
/// @param colour the colour of never-mapped ground, its alpha ignored
/// @param placement where the grid lands in the target
/// @param target the target the quads are drawn into; none for the window
/// @param scissor the pixels of the target the draw may write; null for all
/// @return quads appended
uint32_t append_unmapped(
    card::CardFrame& frame,
    const oa::present::world_renderer::FogGrid& grid,
    const card::Colour& colour,
    const FogPlacement& placement,
    card::TargetHandle target,
    const card::Rect* scissor
);

} // namespace oa::app::full_fog
