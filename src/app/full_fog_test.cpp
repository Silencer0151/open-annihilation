// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Full tier's fog passes over a seeded map, from fog grids built by the
// processor's own builder over synthetic sight: the corner alphas and the
// alphas of levels drawn one over another; the greyed pass covering exactly
// the map pixels under the fog tiles drawn greyed, once each, whole terrain
// tiles where their four fog tiles are wholly greyed and quarters
// elsewhere, every quad's corner alpha the fog tile's own interpolated to
// that corner, its texels the right part of its tile on the greyed pages,
// its batches one for each run on a page, at an aligned camera and a
// camera between tiles, through a placement, and at two levels; the black
// pass and the dithered pass with their runs merged and their alphas; and
// what is refused.
#include "full_fog.hpp"

#include "oa/present/world_renderer/world_fog.hpp"
#include "oa/sim/visibility_state.hpp"
#include "oa/test/check.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <span>
#include <utility>
#include <vector>

namespace {

namespace fog = oa::app::full_fog;
namespace gw = oa::present::gpu_world;
namespace wr = oa::present::world_renderer;
namespace card = oa::app::card;
using oa::formats::tnt::Map;

/// Seed of the test's map and sight.
constexpr uint32_t test_seed = 20261003;
/// The seeded map's tiles each way.
constexpr uint32_t map_tiles_wide = 9;
constexpr uint32_t map_tiles_high = 7;
/// Map pixels in a tile edge, and bytes in a tile.
constexpr int32_t tile_edge = static_cast<int32_t>(gw::tile_edge);
constexpr std::size_t tile_bytes = oa::formats::tnt::layout::tile_bytes;
/// The view's size in map pixels: four tiles and a half each way.
constexpr int32_t view_width = 144;
constexpr int32_t view_height = 144;
/// How near two floats must be to count as equal.
constexpr float tolerance = 1e-5F;
/// The viewer's player, whose bit the mapped cells carry.
constexpr uint8_t viewer = 3;
/// Vertices and indices a quad adds.
constexpr std::size_t quad_vertices = 4;
constexpr std::size_t quad_indices = 6;

/// A seeded map whose every tile is distinct.
///
/// @param random the source of the tiles' pixels
/// @return the map
Map test_map(std::mt19937& random) {
    Map map;
    map.tile_width = map_tiles_wide;
    map.tile_height = map_tiles_high;
    map.tile_count = map_tiles_wide * map_tiles_high;
    map.tile_palette_indices.resize(static_cast<std::size_t>(map.tile_count) * tile_bytes);
    for (auto& index : map.tile_palette_indices)
        index = static_cast<uint8_t>(random());
    map.tile_indices.resize(static_cast<std::size_t>(map.tile_count));
    for (std::size_t cell = 0; cell < map.tile_indices.size(); ++cell)
        map.tile_indices[cell] = static_cast<uint16_t>(cell);
    return map;
}

/// The map's atlas, with the greyed pages' handles numbered from 1.
struct Atlas {
    gw::TerrainAtlas atlas;
    std::vector<card::PageHandle> pages;
};

/// Builds the seeded map's atlas within the largest page edge.
///
/// @return the atlas and a handle for each page
Atlas build_atlas() {
    std::mt19937 random(test_seed);
    const Map map = test_map(random);
    oa::PaletteBytes palette{};
    for (std::size_t byte = 0; byte < palette.size(); ++byte)
        palette[byte] = static_cast<uint8_t>(random());
    Atlas built;
    OA_CHECK(
        gw::build_terrain_atlas(
            map, map.tile_width, map.tile_height, palette, nullptr, gw::page_edge_limit, built.atlas
        ) == gw::TerrainAtlasError::none
    );
    for (std::size_t page = 0; page < built.atlas.pages.size(); ++page)
        built.pages.push_back({static_cast<uint32_t>(page + 1)});
    return built;
}

/// Synthetic sight over the map: a cell for each tile.
struct Sight {
    oa::sim::visibility_state::PlayerSightGrid grid;
    std::vector<uint8_t> coverage;

    /// Makes every cell seen and mapped.
    Sight() {
        grid.width = static_cast<int32_t>(map_tiles_wide);
        grid.height = static_cast<int32_t>(map_tiles_high);
        grid.viewpoint_player = viewer;
        const auto cells = static_cast<std::size_t>(grid.width) * grid.height;
        coverage.assign(cells, 1);
        grid.player_bits.assign(cells, static_cast<uint16_t>(1U << viewer));
    }

    /// Returns a cell's index.
    ///
    /// @param x the cell's column
    /// @param z the cell's row
    /// @return the index into coverage and player_bits
    [[nodiscard]] std::size_t cell(int32_t x, int32_t z) const {
        return static_cast<std::size_t>(z) * static_cast<std::size_t>(grid.width) +
               static_cast<std::size_t>(x);
    }

    /// Takes a cell out of sight.
    ///
    /// @param x the cell's column
    /// @param z the cell's row
    void unsee(int32_t x, int32_t z) { coverage[cell(x, z)] = 0; }

    /// Takes a cell's mapping away, and its sight.
    ///
    /// @param x the cell's column
    /// @param z the cell's row
    void unmap(int32_t x, int32_t z) {
        coverage[cell(x, z)] = 0;
        grid.player_bits[cell(x, z)] = 0;
    }

