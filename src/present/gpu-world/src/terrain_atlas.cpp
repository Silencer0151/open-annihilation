// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/gpu_world/terrain_atlas.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>

namespace oa::present::gpu_world {
namespace {

/// Bytes of one palette colour as the builder keeps it: red, green, blue.
constexpr uint32_t colour_bytes = 3;
/// Channels summed per texel while reducing a page.
constexpr uint32_t sum_channels = 3;
/// The level whose sums seed the reduction of the deeper levels: the last
/// tile level, whose blocks are whole within every slot.
constexpr uint32_t seed_level = tile_level_count - 1U;
/// A tile the slot walk has not met yet.
constexpr uint32_t no_slot = std::numeric_limits<uint32_t>::max();
/// FNV-1a parameters of the tile hash.
constexpr uint64_t fnv_offset = 14695981039346656037ULL;
constexpr uint64_t fnv_prime = 1099511628211ULL;

// A channel's sum over a whole page of 255s fits the sum's word.
static_assert(255ULL * page_edge_limit * page_edge_limit <= std::numeric_limits<uint32_t>::max());
static_assert(texel_bytes == colour_bytes + 1U);

/// Hashes a tile's pixels.
///
/// @param pixels the tile's palette indices
/// @return the FNV-1a hash of the bytes
uint64_t hash_tile(const uint8_t* pixels) noexcept {
    uint64_t hash = fnv_offset;
    for (std::size_t i = 0; i < formats::tnt::layout::tile_bytes; ++i) {
        hash ^= pixels[i];
        hash *= fnv_prime;
    }
    return hash;
}

/// Writes texels of opaque black.
///
/// @param[out] texels first texel
/// @param count texels to write
void fill_opaque_black(uint8_t* texels, std::size_t count) noexcept {
    for (std::size_t i = 0; i < count; ++i, texels += texel_bytes) {
        texels[0] = 0;
        texels[1] = 0;
        texels[2] = 0;
        texels[3] = texel_alpha;
    }
}

/// Writes one texel from a palette colour through the gamma table.
///
/// @param[out] texel the texel
/// @param rgb red, green and blue before gamma
/// @param gamma the gamma table, or null for none
void write_texel(
    uint8_t* texel, const uint8_t* rgb, const std::array<uint8_t, 256>* gamma
) noexcept {
    if (gamma == nullptr) {
        texel[0] = rgb[0];
        texel[1] = rgb[1];
        texel[2] = rgb[2];
    } else {
        texel[0] = (*gamma)[rgb[0]];
        texel[1] = (*gamma)[rgb[1]];
        texel[2] = (*gamma)[rgb[2]];
    }
    texel[3] = texel_alpha;
}

/// Averages a block of palette colours, rounding halves up.
///
/// @param sums the block's channel sums
/// @param count texels in the block
/// @param[out] rgb the averaged colour
void average(const uint32_t* sums, uint32_t count, uint8_t* rgb) noexcept {
    for (uint32_t channel = 0; channel < colour_bytes; ++channel)
        rgb[channel] = static_cast<uint8_t>((sums[channel] + count / 2U) / count);
}

/// Where a slot lies on its page.
struct SlotPlace {
    uint32_t page{};
    uint32_t column{};
    uint32_t row{};
};

/// Locates a slot on its page.
///
/// @param atlas the atlas
/// @param slot the slot, below the slot table's size
/// @return the page and the slot's column and row of slots
SlotPlace place_slot(const TerrainAtlas& atlas, uint32_t slot) noexcept {
    const uint32_t page = slot / atlas.slots_per_page;
    const uint32_t within = slot - atlas.pages[page].first_slot;
    const uint32_t columns = atlas.pages[page].columns;
    return {page, within % columns, within / columns};
}

/// A slot's level-0 content in palette colour: the tile with its gutter
/// ring, each gutter texel the nearest tile pixel.
using SlotColour = std::array<uint8_t, slot_pitch * slot_pitch * colour_bytes>;

/// Fills a slot's level-0 colours from a tile.
///
/// @param pixels the tile's palette indices
/// @param lut red, green and blue per palette index
/// @param[out] colour the slot's colours
void fill_slot_colour(const uint8_t* pixels, const uint8_t* lut, SlotColour& colour) noexcept {
    uint8_t* out = colour.data();
    for (uint32_t y = 0; y < slot_pitch; ++y) {
        const uint32_t tile_y = std::clamp<int32_t>(
            static_cast<int32_t>(y) - static_cast<int32_t>(level_0_gutter),
            0,
            static_cast<int32_t>(tile_edge) - 1
        );
        const uint8_t* row = pixels + static_cast<std::size_t>(tile_y) * tile_edge;
        for (uint32_t x = 0; x < slot_pitch; ++x, out += colour_bytes) {
            const uint32_t tile_x = std::clamp<int32_t>(
                static_cast<int32_t>(x) - static_cast<int32_t>(level_0_gutter),
                0,
                static_cast<int32_t>(tile_edge) - 1
            );
            std::memcpy(
                out, lut + static_cast<std::size_t>(row[tile_x]) * colour_bytes, colour_bytes
            );
        }
    }
}

/// Writes a slot at one tile level: the tile reduced by the exact box, and
/// the gutter ring rebuilt from the reduced tile's edges.
///
/// @param colour the slot's level-0 colours
/// @param level tile level
/// @param gamma the gamma table, or null for none
/// @param[out] texels the level's texels at the slot's top-left
/// @param level_width texels in a row of the level
void write_slot_level(
    const SlotColour& colour,
    uint32_t level,
    const std::array<uint8_t, 256>* gamma,
    uint8_t* texels,
    uint32_t level_width
) noexcept {
    const uint32_t block = 1U << level;
    const uint32_t count = block * block;
    const uint32_t tile = tile_edge >> level;
    const uint32_t gutter = level_0_gutter >> level;
    const uint32_t pitch = slot_pitch >> level;
    std::array<uint8_t, tile_edge * tile_edge * colour_bytes> reduced{};
    for (uint32_t ty = 0; ty < tile; ++ty) {
        for (uint32_t tx = 0; tx < tile; ++tx) {
            uint32_t sums[sum_channels] = {0, 0, 0};
            for (uint32_t by = 0; by < block; ++by) {
                const uint8_t* row =
                    colour.data() +
                    (static_cast<std::size_t>(level_0_gutter + ty * block + by) * slot_pitch +
                     level_0_gutter + tx * block) *
                        colour_bytes;
                for (uint32_t bx = 0; bx < block; ++bx, row += colour_bytes) {
                    sums[0] += row[0];
                    sums[1] += row[1];
                    sums[2] += row[2];
                }
            }
            average(
                sums, count, &reduced[(static_cast<std::size_t>(ty) * tile + tx) * colour_bytes]
            );
        }
    }
    for (uint32_t y = 0; y < pitch; ++y) {
        const uint32_t ty = std::clamp<int32_t>(
            static_cast<int32_t>(y) - static_cast<int32_t>(gutter),
            0,
            static_cast<int32_t>(tile) - 1
        );
        uint8_t* out = texels + static_cast<std::size_t>(y) * level_width * texel_bytes;
        for (uint32_t x = 0; x < pitch; ++x, out += texel_bytes) {
            const uint32_t tx = std::clamp<int32_t>(
                static_cast<int32_t>(x) - static_cast<int32_t>(gutter),
                0,
                static_cast<int32_t>(tile) - 1
            );
            write_texel(
                out, &reduced[(static_cast<std::size_t>(ty) * tile + tx) * colour_bytes], gamma
            );
        }
    }
}

/// Adds a slot's level-0 colours, gutters included, into the page's sums at
/// the seed level, one sum per seed-level block.
///
/// @param colour the slot's level-0 colours
/// @param[out] sums the page's seed-level sums at the slot's first block
/// @param sums_width blocks in a row of the page at the seed level
void add_slot_sums(const SlotColour& colour, uint32_t* sums, uint32_t sums_width) noexcept {
    constexpr uint32_t block = 1U << seed_level;
    constexpr uint32_t blocks = slot_pitch >> seed_level;
    for (uint32_t y = 0; y < slot_pitch; ++y) {
        const uint8_t* row =
            colour.data() + static_cast<std::size_t>(y) * slot_pitch * colour_bytes;
        uint32_t* sum_row = sums + static_cast<std::size_t>(y / block) * sums_width * sum_channels;
        for (uint32_t x = 0; x < slot_pitch; ++x, row += colour_bytes) {
            uint32_t* sum = sum_row + static_cast<std::size_t>(x / block) * sum_channels;
            sum[0] += row[0];
            sum[1] += row[1];
            sum[2] += row[2];
        }
    }
    static_assert(blocks * block == slot_pitch);
}

/// Lays out a page's levels and allocates its texels, all opaque black.
///
/// @param[in,out] page the page, with its edges set
void allocate_page(AtlasPage& page) {
    const PageSize size{page.width, page.height};
    const uint32_t levels = level_count(size);
    page.levels.resize(levels);
    std::size_t offset = 0;
    for (uint32_t level = 0; level < levels; ++level) {
        const PageSize edges = level_size(size, level);
        page.levels[level] = AtlasLevel{edges.width, edges.height, offset};
        offset += static_cast<std::size_t>(edges.width) * edges.height * texel_bytes;
    }
    page.texels.resize(offset);
    fill_opaque_black(page.texels.data(), offset / texel_bytes);
}

/// Writes the levels past the seed level from the seed level's sums, each
/// texel the exact average of the level-0 texels under it.
///
/// @param[in,out] page the page, with its tile levels written
/// @param seed_sums the seed level's sums, consumed
/// @param gamma the gamma table, or null for none
void reduce_deeper_levels(
    AtlasPage& page, std::vector<uint32_t>&& seed_sums, const std::array<uint8_t, 256>* gamma
) {
    const PageSize size{page.width, page.height};
    std::vector<uint32_t> previous = std::move(seed_sums);
    PageSize previous_size = level_size(size, seed_level);
    for (uint32_t level = seed_level + 1U; level < page.levels.size(); ++level) {
        const PageSize edges = level_size(size, level);
        const uint32_t across = previous_size.width / edges.width;
        const uint32_t down = previous_size.height / edges.height;
        const uint32_t count = (page.width / edges.width) * (page.height / edges.height);
        std::vector<uint32_t> sums(
            static_cast<std::size_t>(edges.width) * edges.height * sum_channels
        );
        uint8_t* out = page.texels.data() + page.levels[level].offset;
        for (uint32_t y = 0; y < edges.height; ++y) {
            for (uint32_t x = 0; x < edges.width; ++x, out += texel_bytes) {
                uint32_t* sum =
                    &sums[(static_cast<std::size_t>(y) * edges.width + x) * sum_channels];
                for (uint32_t j = 0; j < down; ++j) {
                    const uint32_t* source =
                        &previous
                            [(static_cast<std::size_t>(y * down + j) * previous_size.width +
                              static_cast<std::size_t>(x) * across) *
                             sum_channels];
                    for (uint32_t i = 0; i < across; ++i, source += sum_channels) {
                        sum[0] += source[0];
                        sum[1] += source[1];
                        sum[2] += source[2];
                    }
                }
                uint8_t rgb[colour_bytes];
                average(sum, count, rgb);
                write_texel(out, rgb, gamma);
            }
        }
        previous = std::move(sums);
        previous_size = edges;
    }
}

/// Builds one page: its slots at the tile levels, then the deeper levels.
///
/// @param map the map the slots' tiles come from
/// @param atlas the atlas, with its slot table and the page's edges set
/// @param page_index the page
/// @param lut red, green and blue per palette index
/// @param gamma the gamma table, or null for none
void build_page(
    const formats::tnt::Map& map,
    TerrainAtlas& atlas,
    uint32_t page_index,
    const uint8_t* lut,
    const std::array<uint8_t, 256>* gamma
) {
    AtlasPage& page = atlas.pages[page_index];
    allocate_page(page);
    const PageSize seed_edges = level_size({page.width, page.height}, seed_level);
    std::vector<uint32_t> seed_sums(
        static_cast<std::size_t>(seed_edges.width) * seed_edges.height * sum_channels
    );
    SlotColour colour{};
    for (uint32_t within = 0; within < page.slot_count; ++within) {
        const uint32_t slot = page.first_slot + within;
        const SlotPlace place = place_slot(atlas, slot);
        const uint8_t* pixels =
            map.tile_palette_indices.data() +
            static_cast<std::size_t>(atlas.slot_tiles[slot]) * formats::tnt::layout::tile_bytes;
        fill_slot_colour(pixels, lut, colour);
        for (uint32_t level = 0; level < tile_level_count; ++level) {
            const uint32_t pitch = slot_pitch >> level;
            const AtlasLevel& edges = page.levels[level];
            uint8_t* texels = page.texels.data() + edges.offset +
                              (static_cast<std::size_t>(place.row) * pitch * edges.width +
                               static_cast<std::size_t>(place.column) * pitch) *
                                  texel_bytes;
            write_slot_level(colour, level, gamma, texels, edges.width);
        }
        constexpr uint32_t seed_pitch = slot_pitch >> seed_level;
        add_slot_sums(
            colour,
            seed_sums.data() +
                (static_cast<std::size_t>(place.row) * seed_pitch * seed_edges.width +
                 static_cast<std::size_t>(place.column) * seed_pitch) *
                    sum_channels,
            seed_edges.width
        );
    }
    reduce_deeper_levels(page, std::move(seed_sums), gamma);
}

} // namespace

const char* terrain_atlas_error_text(TerrainAtlasError error) noexcept {
    switch (error) {
    case TerrainAtlasError::none:
        return "no error";
    case TerrainAtlasError::empty_grid:
        return "the map has no tiles";
    case TerrainAtlasError::grid_size:
        return "the tile grid does not match the map's size";
    case TerrainAtlasError::tile_bytes:
        return "the tile set does not match the tile count";
    case TerrainAtlasError::missing_tile:
        return "a cell names a tile the tile set lacks";
    case TerrainAtlasError::too_many_slots:
        return "more distinct tiles than the atlas holds";
    case TerrainAtlasError::level_out_of_range:
        return "the level is past the tile levels";
    case TerrainAtlasError::camera_unaligned:
        return "the camera is not on a whole texel of the level";
    case TerrainAtlasError::no_output:
        return "the output is missing or its rows are too short";
    }
    return "unknown error";
}

uint32_t fit_page_edge(uint32_t texture_limit) noexcept {
    uint32_t edge = page_edge_minimum;
    while (edge < page_edge_limit && 2U * edge <= texture_limit)
        edge *= 2U;
    return edge;
}

PageSize plan_page(uint32_t slots, uint32_t page_edge) noexcept {
    page_edge = fit_page_edge(page_edge);
    slots = std::clamp(slots, 1U, full_page_slots(page_edge));
    PageSize best{page_edge, page_edge};
    uint64_t best_area = static_cast<uint64_t>(page_edge) * page_edge;
    for (uint32_t width = page_edge_minimum; width <= page_edge; width *= 2U) {
        for (uint32_t height = page_edge_minimum; height <= page_edge; height *= 2U) {
            const uint64_t capacity =
                static_cast<uint64_t>(width / slot_pitch) * (height / slot_pitch);
            if (capacity < slots)
                continue;
            const uint64_t area = static_cast<uint64_t>(width) * height;
            const uint32_t long_edge = std::max(width, height);
            const uint32_t best_long_edge = std::max(best.width, best.height);
            const bool better =
                area < best_area ||
                (area == best_area && (long_edge < best_long_edge ||
                                       (long_edge == best_long_edge && width > best.width)));
            if (better) {
                best = PageSize{width, height};
                best_area = area;
            }
        }
    }
    return best;
}

PageSize level_size(PageSize page, uint32_t level) noexcept {
    const uint32_t shift = std::min(level, 31U);
    return PageSize{std::max(page.width >> shift, 1U), std::max(page.height >> shift, 1U)};
}

uint32_t level_count(PageSize page) noexcept {
    uint32_t levels = 1;
    for (uint32_t edge = std::max(page.width, page.height); edge > 1U; edge >>= 1U)
        ++levels;
    return levels;
}

uint32_t level_gutter(uint32_t level) noexcept {
    return level < tile_level_count ? level_0_gutter >> level : 0U;
}

TerrainAtlasFootprint
terrain_atlas_footprint(uint32_t slots, uint64_t grid_cells, uint32_t page_edge) noexcept {
    TerrainAtlasFootprint footprint{};
    page_edge = fit_page_edge(page_edge);
    const uint32_t per_page = full_page_slots(page_edge);
    slots = std::min(slots, slot_limit);
    footprint.table_bytes =
        grid_cells * sizeof(uint16_t) + static_cast<uint64_t>(slots) * sizeof(uint16_t);
    footprint.pages = (slots + per_page - 1U) / per_page;
    for (uint32_t page = 0; page < footprint.pages; ++page) {
        const uint32_t held = std::min(per_page, slots - page * per_page);
        const PageSize size = plan_page(held, page_edge);
        const uint32_t levels = level_count(size);
        for (uint32_t level = 0; level < levels; ++level) {
            const PageSize edges = level_size(size, level);
            footprint.texel_bytes +=
                static_cast<uint64_t>(edges.width) * edges.height * texel_bytes;
        }
        const PageSize seed = level_size(size, seed_level);
        const PageSize next = level_size(size, seed_level + 1U);
        const uint64_t working = (static_cast<uint64_t>(seed.width) * seed.height +
                                  static_cast<uint64_t>(next.width) * next.height) *
                                 sum_channels * sizeof(uint32_t);
        footprint.building_bytes = std::max(footprint.building_bytes, working);
    }
    return footprint;
}

TerrainAtlasError build_terrain_atlas(
    const formats::tnt::Map& map,
    uint32_t columns,
    uint32_t rows,
    const PaletteBytes& palette,
    const std::array<uint8_t, 256>* gamma,
    uint32_t page_edge,
    TerrainAtlas& atlas
) {
    atlas = TerrainAtlas{};
    if (map.tile_width == 0 || map.tile_height == 0 || columns == 0 || rows == 0)
        return TerrainAtlasError::empty_grid;
    const uint64_t map_cells = static_cast<uint64_t>(map.tile_width) * map.tile_height;
    if (map_cells > grid_cell_limit || map_cells != map.tile_indices.size() ||
        columns > map.tile_width || rows > map.tile_height)
        return TerrainAtlasError::grid_size;
    if (static_cast<uint64_t>(map.tile_count) * formats::tnt::layout::tile_bytes !=
        map.tile_palette_indices.size())
        return TerrainAtlasError::tile_bytes;

    TerrainAtlas built;
    built.grid_width = columns;
    built.grid_height = rows;
    built.page_edge = fit_page_edge(page_edge);
    built.slots_per_page = full_page_slots(built.page_edge);
    built.grid.resize(static_cast<std::size_t>(columns) * rows);
    std::vector<uint32_t> slot_of_tile(map.tile_count, no_slot);
    std::unordered_map<uint64_t, uint32_t> slot_of_hash;
    for (std::size_t cell = 0; cell < built.grid.size(); ++cell) {
        // The grid's cell in the map's own grid, whose rows are tile_width
        // cells long.
        const std::size_t map_cell = (cell / columns) * map.tile_width + cell % columns;
        const uint16_t tile = map.tile_indices[map_cell];
        if (tile >= map.tile_count)
            return TerrainAtlasError::missing_tile;
        uint32_t& slot = slot_of_tile[tile];
        if (slot == no_slot) {
            const uint8_t* pixels =
                map.tile_palette_indices.data() +
                static_cast<std::size_t>(tile) * formats::tnt::layout::tile_bytes;
            const uint64_t hash = hash_tile(pixels);
            const auto found = slot_of_hash.find(hash);
            const bool same = found != slot_of_hash.end() &&
                              std::memcmp(
                                  pixels,
                                  map.tile_palette_indices.data() +
                                      static_cast<std::size_t>(built.slot_tiles[found->second]) *
                                          formats::tnt::layout::tile_bytes,
                                  formats::tnt::layout::tile_bytes
                              ) == 0;
            if (same) {
                slot = found->second;
            } else {
                if (built.slot_tiles.size() >= slot_limit)
                    return TerrainAtlasError::too_many_slots;
                slot = static_cast<uint32_t>(built.slot_tiles.size());
                built.slot_tiles.push_back(tile);
                if (found == slot_of_hash.end())
                    slot_of_hash.emplace(hash, slot);
            }
        }
        built.grid[cell] = static_cast<uint16_t>(slot);
    }

    const auto slots = static_cast<uint32_t>(built.slot_tiles.size());
    const uint32_t pages = (slots + built.slots_per_page - 1U) / built.slots_per_page;
    built.pages.resize(pages);
    for (uint32_t index = 0; index < pages; ++index) {
        AtlasPage& page = built.pages[index];
        page.first_slot = index * built.slots_per_page;
        page.slot_count = std::min(built.slots_per_page, slots - page.first_slot);
        const PageSize size = plan_page(page.slot_count, built.page_edge);
        page.width = size.width;
        page.height = size.height;
        page.columns = size.width / slot_pitch;
    }

    std::array<uint8_t, 256U * colour_bytes> lut{};
    for (std::size_t index = 0; index < 256U; ++index)
        std::memcpy(
            &lut[index * colour_bytes], &palette[index * palette_entry_bytes], colour_bytes
        );
    for (uint32_t index = 0; index < pages; ++index)
        build_page(map, built, index, lut.data(), gamma);

    atlas = std::move(built);
    return TerrainAtlasError::none;
}

std::optional<TileRect>
tile_rect(const TerrainAtlas& atlas, uint32_t slot, uint32_t level) noexcept {
    if (level >= tile_level_count || slot >= atlas.slot_tiles.size() || atlas.slots_per_page == 0 ||
        slot / atlas.slots_per_page >= atlas.pages.size())
        return std::nullopt;
    const SlotPlace place = place_slot(atlas, slot);
    const uint32_t pitch = slot_pitch >> level;
    const uint32_t gutter = level_0_gutter >> level;
    return TileRect{
        place.page, place.column * pitch + gutter, place.row * pitch + gutter, tile_edge >> level
    };
}

TerrainAtlasError read_terrain_view(
    const TerrainAtlas& atlas,
    uint32_t level,
    uint32_t camera_x,
    uint32_t camera_y,
    uint32_t width,
    uint32_t height,
    uint8_t* rgba,
    std::size_t stride_bytes
) noexcept {
    if (level >= tile_level_count)
        return TerrainAtlasError::level_out_of_range;
    const uint32_t block = 1U << level;
    if (camera_x % block != 0 || camera_y % block != 0)
        return TerrainAtlasError::camera_unaligned;
    if (width == 0 || height == 0)
        return TerrainAtlasError::none;
    if (rgba == nullptr || stride_bytes < static_cast<std::size_t>(width) * texel_bytes)
        return TerrainAtlasError::no_output;
    const uint32_t edge = tile_edge >> level;
    const uint64_t map_width = static_cast<uint64_t>(atlas.grid_width) * edge;
    const uint64_t map_height = static_cast<uint64_t>(atlas.grid_height) * edge;
    const uint64_t first_x = camera_x / block;
    const uint64_t first_y = camera_y / block;
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* out = rgba + static_cast<std::size_t>(y) * stride_bytes;
        const uint64_t map_y = first_y + y;
        if (map_y >= map_height) {
            fill_opaque_black(out, width);
            continue;
        }
        const std::size_t cell_row = static_cast<std::size_t>(map_y / edge) * atlas.grid_width;
        const auto within_y = static_cast<uint32_t>(map_y % edge);
        uint64_t map_x = first_x;
        uint32_t remaining = width;
        while (remaining != 0) {
            if (map_x >= map_width) {
                fill_opaque_black(out, remaining);
                break;
            }
            const auto within_x = static_cast<uint32_t>(map_x % edge);
            const uint32_t run = std::min(remaining, edge - within_x);
            const uint16_t slot = atlas.grid[cell_row + static_cast<std::size_t>(map_x / edge)];
            const SlotPlace place = place_slot(atlas, slot);
            const AtlasPage& page = atlas.pages[place.page];
            const AtlasLevel& edges = page.levels[level];
            const uint32_t pitch = slot_pitch >> level;
            const uint32_t gutter = level_0_gutter >> level;
            const std::size_t texel_x =
                static_cast<std::size_t>(place.column) * pitch + gutter + within_x;
            const std::size_t texel_y =
                static_cast<std::size_t>(place.row) * pitch + gutter + within_y;
            std::memcpy(
                out,
                page.texels.data() + edges.offset + (texel_y * edges.width + texel_x) * texel_bytes,
                static_cast<std::size_t>(run) * texel_bytes
            );
            out += static_cast<std::size_t>(run) * texel_bytes;
            map_x += run;
            remaining -= run;
        }
    }
    return TerrainAtlasError::none;
}

} // namespace oa::present::gpu_world
