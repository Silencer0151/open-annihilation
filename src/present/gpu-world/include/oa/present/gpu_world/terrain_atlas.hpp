// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Card-ready terrain: a map's 32x32 tile mosaic as pages of RGBA8 texels,
// each with its mip chain, and the grid of atlas slots the map's cells show.
// Built once per map on the processor; nothing here draws.
//
// Each distinct tile the grid names takes one slot of slot_pitch texels a
// side at level 0: the tile with a gutter ring around it copied from the
// tile's edge, so that linear filtering never reads a neighbour. The gutter
// halves with each level and is one texel wide at the last tile level, so
// at levels 0, 1 and 2 every tile is a whole number of texels with a whole
// gutter ring. Level L of a page is the exact box reduction of level 0:
// each texel the average of the 2^L by 2^L level-0 texels under it, rounded
// once, halves up, in palette colour before the display gamma, which is the
// order today's zoomed-out terrain filter and conversion keep. At the tile
// levels the gutters are rebuilt from the level's own tile edges. Deeper
// levels reduce the whole page, tiles and gutters together, and complete
// the chain down to one texel; a renderer stops at the last tile level.

#include "oa/formats/hpi.hpp"
#include "oa/formats/tnt.hpp"
#include "oa/present/gpu_world/texel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace oa::present::gpu_world {

/// Edge of a terrain tile, in map pixels and level-0 texels.
inline constexpr uint32_t tile_edge = static_cast<uint32_t>(formats::tnt::layout::tile_edge_pixels);
/// Levels at which every tile is a whole number of texels with a whole
/// gutter ring: 0, 1 and 2, with tiles of 32, 16 and 8 texels a side.
inline constexpr uint32_t tile_level_count = 3;
/// Gutter ring around a tile at level 0, in texels; it halves with each
/// level and is one texel at the last tile level.
inline constexpr uint32_t level_0_gutter = 1U << (tile_level_count - 1U);
/// Texels a slot spans at level 0: the tile and the gutter on both sides.
inline constexpr uint32_t slot_pitch = tile_edge + 2U * level_0_gutter;
/// Largest page edge, in texels: the edge a card with no lower limit takes.
inline constexpr uint32_t page_edge_limit = 4096;
/// Smallest page edge: the power of two that holds one slot.
inline constexpr uint32_t page_edge_minimum = 64;
/// Most slots an atlas holds: a cell names its slot in 16 bits.
inline constexpr uint32_t slot_limit = 65536;
/// Most grid cells: the TNT reader's bound on attribute cells, four to a tile.
inline constexpr std::size_t grid_cell_limit =
    formats::tnt::limit::attribute_cells / (formats::tnt::layout::attribute_cells_per_tile_edge *
                                            formats::tnt::layout::attribute_cells_per_tile_edge);
/// Alpha of every texel: the terrain is opaque.
inline constexpr uint8_t texel_alpha = 255;

static_assert(page_edge_minimum >= slot_pitch);
static_assert(page_edge_minimum < 2U * slot_pitch);
static_assert(tile_edge % (1U << (tile_level_count - 1U)) == 0);
static_assert(slot_pitch % (1U << (tile_level_count - 1U)) == 0);

/// Says whether an edge is one a page may have: a power of two from
/// page_edge_minimum to page_edge_limit.
///
/// @param page_edge the edge, in texels
/// @return true for a page edge
[[nodiscard]] constexpr bool page_edge_valid(uint32_t page_edge) noexcept {
    return page_edge >= page_edge_minimum && page_edge <= page_edge_limit &&
           (page_edge & (page_edge - 1U)) == 0;
}

/// Counts the slots a full page of an edge holds: the whole slots along the
/// edge, squared.
///
/// @param page_edge a page edge
/// @return slots; 10,404 at 4096, 2,601 at 2048, 1 at page_edge_minimum
[[nodiscard]] constexpr uint32_t full_page_slots(uint32_t page_edge) noexcept {
    return (page_edge / slot_pitch) * (page_edge / slot_pitch);
}

/// Counts the most pages an atlas of an edge holds: slot_limit slots at
/// full_page_slots a page.
///
/// @param page_edge a page edge
/// @return pages; 7 at 4096, 26 at 2048
[[nodiscard]] constexpr uint32_t page_limit(uint32_t page_edge) noexcept {
    return (slot_limit + full_page_slots(page_edge) - 1U) / full_page_slots(page_edge);
}

