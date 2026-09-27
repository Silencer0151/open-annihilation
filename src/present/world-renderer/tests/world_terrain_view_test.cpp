// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The 8-bit terrain view: edge and interior tiles at every tile phase, the
// rejection of views past the mosaic, and the installation's maps compared with
// the RGB crop renderer through a palette whose red channel is the index.

#include "oa/present/surface.hpp"
#include "oa/present/world_renderer/world_camera.hpp"
#include "oa/present/world_renderer.hpp"
#include "oa/present/world_renderer/world_terrain_view.hpp"
#include "oa/test/game_assets.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace wr = oa::present::world_renderer;

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

constexpr uint8_t untouched = 0;

struct View {
    std::unique_ptr<oa::Game> game = std::make_unique<oa::Game>();
    oa::present::SurfaceBuffer surface;
};

// A battlefield of width x height at (left, top) of a surface with a margin
// around it, clipped to the battlefield as the frame draw does.
View make_view(
    uint32_t map_cells_wide,
    uint32_t map_cells_high,
    int32_t left,
    int32_t top,
    int32_t width,
    int32_t height
) {
    View view;
    oa::Game& game = *view.game;
    game.map_width = static_cast<int32_t>(map_cells_wide);
    game.map_height = static_cast<int32_t>(map_cells_high);
    game.viewport_width = width;
    game.viewport_height = height;
    const oa::Rect32 battlefield{left, top, left + width - 1, top + height - 1};
    game.battlefield_rect = battlefield;
    view.surface = oa::present::create_surface(left + width + 40, top + height + 40);
    view.surface.surface.clip = battlefield;
    return view;
}

// Map pixel (x, y) of the mosaic.
uint8_t mosaic_pixel(const wr::TerrainTiles& tiles, int32_t tiles_per_row, int32_t x, int32_t y) {
    const int32_t tile =
        tiles.tile_map[(y / wr::terrain_tile_pixels) * tiles_per_row + x / wr::terrain_tile_pixels];
    return tiles.tile_pixels
        [tile * wr::terrain_tile_bytes + (y % wr::terrain_tile_pixels) * wr::terrain_tile_pixels +
         x % wr::terrain_tile_pixels];
}

// True when the battlefield shows the mosaic at the camera and nothing
// outside it was written.
bool shows_mosaic(const View& view, const wr::TerrainTiles& tiles) {
    const oa::Game& game = *view.game;
    const oa::Rect32 battlefield = game.battlefield_rect;
    const oa::Surface& surface = view.surface.surface;
    for (int32_t y = 0; y < surface.height; ++y) {
        for (int32_t x = 0; x < surface.width; ++x) {
            const uint8_t pixel = surface.pixels[y * surface.pitch + x];
            const bool inside = x >= battlefield.x1 && x <= battlefield.x2 && y >= battlefield.y1 &&
                                y <= battlefield.y2;
            const uint8_t expected =
                inside ? mosaic_pixel(
                             tiles,
                             game.map_width / 2,
                             static_cast<int32_t>(game.camera_x) + x - battlefield.x1,
                             static_cast<int32_t>(game.camera_y) + y - battlefield.y1
                         )
                       : untouched;
            if (pixel != expected) {
                return false;
            }
        }
    }
    return true;
}

void test_tile_phases() {
    constexpr int32_t tiles_wide = 5;
    constexpr int32_t tiles_high = 4;
    constexpr int32_t tile_count = 7;
    std::vector<uint16_t> tile_map(tiles_wide * tiles_high);
    std::vector<uint8_t> pixels(tile_count * wr::terrain_tile_bytes);
    for (size_t i = 0; i < tile_map.size(); ++i) {
        tile_map[i] = static_cast<uint16_t>((i * 3) % tile_count);
    }
    for (size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<uint8_t>(1 + (i * 7 + i / 32) % 255);
    }
    const wr::TerrainTiles tiles{tile_map.data(), pixels.data()};
    const int32_t sizes[][2] = {{64, 64}, {70, 50}, {1, 1}, {31, 33}, {96, 96}, {130, 90}};
    int drawn = 0;
    for (const auto& size : sizes) {
        for (int32_t camera_y = 0; camera_y + size[1] <= tiles_high * 32; camera_y += 13) {
            for (int32_t camera_x = 0; camera_x + size[0] <= tiles_wide * 32; camera_x += 11) {
                View view = make_view(tiles_wide * 2, tiles_high * 2, 20, 9, size[0], size[1]);
                view.game->camera_x = static_cast<uint32_t>(camera_x);
                view.game->camera_y = static_cast<uint32_t>(camera_y);
                CHECK(wr::draw_terrain_view(&view.surface.surface, *view.game, tiles));
                CHECK(shows_mosaic(view, tiles));
                ++drawn;
            }
        }
    }
    CHECK(drawn > 100);

    View past = make_view(tiles_wide * 2, tiles_high * 2, 20, 9, 64, 64);
    past.game->camera_x = tiles_wide * 32 - 63;
    CHECK(!wr::draw_terrain_view(&past.surface.surface, *past.game, tiles));
    past.game->camera_x = 0;
    past.game->camera_y = tiles_high * 32 - 63;
    CHECK(!wr::draw_terrain_view(&past.surface.surface, *past.game, tiles));
    past.game->camera_y = 0;
    CHECK(!wr::draw_terrain_view(&past.surface.surface, *past.game, wr::TerrainTiles{}));
    CHECK(std::all_of(past.surface.pixels.begin(), past.surface.pixels.end(), [](uint8_t p) {
        return p == untouched;
    }));
}