    /// Builds the fog grid for a camera over the view.
    ///
    /// @param camera_x map pixel at the view's left edge
    /// @param camera_z map pixel at the view's top edge
    /// @return the grid
    [[nodiscard]] wr::FogGrid fog(int32_t camera_x, int32_t camera_z) const {
        return wr::build_fog_grid(
            grid,
            grid.viewpoint_player,
            coverage,
            {true, true},
            camera_x,
            camera_z,
            view_width,
            view_height
        );
    }
};

/// Tells whether two floats are within tolerance.
///
/// @param a a float
/// @param b another
/// @return true when they are near
[[nodiscard]] bool near(float a, float b) {
    return std::fabs(a - b) <= tolerance;
}

/// A quad of a frame, read back from its vertices.
struct Quad {
    std::array<card::Vertex, quad_vertices>
        corners{}; ///< top-left, top-right, bottom-right, bottom-left
    std::size_t batch{};
};

/// Reads the quads of a frame's draw batches, each batch's indices as runs
/// of six, checking the index pattern of a quad.
///
/// @param frame the frame
/// @return the quads in index order
std::vector<Quad> quads_of(const card::CardFrame& frame) {
    std::vector<Quad> quads;
    for (std::size_t batch = 0; batch < frame.batches.size(); ++batch) {
        const auto& drawn = frame.batches[batch];
        OA_CHECK(drawn.operation == card::Operation::draw);
        OA_CHECK(drawn.index_count % quad_indices == 0);
        for (card::Index at = drawn.first_index;
             at + quad_indices <= drawn.first_index + drawn.index_count;
             at += quad_indices) {
            const auto first = frame.indices[at];
            const bool pattern =
                frame.indices[at + 1] == first + 1 && frame.indices[at + 2] == first + 2 &&
                frame.indices[at + 3] == first && frame.indices[at + 4] == first + 2 &&
                frame.indices[at + 5] == first + 3;
            OA_CHECK(pattern);
            Quad quad;
            for (std::size_t corner = 0; corner < quad_vertices; ++corner)
                quad.corners[corner] = frame.vertices[first + corner];
            quad.batch = batch;
            quads.push_back(quad);
        }
    }
    return quads;
}

/// The placement of a view at scale 1 with the camera's map pixel at the
/// target's origin.
///
/// @param camera_x map pixel at the view's left edge
/// @param camera_z map pixel at the view's top edge
/// @return the placement
fog::FogPlacement plain(int32_t camera_x, int32_t camera_z) {
    fog::FogPlacement placement;
    placement.camera_x = camera_x;
    placement.camera_z = camera_z;
    return placement;
}

/// The corner alphas a fog tile's mask gives, as the grid's painter reads
/// them: bit 1 top-left, 2 top-right, 4 bottom-left, 8 bottom-right.
void corner_alphas_follow_the_mask() {
    const auto alphas = [](uint8_t mask) { return fog::corner_alphas(mask); };
    OA_CHECK((alphas(0) == std::array<float, 4>{0, 0, 0, 0}));
    OA_CHECK((alphas(wr::fog_corner_top_left) == std::array<float, 4>{1, 0, 0, 0}));
    OA_CHECK((alphas(wr::fog_corner_top_right) == std::array<float, 4>{0, 1, 0, 0}));
    OA_CHECK((alphas(wr::fog_corner_bottom_left) == std::array<float, 4>{0, 0, 1, 0}));
    OA_CHECK((alphas(wr::fog_corner_bottom_right) == std::array<float, 4>{0, 0, 0, 1}));
    OA_CHECK((alphas(wr::fog_mask_full) == std::array<float, 4>{1, 1, 1, 1}));
    OA_CHECK(
        (alphas(wr::fog_corner_top_right | wr::fog_corner_bottom_left) ==
         std::array<float, 4>{0, 1, 1, 0})
    );
}

/// Levels drawn one over another at pass_alpha's alphas leave the corner's
/// alpha of their weighted picture over the ground: a corner wholly out of
/// sight shows the levels' blend alone, one in sight the ground alone.
void pass_alphas_compose_the_weighted_picture() {
    // One level: the corner's alpha itself.
    OA_CHECK(near(fog::pass_alpha(0.0F, 1.0F, 0.0F), 0.0F));
    OA_CHECK(near(fog::pass_alpha(0.5F, 1.0F, 0.0F), 0.5F));
    OA_CHECK(near(fog::pass_alpha(1.0F, 1.0F, 0.0F), 1.0F));
    // Two levels, the first at share t, the second at 1 - t: the first
    // pass's alpha is 1 at a fogged corner whatever t, since the second
    // then blends the rest in.
    for (const float t : {0.0F, 0.3F, 0.5F, 0.7F, 1.0F}) {
        OA_CHECK(near(fog::pass_alpha(1.0F, t, 1.0F - t), t == 0.0F ? 0.0F : 1.0F));
        OA_CHECK(near(fog::pass_alpha(1.0F, 1.0F - t, 0.0F), 1.0F - t));
        for (const float alpha : {0.0F, 0.25F, 0.5F, 0.75F, 1.0F}) {
            const float first = fog::pass_alpha(alpha, t, 1.0F - t);
            const float second = fog::pass_alpha(alpha, 1.0F - t, 0.0F);
            // Two levels' colours over the ground, drawn in order.
            const float level_1 = 0.8F;
            const float level_0 = 0.2F;
            const float ground = 0.6F;
            const float after_first = first * level_1 + (1.0F - first) * ground;
            const float after_second = second * level_0 + (1.0F - second) * after_first;
            const float wanted =
                alpha * (t * level_1 + (1.0F - t) * level_0) + (1.0F - alpha) * ground;
            OA_CHECK(near(after_second, wanted));
            OA_CHECK(first >= 0.0F && first <= 1.0F && second >= 0.0F && second <= 1.0F);
        }
    }
}

/// What the greyed pass should cover: for each map pixel of the view's
/// margin, the alpha its fog tile's corners interpolate to, or none where
/// the tile is not drawn greyed or the pixel lies past the map.
struct GreyedField {
    int32_t left{}; ///< the first map pixel across the field holds
    int32_t top{};
    int32_t width{};
    int32_t height{};
    std::vector<float> alpha;   ///< the alpha at each map pixel's centre; -1 for none
    std::vector<uint8_t> drawn; ///< 1 where a pixel lies under a tile drawn greyed

