// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/world_renderer/world_fog.hpp"
#include "oa/test/check.hpp"

#include <cstdint>
#include <vector>

namespace wr = oa::present::world_renderer;

namespace {

// The viewer is player 1, so its mapped bit is 0x2.
struct SightMap {
    oa::sim::visibility_state::PlayerSightGrid grid{};

    SightMap(int32_t w, int32_t h, uint8_t seen, uint16_t mapped_bits) {
        grid.width = w;
        grid.height = h;
        grid.coverage.assign(static_cast<std::size_t>(w * h), seen);
        grid.player_bits.assign(static_cast<std::size_t>(w * h), mapped_bits);
        grid.viewpoint_player = 1;
    }

    [[nodiscard]] wr::FogGrid build(
        int32_t camera_x,
        int32_t camera_z,
        int32_t view_w,
        int32_t view_h,
        bool los = true,
        bool mapping = true
    ) const {
        return wr::build_fog_grid(
            grid,
            grid.viewpoint_player,
            grid.coverage,
            {los, mapping},
            camera_x,
            camera_z,
            view_w,
            view_h
        );
    }
};

void placement_follows_camera() {
    const SightMap map(16, 16, 1, 0x2);
    const auto at_origin = map.build(0, 0, 64, 32);
    OA_CHECK(at_origin.width == 4 && at_origin.height == 3);
    OA_CHECK(at_origin.first_cell_x == -1 && at_origin.first_cell_z == -1);
    OA_CHECK(at_origin.offset_x == -16 && at_origin.offset_z == -16);
    OA_CHECK(at_origin.variant_phase == 0);
    const auto shifted = map.build(20, 50, 64, 32);
    OA_CHECK(shifted.first_cell_x == 0 && shifted.offset_x == -4);
    OA_CHECK(shifted.first_cell_z == 1 && shifted.offset_z == -2);
    OA_CHECK(shifted.variant_phase == 1 + 2);
}

void clear_when_everything_is_seen() {
    const SightMap map(16, 16, 1, 0x2);
    const auto grid = map.build(64, 64, 128, 128);
    for (const auto& tile : grid.tiles)
        OA_CHECK(tile.unseen == 0 && tile.unmapped == 0);
}

void one_unseen_cell_marks_four_corners() {
    SightMap map(16, 16, 1, 0x2);
    // Camera at 64 puts sight cell 1 at tile column 0 (offset -16).
    map.grid.coverage[5 * 16 + 4] = 0;
    const auto grid = map.build(64, 64, 128, 128);
    OA_CHECK(grid.first_cell_x == 1 && grid.first_cell_z == 1);
    const auto column = 4 - grid.first_cell_x;
    const auto row = 5 - grid.first_cell_z;
    OA_CHECK(grid.at(column, row).unseen == wr::fog_corner_top_left);
    OA_CHECK(grid.at(column - 1, row).unseen == wr::fog_corner_top_right);
    OA_CHECK(grid.at(column, row - 1).unseen == wr::fog_corner_bottom_left);
    OA_CHECK(grid.at(column - 1, row - 1).unseen == wr::fog_corner_bottom_right);
    std::size_t marked = 0;
    for (const auto& tile : grid.tiles)
        marked += tile.unseen != 0 ? 1U : 0U;
    OA_CHECK(marked == 4);
    const auto no_los = map.build(64, 64, 128, 128, false);
    for (const auto& tile : no_los.tiles)
        OA_CHECK(tile.unseen == 0);
}

void unmapped_cells_use_the_black_mask() {
    SightMap map(16, 16, 1, 0x2);
    map.grid.player_bits[6 * 16 + 6] = 0x1; // mapped by another player only
    const auto grid = map.build(64, 64, 128, 128);
    OA_CHECK(
        grid.at(6 - grid.first_cell_x, 6 - grid.first_cell_z).unmapped == wr::fog_corner_top_left
    );
    const auto unmapped_ignored = map.build(64, 64, 128, 128, true, false);
    for (const auto& tile : unmapped_ignored.tiles)
        OA_CHECK(tile.unmapped == 0);
}

// The fog reads the mapped bit of the player whose view it shows, which
// "+View" moves away from the sight grid's own viewer. A tile takes its
// corners from its own cell, the cell to its right and the two below them.
void mapped_terrain_follows_the_viewer() {
    // Player 1 has mapped every cell, player 0 only cell (4, 4).
    SightMap map(16, 16, 1, 0x2);
    map.grid.player_bits[4 * 16 + 4] = 0x1 | 0x2;
    const auto own = map.build(64, 64, 128, 128);
    for (const auto& tile : own.tiles)
        OA_CHECK(tile.unmapped == 0);
    const auto viewed =
        wr::build_fog_grid(map.grid, 0, map.grid.coverage, {true, true}, 64, 64, 128, 128);
    const auto unmapped = [&](int32_t x, int32_t z) {
        return viewed.at(x - viewed.first_cell_x, z - viewed.first_cell_z).unmapped;
    };
    OA_CHECK(unmapped(2, 2) == wr::fog_mask_full);
    OA_CHECK(unmapped(4, 4) == (wr::fog_mask_full & ~wr::fog_corner_top_left));
    OA_CHECK(unmapped(3, 3) == (wr::fog_mask_full & ~wr::fog_corner_bottom_right));
}

void map_border_extends_masks_outward() {
    const SightMap map(8, 8, 0, 0);
    const auto grid = map.build(0, 0, 64, 64);
    // Tile row 0 straddles the top map edge: its top corners are off the map
    // and inherit the bottom corners.
    OA_CHECK(grid.at(2, 0).unseen == wr::fog_mask_full);
    OA_CHECK(grid.at(2, 0).unmapped == wr::fog_mask_full);
    OA_CHECK(grid.at(0, 2).unseen == wr::fog_mask_full);
    OA_CHECK(grid.at(2, 2).unseen == wr::fog_mask_full);
}

// A view that is not a whole number of tiles, scrolled to the far corner:
// the tile straddling the right and bottom borders is the third one, not the
// last-but-one, and must still be extended while tiles beyond stay clear.
void border_tile_follows_the_edge_cell_at_any_view_size() {
    const SightMap map(8, 8, 0, 0);
    const auto grid = map.build(156, 156, 100, 100);
    OA_CHECK(grid.first_cell_x == 4 && grid.offset_x == -12 && grid.width == 6);
    const auto edge = 7 - grid.first_cell_x;
    for (int32_t along = 0; along <= edge; ++along) {
        OA_CHECK(grid.at(edge, along).unseen == wr::fog_mask_full);
        OA_CHECK(grid.at(edge, along).unmapped == wr::fog_mask_full);
        OA_CHECK(grid.at(along, edge).unseen == wr::fog_mask_full);
        OA_CHECK(grid.at(edge + 1, along).unseen == 0 && grid.at(edge + 1, along).unmapped == 0);
        OA_CHECK(grid.at(along, edge + 1).unseen == 0);
    }
}

void map_span_follows_the_terrain_dda() {
    OA_CHECK(wr::fog_map_span(wr::fog_zoom_one, 100) == 100);
    OA_CHECK(wr::fog_map_span(2 * wr::fog_zoom_one, 100) == 50);
    OA_CHECK(wr::fog_map_span(wr::fog_zoom_one / 2, 100) == 199);
    OA_CHECK(wr::fog_map_span(wr::fog_zoom_one, 0) == 0);
}

struct Canvas {
    wr::Surface surface{};

