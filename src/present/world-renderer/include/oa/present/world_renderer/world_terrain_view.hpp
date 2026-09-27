// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The camera's view of the map's 32x32 tile mosaic, drawn as 8-bit indices
// into the battlefield rectangle of a surface.

#include "oa/core/game_state.h"
#include "oa/present/surface.h"

#include <cstdint>

namespace oa::present::world_renderer {

inline constexpr int32_t terrain_tile_pixels = 32;
inline constexpr int32_t terrain_tile_bytes = terrain_tile_pixels * terrain_tile_pixels;

// The tile mosaic, resolved by the caller from Game.tile_map and the map's
// tile set.
struct TerrainTiles {
    const uint16_t* tile_map =
        nullptr; // one tile index per mosaic cell, Game.map_width / 2 per row
    const uint8_t* tile_pixels = nullptr; // the tile set's pixels, terrain_tile_bytes per tile
};

/// Draws the camera's view of the tile mosaic at the battlefield rectangle's top-left.
///
/// The view is at (Game.camera_x, Game.camera_y), Game.viewport_width x
/// viewport_height map pixels. The battlefield rectangle (Game.battlefield_rect)
/// is inclusive: (0x80, 0x20) to (width - 1, height - 0x21) of the off-screen
/// surface once the game screen is laid out. Tiles cut by the view's edges go through
/// draw_sprite_opaque, clipped to the target, then the whole tiles between them
/// through blit_tile, unclipped.
///
/// @param[in,out] target 8-bit surface to draw on
/// @param game game block giving the camera, view size, map size and battlefield rectangle
/// @param tiles tile map and tile pixels
/// @return false, drawing nothing, when the view reaches past the mosaic or the tiles are missing
/// @quirk Tiles on both a cut row and a cut column are drawn twice, and whole tiles read the mosaic with the low 16 bits of its row length.
bool draw_terrain_view(::oa::Surface* target, const Game& game, const TerrainTiles& tiles) noexcept;

} // namespace oa::present::world_renderer
