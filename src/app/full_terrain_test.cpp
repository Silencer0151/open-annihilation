// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Full tier's terrain builder: the level rule at every kind of zoom, and
// the tiles' quads over a small map packed on three atlas pages: where each
// lands, which texels it reads, one batch per page whatever the grid's
// order, the view's tile range within the map and the frame well formed.
#include "full_terrain.hpp"

#include "oa/test/check.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

namespace card = oa::app::card;
namespace gw = oa::present::gpu_world;
namespace ft = oa::app::full_terrain;

using Map = oa::formats::tnt::Map;

constexpr std::size_t tile_bytes = oa::formats::tnt::layout::tile_bytes;

/// A palette whose entries all differ.
oa::PaletteBytes test_palette() {
    oa::PaletteBytes palette{};
    for (std::size_t index = 0; index < oa::palette_color_count; ++index) {
        palette[index * oa::palette_entry_bytes] = static_cast<uint8_t>(index);
        palette[index * oa::palette_entry_bytes + 1] = static_cast<uint8_t>(index * 5U + 1U);
        palette[index * oa::palette_entry_bytes + 2] = static_cast<uint8_t>(255U - index);
    }
    return palette;
}

/// A map of 3 by 2 tiles naming 3 distinct tiles: the grid 0 1 2 / 2 1 0.
Map test_map() {
    Map map;
    map.tile_width = 3;
    map.tile_height = 2;
    map.tile_count = 3;
    map.tile_palette_indices.resize(static_cast<std::size_t>(map.tile_count) * tile_bytes);
    for (std::size_t i = 0; i < map.tile_palette_indices.size(); ++i)
        map.tile_palette_indices[i] = static_cast<uint8_t>((i * 37U + i / tile_bytes) & 0xffU);
    map.tile_indices = {0, 1, 2, 2, 1, 0};
    return map;
}

bool near(float a, float b) {
    return std::fabs(a - b) < 1.0e-5F;
}

/// The level rule: level 1 alone at 0.5, the blend between, level 2 under
/// level 1 below 0.5 and alone at 0.25 and below, level 0 NEAREST at whole
/// numbers, pixel-art or the target at other zooms above 1.
void test_plan() {
    using card::Blend;
    using card::Sampling;
    const auto half = ft::plan_terrain_draw(0.5F, false);
    OA_CHECK(half.pass_count == 1 && half.passes[0].level == 1);
    OA_CHECK(half.passes[0].sampling == Sampling::linear && half.passes[0].blend == Blend::none);
    OA_CHECK(!half.through_target);
    // Between 0.5 and 0.25 level 2 is drawn first and level 1 blended over
    // it, at the alpha of the same rule one level down.
    const auto below = ft::plan_terrain_draw(0.3F, false);
    OA_CHECK(below.pass_count == 2 && below.passes[0].level == 2 && below.passes[1].level == 1);
    OA_CHECK(below.passes[0].sampling == Sampling::linear && below.passes[1].blend == Blend::alpha);
    OA_CHECK(near(
        below.passes[1].alpha, static_cast<float>(2.0 - std::log2(1.0 / static_cast<double>(0.3F)))
    ));
    // At 0.25 and at the Full tier's floor of one sixth
    // (kMinFullBattlefieldZoom) level 2 alone, reduced.
    for (const float zoom : {0.25F, 1.0F / 6.0F, 0.1F}) {
        const auto far = ft::plan_terrain_draw(zoom, false);
        OA_CHECK(far.pass_count == 1 && far.passes[0].level == 2);
        OA_CHECK(far.passes[0].sampling == Sampling::linear && far.passes[0].blend == Blend::none);
        OA_CHECK(!far.through_target);
    }
    for (const float zoom : {0.6F, 0.75F, 0.9F, 0.999F}) {
        const auto plan = ft::plan_terrain_draw(zoom, false);
        OA_CHECK(plan.pass_count == 2);
        OA_CHECK(plan.passes[0].level == 1 && plan.passes[0].sampling == Sampling::linear);
        OA_CHECK(plan.passes[1].level == 0 && plan.passes[1].sampling == Sampling::linear);
        OA_CHECK(plan.passes[1].blend == Blend::alpha);
        OA_CHECK(near(
            plan.passes[1].alpha,
            static_cast<float>(1.0 - std::log2(1.0 / static_cast<double>(zoom)))
        ));
        OA_CHECK(!plan.through_target);
    }
    OA_CHECK(near(ft::plan_terrain_draw(0.75F, false).passes[1].alpha, 0.584963F));
    for (const float zoom : {1.0F, 2.0F, 3.0F, 4.0F})
        for (const bool pixel_art : {false, true}) {
            const auto plan = ft::plan_terrain_draw(zoom, pixel_art);
            OA_CHECK(plan.pass_count == 1 && plan.passes[0].level == 0);
            OA_CHECK(plan.passes[0].sampling == Sampling::nearest);
            OA_CHECK(plan.passes[0].blend == Blend::none && plan.passes[0].alpha == 1.0F);
            OA_CHECK(!plan.through_target);
        }
    for (const float zoom : {1.37F, 2.5F, 3.9F}) {
        const auto art = ft::plan_terrain_draw(zoom, true);
        OA_CHECK(art.pass_count == 1 && art.passes[0].sampling == Sampling::pixel_art);
        OA_CHECK(!art.through_target);
        const auto target = ft::plan_terrain_draw(zoom, false);
        OA_CHECK(target.pass_count == 1 && target.passes[0].sampling == Sampling::nearest);
        OA_CHECK(target.through_target);
        OA_CHECK(target.target_zoom == static_cast<uint32_t>(std::ceil(zoom)));
    }
    OA_CHECK(ft::plan_terrain_draw(0.0F, false).pass_count == 1);
}

