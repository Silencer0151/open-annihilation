// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The terrain atlas: page planning by table at every page edge; a seeded
// map's slots, grid, texels and gutters; views at level 0 against today's
// terrain fill through the gamma table, at level 1 against the present
// layer's area pass at one half, which world-scene-filter pins to today's
// box filter, and at levels 1 and 2 against that filter written out, byte
// for byte; every level's exact reduction; the same views from every page
// edge; the pinned digests of the seeded map's page and grid; refusals; the
// memory figures. With --data, the same over every map of the installed
// game, or with --data --part K/N over the K-th of N runs of them in name
// order.
#include "oa/present/gpu_world/terrain_atlas.hpp"
#include "oa/present/palette_tables.hpp"
#include "oa/present/world_renderer.hpp"
#include "oa/present/world_renderer/scene_filter.hpp"
#include "oa/test/check.hpp"
#include "oa/test/game_assets.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace gw = oa::present::gpu_world;
namespace wr = oa::present::world_renderer;
using oa::formats::tnt::Map;
using GammaTable = std::array<uint8_t, 256>;

/// Seed of the test's maps.
constexpr uint32_t test_seed = 20261003;
/// Map pixels in a tile edge, and bytes in a tile.
constexpr uint32_t tile_edge = gw::tile_edge;
constexpr std::size_t tile_bytes = oa::formats::tnt::layout::tile_bytes;
/// 16.16 fixed-point one: a step of one destination pixel.
constexpr uint32_t fixed_one = 65536;
/// Rows of a map the installed-game cases compare at a time.
constexpr uint32_t band_rows = 256;
/// Every this-many-th band of an installed map is compared at the deeper
/// tile levels too; the first band of every map always is.
constexpr uint32_t deep_band_step = 4;
/// Maps between the installed-game cases' progress lines.
constexpr std::size_t progress_maps = 25;
/// Every this-many-th installed map is checked through a gamma table too.
constexpr std::size_t gamma_map_step = 8;
/// Bytes in a mebibyte, for the printed figures.
constexpr double mebibyte = 1024.0 * 1024.0;
/// The page edge of a card whose textures stop at 2048 a side.
constexpr uint32_t narrow_page_edge = 2048;
/// FNV-1a parameters of the pinned digests.
constexpr uint64_t fnv_offset = 14695981039346656037ULL;
constexpr uint64_t fnv_prime = 1099511628211ULL;

/// Pinned digests of the seeded map's atlas: FNV-1a over page 0's texels
/// at every level, with no gamma table and through the 1.25 table, and over
/// the grid as each slot's low byte then high byte. They must be the same
/// bytes on every platform and build type; a change that moves one changes
/// the texels or the grid every renderer reads from the atlas, so update
/// the constant only with the reason in the commit message.
constexpr uint64_t pinned_page_plain = 0xc6c5395a85e90b08ULL;
constexpr uint64_t pinned_page_lit = 0x27fc0f1dc50dc59aULL;
constexpr uint64_t pinned_grid = 0x16d1f4106242381dULL;

