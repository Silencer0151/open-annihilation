// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "full_terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <tuple>
#include <utility>
#include <vector>

namespace oa::app::full_terrain {

namespace gw = oa::present::gpu_world;

namespace {

/// Indices one tile's quad takes: two triangles.
constexpr uint32_t quad_indices = 6;

/// The levels of the zoomed-out blend at a zoom below 1.
struct ZoomedOutLevels {
    uint8_t far{};      ///< the level drawn first, alone or under the nearer level
    float near_alpha{}; ///< the alpha the level above it is blended over at; 0 for none
};

/// Returns the levels of the zoomed-out blend: with t = log2(1 / zoom), the
/// two levels that bracket t, the nearer blended over the farther at alpha
/// 1 - (t - near); at a whole t the far level alone; at and beyond the last
/// tile level that level alone, reduced.
///
/// @param zoom window pixels per map pixel, below 1
/// @return the levels
ZoomedOutLevels zoomed_out_levels(float zoom) noexcept {
    const double t = std::log2(1.0 / static_cast<double>(zoom));
    constexpr double last_level = gw::tile_level_count - 1;
    const double far = std::min(std::ceil(t), last_level);
    const double near_alpha = t < far ? std::clamp(1.0 - (t - (far - 1.0)), 0.0, 1.0) : 0.0;
    return {static_cast<uint8_t>(far), static_cast<float>(near_alpha)};
}

} // namespace

TerrainDrawPlan plan_terrain_draw(float zoom, bool pixel_art_sampling) noexcept {
    TerrainDrawPlan plan;
    if (!(zoom > 0.0F))
        zoom = 1.0F;
    if (zoom < 1.0F) {
        const ZoomedOutLevels levels = zoomed_out_levels(zoom);
        plan.passes[0] = {levels.far, card::Sampling::linear, card::Blend::none, 1.0F};
        plan.pass_count = 1;
        if (levels.near_alpha > 0.0F) {
            plan.passes[1] = {
                static_cast<uint8_t>(levels.far - 1U),
                card::Sampling::linear,
                card::Blend::alpha,
                levels.near_alpha
            };
            plan.pass_count = 2;
        }
        return plan;
    }
    const bool whole = std::floor(zoom) == zoom;
    plan.pass_count = 1;
    if (whole || !pixel_art_sampling) {
        plan.passes[0] = {0, card::Sampling::nearest, card::Blend::none, 1.0F};
        plan.through_target = !whole;
        plan.target_zoom = static_cast<uint32_t>(std::ceil(zoom));
        return plan;
    }
    plan.passes[0] = {0, card::Sampling::pixel_art, card::Blend::none, 1.0F};
    return plan;
}

TileRange visible_tiles(const gw::TerrainAtlas& atlas, const TerrainView& view) noexcept {
    TileRange range;
    if (atlas.grid_width == 0 || atlas.grid_height == 0 || !(view.scale > 0.0F))
        return range;
    // The tile a map pixel lies in, rounded toward negative infinity, so
    // that a camera before the map counts its tiles as one on it does.
    const auto tile_of = [](int64_t pixel) {
        constexpr auto edge = static_cast<int64_t>(gw::tile_edge);
        return pixel >= 0 ? pixel / edge : -((edge - 1 - pixel) / edge);
    };
    const auto columns = [&](int32_t camera, uint32_t extent, uint32_t cells) {
        const auto map_pixels = static_cast<int64_t>(
            std::ceil(static_cast<double>(extent) / static_cast<double>(view.scale))
        );
        const auto last = static_cast<int64_t>(cells);
        const int64_t first = std::clamp<int64_t>(tile_of(camera), 0, last);
        const int64_t end = std::clamp<int64_t>(tile_of(camera + map_pixels) + 1, 0, last);
        return std::pair{static_cast<uint32_t>(first), static_cast<uint32_t>(std::max(first, end))};
    };
    std::tie(range.first_column, range.end_column) =
        columns(view.camera_x, view.width, atlas.grid_width);
    std::tie(range.first_row, range.end_row) =
        columns(view.camera_y, view.height, atlas.grid_height);
    return range;
}

uint32_t append_terrain_tiles(
    card::CardFrame& frame,
    const gw::TerrainAtlas& atlas,
    std::span<const card::PageHandle> pages,
    const TerrainView& view,
    const TerrainPass& pass,
    card::TargetHandle target,
    const card::Rect* scissor
) {
    const TileRange range = visible_tiles(atlas, view);
    if (pass.level >= gw::tile_level_count || pages.size() < atlas.pages.size())
        return 0;
    // The tiles of one pass never overlap, so their order is free: the
    // pass's quads are grouped by page, one batch for each page, so that
    // the card draws a view in as many calls as the atlas has pages, not as
    // many as the grid changes page. The tiles are counted by page first,
    // each page is given one run of the frame's indices, and each tile's
    // six indices are then written into its page's run; the vertices stay
    // in grid order.
    std::vector<uint32_t> page_tiles(atlas.pages.size(), 0);
    uint32_t quads = 0;
    for (uint32_t row = range.first_row; row < range.end_row; ++row)
        for (uint32_t column = range.first_column; column < range.end_column; ++column) {
            const auto cell = static_cast<std::size_t>(row) * atlas.grid_width + column;
            const auto rect = gw::tile_rect(atlas, atlas.grid[cell], pass.level);
            if (!rect || rect->page >= atlas.pages.size())
                continue;
            ++page_tiles[rect->page];
            ++quads;
        }
    if (quads == 0)
        return 0;
    // Where each page's run of indices goes next.
    std::vector<card::Index> run_next(atlas.pages.size(), 0);
    auto next = static_cast<card::Index>(frame.indices.size());
    for (std::size_t page = 0; page < atlas.pages.size(); ++page) {
        if (page_tiles[page] == 0)
            continue;
        card::Batch batch;
        batch.operation = card::Operation::draw;
        batch.target = target;
        batch.page = pages[page];
        batch.level = pass.level;
        batch.blend = pass.blend;
        batch.sampling = pass.sampling;
        batch.scissored = scissor != nullptr;
        if (scissor != nullptr)
            batch.scissor = *scissor;
        batch.first_index = next;
        batch.index_count = quad_indices * page_tiles[page];
        frame.batches.push_back(batch);
        run_next[page] = next;
        next += batch.index_count;
    }
    frame.indices.resize(next);
    const card::Colour colour{1.0F, 1.0F, 1.0F, pass.alpha};
    const auto tile_pixels = static_cast<float>(static_cast<double>(gw::tile_edge) * view.scale);
    // Where a tile edge lands, and, on whole pixels, rounded to the nearest.
    const auto edge_at = [&](uint32_t tile, float origin, int32_t camera) {
        const auto at = static_cast<float>(
            static_cast<double>(origin) +
            (static_cast<double>(tile) * gw::tile_edge - static_cast<double>(camera)) * view.scale
        );
        return view.whole_pixels ? std::round(at) : at;
    };
    for (uint32_t row = range.first_row; row < range.end_row; ++row) {
        const float y = edge_at(row, view.origin_y, view.camera_y);
        const float high =
            view.whole_pixels ? edge_at(row + 1, view.origin_y, view.camera_y) - y : tile_pixels;
        for (uint32_t column = range.first_column; column < range.end_column; ++column) {
            const auto cell = static_cast<std::size_t>(row) * atlas.grid_width + column;
            const auto rect = gw::tile_rect(atlas, atlas.grid[cell], pass.level);
            if (!rect || rect->page >= atlas.pages.size())
                continue;
            const auto& level = atlas.pages[rect->page].levels[pass.level];
            const float x = edge_at(column, view.origin_x, view.camera_x);
            const float wide = view.whole_pixels
                                   ? edge_at(column + 1, view.origin_x, view.camera_x) - x
                                   : tile_pixels;
            const float level_width = static_cast<float>(level.width);
            const float level_height = static_cast<float>(level.height);
            const float u0 = static_cast<float>(rect->x) / level_width;
            const float v0 = static_cast<float>(rect->y) / level_height;
            const float u1 = static_cast<float>(rect->x + rect->edge) / level_width;
            const float v1 = static_cast<float>(rect->y + rect->edge) / level_height;
            // The quad's corners and its two triangles as card::append_quad
            // lays them out, the diagonal from the top-left to the
            // bottom-right corner, with the indices in the page's run.
            const auto first = static_cast<card::Index>(frame.vertices.size());
            frame.vertices.push_back({x, y, colour, u0, v0});
            frame.vertices.push_back({x + wide, y, colour, u1, v0});
            frame.vertices.push_back({x + wide, y + high, colour, u1, v1});
            frame.vertices.push_back({x, y + high, colour, u0, v1});
            card::Index* run = frame.indices.data() + run_next[rect->page];
            run[0] = first;
            run[1] = first + 1;
            run[2] = first + 2;
            run[3] = first;
            run[4] = first + 2;
            run[5] = first + 3;
            run_next[rect->page] += quad_indices;
        }
    }
    return quads;
}

} // namespace oa::app::full_terrain