/// The quads of the tiles a view shows, from an atlas of two pages.
void test_tiles() {
    const Map map = test_map();
    const auto palette = test_palette();
    gw::TerrainAtlas atlas;
    // Within the smallest page edge each page holds one slot, so three
    // distinct tiles take three pages.
    OA_CHECK(
        gw::build_terrain_atlas(map, palette, nullptr, gw::page_edge_minimum, atlas) ==
        gw::TerrainAtlasError::none
    );
    OA_CHECK(atlas.pages.size() == 3 && atlas.grid_width == 3 && atlas.grid_height == 2);
    const std::vector<card::PageHandle> pages{{1}, {2}, {3}};

    // Zoom 1 at an origin, the camera inside the first tile: every tile the
    // 70 by 40 view touches, three across and two down, at whole pixels.
    ft::TerrainView view;
    view.camera_x = 5;
    view.camera_y = 7;
    view.origin_x = 10.0F;
    view.origin_y = 20.0F;
    view.scale = 1.0F;
    view.width = 70;
    view.height = 40;
    const ft::TileRange range = ft::visible_tiles(atlas, view);
    OA_CHECK(range.first_column == 0 && range.end_column == 3);
    OA_CHECK(range.first_row == 0 && range.end_row == 2);
    card::CardFrame frame;
    const card::Rect scissor{10, 20, 70, 40};
    const ft::TerrainPass level_0{0, card::Sampling::nearest, card::Blend::none, 1.0F};
    const uint32_t quads = ft::append_terrain_tiles(
        frame, atlas, pages, view, level_0, card::TargetHandle{}, &scissor
    );
    OA_CHECK(quads == 6);
    OA_CHECK(frame.vertices.size() == 24 && frame.indices.size() == 36);
    // Each tile is on its own page, and the grid names the pages 1 2 3 /
    // 3 2 1, so the six tiles make three batches, one per page in page
    // order, each of two tiles: the pass never draws a page twice however
    // the grid alternates.
    OA_CHECK(frame.batches.size() == 3);
    const uint32_t expected_pages[] = {1, 2, 3};
    constexpr uint32_t indices_per_page = 12;
    uint32_t first_index = 0;
    for (std::size_t i = 0; i < frame.batches.size(); ++i) {
        const auto& batch = frame.batches[i];
        OA_CHECK(batch.operation == card::Operation::draw);
        OA_CHECK(batch.page.value == expected_pages[i]);
        OA_CHECK(batch.level == 0 && batch.blend == card::Blend::none);
        OA_CHECK(batch.sampling == card::Sampling::nearest);
        OA_CHECK(batch.scissored && batch.scissor.x == 10 && batch.scissor.width == 70);
        OA_CHECK(batch.first_index == first_index && batch.index_count == indices_per_page);
        OA_CHECK(batch.target == card::TargetHandle{});
        first_index += indices_per_page;
        // Every index of the batch names a corner of a tile whose slot is
        // on the batch's page: each quad's four vertices follow one
        // another, its first the top-left corner, whose place gives the
        // tile's column and row, and the grid its slot.
        for (uint32_t index = batch.first_index; index < batch.first_index + batch.index_count;
             ++index) {
            OA_CHECK(frame.indices[index] < frame.vertices.size());
            const auto& corner = frame.vertices[(frame.indices[index] / 4U) * 4U];
            const auto column = static_cast<uint32_t>(
                (corner.x - view.origin_x + static_cast<float>(view.camera_x)) / gw::tile_edge
            );
            const auto row = static_cast<uint32_t>(
                (corner.y - view.origin_y + static_cast<float>(view.camera_y)) / gw::tile_edge
            );
            OA_CHECK(column < 3 && row < 2);
            const auto corner_rect = gw::tile_rect(atlas, atlas.grid[row * 3 + column], 0);
            OA_CHECK(corner_rect.has_value() && corner_rect->page + 1 == batch.page.value);
        }
        // The quad's two triangles share the diagonal from its top-left to
        // its bottom-right corner, as card::append_quad lays them.
        const card::Index first = frame.indices[batch.first_index];
        OA_CHECK(frame.indices[batch.first_index + 1] == first + 1);
        OA_CHECK(frame.indices[batch.first_index + 2] == first + 2);
        OA_CHECK(frame.indices[batch.first_index + 3] == first);
        OA_CHECK(frame.indices[batch.first_index + 4] == first + 2);
        OA_CHECK(frame.indices[batch.first_index + 5] == first + 3);
    }
    // The second tile of the first row lands 32 pixels right of the first,
    // which lands at origin - camera, and reads its tile's texels without
    // the gutter.
    const auto& first = frame.vertices[0];
    OA_CHECK(near(first.x, 5.0F) && near(first.y, 13.0F));
    const auto& second = frame.vertices[4];
    OA_CHECK(near(second.x, 37.0F) && near(second.y, 13.0F));
    const auto& second_far = frame.vertices[4 + 2];
    OA_CHECK(near(second_far.x, 69.0F) && near(second_far.y, 45.0F));
    const auto rect = gw::tile_rect(atlas, atlas.grid[1], 0);
    OA_CHECK(rect.has_value());
    const auto& level = atlas.pages[rect->page].levels[0];
    OA_CHECK(near(second.u * static_cast<float>(level.width), static_cast<float>(rect->x)));
    OA_CHECK(near(second.v * static_cast<float>(level.height), static_cast<float>(rect->y)));
    OA_CHECK(near(
        second_far.u * static_cast<float>(level.width), static_cast<float>(rect->x + rect->edge)
    ));
    OA_CHECK(rect->edge == gw::tile_edge);
    OA_CHECK(first.colour.alpha == 1.0F && first.colour.red == 1.0F);
    OA_CHECK(card::check_frame(frame).empty());

    // Level 1 at zoom 0.5 over the same map pixels: the quads are half the
    // size at half the offsets, and read level 1's texels, 16 a side.
    ft::TerrainView half = view;
    half.scale = 0.5F;
    half.width = 35;
    half.height = 20;
    half.camera_x = 4;
    half.camera_y = 6;
    card::CardFrame zoomed;
    const ft::TerrainPass level_1{1, card::Sampling::linear, card::Blend::none, 1.0F};
    OA_CHECK(
        ft::append_terrain_tiles(
            zoomed, atlas, pages, half, level_1, card::TargetHandle{}, nullptr
        ) == 6
    );
    OA_CHECK(near(zoomed.vertices[0].x, 8.0F) && near(zoomed.vertices[0].y, 17.0F));
    OA_CHECK(near(zoomed.vertices[2].x, 24.0F) && near(zoomed.vertices[2].y, 33.0F));
    const auto rect_1 = gw::tile_rect(atlas, atlas.grid[0], 1);
    OA_CHECK(rect_1.has_value() && rect_1->edge == gw::tile_edge / 2);
    const auto& level_1_size = atlas.pages[rect_1->page].levels[1];
    OA_CHECK(near(
        zoomed.vertices[0].u * static_cast<float>(level_1_size.width), static_cast<float>(rect_1->x)
    ));
    OA_CHECK(zoomed.batches.front().level == 1 && !zoomed.batches.front().scissored);
    OA_CHECK(zoomed.batches.front().sampling == card::Sampling::linear);
    OA_CHECK(card::check_frame(zoomed).empty());

    // The blend's level-0 pass carries its alpha in every corner.
    card::CardFrame blended;
    const ft::TerrainPass near_pass{0, card::Sampling::linear, card::Blend::alpha, 0.25F};
    OA_CHECK(
        ft::append_terrain_tiles(
            blended, atlas, pages, half, near_pass, card::TargetHandle{7}, nullptr
        ) == 6
    );
    for (const auto& vertex : blended.vertices)
        OA_CHECK(vertex.colour.alpha == 0.25F);
    OA_CHECK(blended.batches.front().blend == card::Blend::alpha);
    OA_CHECK(blended.batches.front().target.value == 7);

    // A view that starts in the last tile shows that tile alone, and one
    // past the map shows nothing; a view narrower than a tile shows the
    // tile under it and the one its far edge touches.
    ft::TerrainView corner = view;
    corner.camera_x = 70;
    corner.camera_y = 40;
    const auto last = ft::visible_tiles(atlas, corner);
    OA_CHECK(last.first_column == 2 && last.end_column == 3);
    OA_CHECK(last.first_row == 1 && last.end_row == 2);
    ft::TerrainView beyond = view;
    beyond.camera_x = 96;
    const auto none = ft::visible_tiles(atlas, beyond);
    OA_CHECK(none.first_column == none.end_column);
    card::CardFrame empty;
    OA_CHECK(
        ft::append_terrain_tiles(
            empty, atlas, pages, beyond, level_0, card::TargetHandle{}, nullptr
        ) == 0
    );
    OA_CHECK(empty.batches.empty());
    ft::TerrainView narrow = view;
    narrow.camera_x = 30;
    narrow.width = 4;
    narrow.height = 4;
    const auto two = ft::visible_tiles(atlas, narrow);
    OA_CHECK(two.first_column == 0 && two.end_column == 2 && two.end_row == 1);

    // A level past the tile levels, or too few pages, appends nothing.
    card::CardFrame refused;
    const ft::TerrainPass deep{3, card::Sampling::nearest, card::Blend::none, 1.0F};
    OA_CHECK(
        ft::append_terrain_tiles(
            refused, atlas, pages, view, deep, card::TargetHandle{}, nullptr
        ) == 0
    );
    const std::vector<card::PageHandle> short_pages{{1}};
    OA_CHECK(
        ft::append_terrain_tiles(
            refused, atlas, short_pages, view, level_0, card::TargetHandle{}, nullptr
        ) == 0
    );
}

} // namespace

int main() {
    test_plan();
    test_tiles();
    return oa::test::check_exit_status();
}