/// Why a build, a lookup or a read was refused; none when it was not.
enum class TerrainAtlasError : uint8_t {
    none,
    empty_grid, ///< the map has no tile columns or no tile rows
    grid_size, ///< the tile grid holds other than tile_width * tile_height cells, or more than grid_cell_limit
    tile_bytes,         ///< the tile set holds other than tile_count tiles of pixels
    missing_tile,       ///< a cell names a tile the tile set lacks
    too_many_slots,     ///< more distinct tiles than slot_limit
    level_out_of_range, ///< a level past the tile levels, where tiles are no longer whole texels
    camera_unaligned,   ///< a camera not on a whole texel of the level
    no_output,          ///< a missing output or a stride shorter than a row
};

/// One level of a page's mip chain.
struct AtlasLevel {
    uint32_t width{};     ///< texels
    uint32_t height{};    ///< texels
    std::size_t offset{}; ///< first byte of the level's texels in the page's texels
};

/// One page: a texture whose edges are powers of two, with its mip chain.
struct AtlasPage {
    uint32_t width{};               ///< level-0 texels, from page_edge_minimum to page_edge_limit
    uint32_t height{};              ///< level-0 texels, from page_edge_minimum to page_edge_limit
    uint32_t columns{};             ///< slots in a row of the page
    uint32_t first_slot{};          ///< the atlas slot at the page's top-left
    uint32_t slot_count{};          ///< slots the page holds, row-major from first_slot
    std::vector<AtlasLevel> levels; ///< level 0 first; the last level is one texel
    std::vector<uint8_t> texels;    ///< every level's texels, level after level, texel_bytes each
};

/// A tile's texels at one level, without its gutter ring.
struct TileRect {
    uint32_t page{};
    uint32_t x{};    ///< texels from the level's left edge
    uint32_t y{};    ///< texels from the level's top edge
    uint32_t edge{}; ///< texels a side: tile_edge at level 0, halved at each level
};

/// A map's terrain, card-ready: its pages and the slot each cell shows.
struct TerrainAtlas {
    uint32_t grid_width{};      ///< cells across, the map's tile columns
    uint32_t grid_height{};     ///< cells down, the map's tile rows
    uint32_t page_edge{};       ///< the largest edge a page was planned within
    uint32_t slots_per_page{};  ///< slots a full page holds: full_page_slots of page_edge
    std::vector<uint16_t> grid; ///< the slot each cell shows, row-major
    std::vector<uint16_t>
        slot_tiles; ///< the map's tile each slot was filled from, the first cell's
    std::vector<AtlasPage> pages;
};

/// A page's edges in texels.
struct PageSize {
    uint32_t width{};
    uint32_t height{};
};

/// What an atlas of a given size costs in memory.
struct TerrainAtlasFootprint {
    uint32_t pages{};
    uint64_t texel_bytes{}; ///< every level of every page
    uint64_t table_bytes{}; ///< the grid and the slot table
    uint64_t
        building_bytes{}; ///< the builder's largest working storage beyond the atlas, freed when it returns
};

/// Names an error.
///
/// @param error the error
/// @return a short lower-case phrase, "no error" for none
[[nodiscard]] const char* terrain_atlas_error_text(TerrainAtlasError error) noexcept;

/// Fits a card's texture limit to a page edge.
///
/// The largest page edge not above the limit: page_edge_limit where the
/// card allows it, 2048 on a card whose textures stop there, and
/// page_edge_minimum for a limit below it. Every function that takes a
/// page edge fits it first, so a renderer's limit may be passed as it is.
///
/// @param texture_limit texels a side the card's textures may have
/// @return a page edge, page_edge_valid
[[nodiscard]] uint32_t fit_page_edge(uint32_t texture_limit) noexcept;

/// Chooses the smallest page that holds a number of slots.
///
/// Each edge is a power of two from page_edge_minimum to the page edge; of
/// the pages whose rows of whole slots hold the count, the smallest area
/// wins, then the one with the shorter long edge, then the wider of two the
/// same shape. A count of 0 plans as 1 and a count above full_page_slots of
/// the edge as a full page.
///
/// @param slots slots the page is to hold
/// @param page_edge the largest edge a page may have, fitted by fit_page_edge
/// @return the page's edges
[[nodiscard]] PageSize plan_page(uint32_t slots, uint32_t page_edge) noexcept;