// Cut tiles are clipped to the target; whole tiles are not.
void test_clip_applies_to_cut_tiles_only() {
    std::vector<uint16_t> tile_map(4 * 4, 0);
    std::vector<uint8_t> pixels(wr::terrain_tile_bytes, 9);
    const wr::TerrainTiles tiles{tile_map.data(), pixels.data()};
    View view = make_view(8, 8, 20, 9, 70, 70);
    view.game->camera_x = 10;
    view.game->camera_y = 10;
    view.surface.surface.clip = oa::Rect32{0, 0, 21, 9};
    CHECK(wr::draw_terrain_view(&view.surface.surface, *view.game, tiles));
    const oa::Surface& surface = view.surface.surface;
    // The first whole tile starts 22 pixels right and below the origin.
    CHECK(surface.pixels[31 * surface.pitch + 42] == 9);
    CHECK(surface.pixels[9 * surface.pitch + 21] == 9);
    CHECK(surface.pixels[9 * surface.pitch + 22] == untouched);
    CHECK(surface.pixels[20 * surface.pitch + 30] == untouched);
}

// Every installed_map_step-th map of the installed game, in name order, at
// camera positions across the whole map for two battlefield sizes.
constexpr size_t installed_map_step = 25;
constexpr int32_t camera_steps = 5;

void test_installed_maps(const oa::AssetStore& assets) {
    const std::vector<std::string> maps = assets.list_effective("maps", ".tnt");
    oa::PaletteBytes red_is_index{};
    for (size_t i = 0; i < oa::palette_color_count; ++i) {
        red_is_index[i * oa::palette_entry_bytes] = static_cast<uint8_t>(i);
    }
    const int32_t sizes[][2] = {{512, 416}, {1664, 952}};
    int maps_checked = 0;
    int views = 0;
    int mismatched = 0;
    for (size_t m = 0; m < maps.size(); m += installed_map_step) {
        const auto bytes = oa::test::read_game_file(assets, maps[m]);
        const auto parsed = oa::formats::tnt::parse(bytes);
        if (!parsed.ok()) {
            continue;
        }
        const oa::formats::tnt::Map& map = *parsed.map;
        const wr::TerrainTiles tiles{map.tile_indices.data(), map.tile_palette_indices.data()};
        const auto map_width = static_cast<int32_t>(map.tile_width) * wr::terrain_tile_pixels;
        const auto map_height = static_cast<int32_t>(map.tile_height) * wr::terrain_tile_pixels;
        ++maps_checked;
        for (const auto& size : sizes) {
            const int32_t width = std::min(size[0], map_width);
            const int32_t height = std::min(size[1], map_height);
            for (int32_t step = 0; step < camera_steps * camera_steps; ++step) {
                const int32_t camera_x =
                    (map_width - width) * (step % camera_steps) / (camera_steps - 1) - step % 3;
                const int32_t camera_y =
                    (map_height - height) * (step / camera_steps) / (camera_steps - 1) - step % 2;
                View view =
                    make_view(map.attribute_width, map.attribute_height, 128, 32, width, height);
                view.game->camera_x = static_cast<uint32_t>(std::max(camera_x, 0));
                view.game->camera_y = static_cast<uint32_t>(std::max(camera_y, 0));
                const auto crop = wr::render_viewport(
                    map,
                    red_is_index,
                    {view.game->camera_x,
                     view.game->camera_y,
                     static_cast<uint32_t>(width),
                     static_cast<uint32_t>(height)}
                );
                CHECK(crop.ok());
                CHECK(wr::draw_terrain_view(&view.surface.surface, *view.game, tiles));
                ++views;
                const oa::Surface& surface = view.surface.surface;
                bool same = crop.ok();
                for (int32_t y = 0; same && y < height; ++y) {
                    for (int32_t x = 0; x < width; ++x) {
                        if (surface.pixels[(32 + y) * surface.pitch + 128 + x] !=
                            crop.surface->rgb[(y * width + x) * 3]) {
                            same = false;
                            break;
                        }
                    }
                }
                mismatched += same ? 0 : 1;
            }
        }
    }
    std::printf(
        "installed terrain comparison: %d maps, %d views, %d mismatched\n",
        maps_checked,
        views,
        mismatched
    );
    CHECK(maps_checked > 0 && mismatched == 0);
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv)) {
        test_installed_maps(oa::test::require_game_assets("the installed terrain comparison"));
    } else {
        test_tile_phases();
        test_clip_applies_to_cut_tiles_only();
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("terrain view tests passed");
    return 0;
}
