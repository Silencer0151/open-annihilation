// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/world_renderer/world_terrain_view.hpp"

#include "oa/present/blit.hpp"
#include "oa/present/world_renderer/world_camera.hpp"

namespace oa::present::world_renderer {

namespace {

constexpr int32_t tile = terrain_tile_pixels;
constexpr int32_t row_stride_mask = 0xFFFF;

const uint8_t* tile_image(const TerrainTiles& tiles, int32_t cell) noexcept {
    return tiles.tile_pixels + static_cast<ptrdiff_t>(tiles.tile_map[cell]) * terrain_tile_bytes;
}

void draw_cut_tile(
    ::oa::Surface* target,
    ::oa::Sprite& cut,
    const TerrainTiles& tiles,
    int32_t cell,
    int32_t x,
    int32_t y
) noexcept {
    cut.data = const_cast<uint8_t*>(tile_image(tiles, cell));
    present::draw_sprite_opaque(target, &cut, x, y);
}

} // namespace

bool draw_terrain_view(
    ::oa::Surface* target, const Game& game, const TerrainTiles& tiles
) noexcept {
    const Rect32 battlefield = game.battlefield_rect;
    const auto camera_x = static_cast<int32_t>(game.camera_x);
    const auto camera_y = static_cast<int32_t>(game.camera_y);
    const int32_t stride = game.map_width / 2;
    int32_t left = battlefield.x1;
    int32_t top = battlefield.y1;
    int32_t tile_x = camera_x / tile;
    int32_t tile_y = camera_y / tile;
    // Pixels of the first tile column/row left of or above the view, and of
    // the last one inside it.
    const int32_t cut_left = camera_x % tile;
    const int32_t cut_top = camera_y % tile;
    int32_t columns = (cut_left + game.viewport_width) / tile;
    int32_t rows = (cut_top + game.viewport_height) / tile;
    const int32_t cut_right = cut_left + game.viewport_width - columns * tile;
    const int32_t cut_bottom = cut_top + game.viewport_height - rows * tile;
    if (cut_right != 0) {
        ++columns;
    }
    if (cut_bottom != 0) {
        ++rows;
    }
    if (tiles.tile_map == nullptr || tiles.tile_pixels == nullptr || camera_x < 0 || camera_y < 0 ||
        tile_x + columns > stride || tile_y + rows > game.map_height / 2) {
        return false;
    }
    ::oa::Sprite cut{};
    cut.width = tile;
    cut.height = tile;
    cut.encoding = OA_SPRITE_RAW;
    if (cut_left != 0 || cut_right != 0) {
        int32_t first = tile_x + stride * tile_y;
        int32_t last = stride * tile_y + columns + tile_x - 1;
        for (int32_t row = 0; row < rows; ++row, first += stride, last += stride) {
            const int32_t y = row * tile - cut_top + top;
            if (cut_left != 0) {
                draw_cut_tile(target, cut, tiles, first, left - cut_left, y);
            }
            if (cut_right != 0) {
                draw_cut_tile(target, cut, tiles, last, columns * tile - cut_left - tile + left, y);
            }
        }
    }
    if (cut_top != 0 || cut_bottom != 0) {
        const int32_t first = stride * tile_y + tile_x;
        const int32_t last = (rows - 1 + tile_y) * stride + tile_x;
        for (int32_t column = 0; column < columns; ++column) {
            const int32_t x = column * tile - cut_left + left;
            if (cut_top != 0) {
                draw_cut_tile(target, cut, tiles, first + column, x, top - cut_top);
            }
            if (cut_bottom != 0) {
                draw_cut_tile(
                    target, cut, tiles, last + column, x, rows * tile - cut_top - tile + top
                );
            }
        }
    }
    if (cut_left != 0) {
        --columns;
        left += tile - cut_left;
        ++tile_x;
    }
    if (cut_top != 0) {
        --rows;
        top += tile - cut_top;
        ++tile_y;
    }
    if (cut_right != 0) {
        --columns;
    }
    if (cut_bottom != 0) {
        --rows;
    }
    const int32_t row_stride = stride & row_stride_mask;
    for (int32_t row = 0; row < rows; ++row) {
        const int32_t first = row_stride * (tile_y + row) + tile_x;
        for (int32_t column = 0; column < columns; ++column) {
            present::blit_tile(
                target, left + column * tile, top + row * tile, tile_image(tiles, first + column)
            );
        }
    }
    return true;
}

} // namespace oa::present::world_renderer