    explicit Canvas(uint32_t size) {
        surface.width = size;
        surface.height = size;
        surface.rgb.assign(static_cast<std::size_t>(size) * size * 3U, 100);
    }

    [[nodiscard]] uint8_t red(int32_t x, int32_t y) const {
        return surface
            .rgb[(static_cast<std::size_t>(y) * surface.width + static_cast<std::size_t>(x)) * 3U];
    }
};

wr::FogShading test_shading() {
    wr::FogShading shading;
    for (std::size_t level = 0; level < shading.gray_levels.size(); ++level)
        shading.gray_levels[level] = {static_cast<uint8_t>(level / 2), 0, 0};
    for (std::size_t index = 0; index < shading.palette_rgb.size(); ++index)
        shading.palette_rgb[index] = {static_cast<uint8_t>(index), 7, 7};
    shading.unmapped_rgb = {1, 2, 3};
    shading.dither_rgb = {9, 9, 9};
    return shading;
}

// Gray tile for the top-left corner mask covers its top-left quadrant; the
// black tile for the same mask covers only its first texel with index 200.
wr::FogTileSet test_tiles() {
    wr::FogTileSet tiles;
    tiles.art.resize(2 * wr::FogTileSet::tiles_per_bank);
    for (int32_t variant = 0; variant < wr::fog_tile_variants; ++variant) {
        auto& gray = tiles.at(wr::FogTileSet::gray, variant, wr::fog_corner_top_left);
        for (int32_t y = 0; y < 16; ++y)
            for (int32_t x = 0; x < 16; ++x)
                gray.opaque[static_cast<std::size_t>(y * 32 + x)] = 1;
        auto& black = tiles.at(wr::FogTileSet::black, variant, wr::fog_corner_top_left);
        black.opaque[0] = 1;
        black.index[0] = 200;
    }
    return tiles;
}

// Three-by-three grid with tile (0, 0) at map (-16, -16): tile (1, 1) is
// never mapped, tile (2, 1) is unseen, and tile (1, 2) has one unseen and one
// unmapped corner.
wr::FogGrid test_grid() {
    wr::FogGrid grid;
    grid.width = 3;
    grid.height = 3;
    grid.offset_x = -16;
    grid.offset_z = -16;
    grid.tiles.assign(9, {});
    grid.tiles[1 * 3 + 1].unmapped = wr::fog_mask_full;
    grid.tiles[1 * 3 + 2].unseen = wr::fog_mask_full;
    grid.tiles[2 * 3 + 1].unseen = wr::fog_corner_top_left;
    grid.tiles[2 * 3 + 1].unmapped = wr::fog_corner_top_left;
    return grid;
}

void drawing_fills_grays_and_masks_tiles() {
    Canvas canvas(64);
    const wr::FogView view{0, 0, 64, 64, 0, 0, wr::fog_zoom_one};
    wr::draw_fog_grid(canvas.surface, view, test_grid(), test_tiles(), test_shading());
    // Never-mapped tile (1, 1) spans map [16, 48) on both axes.
    OA_CHECK(canvas.red(16, 16) == 1 && canvas.red(47, 47) == 1);
    OA_CHECK(canvas.red(15, 16) == 100 && canvas.red(16, 15) == 100);
    // Unseen tile (2, 1) grays [48, 64) x [16, 48): level 100 maps to 50.
    OA_CHECK(canvas.red(48, 16) == 50 && canvas.red(63, 47) == 50 && canvas.red(48, 48) == 100);
    // Partial tile (1, 2) at [16, 48) x [48, 80): the gray quadrant then the
    // black texel at its top-left map pixel.
    OA_CHECK(canvas.red(16, 48) == 200);
    OA_CHECK(canvas.red(17, 48) == 50 && canvas.red(31, 63) == 50);
    OA_CHECK(canvas.red(32, 48) == 100);
}

void drawing_scales_with_the_zoom() {
    Canvas canvas(64);
    // Zoom 2: map pixel m covers destination [2m, 2m + 2).
    const wr::FogView view{0, 0, 64, 64, 0, 0, 2 * wr::fog_zoom_one};
    wr::draw_fog_grid(canvas.surface, view, test_grid(), test_tiles(), test_shading());
    OA_CHECK(canvas.red(31, 32) == 100 && canvas.red(32, 32) == 1 && canvas.red(63, 63) == 1);
    Canvas half(64);
    // Zoom 1/2: only even map pixels are shown, one destination pixel each.
    const wr::FogView zoomed_out{0, 0, 64, 64, 0, 0, wr::fog_zoom_one / 2};
    wr::draw_fog_grid(half.surface, zoomed_out, test_grid(), test_tiles(), test_shading());
    OA_CHECK(half.red(7, 8) == 100 && half.red(8, 8) == 1 && half.red(23, 23) == 1);
    OA_CHECK(half.red(24, 8) == 50 && half.red(31, 23) == 50 && half.red(40, 8) == 100);
    OA_CHECK(half.red(8, 24) == 200 && half.red(9, 24) == 50 && half.red(16, 24) == 100);
}

void drawing_clips_to_the_surface_and_view() {
    Canvas canvas(40);
    const wr::FogView view{8, 8, 64, 64, 0, 0, wr::fog_zoom_one};
    wr::draw_fog_grid(canvas.surface, view, test_grid(), test_tiles(), test_shading());
    OA_CHECK(canvas.red(23, 24) == 100 && canvas.red(24, 24) == 1 && canvas.red(39, 39) == 1);
    Canvas untouched(64);
    wr::FogGrid empty;
    wr::draw_fog_grid(untouched.surface, view, empty, test_tiles(), test_shading());
    OA_CHECK(untouched.red(24, 24) == 100);
}

void dithered_fog_clears_alternate_map_pixels() {
    Canvas canvas(64);
    auto shading = test_shading();
    shading.dithered = true;
    const wr::FogView view{0, 0, 64, 64, 5, 0, wr::fog_zoom_one};
    wr::draw_fog_grid(canvas.surface, view, test_grid(), test_tiles(), shading);
    // Unseen tile (2, 1): map x 48 + camera 5 is odd, so 48 keeps the ground
    // and 49 clears.
    OA_CHECK(canvas.red(48, 16) == 100 && canvas.red(49, 16) == 9 && canvas.red(49, 17) == 100);
    // The partial tile's gray quadrant dithers the same way.
    OA_CHECK(canvas.red(17, 48) == 9 && canvas.red(18, 48) == 100);
}

} // namespace