    /// Returns the index of a map pixel.
    ///
    /// @param map_x the pixel's column
    /// @param map_z the pixel's row
    /// @return the index into alpha and drawn
    [[nodiscard]] std::size_t at(int32_t map_x, int32_t map_z) const {
        return static_cast<std::size_t>(map_z - top) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(map_x - left);
    }

    /// Tells whether a map pixel lies in the field.
    ///
    /// @param map_x the pixel's column
    /// @param map_z the pixel's row
    /// @return true when it does
    [[nodiscard]] bool holds(int32_t map_x, int32_t map_z) const {
        return map_x >= left && map_z >= top && map_x < left + width && map_z < top + height;
    }
};

/// Works out the greyed field of a grid: a pixel under a fog tile with a
/// corner out of sight, not wholly never mapped, within the map.
///
/// @param grid the fog grid
/// @param camera_x map pixel at the view's left edge
/// @param camera_z map pixel at the view's top edge
/// @return the field over the grid's tiles
GreyedField greyed_field(const wr::FogGrid& grid, int32_t camera_x, int32_t camera_z) {
    GreyedField field;
    field.left = camera_x + grid.offset_x;
    field.top = camera_z + grid.offset_z;
    field.width = grid.width * tile_edge;
    field.height = grid.height * tile_edge;
    const auto pixels =
        static_cast<std::size_t>(field.width) * static_cast<std::size_t>(field.height);
    field.alpha.assign(pixels, -1.0F);
    field.drawn.assign(pixels, 0);
    const int32_t map_width = static_cast<int32_t>(map_tiles_wide) * tile_edge;
    const int32_t map_height = static_cast<int32_t>(map_tiles_high) * tile_edge;
    for (int32_t row = 0; row < grid.height; ++row)
        for (int32_t column = 0; column < grid.width; ++column) {
            const auto& tile = grid.at(column, row);
            if (tile.unseen == 0 || tile.unmapped == wr::fog_mask_full)
                continue;
            const auto corners = fog::corner_alphas(tile.unseen);
            for (int32_t z = 0; z < tile_edge; ++z)
                for (int32_t x = 0; x < tile_edge; ++x) {
                    const int32_t map_x = field.left + column * tile_edge + x;
                    const int32_t map_z = field.top + row * tile_edge + z;
                    if (map_x < 0 || map_z < 0 || map_x >= map_width || map_z >= map_height)
                        continue;
                    const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(tile_edge);
                    const float v = (static_cast<float>(z) + 0.5F) / static_cast<float>(tile_edge);
                    const auto index = field.at(map_x, map_z);
                    field.drawn[index] = 1;
                    field.alpha[index] = corners[0] * (1.0F - u) * (1.0F - v) +
                                         corners[1] * u * (1.0F - v) + corners[2] * (1.0F - u) * v +
                                         corners[3] * u * v;
                }
        }
    return field;
}

/// Returns the alpha the field's fog tile interpolates to at a map point
/// on a tile's or a quarter's corner, which lies on a fog tile's corner,
/// edge or centre: the mean of the alphas of the pixels around the point
/// that one fog tile holds, which for the piecewise bilinear field is the
/// value at the point.
///
/// @param grid the fog grid
/// @param camera_x map pixel at the view's left edge
/// @param camera_z map pixel at the view's top edge
/// @param map_x the point's column, a multiple of half a tile
/// @param map_z the point's row
/// @param inside_x which side of the point the fog tile lies: 0 for the left
/// @param inside_z 0 for above
/// @return the alpha
float field_alpha_at(
    const wr::FogGrid& grid,
    int32_t camera_x,
    int32_t camera_z,
    int32_t map_x,
    int32_t map_z,
    int inside_x,
    int inside_z
) {
    const int32_t left = camera_x + grid.offset_x;
    const int32_t top = camera_z + grid.offset_z;
    // The fog tile holding the point on the side asked for.
    const int32_t column = (map_x - left - (inside_x == 0 ? 1 : 0)) / tile_edge;
    const int32_t row = (map_z - top - (inside_z == 0 ? 1 : 0)) / tile_edge;
    if (column < 0 || row < 0 || column >= grid.width || row >= grid.height)
        return 0.0F;
    const auto& tile = grid.at(column, row);
    const uint8_t mask = tile.unmapped == wr::fog_mask_full ? 0 : tile.unseen;
    const auto corners = fog::corner_alphas(mask);
    const float u =
        static_cast<float>(map_x - left - column * tile_edge) / static_cast<float>(tile_edge);
    const float v =
        static_cast<float>(map_z - top - row * tile_edge) / static_cast<float>(tile_edge);
    return corners[0] * (1.0F - u) * (1.0F - v) + corners[1] * u * (1.0F - v) +
           corners[2] * (1.0F - u) * v + corners[3] * u * v;
}

/// Checks the greyed pass of a grid at one level against the field: every
/// pixel drawn greyed covered once, no other pixel covered, each quad a
/// whole tile or a quarter at its tile's texels, its corner alphas the
/// field's, whole tiles exactly where their four fog tiles are wholly
/// greyed, and one batch for each run of quads on a page.
///
/// @param built the atlas
/// @param grid the fog grid
/// @param camera_x map pixel at the view's left edge
/// @param camera_z map pixel at the view's top edge
/// @param level the level drawn
/// @param sampling how it is sampled
/// @return the quads drawn
std::size_t check_greyed_pass(
    const Atlas& built,
    const wr::FogGrid& grid,
    int32_t camera_x,
    int32_t camera_z,
    uint8_t level,
    card::Sampling sampling
) {
    card::CardFrame frame;
    const fog::GreyedLevel levels[] = {{level, sampling, 1.0F}};
    const card::Rect scissor{3, 4, 100, 90};
    const card::TargetHandle target{7};
    const uint32_t appended = fog::append_unseen_terrain(
        frame, grid, built.atlas, built.pages, levels, plain(camera_x, camera_z), target, &scissor
    );
    const auto quads = quads_of(frame);
    OA_CHECK(quads.size() == appended);
    OA_CHECK(frame.vertices.size() == quads.size() * quad_vertices);
    OA_CHECK(frame.indices.size() == quads.size() * quad_indices);
    const GreyedField field = greyed_field(grid, camera_x, camera_z);
    std::vector<uint8_t> covered(field.drawn.size(), 0);
    const auto& atlas = built.atlas;
    std::size_t whole = 0;
    for (const auto& quad : quads) {
        const auto& batch = frame.batches[quad.batch];
        OA_CHECK(batch.target == target && batch.scissored && batch.scissor.x == scissor.x);
        OA_CHECK(batch.blend == card::Blend::alpha && batch.sampling == sampling);
        OA_CHECK(batch.level == level);
        // The quad's map rectangle, at scale 1 from the camera.
        const auto map_x = static_cast<int32_t>(std::lround(quad.corners[0].x)) + camera_x;
        const auto map_z = static_cast<int32_t>(std::lround(quad.corners[0].y)) + camera_z;
        const auto span = static_cast<int32_t>(std::lround(quad.corners[1].x - quad.corners[0].x));
        OA_CHECK(near(quad.corners[2].y - quad.corners[0].y, static_cast<float>(span)));
        OA_CHECK(span == tile_edge || span == tile_edge / 2);
        OA_CHECK(map_x % span == 0 && map_z % span == 0);
        if (span == tile_edge)
            ++whole;
        // Covered once.
        for (int32_t z = map_z; z < map_z + span; ++z)
            for (int32_t x = map_x; x < map_x + span; ++x) {
                OA_CHECK(field.holds(x, z));
                if (!field.holds(x, z))
                    continue;
                OA_CHECK(field.drawn[field.at(x, z)] == 1);
                OA_CHECK(covered[field.at(x, z)] == 0);
                covered[field.at(x, z)] = 1;
            }
        // The texels: the tile at the map position, whole or its quarter.
        const int32_t tile_column = map_x / tile_edge;
        const int32_t tile_row = map_z / tile_edge;
        const auto slot = atlas.grid
                              [static_cast<std::size_t>(tile_row) * atlas.grid_width +
                               static_cast<std::size_t>(tile_column)];
        const auto rect = gw::tile_rect(atlas, slot, level);
        OA_CHECK(rect.has_value());
        if (!rect)
            continue;
        OA_CHECK(batch.page == built.pages[rect->page]);
        const auto& page_level = atlas.pages[rect->page].levels[level];
        const float texel_x = quad.corners[0].u * static_cast<float>(page_level.width);
        const float texel_y = quad.corners[0].v * static_cast<float>(page_level.height);
        const float texel_right = quad.corners[1].u * static_cast<float>(page_level.width);
        const float texel_bottom = quad.corners[2].v * static_cast<float>(page_level.height);
        const uint32_t edge = span == tile_edge ? rect->edge : rect->edge / 2;
        const uint32_t quarter_x =
            span == tile_edge ? 0 : static_cast<uint32_t>(map_x % tile_edge) / (tile_edge / 2);
        const uint32_t quarter_z =
            span == tile_edge ? 0 : static_cast<uint32_t>(map_z % tile_edge) / (tile_edge / 2);
        OA_CHECK(near(texel_x, static_cast<float>(rect->x + quarter_x * edge)));
        OA_CHECK(near(texel_y, static_cast<float>(rect->y + quarter_z * edge)));
        OA_CHECK(near(texel_right, static_cast<float>(rect->x + quarter_x * edge + edge)));
        OA_CHECK(near(texel_bottom, static_cast<float>(rect->y + quarter_z * edge + edge)));
        // The corner alphas: the field's at each corner, taken from inside
        // the quad, where the fog tile holding the corner is the quad's.
        const std::array<std::pair<int32_t, int32_t>, 4> at{
            {{map_x, map_z},
             {map_x + span, map_z},
             {map_x + span, map_z + span},
             {map_x, map_z + span}}
        };
        const std::array<std::pair<int, int>, 4> inside{{{1, 1}, {0, 1}, {0, 0}, {1, 0}}};
        for (std::size_t corner = 0; corner < quad_vertices; ++corner) {
            const auto& vertex = quad.corners[corner];
            OA_CHECK(
                vertex.colour.red == 1.0F && vertex.colour.green == 1.0F &&
                vertex.colour.blue == 1.0F
            );
            float wanted = 0.0F;
            if (span == tile_edge) {
                // A whole tile's corners lie at fog tile centres, every
                // corner of those tiles out of sight.
                wanted = 1.0F;
            } else {
                wanted = field_alpha_at(
                    grid,
                    camera_x,
                    camera_z,
                    at[corner].first,
                    at[corner].second,
                    inside[corner].first,
                    inside[corner].second
                );
            }
            OA_CHECK(near(vertex.colour.alpha, wanted));
        }
    }
    // Every greyed pixel covered.
    OA_CHECK(covered == field.drawn);
    // Whole tiles exactly where the four fog tiles around a terrain tile
    // are wholly greyed.
    std::size_t wanted_whole = 0;
    const int32_t left = camera_x + grid.offset_x;
    const int32_t top = camera_z + grid.offset_z;
    for (int32_t tile_row = 0; tile_row < static_cast<int32_t>(map_tiles_high); ++tile_row)
        for (int32_t tile_column = 0; tile_column < static_cast<int32_t>(map_tiles_wide);
             ++tile_column) {
            // The fog tile at the terrain tile's top-left corner.
            const int32_t column = (tile_column * tile_edge - left - tile_edge / 2) / tile_edge;
            const int32_t row = (tile_row * tile_edge - top - tile_edge / 2) / tile_edge;
            if (tile_column * tile_edge - left - tile_edge / 2 < 0 ||
                tile_row * tile_edge - top - tile_edge / 2 < 0)
                continue;
            bool all = true;
            for (int32_t dz = 0; dz < 2 && all; ++dz)
                for (int32_t dx = 0; dx < 2 && all; ++dx) {
                    if (column + dx >= grid.width || row + dz >= grid.height) {
                        all = false;
                        break;
                    }
                    const auto& tile = grid.at(column + dx, row + dz);
                    all = tile.unseen == wr::fog_mask_full && tile.unmapped != wr::fog_mask_full;
                }
            if (all)
                ++wanted_whole;
        }
    OA_CHECK(whole == wanted_whole);
    // Batches: one for each run of quads on a page, each run's indices
    // following the one before.
    for (std::size_t batch = 1; batch < frame.batches.size(); ++batch) {
        OA_CHECK(frame.batches[batch].page != frame.batches[batch - 1].page);
        OA_CHECK(
            frame.batches[batch].first_index ==
            frame.batches[batch - 1].first_index + frame.batches[batch - 1].index_count
        );
    }
    return quads.size();
}

/// Sight with a band of cells out of sight, a corner never mapped and a
/// lone cell out of sight in the middle, at a seeded scatter of others.
///
/// @return the sight
Sight mixed_sight() {
    Sight sight;
    std::mt19937 random(test_seed + 1);
    for (int32_t z = 0; z < sight.grid.height; ++z)
        for (int32_t x = 0; x < sight.grid.width; ++x) {
            if (x <= 1)
                sight.unsee(x, z);
            if (x >= 7 && z >= 5)
                sight.unmap(x, z);
            if (random() % 5 == 0)
                sight.unsee(x, z);
        }
    sight.unsee(4, 3);
    return sight;
}

/// Nothing is drawn where everything is seen; everything in view where
/// nothing is, as whole tiles but at the map's edges.
void greyed_pass_covers_what_is_out_of_sight() {
    const Atlas built = build_atlas();
    // Everything seen: nothing.
    {
        const Sight seen;
        OA_CHECK(check_greyed_pass(built, seen.fog(0, 0), 0, 0, 0, card::Sampling::nearest) == 0);
    }
    // Nothing seen, the camera on a tile: whole tiles.
    {
        Sight sight;
        for (int32_t z = 0; z < sight.grid.height; ++z)
            for (int32_t x = 0; x < sight.grid.width; ++x)
                sight.unsee(x, z);
        const auto quads =
            check_greyed_pass(built, sight.fog(0, 0), 0, 0, 0, card::Sampling::nearest);
        OA_CHECK(quads > 0);
    }
    // A mixed pattern at an aligned camera, at one between tiles, and at
    // level 1.
    {
        const Sight sight = mixed_sight();
        OA_CHECK(
            check_greyed_pass(built, sight.fog(32, 64), 32, 64, 0, card::Sampling::nearest) > 0
        );
        OA_CHECK(check_greyed_pass(built, sight.fog(37, 9), 37, 9, 0, card::Sampling::linear) > 0);
        OA_CHECK(check_greyed_pass(built, sight.fog(37, 9), 37, 9, 1, card::Sampling::linear) > 0);
        OA_CHECK(check_greyed_pass(built, sight.fog(0, 0), 0, 0, 2, card::Sampling::nearest) > 0);
    }
}

/// Two levels draw the same quads twice, the first at pass_alpha's alphas
/// for its share and the second at the rest.
void two_levels_draw_the_quads_twice() {
    const Atlas built = build_atlas();
    const Sight sight = mixed_sight();
    const auto grid = sight.fog(37, 9);
    const float t = 0.3F;
    card::CardFrame one;
    const fog::GreyedLevel single[] = {{0, card::Sampling::linear, 1.0F}};
    const uint32_t alone = fog::append_unseen_terrain(
        one, grid, built.atlas, built.pages, single, plain(37, 9), {}, nullptr
    );
    card::CardFrame two;
    const fog::GreyedLevel pair[] = {
        {1, card::Sampling::linear, t}, {0, card::Sampling::linear, 1.0F - t}
    };
    const uint32_t both = fog::append_unseen_terrain(
        two, grid, built.atlas, built.pages, pair, plain(37, 9), {}, nullptr
    );
    OA_CHECK(both == 2 * alone);
    const auto first = quads_of(one);
    const auto quads = quads_of(two);
    OA_CHECK(quads.size() == 2 * first.size());
    if (quads.size() != 2 * first.size())
        return;
    for (std::size_t index = 0; index < first.size(); ++index) {
        const auto& plain_quad = first[index];
        const auto& far = quads[index];
        const auto& near_quad = quads[index + first.size()];
        OA_CHECK(two.batches[far.batch].level == 1);
        OA_CHECK(two.batches[near_quad.batch].level == 0);
        for (std::size_t corner = 0; corner < quad_vertices; ++corner) {
            const float alpha = plain_quad.corners[corner].colour.alpha;
            OA_CHECK(near(far.corners[corner].x, plain_quad.corners[corner].x));
            OA_CHECK(near(near_quad.corners[corner].y, plain_quad.corners[corner].y));
            OA_CHECK(near(far.corners[corner].colour.alpha, fog::pass_alpha(alpha, t, 1.0F - t)));
            OA_CHECK(near(near_quad.corners[corner].colour.alpha, alpha * (1.0F - t)));
        }
    }
    // The first level's batches all come before the second's.
    bool ordered = true;
    bool seen_near = false;
    for (const auto& batch : two.batches) {
        if (batch.level == 0)
            seen_near = true;
        else if (seen_near)
            ordered = false;
    }
    OA_CHECK(ordered);
}

/// A placement moves and scales every corner: origin + (map - camera) * scale.
void placement_moves_and_scales_the_quads() {
    const Atlas built = build_atlas();
    const Sight sight = mixed_sight();
    const auto grid = sight.fog(37, 9);
    fog::FogPlacement placement;
    placement.camera_x = 37;
    placement.camera_z = 9;
    placement.origin_x = 10.5F;
    placement.origin_y = 20.25F;
    placement.scale = 0.75F;
    card::CardFrame placed;
    const fog::GreyedLevel single[] = {{0, card::Sampling::linear, 1.0F}};
    fog::append_unseen_terrain(
        placed, grid, built.atlas, built.pages, single, placement, {}, nullptr
    );
    card::CardFrame unplaced;
    fog::append_unseen_terrain(
        unplaced, grid, built.atlas, built.pages, single, plain(37, 9), {}, nullptr
    );
    OA_CHECK(placed.vertices.size() == unplaced.vertices.size());
    for (std::size_t index = 0; index < std::min(placed.vertices.size(), unplaced.vertices.size());
         ++index) {
        const auto& at = placed.vertices[index];
        const auto& from = unplaced.vertices[index];
        OA_CHECK(near(at.x, placement.origin_x + from.x * placement.scale));
        OA_CHECK(near(at.y, placement.origin_y + from.y * placement.scale));
        OA_CHECK(at.u == from.u && at.v == from.v && at.colour.alpha == from.colour.alpha);
    }
    // The black pass takes the same placement, over a view that reaches the
    // corner of the map never mapped.
    placement.camera_x = 150;
    placement.camera_z = 100;
    const auto corner_grid = sight.fog(150, 100);
    card::CardFrame black;
    fog::append_unmapped(black, corner_grid, {0.0F, 0.0F, 0.0F, 1.0F}, placement, {}, nullptr);
    card::CardFrame black_plain;
    fog::append_unmapped(
        black_plain, corner_grid, {0.0F, 0.0F, 0.0F, 1.0F}, plain(150, 100), {}, nullptr
    );
    OA_CHECK(black.vertices.size() == black_plain.vertices.size() && !black.vertices.empty());
    for (std::size_t index = 0;
         index < std::min(black.vertices.size(), black_plain.vertices.size());
         ++index) {
        OA_CHECK(near(
            black.vertices[index].x,
            placement.origin_x + black_plain.vertices[index].x * placement.scale
        ));
        OA_CHECK(near(
            black.vertices[index].y,
            placement.origin_y + black_plain.vertices[index].y * placement.scale
        ));
    }
}

/// With whole pixels at a sixth, where a fog tile spans 2 2/3 pixels, every
/// corner of the black and the greyed passes lies on a whole pixel, and
/// each row of tiles begins where the row above it ends, so that no row of
/// pixels lies between them.
void whole_pixels_meet_at_a_sixth() {
    const Atlas built = build_atlas();
    const Sight sight = mixed_sight();
    fog::FogPlacement placement;
    placement.camera_x = 150;
    placement.camera_z = 100;
    placement.origin_x = 10.25F;
    placement.origin_y = 20.5F;
    placement.scale = 1.0F / 6.0F;
    placement.whole_pixels = true;
    const auto grid = sight.fog(150, 100);
    card::CardFrame black;
    fog::append_unmapped(black, grid, {0.0F, 0.0F, 0.0F, 1.0F}, placement, {}, nullptr);
    card::CardFrame greyed;
    const fog::GreyedLevel single[] = {{0, card::Sampling::linear, 1.0F}};
    fog::append_unseen_terrain(
        greyed, grid, built.atlas, built.pages, single, placement, {}, nullptr
    );
    OA_CHECK(!black.vertices.empty() && !greyed.vertices.empty());
    for (const card::CardFrame* frame : {&black, &greyed}) {
        std::vector<float> tops;
        std::vector<float> bottoms;
        for (const auto& vertex : frame->vertices)
            OA_CHECK(vertex.x == std::round(vertex.x) && vertex.y == std::round(vertex.y));
        // Each quad's four corners, its top-left first and its bottom-left
        // last.
        for (std::size_t first = 0; first + 3 < frame->vertices.size(); first += 4) {
            tops.push_back(frame->vertices[first].y);
            bottoms.push_back(frame->vertices[first + 3].y);
        }
        const float highest = *std::min_element(tops.begin(), tops.end());
        for (const float top : tops)
            OA_CHECK(
                top == highest || std::find(bottoms.begin(), bottoms.end(), top) != bottoms.end()
            );
    }
}

/// Checks a solid pass over a layer of the grid: a quad for each tile with
/// a corner marked, tiles wholly marked that follow one another merged,
/// the colour kept, the alphas the corners' times the factor.
///
/// @param frame the frame the pass was appended to
/// @param grid the fog grid
/// @param camera_x map pixel at the view's left edge
/// @param camera_z map pixel at the view's top edge
/// @param mask_of returns the layer's mask of a tile
/// @param colour the pass's colour
/// @param factor what the corner alphas were scaled by
void check_solid_pass(
    const card::CardFrame& frame,
    const wr::FogGrid& grid,
    int32_t camera_x,
    int32_t camera_z,
    uint8_t (*mask_of)(const wr::FogTile&),
    const card::Colour& colour,
    float factor
) {
    const auto quads = quads_of(frame);

    // The runs and the single tiles the grid asks for, in row order.
    struct Wanted {
        int32_t map_x{};
        int32_t map_z{};
        int32_t width{};
        std::array<float, 4> alphas{};
    };

    std::vector<Wanted> wanted;
    const int32_t left = camera_x + grid.offset_x;
    const int32_t top = camera_z + grid.offset_z;
    for (int32_t row = 0; row < grid.height; ++row)
        for (int32_t column = 0; column < grid.width;) {
            const uint8_t mask = mask_of(grid.at(column, row));
            if (mask == 0) {
                ++column;
                continue;
            }
            Wanted one;
            one.map_x = left + column * tile_edge;
            one.map_z = top + row * tile_edge;
            if (mask == wr::fog_mask_full) {
                int32_t end = column + 1;
                while (end < grid.width && mask_of(grid.at(end, row)) == wr::fog_mask_full)
                    ++end;
                one.width = (end - column) * tile_edge;
                one.alphas = {1.0F, 1.0F, 1.0F, 1.0F};
                column = end;
            } else {
                one.width = tile_edge;
                one.alphas = fog::corner_alphas(mask);
                ++column;
            }
            wanted.push_back(one);
        }
    OA_CHECK(quads.size() == wanted.size());
    if (quads.size() != wanted.size())
        return;
    for (std::size_t index = 0; index < quads.size(); ++index) {
        const auto& quad = quads[index];
        const auto& one = wanted[index];
        OA_CHECK(near(quad.corners[0].x, static_cast<float>(one.map_x - camera_x)));
        OA_CHECK(near(quad.corners[0].y, static_cast<float>(one.map_z - camera_z)));
        OA_CHECK(near(quad.corners[1].x - quad.corners[0].x, static_cast<float>(one.width)));
        OA_CHECK(near(quad.corners[2].y - quad.corners[0].y, static_cast<float>(tile_edge)));
        // Corners in the frame's order: top-left, top-right, bottom-right, bottom-left.
        const std::array<std::size_t, 4> order{0, 1, 3, 2};
        for (std::size_t corner = 0; corner < quad_vertices; ++corner) {
            const auto& vertex = quad.corners[corner];
            OA_CHECK(
                vertex.colour.red == colour.red && vertex.colour.green == colour.green &&
                vertex.colour.blue == colour.blue
            );
            OA_CHECK(near(vertex.colour.alpha, one.alphas[order[corner]] * factor));
        }
        const auto& batch = frame.batches[quad.batch];
        OA_CHECK(
            batch.page == card::PageHandle{} && batch.blend == card::Blend::alpha &&
            batch.level == 0
        );
    }
    // One batch holds the pass.
    OA_CHECK(frame.batches.size() == (quads.empty() ? 0U : 1U));
}

/// The black pass over the cells never mapped, and the dithered pass over
/// those out of sight at half alpha, leaving the tiles wholly never mapped
/// to the black pass.
void solid_passes_merge_their_runs() {
    const Sight sight = mixed_sight();
    for (const auto [camera_x, camera_z] :
         {std::pair{0, 0}, std::pair{37, 9}, std::pair{150, 100}}) {
        const auto grid = sight.fog(camera_x, camera_z);
        const card::Colour black{0.0F, 0.0F, 0.0F, 0.5F};
        card::CardFrame frame;
        const uint32_t appended =
            fog::append_unmapped(frame, grid, black, plain(camera_x, camera_z), {}, nullptr);
        OA_CHECK(appended == quads_of(frame).size());
        check_solid_pass(
            frame,
            grid,
            camera_x,
            camera_z,
            [](const wr::FogTile& tile) { return tile.unmapped; },
            black,
            1.0F
        );
        const card::Colour dither{0.1F, 0.2F, 0.3F, 1.0F};
        card::CardFrame dithered;
        fog::append_unseen_dither(dithered, grid, dither, plain(camera_x, camera_z), {}, nullptr);
        check_solid_pass(
            dithered,
            grid,
            camera_x,
            camera_z,
            [](const wr::FogTile& tile) {
                return tile.unmapped == wr::fog_mask_full ? uint8_t{0} : tile.unseen;
            },
            dither,
            0.5F
        );
    }
    // A corner of the map never mapped shows up as a merged run of black.
    const auto grid = sight.fog(150, 100);
    card::CardFrame frame;
    fog::append_unmapped(frame, grid, {}, plain(150, 100), {}, nullptr);
    bool merged = false;
    for (const auto& quad : quads_of(frame))
        merged = merged || quad.corners[1].x - quad.corners[0].x > static_cast<float>(tile_edge);
    OA_CHECK(merged);
}

/// A shade level is black by alpha at one less the row's scale, a light
/// level white added at one less the scale's reciprocal, each clamped.
void level_quads_follow_the_tables() {
    // The kill board's shade, row 8: scale 0.55.
    const auto board = fog::level_quad(-0x18);
    OA_CHECK(board.blend == card::Blend::alpha);
    OA_CHECK(board.colour.red == 0.0F && board.colour.green == 0.0F && board.colour.blue == 0.0F);
    OA_CHECK(near(board.colour.alpha, 1.0F - 8.0F * fog::shade_row_step));
    // Row 0 is black; row 15 and above change nothing; below row 0 clamps.
    OA_CHECK(near(fog::level_quad(-0x20).colour.alpha, 1.0F));
    OA_CHECK(near(fog::level_quad(-0x40).colour.alpha, 1.0F));
    OA_CHECK(near(fog::level_quad(-0x20 + 15).colour.alpha, 0.0F));
    OA_CHECK(near(fog::level_quad(-1).colour.alpha, 0.0F));
    // Light levels: the kill board's highlight rows, and the clamp.
    const auto highlight = fog::level_quad(0x1f);
    OA_CHECK(highlight.blend == card::Blend::additive);
    OA_CHECK(
        highlight.colour.red == 1.0F && highlight.colour.green == 1.0F &&
        highlight.colour.blue == 1.0F
    );
    OA_CHECK(near(highlight.colour.alpha, 1.0F - 1.0F / (1.0F + 31.0F * fog::light_row_step)));
    OA_CHECK(
        near(fog::level_quad(0x14).colour.alpha, 1.0F - 1.0F / (1.0F + 20.0F * fog::light_row_step))
    );
    OA_CHECK(near(fog::level_quad(0).colour.alpha, 0.0F));
    OA_CHECK(near(fog::level_quad(0x7f).colour.alpha, fog::level_quad(0x1f).colour.alpha));
}

/// An empty grid, a level past the tile levels, too few pages and no level
/// add nothing.
void refusals_add_nothing() {
    const Atlas built = build_atlas();
    const Sight sight = mixed_sight();
    const auto grid = sight.fog(0, 0);
    card::CardFrame frame;
    const fog::GreyedLevel single[] = {{0, card::Sampling::nearest, 1.0F}};
    const fog::GreyedLevel deep[] = {{gw::tile_level_count, card::Sampling::nearest, 1.0F}};
    const fog::GreyedLevel three[] = {
        {0, card::Sampling::nearest, 0.4F},
        {1, card::Sampling::nearest, 0.3F},
        {2, card::Sampling::nearest, 0.3F},
    };
    OA_CHECK(
        fog::append_unseen_terrain(
            frame, {}, built.atlas, built.pages, single, plain(0, 0), {}, nullptr
        ) == 0
    );
    OA_CHECK(
        fog::append_unseen_terrain(
            frame, grid, built.atlas, built.pages, deep, plain(0, 0), {}, nullptr
        ) == 0
    );
    OA_CHECK(
        fog::append_unseen_terrain(
            frame, grid, built.atlas, built.pages, three, plain(0, 0), {}, nullptr
        ) == 0
    );
    OA_CHECK(
        fog::append_unseen_terrain(
            frame, grid, built.atlas, built.pages, {}, plain(0, 0), {}, nullptr
        ) == 0
    );
    OA_CHECK(
        fog::append_unseen_terrain(
            frame, grid, built.atlas, {}, single, plain(0, 0), {}, nullptr
        ) == 0
    );
    OA_CHECK(fog::append_unmapped(frame, {}, {}, plain(0, 0), {}, nullptr) == 0);
    OA_CHECK(fog::append_unseen_dither(frame, {}, {}, plain(0, 0), {}, nullptr) == 0);
    OA_CHECK(frame.vertices.empty() && frame.indices.empty() && frame.batches.empty());
}

} // namespace

int main() {
    corner_alphas_follow_the_mask();
    pass_alphas_compose_the_weighted_picture();
    greyed_pass_covers_what_is_out_of_sight();
    two_levels_draw_the_quads_twice();
    placement_moves_and_scales_the_quads();
    whole_pixels_meet_at_a_sixth();
    solid_passes_merge_their_runs();
    level_quads_follow_the_tables();
    refusals_add_nothing();
    if (oa::test::check_exit_status() != 0)
        return 1;
    std::puts(
        "full fog: the greyed pass covers the ground out of sight once at its tiles' texels, "
        "the black and dithered passes their tiles with runs merged, at the corner alphas; "
        "shade and light levels as quads"
    );
    return 0;
}