/// Sizes a level of a page: each edge halves until it reaches one texel.
///
/// @param page level-0 edges
/// @param level level, from 0
/// @return the level's edges
[[nodiscard]] PageSize level_size(PageSize page, uint32_t level) noexcept;

/// Counts the levels of a page's chain, down to one texel.
///
/// @param page level-0 edges
/// @return levels, log2 of the longer edge plus one
[[nodiscard]] uint32_t level_count(PageSize page) noexcept;

/// Gives the gutter around a tile at a tile level.
///
/// @param level level below tile_level_count
/// @return texels, level_0_gutter halved per level
[[nodiscard]] uint32_t level_gutter(uint32_t level) noexcept;

/// Costs an atlas before building it.
///
/// Slots fill pages of full_page_slots of the edge, the last page planned
/// for the rest; counts beyond slot_limit are costed as slot_limit.
///
/// @param slots distinct tiles the grid names
/// @param grid_cells cells of the grid
/// @param page_edge the largest edge a page may have, fitted by fit_page_edge
/// @return the pages, their texels, the tables and the builder's working storage
[[nodiscard]] TerrainAtlasFootprint
terrain_atlas_footprint(uint32_t slots, uint64_t grid_cells, uint32_t page_edge) noexcept;

/// Builds the atlas of a map's terrain.
///
/// Walks the grid row by row, giving each distinct tile, by its pixels, the
/// next slot in order of first use, so cells that share a tile share a
/// slot; tiles no cell names take none. Pages fill in slot order with
/// full_page_slots of the edge each, the last with the rest, each planned
/// by plan_page for the slots it holds. A texel of a tile is the tile's
/// pixel through the palette and then the gamma table, the bytes today's
/// terrain fill and conversion give it at zoom 1; every other texel of a
/// page is opaque black. Refuses a malformed map, leaving the atlas empty.
///
/// @param map the parsed map
/// @param palette the palette the tiles' indices are shown in
/// @param gamma display gamma per channel, applied after the palette; null for none
/// @param page_edge the largest edge a page may have, fitted by fit_page_edge
/// @param[out] atlas the atlas; emptied first, and empty on failure
/// @return none on success
[[nodiscard]] TerrainAtlasError build_terrain_atlas(
    const formats::tnt::Map& map,
    const PaletteBytes& palette,
    const std::array<uint8_t, 256>* gamma,
    uint32_t page_edge,
    TerrainAtlas& atlas
);

/// Finds a slot's tile at a tile level.
///
/// @param atlas the atlas
/// @param slot the slot, below the slot table's size
/// @param level level below tile_level_count
/// @return the tile's texels, or nullopt for a slot or level the atlas lacks
[[nodiscard]] std::optional<TileRect>
tile_rect(const TerrainAtlas& atlas, uint32_t slot, uint32_t level) noexcept;

/// Reads the view a card shows at a tile level with every texel whole.
///
/// Texel (x, y) of the view shows map pixel (camera_x + x * 2^level,
/// camera_y + y * 2^level) and the 2^level by 2^level pixels from it,
/// averaged as the level holds them; past the map's edge it is opaque
/// black, as today's fill paints ground beyond the terrain. The camera lies
/// on a multiple of 2^level along each axis: today's zoomed-out filter
/// takes a camera on any map pixel, and a level holds no texel for one
/// between its own, so a renderer snaps the camera to the level's grid or
/// shows the view shifted by the remainder, under one texel of the level.
///
/// @param atlas the atlas
/// @param level level below tile_level_count
/// @param camera_x map pixel of the view's left column
/// @param camera_y map pixel of the view's top row
/// @param width view texels across
/// @param height view texels down
/// @param[out] rgba height rows of width texels, texel_bytes each
/// @param stride_bytes bytes between the rows of rgba, at least a row's
/// @return none on success; nothing is written on failure
[[nodiscard]] TerrainAtlasError read_terrain_view(
    const TerrainAtlas& atlas,
    uint32_t level,
    uint32_t camera_x,
    uint32_t camera_y,
    uint32_t width,
    uint32_t height,
    uint8_t* rgba,
    std::size_t stride_bytes
) noexcept;

} // namespace oa::present::gpu_world
