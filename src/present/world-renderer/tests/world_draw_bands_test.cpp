// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The terrain fill and the fog drawn in bands: every pool size draws the
// bytes the calling thread alone draws, and the terrain fill samples the map
// pixel its scale names, from the shown map where shown_map_span says; and
// zoomed out, a view placed on the scene pixels laid from the map's corner
// (scene_origin) and moved by fractions of a map pixel shows its ground
// moved by whole pixels, never sampled afresh, where a view filled from its
// whole map pixel samples it afresh.
#include "oa/platform/job_pool.hpp"
#include "oa/present/scene_grid.hpp"
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
               map_tiles_wide * 32U,
               map_tiles_high * 32U,
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
        map,
        palette,
        0,
        0,
        map_tiles_wide * 32U,
        map_tiles_high * 32U,
        view_width,
        view_height,
        1.0F,
        rgb.data(),
        view_width,
        &pool
    );
    CHECK(error.has_value() && error->code == wr::ErrorCode::invalid_map_model);
}

/// A fill whose shown map ends before the mosaic, the last tile column and
/// the last four tile rows never shown: the pixels past the shown map are
/// black, as past the mosaic, and the rest are the whole mosaic's fill, at
/// zoom 1 and zoomed out, on the calling thread and on a pool.
void terrain_fill_ends_at_the_shown_map() {
    std::mt19937 random(test_seed);
    const auto map = test_map(random);
    const auto palette = test_palette();
    constexpr uint32_t mosaic_width = map_tiles_wide * 32U;
    constexpr uint32_t mosaic_height = map_tiles_high * 32U;
    constexpr uint32_t shown_width = mosaic_width - 32U;
    constexpr uint32_t shown_height = mosaic_height - 128U;
    constexpr uint32_t source_x = 700;
    constexpr uint32_t source_y = 500;
    job_pool::Pool pool(3);
    for (const float scale : {1.0F, 0.5F})
        for (job_pool::Pool* threads : {static_cast<job_pool::Pool*>(nullptr), &pool}) {
            const auto scale_fp = static_cast<uint64_t>(std::lround(scale * 65536.0));
            std::vector<uint8_t> whole(static_cast<std::size_t>(view_width) * view_height * 3U);
            std::vector<uint8_t> shown(whole.size());
            CHECK(!wr::fill_scaled_viewport(
                       map,
                       palette,
                       source_x,
                       source_y,
                       mosaic_width,
                       mosaic_height,
                       view_width,
                       view_height,
                       scale,
                       whole.data(),
                       view_width,
                       threads
            )
                       .has_value());
            CHECK(!wr::fill_scaled_viewport(
                       map,
                       palette,
                       source_x,
                       source_y,
                       shown_width,
                       shown_height,
                       view_width,
                       view_height,
                       scale,
                       shown.data(),
                       view_width,
                       threads
            )
                       .has_value());
            bool all = true;
            uint32_t beyond = 0;
            uint32_t filler_lit = 0;
            for (uint32_t y = 0; y < view_height; ++y) {
                const uint64_t map_y = source_y + static_cast<uint64_t>(y) * 65536U / scale_fp;
                for (uint32_t x = 0; x < view_width; ++x) {
                    const uint64_t map_x = source_x + static_cast<uint64_t>(x) * 65536U / scale_fp;
                    const std::size_t at = (static_cast<std::size_t>(y) * view_width + x) * 3U;
                    const bool past = map_x >= shown_width || map_y >= shown_height;
                    const bool filler = past && map_x < mosaic_width && map_y < mosaic_height;
                    beyond += past ? 1U : 0U;
                    for (std::size_t channel = 0; channel < 3U; ++channel) {
                        all = all && shown[at + channel] == (past ? 0U : whole[at + channel]);
                        filler_lit += filler && whole[at + channel] != 0 ? 1U : 0U;
                    }
                }
            }
            CHECK(all);
            // The view reaches past the shown map, where the whole fill
            // drew the mosaic's filler tiles that the shown fill left black.
            CHECK(beyond != 0);
            CHECK(filler_lit != 0);
        }
}