/// A gamma table as the game builds it from a channel multiplier.
GammaTable gamma_table(float gamma) {
    GammaTable table{};
    for (std::size_t channel = 0; channel < table.size(); ++channel)
        table[channel] = oa::present::gamma_channel(static_cast<uint8_t>(channel), gamma);
    return table;
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

/// A map of random tiles: `distinct` tiles of random pixels, then
/// `duplicates` copies of the first tiles, then `unused` tiles no cell
/// names. The first cells name the distinct and duplicate tiles in order;
/// the rest name them at random.
Map test_map(
    std::mt19937& random,
    uint32_t tiles_wide,
    uint32_t tiles_high,
    uint32_t distinct,
    uint32_t duplicates,
    uint32_t unused
) {
    Map map;
    map.tile_width = tiles_wide;
    map.tile_height = tiles_high;
    map.tile_count = distinct + duplicates + unused;
    map.tile_palette_indices.resize(static_cast<std::size_t>(map.tile_count) * tile_bytes);
    for (std::size_t i = 0; i < static_cast<std::size_t>(distinct) * tile_bytes; ++i)
        map.tile_palette_indices[i] = static_cast<uint8_t>(random());
    for (uint32_t copy = 0; copy < duplicates; ++copy)
        std::memcpy(
            &map.tile_palette_indices[static_cast<std::size_t>(distinct + copy) * tile_bytes],
            &map.tile_palette_indices[static_cast<std::size_t>(copy) * tile_bytes],
            tile_bytes
        );
    for (std::size_t i = static_cast<std::size_t>(distinct + duplicates) * tile_bytes;
         i < map.tile_palette_indices.size();
         ++i)
        map.tile_palette_indices[i] = static_cast<uint8_t>(random());
    map.tile_indices.resize(static_cast<std::size_t>(tiles_wide) * tiles_high);
    const uint32_t named = distinct + duplicates;
    for (std::size_t cell = 0; cell < map.tile_indices.size(); ++cell)
        map.tile_indices[cell] = static_cast<uint16_t>(cell < named ? cell : random() % named);
    return map;
}

/// Applies a gamma table to RGB bytes, as the conversion for the window does.
void apply_gamma(std::vector<uint8_t>& rgb, const GammaTable* gamma) {
    if (gamma == nullptr)
        return;
    for (auto& byte : rgb)
        byte = (*gamma)[byte];
}

/// Today's terrain fill of a view at zoom 1, through the gamma table.
std::vector<uint8_t> fill_view(
    const Map& map,
    const oa::PaletteBytes& palette,
    const GammaTable* gamma,
    int32_t source_x,
    int32_t source_y,
    uint32_t width,
    uint32_t height
) {
    std::vector<uint8_t> rgb(static_cast<std::size_t>(width) * height * 3U);
    const auto error = wr::fill_scaled_viewport(
        map,
        palette,
        source_x,
        source_y,
        map.tile_width * tile_edge,
        map.tile_height * tile_edge,
        width,
        height,
        1.0F,
        rgb.data(),
        width
    );
    OA_CHECK(!error.has_value());
    apply_gamma(rgb, gamma);
    return rgb;
}

/// Lists the map coordinate under each destination column (or row) plus one
/// past the end, stepped as the terrain filter steps them. With
/// box_filter_view below, a transcription of src/app/runtime_terrain_filter.cpp,
/// whose filter the app keeps to itself: it stands for zoom one quarter,
/// which the area pass does not reach; at one half the reference is the area
/// pass the present layer exports (area_filter_view).
void footprint_bounds(
    std::vector<uint32_t>& bounds, uint32_t start, uint32_t count, uint32_t scale_fp
) {
    bounds.resize(static_cast<std::size_t>(count) + 1U);
    uint32_t position = start;
    uint32_t fraction = 0;
    for (auto& bound : bounds) {
        bound = position;
        fraction += fixed_one;
        while (fraction >= scale_fp) {
            fraction -= scale_fp;
            ++position;
        }
    }
}

/// Today's box filter of a view below zoom 1, as the match computes it:
/// each destination pixel the average of the whole map pixels under its
/// footprint, rounded halves up, ground beyond the map black; then the
/// gamma table.
std::vector<uint8_t> box_filter_view(
    const Map& map,
    const oa::PaletteBytes& palette,
    const GammaTable* gamma,
    uint32_t source_x,
    uint32_t source_y,
    uint32_t dest_w,
    uint32_t dest_h,
    float zoom
) {
    auto scale_fp = static_cast<uint32_t>(std::lround(static_cast<double>(zoom) * fixed_one));
    if (scale_fp == 0)
        scale_fp = 1;
    const uint64_t terrain_w = static_cast<uint64_t>(map.tile_width) * tile_edge;
    const uint64_t terrain_h = static_cast<uint64_t>(map.tile_height) * tile_edge;
    std::vector<uint32_t> columns;
    std::vector<uint32_t> rows;
    footprint_bounds(columns, source_x, dest_w, scale_fp);
    footprint_bounds(rows, source_y, dest_h, scale_fp);
    std::array<uint8_t, 256U * 3U> lut{};
    for (std::size_t index = 0; index < 256U; ++index)
        std::memcpy(&lut[index * 3U], &palette[index * oa::palette_entry_bytes], 3U);
    std::vector<uint8_t> out(static_cast<std::size_t>(dest_w) * dest_h * 3U);
    std::vector<uint32_t> sums(static_cast<std::size_t>(dest_w) * 3U);
    const uint16_t* const grid = map.tile_indices.data();
    const uint8_t* const tiles = map.tile_palette_indices.data();
    const auto column_end = static_cast<uint32_t>(std::min<uint64_t>(columns[dest_w], terrain_w));
    for (uint32_t dy = 0; dy < dest_h; ++dy) {
        const uint32_t row_begin = rows[dy];
        const uint32_t row_end = rows[dy + 1U];
        std::fill(sums.begin(), sums.end(), 0U);
        for (uint32_t sy = row_begin; sy < row_end && sy < terrain_h; ++sy) {
            const std::size_t row_tiles = static_cast<std::size_t>(sy / tile_edge) * map.tile_width;
            const std::size_t within_y = static_cast<std::size_t>(sy % tile_edge) * tile_edge;
            std::size_t column = 0;
            uint32_t next_column = columns[1];
            uint32_t* sum = sums.data();
            uint32_t sx = columns[0];
            while (sx < column_end) {
                const uint16_t tile = grid[row_tiles + sx / tile_edge];
                const uint32_t within_x = sx % tile_edge;
                const uint32_t run_end = std::min(column_end, sx - within_x + tile_edge);
                const uint8_t* source =
                    tiles + static_cast<std::size_t>(tile) * tile_bytes + within_y + within_x;
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
        uint8_t* out_row = out.data() + static_cast<std::size_t>(dy) * dest_w * 3U;
        const uint32_t* sum = sums.data();
        for (uint32_t dx = 0; dx < dest_w; ++dx, out_row += 3, sum += 3) {
            const uint32_t count = row_count * (columns[dx + 1U] - columns[dx]);
            out_row[0] = static_cast<uint8_t>((sum[0] + count / 2U) / count);
            out_row[1] = static_cast<uint8_t>((sum[1] + count / 2U) / count);
            out_row[2] = static_cast<uint8_t>((sum[2] + count / 2U) / count);
        }
    }
    apply_gamma(out, gamma);
    return out;
}

/// Today's picture at zoom one half by the present layer's own area pass:
/// the view's map pixels filled at zoom 1, then reduced at a scale of
/// exactly one half, which world-scene-filter pins to today's box filter
/// byte for byte; then the gamma table, as the conversion applies it.
std::vector<uint8_t> area_filter_view(
    const Map& map,
    const oa::PaletteBytes& palette,
    const GammaTable* gamma,
    uint32_t source_x,
    uint32_t source_y,
    uint32_t dest_w,
    uint32_t dest_h
) {
    const uint32_t scene_w = dest_w * 2U;
    const uint32_t scene_h = dest_h * 2U;
    const std::vector<uint8_t> scene =
        fill_view(map, palette, nullptr, source_x, source_y, scene_w, scene_h);
    wr::AreaPlan plan;
    const auto planned = wr::plan_area_filter(plan, wr::area_fixed_one / 2U, dest_w, dest_h);
    if (planned != wr::AreaError::none)
        std::fprintf(
            stderr, "area plan of %ux%u refused: %s\n", dest_w, dest_h, wr::area_error_text(planned)
        );
    OA_CHECK(planned == wr::AreaError::none);
    std::vector<uint8_t> out(static_cast<std::size_t>(dest_w) * dest_h * 3U);
    const auto filtered = wr::area_filter_rgb24(
        plan,
        wr::RgbSource{scene.data(), scene_w, scene_h, scene_w},
        wr::RgbTarget{out.data(), dest_w, dest_h, dest_w}
    );
    if (filtered != wr::AreaError::none)
        std::fprintf(stderr, "area pass refused: %s\n", wr::area_error_text(filtered));
    OA_CHECK(filtered == wr::AreaError::none);
    apply_gamma(out, gamma);
    return out;
}

/// Folds bytes into an FNV-1a digest.
uint64_t fnv1a(uint64_t digest, const uint8_t* bytes, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        digest ^= bytes[i];
        digest *= fnv_prime;
    }
    return digest;
}

/// The digests the pinned values cover: an atlas's first page, every level,
/// and its grid as each slot's low byte then high byte.
struct AtlasDigests {
    uint64_t page{};
    uint64_t grid{};
};

AtlasDigests digest_atlas(const gw::TerrainAtlas& atlas) {
    AtlasDigests digests{fnv_offset, fnv_offset};
    if (!atlas.pages.empty())
        digests.page =
            fnv1a(digests.page, atlas.pages[0].texels.data(), atlas.pages[0].texels.size());
    for (const uint16_t slot : atlas.grid) {
        const uint8_t bytes[2] = {
            static_cast<uint8_t>(slot & 0xffU), static_cast<uint8_t>(slot >> 8U)
        };
        digests.grid = fnv1a(digests.grid, bytes, sizeof bytes);
    }
    return digests;
}

/// Reads a view of the atlas at a level into tightly packed RGBA texels.
std::vector<uint8_t> read_view(
    const gw::TerrainAtlas& atlas,
    uint32_t level,
    int32_t camera_x,
    int32_t camera_y,
    uint32_t width,
    uint32_t height
) {
    std::vector<uint8_t> rgba(static_cast<std::size_t>(width) * height * gw::texel_bytes);
    const auto error = gw::read_terrain_view(
        atlas,
        level,
        camera_x,
        camera_y,
        width,
        height,
        rgba.data(),
        static_cast<std::size_t>(width) * gw::texel_bytes
    );
    OA_CHECK(error == gw::TerrainAtlasError::none);
    return rgba;
}

/// Counts the texels of a read whose colour differs from a reference's
/// pixels, or whose alpha is not opaque.
std::size_t mismatches(const std::vector<uint8_t>& rgba, const std::vector<uint8_t>& rgb) {
    const std::size_t texels = rgb.size() / 3U;
    std::size_t count = 0;
    for (std::size_t i = 0; i < texels; ++i) {
        const uint8_t* texel = &rgba[i * gw::texel_bytes];
        const uint8_t* pixel = &rgb[i * 3U];
        if (texel[0] != pixel[0] || texel[1] != pixel[1] || texel[2] != pixel[2] ||
            texel[3] != gw::texel_alpha)
            ++count;
    }
    return count;
}

/// Bytes of texels an atlas holds, every page's every level.
uint64_t atlas_texel_bytes(const gw::TerrainAtlas& atlas) {
    uint64_t bytes = 0;
    for (const auto& page : atlas.pages)
        bytes += page.texels.size();
    return bytes;
}

/// A texel of a page at a level.
const uint8_t* texel_at(const gw::AtlasPage& page, uint32_t level, uint32_t x, uint32_t y) {
    const gw::AtlasLevel& edges = page.levels[level];
    return page.texels.data() + edges.offset +
           (static_cast<std::size_t>(y) * edges.width + x) * gw::texel_bytes;
}

/// Builds an atlas within a page edge, checking that the build succeeds.
gw::TerrainAtlas build(
    const Map& map, const oa::PaletteBytes& palette, const GammaTable* gamma, uint32_t page_edge
) {
    gw::TerrainAtlas atlas;
    const auto error = gw::build_terrain_atlas(
        map, map.tile_width, map.tile_height, palette, gamma, page_edge, atlas
    );
    if (error != gw::TerrainAtlasError::none)
        std::fprintf(stderr, "build refused: %s\n", gw::terrain_atlas_error_text(error));
    OA_CHECK(error == gw::TerrainAtlasError::none);
    return atlas;
}

/// Says whether two atlases of one map show the same texels at every tile
/// level: the view from the origin over the whole map and a margin past
/// its edges, whatever their pages.
bool same_views(const gw::TerrainAtlas& atlas, const gw::TerrainAtlas& other, const Map& map) {
    bool same = true;
    for (uint32_t level = 0; level < gw::tile_level_count; ++level) {
        const uint32_t block = 1U << level;
        const uint32_t width = (map.tile_width * tile_edge) / block + 8U;
        const uint32_t height = (map.tile_height * tile_edge) / block + 8U;
        same = same && read_view(atlas, level, 0, 0, width, height) ==
                           read_view(other, level, 0, 0, width, height);
    }
    return same;
}

void test_page_plan() {
    struct Row {
        uint32_t edge;
        uint32_t slots;
        uint32_t width;
        uint32_t height;
    };

    // The smallest page whose rows of whole 40-texel slots hold the count,
    // the squarer before the wider of one area: a longer page wins only
    // where its rows hold more slots than the squarer page of the same area,
    // as 1024x128 holds 75 against the 72 of 512x256 and 1024x256 holds 150
    // against the 144 of 512x512. A smaller page edge caps the plan at a
    // full page of that edge, and an edge that is no page edge is fitted
    // first: 3000 plans as 2048 and 0 as 64.
    constexpr uint32_t limit = gw::page_edge_limit;
    constexpr Row rows[] = {
        {limit, 0, 64, 64},
        {limit, 1, 64, 64},
        {limit, 2, 128, 64},
        {limit, 3, 128, 64},
        {limit, 4, 128, 128},
        {limit, 9, 128, 128},
        {limit, 10, 256, 128},
        {limit, 18, 256, 128},
        {limit, 19, 256, 256},
        {limit, 36, 256, 256},
        {limit, 37, 512, 256},
        {limit, 72, 512, 256},
        {limit, 73, 1024, 128},
        {limit, 75, 1024, 128},
        {limit, 76, 512, 512},
        {limit, 144, 512, 512},
        {limit, 145, 1024, 256},
        {limit, 150, 1024, 256},
        {limit, 151, 2048, 128},
        {limit, 153, 2048, 128},
        {limit, 154, 1024, 512},
        {limit, 300, 1024, 512},
        {limit, 301, 2048, 256},
        {limit, 306, 2048, 256},
        {limit, 307, 1024, 1024},
        {limit, 625, 1024, 1024},
        {limit, 626, 2048, 1024},
        {limit, 1275, 2048, 1024},
        {limit, 1276, 2048, 2048},
        {limit, 2601, 2048, 2048},
        {limit, 2602, 4096, 2048},
        {limit, 5202, 4096, 2048},
        {limit, 5203, 4096, 4096},
        {limit, 10404, 4096, 4096},
        {limit, 10405, 4096, 4096},
        {limit, 65536, 4096, 4096},
        {2048, 1, 64, 64},
        {2048, 151, 2048, 128},
        {2048, 626, 2048, 1024},
        {2048, 1275, 2048, 1024},
        {2048, 1276, 2048, 2048},
        {2048, 2601, 2048, 2048},
        {2048, 2602, 2048, 2048},
        {2048, 65536, 2048, 2048},
        {1024, 625, 1024, 1024},
        {1024, 626, 1024, 1024},
        {128, 4, 128, 128},
        {128, 5, 128, 128},
        {64, 1, 64, 64},
        {64, 2, 64, 64},
        {3000, 2602, 2048, 2048},
        {0, 7, 64, 64},
    };
    for (const Row& row : rows) {
        const gw::PageSize size = gw::plan_page(row.slots, row.edge);
        if (size.width != row.width || size.height != row.height)
            std::fprintf(
                stderr,
                "plan_page(%u, %u) gave %ux%u, not %ux%u\n",
                row.slots,
                row.edge,
                size.width,
                size.height,
                row.width,
                row.height
            );
        OA_CHECK(size.width == row.width && size.height == row.height);
    }
    OA_CHECK(gw::full_page_slots(4096) == 10404 && gw::full_page_slots(2048) == 2601);
    OA_CHECK(gw::full_page_slots(1024) == 625 && gw::full_page_slots(64) == 1);
    OA_CHECK(gw::page_limit(4096) == 7 && gw::page_limit(2048) == 26);
    OA_CHECK(gw::page_limit(64) == gw::slot_limit);
    OA_CHECK(gw::page_edge_valid(64) && gw::page_edge_valid(2048) && gw::page_edge_valid(4096));
    OA_CHECK(!gw::page_edge_valid(0) && !gw::page_edge_valid(32) && !gw::page_edge_valid(96));
    OA_CHECK(!gw::page_edge_valid(8192));
    OA_CHECK(
        gw::fit_page_edge(0) == 64 && gw::fit_page_edge(63) == 64 && gw::fit_page_edge(64) == 64
    );
    OA_CHECK(gw::fit_page_edge(127) == 64 && gw::fit_page_edge(128) == 128);
    OA_CHECK(gw::fit_page_edge(2047) == 1024 && gw::fit_page_edge(2048) == 2048);
    OA_CHECK(gw::fit_page_edge(3000) == 2048 && gw::fit_page_edge(4096) == 4096);
    OA_CHECK(gw::fit_page_edge(8192) == 4096 && gw::fit_page_edge(0xffffffffU) == 4096);
    bool fitted = true;
    for (uint32_t texture_limit = 0; texture_limit <= 9000; texture_limit += 7) {
        const uint32_t edge = gw::fit_page_edge(texture_limit);
        fitted = fitted && gw::page_edge_valid(edge) &&
                 (edge <= texture_limit || edge == gw::page_edge_minimum) &&
                 (2U * edge > texture_limit || edge == gw::page_edge_limit);
    }
    OA_CHECK(fitted);
    OA_CHECK(gw::slot_pitch == 40 && gw::level_0_gutter == 4 && gw::tile_level_count == 3);
    OA_CHECK(gw::level_count({4096, 2048}) == 13 && gw::level_count({64, 64}) == 7);
    OA_CHECK(gw::level_count({64, 4096}) == 13);
    const gw::PageSize last = gw::level_size({4096, 2048}, 12);
    OA_CHECK(last.width == 1 && last.height == 1);
    const gw::PageSize tall = gw::level_size({64, 4096}, 8);
    OA_CHECK(tall.width == 1 && tall.height == 16);
    const gw::PageSize same = gw::level_size({256, 256}, 0);
    OA_CHECK(same.width == 256 && same.height == 256);
    OA_CHECK(gw::level_gutter(0) == 4 && gw::level_gutter(1) == 2 && gw::level_gutter(2) == 1);
    OA_CHECK(gw::level_gutter(3) == 0);
}

/// The seeded map every self-contained case shares: 48x40 cells over 24
/// distinct tiles, 4 duplicates of the first four and 2 tiles no cell names.
constexpr uint32_t map_tiles_wide = 48;
constexpr uint32_t map_tiles_high = 40;
constexpr uint32_t map_distinct = 24;
constexpr uint32_t map_duplicates = 4;
constexpr uint32_t map_unused = 2;

Map shared_map() {
    std::mt19937 random(test_seed);
    return test_map(
        random, map_tiles_wide, map_tiles_high, map_distinct, map_duplicates, map_unused
    );
}

/// Checks the slots, the grid, every tile's texels, the gutters at each tile
/// level, that no slot overlaps another and that the rest of the page is
/// black; the map's slots fit one 256x256 page within either page edge.
void check_packing(
    const Map& map, const oa::PaletteBytes& palette, const GammaTable* gamma, uint32_t page_edge
) {
    const gw::TerrainAtlas atlas = build(map, palette, gamma, page_edge);
    OA_CHECK(atlas.grid_width == map_tiles_wide && atlas.grid_height == map_tiles_high);
    OA_CHECK(atlas.page_edge == page_edge);
    OA_CHECK(atlas.slots_per_page == gw::full_page_slots(page_edge));
    OA_CHECK(atlas.slot_tiles.size() == map_distinct);
    for (uint32_t slot = 0; slot < atlas.slot_tiles.size(); ++slot)
        OA_CHECK(atlas.slot_tiles[slot] == slot);
    bool grid_right = atlas.grid.size() == map.tile_indices.size();
    for (std::size_t cell = 0; grid_right && cell < atlas.grid.size(); ++cell) {
        const uint16_t tile = map.tile_indices[cell];
        grid_right = atlas.grid[cell] == (tile < map_distinct ? tile : tile - map_distinct);
    }
    OA_CHECK(grid_right);
    OA_CHECK(atlas.pages.size() == 1);
    if (atlas.pages.size() != 1)
        return;
    const gw::AtlasPage& page = atlas.pages[0];
    OA_CHECK(page.width == 256 && page.height == 256 && page.columns == 6);
    OA_CHECK(page.first_slot == 0 && page.slot_count == map_distinct);
    OA_CHECK(page.levels.size() == 9);
    std::size_t expected_bytes = 0;
    for (uint32_t level = 0; level < page.levels.size(); ++level) {
        const gw::AtlasLevel& edges = page.levels[level];
        OA_CHECK(edges.width == (256U >> level) && edges.height == (256U >> level));
        OA_CHECK(edges.offset == expected_bytes);
        expected_bytes += static_cast<std::size_t>(edges.width) * edges.height * gw::texel_bytes;
    }
    OA_CHECK(page.texels.size() == expected_bytes);

    // Each tile's texels at level 0 are its pixels through the palette and
    // the gamma table.
    bool tiles_right = true;
    for (uint32_t slot = 0; slot < atlas.slot_tiles.size(); ++slot) {
        const auto rect = gw::tile_rect(atlas, slot, 0);
        OA_CHECK(rect.has_value());
        if (!rect)
            continue;
        OA_CHECK(rect->page == 0 && rect->edge == tile_edge);
        const uint8_t* pixels =
            &map.tile_palette_indices
                 [static_cast<std::size_t>(atlas.slot_tiles[slot]) * tile_bytes];
        for (uint32_t y = 0; y < tile_edge && tiles_right; ++y) {
            for (uint32_t x = 0; x < tile_edge; ++x) {
                const uint8_t* texel = texel_at(page, 0, rect->x + x, rect->y + y);
                const uint8_t* colour = &palette
                                            [static_cast<std::size_t>(pixels[y * tile_edge + x]) *
                                             oa::palette_entry_bytes];
                for (uint32_t channel = 0; channel < 3U; ++channel) {
                    const uint8_t expected = gamma ? (*gamma)[colour[channel]] : colour[channel];
                    if (texel[channel] != expected)
                        tiles_right = false;
                }
                if (texel[3] != gw::texel_alpha)
                    tiles_right = false;
            }
        }
    }
    OA_CHECK(tiles_right);

    // At each tile level the gutter ring holds the nearest tile texel, and
    // the slots lie apart; the texels outside every slot are opaque black.
    for (uint32_t level = 0; level < gw::tile_level_count; ++level) {
        const uint32_t pitch = gw::slot_pitch >> level;
        const uint32_t gutter = gw::level_gutter(level);
        const gw::AtlasLevel& edges = page.levels[level];
        std::vector<uint8_t> covered(static_cast<std::size_t>(edges.width) * edges.height, 0);
        bool gutters_right = true;
        bool apart = true;
        for (uint32_t slot = 0; slot < atlas.slot_tiles.size(); ++slot) {
            const auto rect = gw::tile_rect(atlas, slot, level);
            OA_CHECK(rect.has_value() && rect->edge == (tile_edge >> level));
            if (!rect)
                continue;
            const uint32_t left = rect->x - gutter;
            const uint32_t top = rect->y - gutter;
            for (uint32_t y = 0; y < pitch; ++y) {
                for (uint32_t x = 0; x < pitch; ++x) {
                    uint8_t& mark =
                        covered[static_cast<std::size_t>(top + y) * edges.width + left + x];
                    if (mark != 0)
                        apart = false;
                    mark = 1;
                    const uint32_t inside_x = std::clamp(x, gutter, gutter + rect->edge - 1U);
                    const uint32_t inside_y = std::clamp(y, gutter, gutter + rect->edge - 1U);
                    if (inside_x == x && inside_y == y)
                        continue;
                    if (std::memcmp(
                            texel_at(page, level, left + x, top + y),
                            texel_at(page, level, left + inside_x, top + inside_y),
                            gw::texel_bytes
                        ) != 0)
                        gutters_right = false;
                }
            }
        }
        OA_CHECK(gutters_right);
        OA_CHECK(apart);
        bool rest_black = true;
        for (uint32_t y = 0; y < edges.height; ++y)
            for (uint32_t x = 0; x < edges.width; ++x) {
                if (covered[static_cast<std::size_t>(y) * edges.width + x] != 0)
                    continue;
                const uint8_t* texel = texel_at(page, level, x, y);
                if (texel[0] != 0 || texel[1] != 0 || texel[2] != 0 || texel[3] != gw::texel_alpha)
                    rest_black = false;
            }
        OA_CHECK(rest_black);
    }
    OA_CHECK(!gw::tile_rect(atlas, 0, gw::tile_level_count).has_value());
    OA_CHECK(!gw::tile_rect(atlas, map_distinct, 0).has_value());
}

void test_packing() {
    const Map map = shared_map();
    const oa::PaletteBytes palette = test_palette();
    const GammaTable brighter = gamma_table(1.25F);
    for (const uint32_t page_edge : {gw::page_edge_limit, narrow_page_edge}) {
        check_packing(map, palette, nullptr, page_edge);
        check_packing(map, palette, &brighter, page_edge);
    }

    // One tile fills the smallest page.
    std::mt19937 random(test_seed + 1U);
    const Map one = test_map(random, 3, 2, 1, 0, 0);
    const gw::TerrainAtlas atlas = build(one, palette, nullptr, gw::page_edge_limit);
    OA_CHECK(atlas.slot_tiles.size() == 1 && atlas.pages.size() == 1);
    if (atlas.pages.size() == 1) {
        OA_CHECK(atlas.pages[0].width == 64 && atlas.pages[0].height == 64);
        OA_CHECK(atlas.pages[0].levels.size() == 7);
        OA_CHECK(atlas.pages[0].texels.size() == 5461U * gw::texel_bytes);
    }
}

/// A map gives the same tiles and views within every page edge, on more
/// pages as the edge falls: the seeded map's 24 slots on 24 pages of one
/// slot at the smallest edge and on 3 pages of 9 at 128; a map of one slot
/// more than a 2048 page holds on two pages at 2048 against one at 4096,
/// and a limit of 3000 fitted to 2048. The footprint of each equals its
/// build.
void test_page_edges() {
    const oa::PaletteBytes palette = test_palette();
    const GammaTable brighter = gamma_table(1.25F);
    const Map map = shared_map();
    const gw::TerrainAtlas reference = build(map, palette, &brighter, gw::page_edge_limit);
    const AtlasDigests reference_digests = digest_atlas(reference);

    struct Edge {
        uint32_t edge;
        uint32_t pages;
        uint32_t per_page;
    };

    for (const Edge e : {Edge{64, 24, 1}, Edge{128, 3, 9}, Edge{2048, 1, 2601}}) {
        const gw::TerrainAtlas atlas = build(map, palette, &brighter, e.edge);
        OA_CHECK(atlas.page_edge == e.edge && atlas.slots_per_page == e.per_page);
        OA_CHECK(atlas.pages.size() == e.pages);
        OA_CHECK(atlas.grid == reference.grid && atlas.slot_tiles == reference.slot_tiles);
        bool placed = atlas.pages.size() == e.pages;
        bool tiles_same = true;
        for (uint32_t slot = 0; placed && slot < map_distinct; ++slot) {
            const auto rect = gw::tile_rect(atlas, slot, 0);
            const auto expected = gw::tile_rect(reference, slot, 0);
            placed = rect.has_value() && expected.has_value() && rect->page == slot / e.per_page &&
                     atlas.pages[rect->page].first_slot == rect->page * e.per_page &&
                     atlas.pages[rect->page].width <= e.edge &&
                     atlas.pages[rect->page].height <= e.edge;
            for (uint32_t y = 0; placed && y < tile_edge; ++y)
                for (uint32_t x = 0; x < tile_edge; ++x)
                    if (std::memcmp(
                            texel_at(atlas.pages[rect->page], 0, rect->x + x, rect->y + y),
                            texel_at(
                                reference.pages[expected->page], 0, expected->x + x, expected->y + y
                            ),
                            gw::texel_bytes
                        ) != 0)
                        tiles_same = false;
        }
        OA_CHECK(placed);
        OA_CHECK(tiles_same);
        OA_CHECK(same_views(atlas, reference, map));
        OA_CHECK(digest_atlas(atlas).grid == reference_digests.grid);
        const auto footprint =
            gw::terrain_atlas_footprint(map_distinct, map.tile_indices.size(), e.edge);
        OA_CHECK(footprint.pages == atlas.pages.size());
        OA_CHECK(footprint.texel_bytes == atlas_texel_bytes(atlas));
        if (e.edge == gw::page_edge_minimum)
            for (const auto& page : atlas.pages)
                OA_CHECK(page.width == 64 && page.height == 64 && page.slot_count == 1);
    }

    // One slot more than a 2048 page holds.
    std::mt19937 random(test_seed + 2U);
    const uint32_t over = gw::full_page_slots(narrow_page_edge) + 1U;
    const Map wide = test_map(random, 52, 51, over, 0, 0);
    const gw::TerrainAtlas one = build(wide, palette, nullptr, gw::page_edge_limit);
    const gw::TerrainAtlas two = build(wide, palette, nullptr, narrow_page_edge);
    const gw::TerrainAtlas fitted = build(wide, palette, nullptr, 3000);
    OA_CHECK(one.slot_tiles.size() == over && one.pages.size() == 1);
    if (one.pages.size() == 1)
        OA_CHECK(one.pages[0].width == 4096 && one.pages[0].height == 2048);
    OA_CHECK(two.pages.size() == 2 && fitted.pages.size() == 2);
    OA_CHECK(fitted.page_edge == narrow_page_edge);
    if (two.pages.size() == 2) {
        OA_CHECK(two.pages[0].width == 2048 && two.pages[0].height == 2048);
        OA_CHECK(two.pages[0].first_slot == 0 && two.pages[0].slot_count == over - 1U);
        OA_CHECK(two.pages[1].width == 64 && two.pages[1].height == 64);
        OA_CHECK(two.pages[1].first_slot == over - 1U && two.pages[1].slot_count == 1);
        const auto last = gw::tile_rect(two, over - 1U, 0);
        OA_CHECK(last.has_value());
        if (last)
            OA_CHECK(
                last->page == 1 && last->x == gw::level_0_gutter && last->y == gw::level_0_gutter
            );
        OA_CHECK(!gw::tile_rect(two, over, 0).has_value());
    }
    OA_CHECK(same_views(one, two, wide));
    OA_CHECK(same_views(one, fitted, wide));
    for (const gw::TerrainAtlas* atlas : {&one, &two, &fitted}) {
        const auto footprint =
            gw::terrain_atlas_footprint(over, wide.tile_indices.size(), atlas->page_edge);
        OA_CHECK(footprint.pages == atlas->pages.size());
        OA_CHECK(footprint.texel_bytes == atlas_texel_bytes(*atlas));
    }
    std::printf(
        "page edges: %u slots on %zu page(s) at 4096 (%.1f MiB), %zu at 2048 (%.1f MiB)\n",
        over,
        one.pages.size(),
        static_cast<double>(atlas_texel_bytes(one)) / mebibyte,
        two.pages.size(),
        static_cast<double>(atlas_texel_bytes(two)) / mebibyte
    );
}

/// Views at level 0 give the bytes of today's fill at zoom 1.
void test_zoom_1_matches_fill() {
    const Map map = shared_map();
    const oa::PaletteBytes palette = test_palette();
    const uint32_t map_w = map_tiles_wide * tile_edge;
    const uint32_t map_h = map_tiles_high * tile_edge;
    const GammaTable darker = gamma_table(0.75F);
    const GammaTable brighter = gamma_table(1.25F);
    const GammaTable* gammas[] = {nullptr, &darker, &brighter};
    const uint32_t sizes[][2] = {{1000, 700}, {1, 1}, {31, 33}, {1664, 952}};
    const auto w = static_cast<int32_t>(map_w);
    const auto h = static_cast<int32_t>(map_h);
    // On the map, past its end, and before its left and top edges.
    const int32_t cameras[][2] = {
        {0, 0},
        {13, 7},
        {w - 500, h - 300},
        {w - 1, h - 1},
        {w + 5, h + 5},
        {-40, -7},
        {-1, 13},
        {-2000, -900}
    };
    int views = 0;
    std::size_t wrong = 0;
    for (const GammaTable* gamma : gammas) {
        const gw::TerrainAtlas atlas = build(map, palette, gamma, gw::page_edge_limit);
        for (const auto& size : sizes)
            for (const auto& camera : cameras) {
                const auto expected =
                    fill_view(map, palette, gamma, camera[0], camera[1], size[0], size[1]);
                const auto read = read_view(atlas, 0, camera[0], camera[1], size[0], size[1]);
                wrong += mismatches(read, expected);
                ++views;
            }
    }
    std::printf("zoom 1 against the fill: %d views, %zu texels differ\n", views, wrong);
    OA_CHECK(wrong == 0);
}

/// Views at levels 1 and 2 give the bytes of today's box filter at zoom one
/// half and one quarter, the camera on whole texels of the level: at level
/// 1 against the present layer's area pass at one half as well as the
/// transcription, at level 2 against the transcription.
void test_mips_match_box_filter() {
    const Map map = shared_map();
    const oa::PaletteBytes palette = test_palette();
    const uint32_t map_w = map_tiles_wide * tile_edge;
    const uint32_t map_h = map_tiles_high * tile_edge;
    const GammaTable brighter = gamma_table(1.25F);
    const GammaTable* gammas[] = {nullptr, &brighter};
    int views = 0;
    int area_views = 0;
    std::size_t wrong = 0;
    for (const GammaTable* gamma : gammas) {
        const gw::TerrainAtlas atlas = build(map, palette, gamma, gw::page_edge_limit);
        for (uint32_t level = 1; level < gw::tile_level_count; ++level) {
            const uint32_t block = 1U << level;
            const float zoom = 1.0F / static_cast<float>(block);
            const uint32_t cameras[][2] = {
                {0, 0},
                {7U * block, 3U * block},
                {map_w - 100U * block, map_h - 50U * block},
                {map_w - block, map_h - block},
                {map_w + block, map_h},
            };
            const uint32_t sizes[][2] = {{500, 350}, {1, 1}, {33, 17}};
            for (const auto& camera : cameras)
                for (const auto& size : sizes) {
                    const auto expected = box_filter_view(
                        map, palette, gamma, camera[0], camera[1], size[0], size[1], zoom
                    );
                    const auto read =
                        read_view(atlas, level, camera[0], camera[1], size[0], size[1]);
                    wrong += mismatches(read, expected);
                    ++views;
                    if (level == 1) {
                        wrong += mismatches(
                            read,
                            area_filter_view(
                                map, palette, gamma, camera[0], camera[1], size[0], size[1]
                            )
                        );
                        ++area_views;
                    }
                }
        }
    }
    std::printf(
        "levels 1 and 2 against the box filter: %d views, %d of them against the area pass too, "
        "%zu texels differ\n",
        views,
        area_views,
        wrong
    );
    OA_CHECK(wrong == 0);
}

/// Every texel of every level past 0 is the exact average of the level-0
/// texels under it, apart from the rebuilt gutters of the tile levels.
void test_full_chain_exact() {
    const Map map = shared_map();
    const oa::PaletteBytes palette = test_palette();
    const gw::TerrainAtlas atlas = build(map, palette, nullptr, gw::page_edge_limit);
    OA_CHECK(atlas.pages.size() == 1);
    if (atlas.pages.size() != 1)
        return;
    const gw::AtlasPage& page = atlas.pages[0];
    std::size_t checked = 0;
    std::size_t wrong = 0;
    for (uint32_t level = 1; level < page.levels.size(); ++level) {
        const gw::AtlasLevel& edges = page.levels[level];
        const uint32_t across = page.width / edges.width;
        const uint32_t down = page.height / edges.height;
        const uint32_t count = across * down;
        const uint32_t pitch = gw::slot_pitch >> level;
        const uint32_t gutter = gw::level_gutter(level);
        for (uint32_t y = 0; y < edges.height; ++y) {
            for (uint32_t x = 0; x < edges.width; ++x) {
                if (level < gw::tile_level_count) {
                    const uint32_t column = x / pitch;
                    const uint32_t row = y / pitch;
                    const uint32_t slot = row * page.columns + column;
                    const uint32_t within_x = x % pitch;
                    const uint32_t within_y = y % pitch;
                    const bool in_gutter =
                        within_x < gutter || within_x >= gutter + (tile_edge >> level) ||
                        within_y < gutter || within_y >= gutter + (tile_edge >> level);
                    if (column < page.columns && slot < page.slot_count && in_gutter)
                        continue;
                }
                uint32_t sums[3] = {0, 0, 0};
                for (uint32_t j = 0; j < down; ++j)
                    for (uint32_t i = 0; i < across; ++i) {
                        const uint8_t* source = texel_at(page, 0, x * across + i, y * down + j);
                        sums[0] += source[0];
                        sums[1] += source[1];
                        sums[2] += source[2];
                    }
                const uint8_t* texel = texel_at(page, level, x, y);
                ++checked;
                for (uint32_t channel = 0; channel < 3U; ++channel)
                    if (texel[channel] !=
                        static_cast<uint8_t>((sums[channel] + count / 2U) / count)) {
                        ++wrong;
                        break;
                    }
                if (texel[3] != gw::texel_alpha)
                    ++wrong;
            }
        }
    }
    std::printf(
        "exact reduction: %zu texels checked over %zu levels, %zu wrong\n",
        checked,
        page.levels.size() - 1U,
        wrong
    );
    OA_CHECK(checked > 0 && wrong == 0);
}

/// The seeded map's atlas gives the pinned bytes, with no gamma table and
/// through the 1.25 table, the same bytes twice, and the same page within a
/// page edge of 2048, where its slots fit one 256x256 page as well.
void test_pinned_digests() {
    const Map map = shared_map();
    const oa::PaletteBytes palette = test_palette();
    const GammaTable brighter = gamma_table(1.25F);
    const gw::TerrainAtlas plain = build(map, palette, nullptr, gw::page_edge_limit);
    const gw::TerrainAtlas lit = build(map, palette, &brighter, gw::page_edge_limit);
    const gw::TerrainAtlas again = build(map, palette, &brighter, gw::page_edge_limit);
    const gw::TerrainAtlas narrow = build(map, palette, &brighter, narrow_page_edge);
    OA_CHECK(lit.grid == again.grid && lit.slot_tiles == again.slot_tiles);
    OA_CHECK(lit.pages.size() == again.pages.size());
    for (std::size_t i = 0; i < lit.pages.size() && i < again.pages.size(); ++i)
        OA_CHECK(lit.pages[i].texels == again.pages[i].texels);
    const auto check = [](const char* name, AtlasDigests got, uint64_t page, uint64_t grid) {
        if (got.page != page || got.grid != grid)
            std::fprintf(
                stderr,
                "%s: page %016llx and grid %016llx, pinned %016llx and %016llx\n",
                name,
                static_cast<unsigned long long>(got.page),
                static_cast<unsigned long long>(got.grid),
                static_cast<unsigned long long>(page),
                static_cast<unsigned long long>(grid)
            );
        OA_CHECK(got.page == page && got.grid == grid);
    };
    check("no gamma table", digest_atlas(plain), pinned_page_plain, pinned_grid);
    check("gamma 1.25", digest_atlas(lit), pinned_page_lit, pinned_grid);
    check("gamma 1.25 within 2048", digest_atlas(narrow), pinned_page_lit, pinned_grid);
}

/// An atlas of fewer cells than the map, the last tile column and the last
/// four rows left out as the game leaves out the edges it never shows: the
/// grid is those columns of those rows, each cell's slot holding the tile
/// the map names there, pixel for pixel; no more slots than the whole
/// map's; and a grid of no cells or of more than the map's is refused.
void test_shown_grid() {
    const oa::PaletteBytes palette = test_palette();
    const Map map = shared_map();
    constexpr uint32_t columns = map_tiles_wide - 1;
    constexpr uint32_t rows = map_tiles_high - 4;
    const gw::TerrainAtlas whole = build(map, palette, nullptr, gw::page_edge_limit);
    gw::TerrainAtlas part;
    OA_CHECK(
        gw::build_terrain_atlas(map, columns, rows, palette, nullptr, gw::page_edge_limit, part) ==
        gw::TerrainAtlasError::none
    );
    OA_CHECK(part.grid_width == columns && part.grid_height == rows);
    OA_CHECK(part.grid.size() == static_cast<std::size_t>(columns) * rows);
    OA_CHECK(!part.slot_tiles.empty() && part.slot_tiles.size() <= whole.slot_tiles.size());
    std::size_t same = 0;
    for (uint32_t row = 0; row < rows; ++row)
        for (uint32_t column = 0; column < columns; ++column) {
            const std::size_t cell = static_cast<std::size_t>(row) * columns + column;
            const std::size_t map_cell = static_cast<std::size_t>(row) * map_tiles_wide + column;
            if (part.grid[cell] >= part.slot_tiles.size())
                continue;
            const uint16_t named = map.tile_indices[map_cell];
            const uint16_t held = part.slot_tiles[part.grid[cell]];
            if (std::memcmp(
                    &map.tile_palette_indices[static_cast<std::size_t>(named) * tile_bytes],
                    &map.tile_palette_indices[static_cast<std::size_t>(held) * tile_bytes],
                    tile_bytes
                ) == 0)
                ++same;
        }
    OA_CHECK(same == part.grid.size());
    gw::TerrainAtlas refused;
    OA_CHECK(
        gw::build_terrain_atlas(map, 0, rows, palette, nullptr, gw::page_edge_limit, refused) ==
        gw::TerrainAtlasError::empty_grid
    );
    OA_CHECK(
        gw::build_terrain_atlas(map, columns, 0, palette, nullptr, gw::page_edge_limit, refused) ==
        gw::TerrainAtlasError::empty_grid
    );
    OA_CHECK(
        gw::build_terrain_atlas(
            map, map_tiles_wide + 1, rows, palette, nullptr, gw::page_edge_limit, refused
        ) == gw::TerrainAtlasError::grid_size
    );
    OA_CHECK(
        gw::build_terrain_atlas(
            map, columns, map_tiles_high + 1, palette, nullptr, gw::page_edge_limit, refused
        ) == gw::TerrainAtlasError::grid_size
    );
    OA_CHECK(refused.pages.empty() && refused.grid.empty() && refused.slot_tiles.empty());
}

/// Each malformed map is refused and leaves the atlas empty, after a build
/// that succeeded too.
void test_malformed_maps() {
    const oa::PaletteBytes palette = test_palette();
    const Map good = shared_map();
    gw::TerrainAtlas atlas;
    OA_CHECK(
        gw::build_terrain_atlas(
            good, good.tile_width, good.tile_height, palette, nullptr, gw::page_edge_limit, atlas
        ) == gw::TerrainAtlasError::none
    );
    OA_CHECK(!atlas.pages.empty());
    const auto refuses = [&](const Map& map, gw::TerrainAtlasError expected) {
        OA_CHECK(
            gw::build_terrain_atlas(
                good,
                good.tile_width,
                good.tile_height,
                palette,
                nullptr,
                gw::page_edge_limit,
                atlas
            ) == gw::TerrainAtlasError::none
        );
        const auto error = gw::build_terrain_atlas(
            map, map.tile_width, map.tile_height, palette, nullptr, gw::page_edge_limit, atlas
        );
        if (error != expected)
            std::fprintf(
                stderr,
                "expected %s, got %s\n",
                gw::terrain_atlas_error_text(expected),
                gw::terrain_atlas_error_text(error)
            );
        OA_CHECK(error == expected);
        OA_CHECK(atlas.pages.empty() && atlas.grid.empty() && atlas.slot_tiles.empty());
        OA_CHECK(atlas.grid_width == 0 && atlas.grid_height == 0);
        OA_CHECK(atlas.page_edge == 0 && atlas.slots_per_page == 0);
        OA_CHECK(!gw::tile_rect(atlas, 0, 0).has_value());
    };
    Map empty = good;
    empty.tile_width = 0;
    refuses(empty, gw::TerrainAtlasError::empty_grid);
    empty = good;
    empty.tile_height = 0;
    refuses(empty, gw::TerrainAtlasError::empty_grid);
    Map short_grid = good;
    short_grid.tile_indices.pop_back();
    refuses(short_grid, gw::TerrainAtlasError::grid_size);
    Map huge = good;
    huge.tile_width = 3000;
    huge.tile_height = 3000;
    huge.tile_indices.clear();
    refuses(huge, gw::TerrainAtlasError::grid_size);
    Map short_tiles = good;
    short_tiles.tile_palette_indices.pop_back();
    refuses(short_tiles, gw::TerrainAtlasError::tile_bytes);
    Map missing = good;
    missing.tile_indices[5] = static_cast<uint16_t>(missing.tile_count);
    refuses(missing, gw::TerrainAtlasError::missing_tile);
    OA_CHECK(std::string(gw::terrain_atlas_error_text(gw::TerrainAtlasError::none)) == "no error");
}

/// Reads refuse a deeper level, a camera between texels and a missing output.
void test_read_refusals() {
    const Map map = shared_map();
    const oa::PaletteBytes palette = test_palette();
    const gw::TerrainAtlas atlas = build(map, palette, nullptr, gw::page_edge_limit);
    std::vector<uint8_t> rgba(64U * 64U * gw::texel_bytes, 0x5a);
    const std::vector<uint8_t> untouched = rgba;
    OA_CHECK(
        gw::read_terrain_view(
            atlas, gw::tile_level_count, 0, 0, 64, 64, rgba.data(), 64U * gw::texel_bytes
        ) == gw::TerrainAtlasError::level_out_of_range
    );
    OA_CHECK(
        gw::read_terrain_view(atlas, 1, 1, 0, 64, 64, rgba.data(), 64U * gw::texel_bytes) ==
        gw::TerrainAtlasError::camera_unaligned
    );
    OA_CHECK(
        gw::read_terrain_view(atlas, 2, 4, 2, 64, 64, rgba.data(), 64U * gw::texel_bytes) ==
        gw::TerrainAtlasError::camera_unaligned
    );
    OA_CHECK(
        gw::read_terrain_view(atlas, 0, 0, 0, 64, 64, nullptr, 64U * gw::texel_bytes) ==
        gw::TerrainAtlasError::no_output
    );
    OA_CHECK(
        gw::read_terrain_view(atlas, 0, 0, 0, 64, 64, rgba.data(), 64U * gw::texel_bytes - 1U) ==
        gw::TerrainAtlasError::no_output
    );
    OA_CHECK(
        gw::read_terrain_view(atlas, 0, 0, 0, 0, 64, rgba.data(), 64U * gw::texel_bytes) ==
        gw::TerrainAtlasError::none
    );
    OA_CHECK(rgba == untouched);
    // A stride wider than the row leaves the bytes past each row alone.
    OA_CHECK(
        gw::read_terrain_view(atlas, 0, 0, 0, 60, 64, rgba.data(), 64U * gw::texel_bytes) ==
        gw::TerrainAtlasError::none
    );
    bool spare_untouched = true;
    for (uint32_t y = 0; y < 64; ++y)
        for (std::size_t byte = 60U * gw::texel_bytes; byte < 64U * gw::texel_bytes; ++byte)
            if (rgba[static_cast<std::size_t>(y) * 64U * gw::texel_bytes + byte] != 0x5a)
                spare_untouched = false;
    OA_CHECK(spare_untouched);
}

/// The memory figures: the footprint of an atlas before it is built matches
/// the atlas built, within either page edge, and the figures of the bounds
/// are printed at both.
void test_footprint() {
    constexpr uint32_t limit = gw::page_edge_limit;
    const Map map = shared_map();
    const oa::PaletteBytes palette = test_palette();
    const gw::TerrainAtlas atlas = build(map, palette, nullptr, limit);
    const auto shared = gw::terrain_atlas_footprint(map_distinct, map.tile_indices.size(), limit);
    OA_CHECK(shared.pages == 1 && shared.texel_bytes == atlas_texel_bytes(atlas));
    OA_CHECK(shared.table_bytes == (map.tile_indices.size() + map_distinct) * sizeof(uint16_t));
    const auto one = gw::terrain_atlas_footprint(1, 6, limit);
    OA_CHECK(one.pages == 1 && one.texel_bytes == 5461U * gw::texel_bytes);
    const auto none = gw::terrain_atlas_footprint(0, 6, limit);
    OA_CHECK(none.pages == 0 && none.texel_bytes == 0);
    const auto full = gw::terrain_atlas_footprint(gw::full_page_slots(limit), 1, limit);
    OA_CHECK(full.pages == 1 && full.texel_bytes == 22369621ULL * gw::texel_bytes);
    OA_CHECK(
        full.building_bytes == (1024ULL * 1024ULL + 512ULL * 512ULL) * 3ULL * sizeof(uint32_t)
    );
    const auto over = gw::terrain_atlas_footprint(gw::full_page_slots(limit) + 1U, 1, limit);
    OA_CHECK(over.pages == 2 && over.texel_bytes == full.texel_bytes + one.texel_bytes);
    const auto most = gw::terrain_atlas_footprint(gw::slot_limit, 1, limit);
    OA_CHECK(most.pages == gw::page_limit(limit));
    OA_CHECK(
        gw::terrain_atlas_footprint(gw::slot_limit + 100U, 1, limit).texel_bytes == most.texel_bytes
    );

    // Within 2048: a full page's chain of 12 levels, one slot more on a
    // second page, the bound on 26 pages, and a limit of 3000 fitted to 2048.
    const auto full_narrow =
        gw::terrain_atlas_footprint(gw::full_page_slots(narrow_page_edge), 1, narrow_page_edge);
    OA_CHECK(full_narrow.pages == 1 && full_narrow.texel_bytes == 5592405ULL * gw::texel_bytes);
    OA_CHECK(
        full_narrow.building_bytes == (512ULL * 512ULL + 256ULL * 256ULL) * 3ULL * sizeof(uint32_t)
    );
    const auto over_narrow = gw::terrain_atlas_footprint(
        gw::full_page_slots(narrow_page_edge) + 1U, 1, narrow_page_edge
    );
    OA_CHECK(over_narrow.pages == 2);
    OA_CHECK(over_narrow.texel_bytes == full_narrow.texel_bytes + one.texel_bytes);
    OA_CHECK(gw::terrain_atlas_footprint(gw::slot_limit, 1, narrow_page_edge).pages == 26);
    const auto fitted =
        gw::terrain_atlas_footprint(gw::full_page_slots(narrow_page_edge) + 1U, 1, 3000);
    OA_CHECK(fitted.pages == 2 && fitted.texel_bytes == over_narrow.texel_bytes);

    for (const uint32_t page_edge : {limit, narrow_page_edge}) {
        uint64_t previous = 0;
        bool monotonic = true;
        for (uint32_t slots = 1; slots <= gw::slot_limit; slots += 997U) {
            const uint64_t bytes = gw::terrain_atlas_footprint(slots, 1, page_edge).texel_bytes;
            monotonic = monotonic && bytes >= previous;
            previous = bytes;
        }
        OA_CHECK(monotonic);
    }
    const uint32_t figures[] = {1, 24, 3466, 10404, 11444, gw::slot_limit};
    for (uint32_t slots : figures)
        for (const uint32_t page_edge : {limit, narrow_page_edge}) {
            const auto footprint = gw::terrain_atlas_footprint(slots, 0, page_edge);
            std::printf(
                "memory: %u slots within %u on %u page(s): %llu bytes of texels (%.1f MiB), "
                "building %.1f MiB\n",
                slots,
                page_edge,
                footprint.pages,
                static_cast<unsigned long long>(footprint.texel_bytes),
                static_cast<double>(footprint.texel_bytes) / mebibyte,
                static_cast<double>(footprint.building_bytes) / mebibyte
            );
        }
}

/// The installed game's palette, or the test palette when it is not there.
oa::PaletteBytes installed_palette(const oa::AssetStore& assets) {
    const auto bytes = oa::test::read_game_file(assets, "palettes/palette.pal");
    oa::PaletteBytes palette = test_palette();
    if (bytes.size() == palette.size())
        std::memcpy(palette.data(), bytes.data(), palette.size());
    else
        std::printf("palettes/palette.pal not found; using the test palette\n");
    return palette;
}

/// Compares a map's atlas with the fill at level 0 over the whole map in
/// bands, and on every deep_band_step-th band with the area pass at one
/// half at level 1 and the box filter written out at one quarter at level 2.
///
/// @return texels that differ
std::size_t compare_installed_map(
    const Map& map,
    const gw::TerrainAtlas& atlas,
    const oa::PaletteBytes& palette,
    const GammaTable* gamma
) {
    const uint32_t map_w = map.tile_width * tile_edge;
    const uint32_t map_h = map.tile_height * tile_edge;
    std::size_t wrong = 0;
    for (uint32_t top = 0; top < map_h; top += band_rows) {
        const uint32_t rows = std::min(band_rows, map_h - top);
        wrong += mismatches(
            read_view(atlas, 0, 0, top, map_w, rows),
            fill_view(map, palette, gamma, 0, top, map_w, rows)
        );
        if ((top / band_rows) % deep_band_step != 0)
            continue;
        for (uint32_t level = 1; level < gw::tile_level_count; ++level) {
            const uint32_t block = 1U << level;
            const uint32_t dest_w = map_w / block;
            const uint32_t dest_h = rows / block;
            if (level == 1) {
                // The area pass takes a picture up to area_picture_edge_limit
                // columns wide, so a wider band is compared in pieces; at
                // exactly one half every footprint is whole scene pixels,
                // and the pieces give the bytes of the whole.
                for (uint32_t left = 0; left < dest_w; left += wr::area_picture_edge_limit) {
                    const uint32_t piece = std::min(wr::area_picture_edge_limit, dest_w - left);
                    wrong += mismatches(
                        read_view(atlas, level, left * block, top, piece, dest_h),
                        area_filter_view(map, palette, gamma, left * block, top, piece, dest_h)
                    );
                }
                continue;
            }
            const auto expected = box_filter_view(
                map, palette, gamma, 0, top, dest_w, dest_h, 1.0F / static_cast<float>(block)
            );
            wrong += mismatches(read_view(atlas, level, 0, top, dest_w, dest_h), expected);
        }
    }
    return wrong;
}

/// Describes an atlas and its footprint for the printed figures.
std::string atlas_figure(const std::string& name, const Map& map, const gw::TerrainAtlas& atlas) {
    const auto footprint = gw::terrain_atlas_footprint(
        static_cast<uint32_t>(atlas.slot_tiles.size()), atlas.grid.size(), atlas.page_edge
    );
    char line[512];
    std::snprintf(
        line,
        sizeof line,
        "largest atlas within %u: %s, %ux%u cells, %u tiles in the set, %zu slots, %zu page(s)",
        atlas.page_edge,
        name.c_str(),
        atlas.grid_width,
        atlas.grid_height,
        map.tile_count,
        atlas.slot_tiles.size(),
        atlas.pages.size()
    );
    std::string figure = line;
    for (const auto& page : atlas.pages) {
        std::snprintf(
            line,
            sizeof line,
            "\n  page %ux%u, %zu levels, %zu bytes (%.1f MiB)",
            page.width,
            page.height,
            page.levels.size(),
            page.texels.size(),
            static_cast<double>(page.texels.size()) / mebibyte
        );
        figure += line;
    }
    std::snprintf(
        line,
        sizeof line,
        "\n  texels %llu bytes (%.1f MiB), tables %llu bytes, building %llu bytes (%.1f MiB); "
        "the map's 8-bit tiles %llu bytes (%.1f MiB)",
        static_cast<unsigned long long>(footprint.texel_bytes),
        static_cast<double>(footprint.texel_bytes) / mebibyte,
        static_cast<unsigned long long>(footprint.table_bytes),
        static_cast<unsigned long long>(footprint.building_bytes),
        static_cast<double>(footprint.building_bytes) / mebibyte,
        static_cast<unsigned long long>(map.tile_palette_indices.size()),
        static_cast<double>(map.tile_palette_indices.size()) / mebibyte
    );
    figure += line;
    return figure;
}

/// The part of the installed maps --data checks: the K-th of N runs of them
/// in name order, each a whole share of the maps.
struct MapPart {
    std::size_t index = 1; // K, from 1
    std::size_t count = 1; // N
};

/// Returns the part a --part value names, K/N with K from 1 through N.
///
/// Throws std::invalid_argument for any other value.
///
/// @param text the option's value
/// @return the part
MapPart parse_map_part(std::string_view text) {
    const auto slash = text.find('/');
    const auto number = [](std::string_view digits) {
        std::size_t value = 0;
        if (digits.empty() || digits.size() > 3)
            throw std::invalid_argument("--part expects K/N");
        for (const char digit : digits) {
            if (digit < '0' || digit > '9')
                throw std::invalid_argument("--part expects K/N");
            value = value * 10U + static_cast<std::size_t>(digit - '0');
        }
        return value;
    };
    if (slash == std::string_view::npos)
        throw std::invalid_argument("--part expects K/N");
    const MapPart part{number(text.substr(0, slash)), number(text.substr(slash + 1))};
    if (part.index == 0 || part.index > part.count)
        throw std::invalid_argument("--part expects K/N with K from 1 through N");
    return part;
}

void test_installed_maps(const oa::AssetStore& assets, MapPart part) {
    std::vector<std::string> maps = assets.list_effective("maps", ".tnt");
    std::sort(maps.begin(), maps.end());
    // The part's maps; each keeps its place in the whole list, which picks
    // the maps checked through a gamma table.
    const std::size_t first = maps.size() * (part.index - 1U) / part.count;
    const std::size_t end = maps.size() * part.index / part.count;
    const oa::PaletteBytes palette = installed_palette(assets);
    const GammaTable brighter = gamma_table(1.25F);
    int checked = 0;
    int unparsed = 0;
    int refused = 0;
    int through_gamma = 0;
    std::size_t wrong = 0;
    uint64_t cells = 0;
    uint64_t slots = 0;
    uint64_t pages = 0;
    uint64_t texel_bytes = 0;
    std::string largest_name;
    uint64_t largest_bytes = 0;
    std::string largest_figure;
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t m = first; m < end; ++m) {
        if ((m + 1U) % progress_maps == 0 || m + 1U == end) {
            const std::chrono::duration<double> elapsed =
                std::chrono::steady_clock::now() - started;
            std::printf("  %zu of %zu maps, %.1f s\n", m + 1U, maps.size(), elapsed.count());
            std::fflush(stdout);
        }
        const auto bytes = oa::test::read_game_file(assets, maps[m]);
        const auto parsed = oa::formats::tnt::parse(bytes);
        if (!parsed.ok()) {
            ++unparsed;
            continue;
        }
        const Map& map = *parsed.map;
        gw::TerrainAtlas atlas;
        const auto error = gw::build_terrain_atlas(
            map, map.tile_width, map.tile_height, palette, nullptr, gw::page_edge_limit, atlas
        );
        if (error != gw::TerrainAtlasError::none) {
            // A map the atlas refuses is one the fill refuses too.
            std::vector<uint8_t> rgb(static_cast<std::size_t>(map.tile_width) * tile_edge * 3U);
            const auto fill_error = wr::fill_scaled_viewport(
                map,
                palette,
                0,
                0,
                map.tile_width * tile_edge,
                map.tile_height * tile_edge,
                map.tile_width * tile_edge,
                1,
                1.0F,
                rgb.data(),
                map.tile_width * tile_edge
            );
            std::printf("%s refused: %s\n", maps[m].c_str(), gw::terrain_atlas_error_text(error));
            OA_CHECK(fill_error.has_value());
            ++refused;
            continue;
        }
        ++checked;
        wrong += compare_installed_map(map, atlas, palette, nullptr);
        if (m % gamma_map_step == 0) {
            gw::TerrainAtlas lit;
            OA_CHECK(
                gw::build_terrain_atlas(
                    map,
                    map.tile_width,
                    map.tile_height,
                    palette,
                    &brighter,
                    gw::page_edge_limit,
                    lit
                ) == gw::TerrainAtlasError::none
            );
            wrong += compare_installed_map(map, lit, palette, &brighter);
            ++through_gamma;
        }
        const uint64_t atlas_bytes = atlas_texel_bytes(atlas);
        const auto footprint = gw::terrain_atlas_footprint(
            static_cast<uint32_t>(atlas.slot_tiles.size()), atlas.grid.size(), gw::page_edge_limit
        );
        OA_CHECK(footprint.texel_bytes == atlas_bytes && footprint.pages == atlas.pages.size());
        cells += atlas.grid.size();
        slots += atlas.slot_tiles.size();
        pages += atlas.pages.size();
        texel_bytes += atlas_bytes;
        if (atlas_bytes > largest_bytes) {
            largest_bytes = atlas_bytes;
            largest_name = maps[m];
            largest_figure = atlas_figure(maps[m], map, atlas);
        }
    }
    // The largest atlas within 2048 as well: the same tiles and views on
    // more pages, each within the edge, and its footprint equal to the build.
    // A part checks its own largest.
    if (!largest_name.empty()) {
        const auto bytes = oa::test::read_game_file(assets, largest_name);
        const auto parsed = oa::formats::tnt::parse(bytes);
        OA_CHECK(parsed.ok());
        if (parsed.ok()) {
            const Map& map = *parsed.map;
            const gw::TerrainAtlas wide = build(map, palette, nullptr, gw::page_edge_limit);
            const gw::TerrainAtlas narrow = build(map, palette, nullptr, narrow_page_edge);
            OA_CHECK(narrow.grid == wide.grid && narrow.slot_tiles == wide.slot_tiles);
            bool within = true;
            for (const auto& page : narrow.pages)
                within =
                    within && page.width <= narrow_page_edge && page.height <= narrow_page_edge;
            OA_CHECK(within);
            OA_CHECK(same_views(wide, narrow, map));
            const auto footprint = gw::terrain_atlas_footprint(
                static_cast<uint32_t>(narrow.slot_tiles.size()),
                narrow.grid.size(),
                narrow_page_edge
            );
            OA_CHECK(footprint.pages == narrow.pages.size());
            OA_CHECK(footprint.texel_bytes == atlas_texel_bytes(narrow));
            largest_figure += "\n" + atlas_figure(largest_name, map, narrow);
        }
    }
    std::printf(
        "installed terrain atlases, maps %zu to %zu of %zu: %d maps checked (%d through a gamma "
        "table), %d unparsed, %d refused; "
        "%llu cells, %llu slots, %llu pages, %llu bytes of texels in all; %zu texels differ\n",
        first + 1U,
        end,
        maps.size(),
        checked,
        through_gamma,
        unparsed,
        refused,
        static_cast<unsigned long long>(cells),
        static_cast<unsigned long long>(slots),
        static_cast<unsigned long long>(pages),
        static_cast<unsigned long long>(texel_bytes),
        wrong
    );
    std::printf("%s\n", largest_figure.c_str());
    OA_CHECK(checked > 0 && wrong == 0);
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv)) {
        MapPart part;
        if (argc > 3 && std::string_view(argv[2]) == "--part") {
            try {
                part = parse_map_part(argv[3]);
            } catch (const std::invalid_argument& error) {
                std::fprintf(stderr, "%s\n", error.what());
                return 2;
            }
        }
        test_installed_maps(
            oa::test::require_game_assets("the installed maps' terrain atlases"), part
        );
    } else {
        test_page_plan();
        test_packing();
        test_page_edges();
        test_zoom_1_matches_fill();
        test_mips_match_box_filter();
        test_full_chain_exact();
        test_pinned_digests();
        test_shown_grid();
        test_malformed_maps();
        test_read_refusals();
        test_footprint();
    }
    const int status = oa::test::check_exit_status();
    if (status == 0)
        std::puts("terrain atlas tests passed");
    return status;
}
