// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The terrain fill and the fog drawn in bands: every pool size draws the
// bytes the calling thread alone draws, and the terrain fill samples the map
// pixel its scale names.
#include "oa/platform/job_pool.hpp"
#include "oa/present/world_renderer.hpp"
#include "oa/present/world_renderer/world_fog.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace wr = oa::present::world_renderer;
namespace job_pool = oa::platform::job_pool;

namespace {

int failures = 0;

void check_at(bool condition, const char* expression, const char* file, int line) {
    if (condition)
        return;
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
    ++failures;
}

#define CHECK(condition) check_at((condition), #condition, __FILE__, __LINE__)

/// Thread counts the bands are drawn on besides the calling thread alone.
constexpr uint32_t pool_sizes[] = {2, 3, 4, 8};
/// Seed of the test's map, fog grid and fog art.
constexpr uint32_t test_seed = 20261001;
/// The test map's size in tiles, and its distinct tiles.
constexpr uint32_t map_tiles_wide = 48;
constexpr uint32_t map_tiles_high = 40;
constexpr uint32_t map_tile_count = 24;
/// The destination of the terrain fill and the fog, in pixels.
constexpr uint32_t view_width = 1000;
constexpr uint32_t view_height = 700;

/// A map of random tiles, each of random palette indices.
oa::formats::tnt::Map test_map(std::mt19937& random) {
    oa::formats::tnt::Map map;
    map.tile_width = map_tiles_wide;
    map.tile_height = map_tiles_high;
    map.tile_count = map_tile_count;
    map.tile_indices.resize(static_cast<std::size_t>(map_tiles_wide) * map_tiles_high);
    for (auto& index : map.tile_indices)
        index = static_cast<uint16_t>(random() % map_tile_count);
    map.tile_palette_indices.resize(
        static_cast<std::size_t>(map_tile_count) * oa::formats::tnt::layout::tile_bytes
    );
    for (auto& index : map.tile_palette_indices)
        index = static_cast<uint8_t>(random());
    return map;
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

/// One terrain fill of the test's view, without a pool or on one.
std::vector<uint8_t> fill(
    const oa::formats::tnt::Map& map,
    const oa::PaletteBytes& palette,
    uint32_t source_x,
    uint32_t source_y,
    float scale,
    job_pool::Pool* pool
) {
    // A stride wider than the view, whose spare bytes no band may write.
    constexpr uint32_t stride = view_width + 5;
    std::vector<uint8_t> rgb(static_cast<std::size_t>(stride) * view_height * 3U, 0xa5);
    CHECK(!wr::fill_scaled_viewport(
               map,
               palette,
               source_x,
               source_y,
               view_width,
               view_height,
               scale,
               rgb.data(),
               stride,
               pool
    )
               .has_value());
    return rgb;
}

/// Checks each pixel of a fill against the map pixel its scale names:
/// pixel x shows map pixel source_x + floor(x * 65536 / scale_fp), and the
/// same down; past the map, black. The bytes past each row are untouched.
void check_samples(
    const std::vector<uint8_t>& rgb,
    const oa::formats::tnt::Map& map,
    const oa::PaletteBytes& palette,
    uint32_t source_x,
    uint32_t source_y,
    float scale
) {
    constexpr uint32_t stride = view_width + 5;
    constexpr uint32_t tile_edge = oa::formats::tnt::layout::tile_edge_pixels;
    const auto scale_fp = static_cast<uint64_t>(std::lround(static_cast<double>(scale) * 65536.0));
    const uint64_t map_width = static_cast<uint64_t>(map.tile_width) * tile_edge;
    const uint64_t map_height = static_cast<uint64_t>(map.tile_height) * tile_edge;
    bool all = true;
    for (uint32_t y = 0; y < view_height; ++y) {
        const uint64_t map_y = source_y + static_cast<uint64_t>(y) * 65536U / scale_fp;
        for (uint32_t x = 0; x < stride; ++x) {
            const uint8_t* pixel = &rgb[(static_cast<std::size_t>(y) * stride + x) * 3U];
            if (x >= view_width) {
                all = all && pixel[0] == 0xa5 && pixel[1] == 0xa5 && pixel[2] == 0xa5;
                continue;
            }
            const uint64_t map_x = source_x + static_cast<uint64_t>(x) * 65536U / scale_fp;
            uint8_t expected[3] = {0, 0, 0};
            if (map_x < map_width && map_y < map_height) {
                const auto tile =
                    map.tile_indices
                        [static_cast<std::size_t>(map_y / tile_edge) * map.tile_width +
                         static_cast<std::size_t>(map_x / tile_edge)];
                const auto index =
                    map.tile_palette_indices
                        [static_cast<std::size_t>(tile) * oa::formats::tnt::layout::tile_bytes +
                         static_cast<std::size_t>(map_y % tile_edge) * tile_edge +
                         static_cast<std::size_t>(map_x % tile_edge)];
                std::memcpy(expected, &palette[index * oa::palette_entry_bytes], 3);
            }
            all = all && std::memcmp(pixel, expected, 3) == 0;
        }
    }
    CHECK(all);
}

void terrain_fill_is_the_same_on_every_pool() {
    std::mt19937 random(test_seed);
    const auto map = test_map(random);
    const auto palette = test_palette();
    std::vector<std::unique_ptr<job_pool::Pool>> pools;
    for (const uint32_t threads : pool_sizes)
        pools.push_back(std::make_unique<job_pool::Pool>(threads));

    // Zooms in, out and unscaled, from the corner, the middle and past the
    // map's right and bottom edges.
    const struct {
        uint32_t source_x;
        uint32_t source_y;
        float scale;
    } views[] = {
        {0, 0, 1.0F},
        {517, 333, 1.0F},
        {1100, 900, 1.0F},
        {211, 97, 1.37F},
        {64, 640, 0.6F},
        {1300, 1100, 0.6F},
        {5, 7, 2.0F},
        {800, 3, 0.75F},
    };

    for (const auto& view : views) {
        const auto alone = fill(map, palette, view.source_x, view.source_y, view.scale, nullptr);
        check_samples(alone, map, palette, view.source_x, view.source_y, view.scale);
        for (const auto& pool : pools)
            CHECK(
                fill(map, palette, view.source_x, view.source_y, view.scale, pool.get()) == alone
            );
    }
}

void terrain_fill_reports_a_missing_tile_on_a_pool() {
    std::mt19937 random(test_seed);
    auto map = test_map(random);
    map.tile_indices[static_cast<std::size_t>(10) * map_tiles_wide + 10] = map_tile_count;
    const auto palette = test_palette();
    job_pool::Pool pool(4);
    std::vector<uint8_t> rgb(static_cast<std::size_t>(view_width) * view_height * 3U);
    const auto error = wr::fill_scaled_viewport(
        map, palette, 0, 0, view_width, view_height, 1.0F, rgb.data(), view_width, &pool
    );
    CHECK(error.has_value() && error->code == wr::ErrorCode::invalid_map_model);
}

/// The fog's colours, each entry distinct.
wr::FogShading test_shading() {
    wr::FogShading shading;
    for (std::size_t level = 0; level < shading.gray_levels.size(); ++level)
        shading.gray_levels[level] = {
            static_cast<uint8_t>(level / 2), static_cast<uint8_t>(level / 3), 11
        };
    for (std::size_t index = 0; index < shading.palette_rgb.size(); ++index)
        shading.palette_rgb[index] = {static_cast<uint8_t>(index), 7, static_cast<uint8_t>(~index)};
    shading.unmapped_rgb = {1, 2, 3};
    shading.dither_rgb = {9, 9, 9};
    return shading;
}

/// Fog art with random opaque texels and indices in every tile.
wr::FogTileSet test_tiles(std::mt19937& random) {
    wr::FogTileSet tiles;
    tiles.art.resize(2 * wr::FogTileSet::tiles_per_bank);
    for (auto& art : tiles.art)
        for (std::size_t texel = 0; texel < art.opaque.size(); ++texel) {
            art.opaque[texel] = static_cast<uint8_t>(random() % 3 == 0 ? 1 : 0);
            art.index[texel] = static_cast<uint8_t>(random());
        }
    return tiles;
}

/// A grid covering the test's view at a zoom, with random corner masks.
wr::FogGrid test_grid(std::mt19937& random, uint32_t zoom_fp) {
    wr::FogGrid grid;
    const int32_t span_x = wr::fog_map_span(zoom_fp, static_cast<int32_t>(view_width));
    const int32_t span_z = wr::fog_map_span(zoom_fp, static_cast<int32_t>(view_height));
    grid.width = (span_x + wr::fog_cell_pixels - 1) / wr::fog_cell_pixels + 2;
    grid.height = (span_z + wr::fog_cell_pixels - 1) / wr::fog_cell_pixels + 2;
    grid.offset_x = -13;
    grid.offset_z = -21;
    grid.variant_phase = 3;
    grid.tiles.resize(static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height));
    for (auto& tile : grid.tiles) {
        tile.unseen = static_cast<uint8_t>(random() % 2 == 0 ? random() % 16 : 0);
        tile.unmapped = static_cast<uint8_t>(random() % 3 == 0 ? random() % 16 : 0);
    }
    return grid;
}

/// One fog drawing over a ground of random colours.
std::vector<uint8_t> draw_fog(
    const std::vector<uint8_t>& ground,
    const wr::FogView& view,
    const wr::FogGrid& grid,
    const wr::FogTileSet& tiles,
    const wr::FogShading& shading,
    job_pool::Pool* pool
) {
    wr::Surface surface{view_width + 40, view_height + 30, ground};
    wr::draw_fog_grid(surface, view, grid, tiles, shading, pool);
    return surface.rgb;
}

void fog_is_the_same_on_every_pool() {
    std::mt19937 random(test_seed + 1);
    const auto tiles = test_tiles(random);
    std::vector<uint8_t> ground(
        static_cast<std::size_t>(view_width + 40) * (view_height + 30) * 3U
    );
    for (auto& byte : ground)
        byte = static_cast<uint8_t>(random());
    std::vector<std::unique_ptr<job_pool::Pool>> pools;
    for (const uint32_t threads : pool_sizes)
        pools.push_back(std::make_unique<job_pool::Pool>(threads));
    for (const uint32_t zoom_fp : {wr::fog_zoom_one, 89784U, 39322U, 2 * wr::fog_zoom_one}) {
        const auto grid = test_grid(random, zoom_fp);
        for (const bool dithered : {false, true}) {
            auto shading = test_shading();
            shading.dithered = dithered;
            const wr::FogView view{
                17,
                9,
                static_cast<int32_t>(view_width),
                static_cast<int32_t>(view_height),
                135,
                77,
                zoom_fp
            };
            const auto alone = draw_fog(ground, view, grid, tiles, shading, nullptr);
            CHECK(alone != ground);
            for (const auto& pool : pools)
                CHECK(draw_fog(ground, view, grid, tiles, shading, pool.get()) == alone);
        }
    }
}

} // namespace

int main() {
    terrain_fill_is_the_same_on_every_pool();
    terrain_fill_reports_a_missing_tile_on_a_pool();
    fog_is_the_same_on_every_pool();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("world draw bands: the terrain fill and the fog are the same on every pool");
    return EXIT_SUCCESS;
}