/// The pixels shown_map_span names are those the fill draws from the shown
/// map, and only those, the rest black (no entry of the test's palette is):
/// a view over the map's top-left corner zoomed out, one past its bottom-right
/// corner magnified, and one inside it at zoom 1, whose span reaches past
/// the view on both sides.
void shown_map_span_is_what_the_fill_shows() {
    std::mt19937 random(test_seed);
    const auto map = test_map(random);
    const auto palette = test_palette();
    constexpr uint32_t shown_width = map_tiles_wide * 32U - 32U;
    constexpr uint32_t shown_height = map_tiles_high * 32U - 128U;

    const struct {
        int32_t source_x;
        int32_t source_y;
        float scale;
    } views[] = {
        {-300, -170, 0.6F},
        {1200, 900, 1.37F},
        {200, 150, 1.0F},
    };

    for (const auto& view : views) {
        std::vector<uint8_t> rgb(static_cast<std::size_t>(view_width) * view_height * 3U);
        CHECK(!wr::fill_scaled_viewport(
                   map,
                   palette,
                   view.source_x,
                   view.source_y,
                   shown_width,
                   shown_height,
                   view_width,
                   view_height,
                   view.scale,
                   rgb.data(),
                   view_width
        )
                   .has_value());
        const auto across = wr::shown_map_span(view.source_x, shown_width, view.scale);
        const auto down = wr::shown_map_span(view.source_y, shown_height, view.scale);
        bool all = true;
        uint32_t shown_pixels = 0;
        uint32_t black_pixels = 0;
        for (uint32_t y = 0; y < view_height; ++y)
            for (uint32_t x = 0; x < view_width; ++x) {
                const auto at = (static_cast<std::size_t>(y) * view_width + x) * 3U;
                const bool black = rgb[at] == 0 && rgb[at + 1] == 0 && rgb[at + 2] == 0;
                const bool spanned = static_cast<int32_t>(x) >= across.first &&
                                     static_cast<int32_t>(x) < across.end &&
                                     static_cast<int32_t>(y) >= down.first &&
                                     static_cast<int32_t>(y) < down.end;
                all = all && black != spanned;
                shown_pixels += spanned ? 1U : 0U;
                black_pixels += black ? 1U : 0U;
            }
        CHECK(all);
        CHECK(shown_pixels != 0);
        if (view.scale != 1.0F)
            CHECK(black_pixels != 0);
        else
            CHECK(
                across.first < 0 && down.first < 0 &&
                across.end > static_cast<int32_t>(view_width) &&
                down.end > static_cast<int32_t>(view_height)
            );
    }
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

/// The zoomed-out scale the moving view is filled at, a scale whose step
/// lays the map's pixels unevenly, the place it starts from and the map
/// pixels it moves by a frame, a fraction of one.
constexpr float moving_scale = 0.35F;
constexpr double moving_start = 301.25;
constexpr double moving_step = 0.37;
constexpr uint32_t moving_frames = 12;

/// Checks that a view placed on the scene grid from the map's corner, moved
/// by fractions of a map pixel at a zoomed-out scale, fills the ground of
/// the first frame moved by whole pixels, the pixels the view's place moved
/// by on that grid; and that the same view filled from its whole map pixel,
/// with no phase, samples the ground afresh.
void terrain_fill_moves_by_whole_pixels() {
    std::mt19937 random(test_seed);
    const auto map = test_map(random);
    const auto palette = test_palette();
    const uint32_t step = oa::present::scene_step(moving_scale);
    const auto filled = [&](int32_t source, uint32_t phase) {
        std::vector<uint8_t> rgb(static_cast<std::size_t>(view_width) * view_height * 3U);
        CHECK(!wr::fill_scaled_viewport(
                   map,
                   palette,
                   source,
                   source,
                   map_tiles_wide * 32U,
                   map_tiles_high * 32U,
                   view_width,
                   view_height,
                   moving_scale,
                   rgb.data(),
                   view_width,
                   nullptr,
                   phase,
                   phase
        )
                   .has_value());
        return rgb;
    };
    // Whether frame b is frame a moved up and left by `moved` pixels, over
    // the pixels both show.
    const auto moved_by =
        [](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int64_t moved) {
            for (uint32_t y = 0; y + moved < view_height; ++y)
                for (uint32_t x = 0; x + moved < view_width; ++x)
                    if (std::memcmp(
                            &b[(static_cast<std::size_t>(y) * view_width + x) * 3U],
                            &a[(static_cast<std::size_t>(y + moved) * view_width + x + moved) * 3U],
                            3U
                        ) != 0)
                        return false;
            return true;
        };
    const auto pixel_of = [&](double place) {
        return std::llround(place * step / oa::present::scene_step_one);
    };
    const auto first = oa::present::scene_origin(moving_start, step);
    const auto first_frame = filled(static_cast<int32_t>(first.map_pixel), first.phase);
    bool resampled = false;
    for (uint32_t frame = 1; frame < moving_frames; ++frame) {
        const double place = moving_start + moving_step * frame;
        const auto origin = oa::present::scene_origin(place, step);
        CHECK(origin.phase < step);
        const int64_t moved = pixel_of(place) - pixel_of(moving_start);
        CHECK(moved_by(
            first_frame, filled(static_cast<int32_t>(origin.map_pixel), origin.phase), moved
        ));
        // From the whole map pixel the ground shows other map pixels.
        const auto whole = static_cast<int32_t>(std::floor(place));
        const auto whole_first = static_cast<int32_t>(std::floor(moving_start));
        if (whole != whole_first) {
            const auto from_whole = filled(whole, 0);
            const auto first_whole = filled(whole_first, 0);
            bool any = false;
            for (int64_t shift = 0; shift <= 2 && !any; ++shift)
                any = moved_by(first_whole, from_whole, shift);
            resampled = resampled || !any;
        }
    }
    CHECK(resampled);
    // At a scale of 1 the view starts on its nearest map pixel, with no phase.
    const auto unscaled = oa::present::scene_origin(moving_start, oa::present::scene_step_one);
    CHECK(unscaled.map_pixel == 301 && unscaled.phase == 0);
}

int main() {
    terrain_fill_is_the_same_on_every_pool();
    terrain_fill_reports_a_missing_tile_on_a_pool();
    terrain_fill_ends_at_the_shown_map();
    shown_map_span_is_what_the_fill_shows();
    terrain_fill_moves_by_whole_pixels();
    fog_is_the_same_on_every_pool();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("world draw bands: the terrain fill and the fog are the same on every pool");
    return EXIT_SUCCESS;
}