// ui.map-features-ignore-los: a map-placed feature's owner slot decides
// whether it skips the sight test.
void feature_owner_rule_skips_the_sight_test() {
    constexpr wr::FeatureOwnerRule base{};
    // 3.1c: the map's owner 10 never matches a player.
    OA_CHECK(!wr::feature_drawn_without_sight(10, true, 1, base));
    OA_CHECK(wr::feature_drawn_without_sight(1, false, 1, base));
    OA_CHECK(!wr::feature_drawn_without_sight(11, false, 1, base));
    // Owner 11: map features show to everyone; replaced ones do not.
    constexpr wr::FeatureOwnerRule free{true, 11};
    OA_CHECK(wr::feature_drawn_without_sight(10, true, 1, free));
    OA_CHECK(wr::feature_drawn_without_sight(10, true, 7, free));
    OA_CHECK(!wr::feature_drawn_without_sight(10, false, 1, free));
    OA_CHECK(wr::feature_drawn_without_sight(3, false, 3, free));
    // Owner 10 under the rule leaves the sight test.
    constexpr wr::FeatureOwnerRule inert{true, 10};
    OA_CHECK(!wr::feature_drawn_without_sight(10, true, 1, inert));
    // A player slot shows the map's features to that player alone.
    constexpr wr::FeatureOwnerRule player{true, 2};
    OA_CHECK(wr::feature_drawn_without_sight(10, true, 2, player));
    OA_CHECK(!wr::feature_drawn_without_sight(10, true, 1, player));
}

int main() {
    placement_follows_camera();
    clear_when_everything_is_seen();
    one_unseen_cell_marks_four_corners();
    unmapped_cells_use_the_black_mask();
    mapped_terrain_follows_the_viewer();
    map_border_extends_masks_outward();
    border_tile_follows_the_edge_cell_at_any_view_size();
    map_span_follows_the_terrain_dda();
    drawing_fills_grays_and_masks_tiles();
    drawing_scales_with_the_zoom();
    drawing_clips_to_the_surface_and_view();
    dithered_fog_clears_alternate_map_pixels();
    feature_owner_rule_skips_the_sight_test();
    return oa::test::check_exit_status();
}
