// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "full_fog.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace oa::app::full_fog {

namespace gw = oa::present::gpu_world;
namespace wr = oa::present::world_renderer;

namespace {

/// Map pixels a fog tile spans: a sight cell.
constexpr int32_t tile_pixels = wr::fog_cell_pixels;
/// Map pixels a quarter of a fog tile spans: the part of one terrain tile
/// under it.
constexpr int32_t quarter_pixels = tile_pixels / 2;
/// Half way across a fog tile, as a fraction of it: where a terrain tile's
/// edge crosses it.
constexpr float half_tile = 0.5F;
/// The alpha the dithered pass draws a corner out of sight at.
constexpr float dither_alpha = 0.5F;
/// Indices a quad adds.
constexpr uint32_t quad_indices = 6;

static_assert(static_cast<uint32_t>(tile_pixels) == gw::tile_edge);

/// The corner bits of a fog tile in the order the alphas are given.
constexpr std::array<uint8_t, fog_tile_corners> corner_bits{
    wr::fog_corner_top_left,
    wr::fog_corner_top_right,
    wr::fog_corner_bottom_left,
    wr::fog_corner_bottom_right,
};

/// Which fog layer a pass reads.
enum class Layer : uint8_t { unseen, unmapped };

/// Returns the corners of a fog tile the greyed passes draw: those out of
/// sight, unless every corner was never mapped, since the black pass covers
/// the whole tile then.
///
/// @param tile the tile
/// @return the corner mask; 0 for a tile the passes leave alone
[[nodiscard]] uint8_t greyed_mask(const wr::FogTile& tile) noexcept {
    return tile.unmapped == wr::fog_mask_full ? 0 : tile.unseen;
}

/// Returns a tile's mask on a layer: the greyed corners, or the corners
/// never mapped.
///
/// @param tile the tile
/// @param layer the layer
/// @return the corner mask
[[nodiscard]] uint8_t mask_of(const wr::FogTile& tile, Layer layer) noexcept {
    return layer == Layer::unseen ? greyed_mask(tile) : tile.unmapped;
}

/// Tells whether a fog tile of the grid is drawn greyed at every corner.
///
/// @param grid the grid
/// @param column the tile's column; one off the grid is not
/// @param row the tile's row
/// @return true when every corner is out of sight and the black pass does
///     not cover the tile
[[nodiscard]] bool wholly_greyed(const wr::FogGrid& grid, int32_t column, int32_t row) noexcept {
    if (column < 0 || row < 0 || column >= grid.width || row >= grid.height)
        return false;
    return greyed_mask(grid.at(column, row)) == wr::fog_mask_full;
}

/// Appends a quad whose corners have their own colours, as card::append_quad
/// lays its vertices and indices out: top-left, top-right, bottom-right,
/// bottom-left, the diagonal from the top-left to the bottom-right corner.
///
/// @param[in,out] frame the frame
/// @param x the left edge, in pixels of the target
/// @param y the top edge
/// @param width pixels across
/// @param height pixels down
/// @param u0 the texture coordinate of the left edge
/// @param v0 the texture coordinate of the top edge
/// @param u1 the texture coordinate of the right edge
/// @param v1 the texture coordinate of the bottom edge
/// @param colours the corners' colours, top-left, top-right, bottom-left, bottom-right
void append_corner_quad(
    card::CardFrame& frame,
    float x,
    float y,
    float width,
    float height,
    float u0,
    float v0,
    float u1,
    float v1,
    const std::array<card::Colour, fog_tile_corners>& colours
) {
    const auto first = static_cast<card::Index>(frame.vertices.size());
    frame.vertices.push_back({x, y, colours[0], u0, v0});
    frame.vertices.push_back({x + width, y, colours[1], u1, v0});
    frame.vertices.push_back({x + width, y + height, colours[3], u1, v1});
    frame.vertices.push_back({x, y + height, colours[2], u0, v1});
    frame.indices.push_back(first);
    frame.indices.push_back(first + 1);
    frame.indices.push_back(first + 2);
    frame.indices.push_back(first);
    frame.indices.push_back(first + 2);
    frame.indices.push_back(first + 3);
}

/// Opens a draw batch of alpha-blended quads, or keeps the open one when it
/// shares the page.
///
/// @param[in,out] frame the frame
/// @param[in,out] batch the open batch; null for none
/// @param[in,out] batch_page the page the open batch draws
/// @param page the page the next quad draws; none for untextured
/// @param level the page's level
/// @param sampling how the page is read
/// @param target the target
/// @param scissor the scissor; null for none
void open_batch(
    card::CardFrame& frame,
    card::Batch*& batch,
    card::PageHandle& batch_page,
    card::PageHandle page,
    uint8_t level,
    card::Sampling sampling,
    card::TargetHandle target,
    const card::Rect* scissor
) {
    if (batch != nullptr && batch_page == page)
        return;
    frame.batches.emplace_back();
    batch = &frame.batches.back();
    batch_page = page;
    batch->operation = card::Operation::draw;
    batch->target = target;
    batch->page = page;
    batch->level = page == card::PageHandle{} ? 0 : level;
    batch->blend = card::Blend::alpha;
    batch->sampling = sampling;
    batch->scissored = scissor != nullptr;
    if (scissor != nullptr)
        batch->scissor = *scissor;
    batch->first_index = static_cast<card::Index>(frame.indices.size());
    batch->index_count = 0;
}

/// Returns the target pixel a map pixel lands on along one axis.
///
/// @param origin the target pixel the camera's map pixel lands on
/// @param map the map pixel
/// @param camera the camera's map pixel
/// @param scale target pixels per map pixel
/// @return the target pixel
[[nodiscard]] float placed(float origin, int32_t map, int32_t camera, float scale) noexcept {
    return static_cast<float>(
        static_cast<double>(origin) + static_cast<double>(map - camera) * scale
    );
}

/// One piece of the greyed pass: a terrain tile, or a quarter of one under
/// a fog tile, with the alphas at its corners.
struct GreyedPiece {
    int32_t map_x{};     ///< the piece's left edge, in map pixels
    int32_t map_z{};     ///< the piece's top edge
    int32_t span{};      ///< map pixels a side: a tile or a quarter
    uint16_t slot{};     ///< the atlas slot of the terrain tile it lies in
    uint8_t quarter_x{}; ///< the quarter of the tile it is: 0 for the left, 1 for the right
    uint8_t quarter_z{}; ///< 0 for the top, 1 for the bottom
    bool whole{};        ///< the whole tile
    std::array<float, fog_tile_corners> alphas{};
};

/// Returns the alpha a tile's corners interpolate to at a point of the tile.
///
/// @param corners the tile's corner alphas
/// @param u how far across the tile, 0 to 1
/// @param v how far down the tile, 0 to 1
/// @return the alpha
[[nodiscard]] float
alpha_at(const std::array<float, fog_tile_corners>& corners, float u, float v) noexcept {
    return corners[0] * (1.0F - u) * (1.0F - v) + corners[1] * u * (1.0F - v) +
           corners[2] * (1.0F - u) * v + corners[3] * u * v;
}

/// Collects the pieces of the greyed pass over a grid: a whole terrain tile
/// where its four fog tiles are wholly out of sight, else the quarters of
/// terrain tiles under each fog tile drawn greyed.
///
/// @param grid the fog grid
/// @param atlas the map's atlas
/// @param placement where the grid lands
/// @param[out] pieces the pieces, in the grid's row order
void collect_greyed_pieces(
    const wr::FogGrid& grid,
    const gw::TerrainAtlas& atlas,
    const FogPlacement& placement,
    std::vector<GreyedPiece>& pieces
) {
    pieces.clear();
    const auto grid_width = static_cast<int32_t>(atlas.grid_width);
    const auto grid_height = static_cast<int32_t>(atlas.grid_height);
    for (int32_t row = 0; row < grid.height; ++row) {
        const int32_t tile_z = placement.camera_z + grid.offset_z + row * tile_pixels;
        for (int32_t column = 0; column < grid.width; ++column) {
            const uint8_t mask = greyed_mask(grid.at(column, row));
            if (mask == 0)
                continue;
            const int32_t tile_x = placement.camera_x + grid.offset_x + column * tile_pixels;
            const auto corners = corner_alphas(mask);
            for (uint8_t quarter_z = 0; quarter_z < 2; ++quarter_z)
                for (uint8_t quarter_x = 0; quarter_x < 2; ++quarter_x) {
                    const int32_t map_x = tile_x + quarter_x * quarter_pixels;
                    const int32_t map_z = tile_z + quarter_z * quarter_pixels;
                    if (map_x < 0 || map_z < 0)
                        continue;
                    const int32_t terrain_column = map_x / tile_pixels;
                    const int32_t terrain_row = map_z / tile_pixels;
                    if (terrain_column >= grid_width || terrain_row >= grid_height)
                        continue;
                    GreyedPiece piece;
                    piece.slot = atlas.grid
                                     [static_cast<std::size_t>(terrain_row) * atlas.grid_width +
                                      static_cast<std::size_t>(terrain_column)];
                    // The fog tile at the terrain tile's top-left corner, and
                    // the three after it: all wholly out of sight, the tile
                    // is drawn once, from the fog tile at its top-left.
                    const int32_t first_column = column + quarter_x - 1;
                    const int32_t first_row = row + quarter_z - 1;
                    const bool uniform = wholly_greyed(grid, first_column, first_row) &&
                                         wholly_greyed(grid, first_column + 1, first_row) &&
                                         wholly_greyed(grid, first_column, first_row + 1) &&
                                         wholly_greyed(grid, first_column + 1, first_row + 1);
                    if (uniform) {
                        if (quarter_x != 1 || quarter_z != 1)
                            continue;
                        piece.map_x = terrain_column * tile_pixels;
                        piece.map_z = terrain_row * tile_pixels;
                        piece.span = tile_pixels;
                        piece.whole = true;
                        piece.alphas.fill(1.0F);
                        pieces.push_back(piece);
                        continue;
                    }
                    piece.map_x = map_x;
                    piece.map_z = map_z;
                    piece.span = quarter_pixels;
                    // Which quarter of the terrain tile the piece is, from
                    // where it lies within the tile.
                    piece.quarter_x = static_cast<uint8_t>((map_x % tile_pixels) / quarter_pixels);
                    piece.quarter_z = static_cast<uint8_t>((map_z % tile_pixels) / quarter_pixels);
                    const float u0 = quarter_x == 0 ? 0.0F : half_tile;
                    const float v0 = quarter_z == 0 ? 0.0F : half_tile;
                    piece.alphas = {
                        alpha_at(corners, u0, v0),
                        alpha_at(corners, u0 + half_tile, v0),
                        alpha_at(corners, u0, v0 + half_tile),
                        alpha_at(corners, u0 + half_tile, v0 + half_tile),
                    };
                    pieces.push_back(piece);
                }
        }
    }
}

/// Appends a pass of solid quads over one layer of the grid: a quad for
/// each tile with any corner marked on the layer, at the corner alphas
/// times a factor, tiles wholly marked that follow one another in a row
/// merged into one.
///
/// @param[in,out] frame the frame
/// @param grid the fog grid
/// @param layer the layer read
/// @param colour the quads' colour
/// @param factor what the corner alphas are scaled by
/// @param placement where the grid lands
/// @param target the target
/// @param scissor the scissor; null for none
/// @return quads appended
uint32_t append_solid_pass(
    card::CardFrame& frame,
    const wr::FogGrid& grid,
    Layer layer,
    const card::Colour& colour,
    float factor,
    const FogPlacement& placement,
    card::TargetHandle target,
    const card::Rect* scissor
) {
    if (grid.tiles.empty())
        return 0;
    uint32_t quads = 0;
    card::Batch* batch = nullptr;
    card::PageHandle batch_page{};
    const float tile_span = static_cast<float>(static_cast<double>(tile_pixels) * placement.scale);
    const auto coloured = [&](float alpha) {
        return card::Colour{colour.red, colour.green, colour.blue, alpha * factor};
    };
    for (int32_t row = 0; row < grid.height; ++row) {
        const int32_t tile_z = placement.camera_z + grid.offset_z + row * tile_pixels;
        const float y = placed(placement.origin_y, tile_z, placement.camera_z, placement.scale);
        for (int32_t column = 0; column < grid.width;) {
            const uint8_t mask = mask_of(grid.at(column, row), layer);
            if (mask == 0) {
                ++column;
                continue;
            }
            const int32_t tile_x = placement.camera_x + grid.offset_x + column * tile_pixels;
            const float x = placed(placement.origin_x, tile_x, placement.camera_x, placement.scale);
            open_batch(frame, batch, batch_page, {}, 0, card::Sampling::nearest, target, scissor);
            if (mask == wr::fog_mask_full) {
                int32_t end = column + 1;
                while (end < grid.width && mask_of(grid.at(end, row), layer) == wr::fog_mask_full)
                    ++end;
                const int32_t end_x = tile_x + (end - column) * tile_pixels;
                const float right =
                    placed(placement.origin_x, end_x, placement.camera_x, placement.scale);
                card::append_quad(
                    frame, x, y, right - x, tile_span, 0.0F, 0.0F, 0.0F, 0.0F, coloured(1.0F)
                );
                column = end;
            } else {
                const auto alphas = corner_alphas(mask);
                append_corner_quad(
                    frame,
                    x,
                    y,
                    tile_span,
                    tile_span,
                    0.0F,
                    0.0F,
                    0.0F,
                    0.0F,
                    {coloured(alphas[0]),
                     coloured(alphas[1]),
                     coloured(alphas[2]),
                     coloured(alphas[3])}
                );
                ++column;
            }
            batch->index_count += quad_indices;
            ++quads;
        }
    }
    return quads;
}

} // namespace

std::array<float, fog_tile_corners> corner_alphas(uint8_t mask) noexcept {
    std::array<float, fog_tile_corners> alphas{};
    for (std::size_t corner = 0; corner < fog_tile_corners; ++corner)
        alphas[corner] = (mask & corner_bits[corner]) != 0 ? 1.0F : 0.0F;
    return alphas;
}

LevelQuad level_quad(int32_t level) noexcept {
    LevelQuad quad;
    if (level < 0) {
        const int32_t row = std::max<int32_t>(0, table_rows + level);
        const float scale = static_cast<float>(row) * shade_row_step;
        quad.colour = {0.0F, 0.0F, 0.0F, std::clamp(1.0F - scale, 0.0F, 1.0F)};
        quad.blend = card::Blend::alpha;
        return quad;
    }
    const int32_t row = std::min<int32_t>(level, table_rows - 1);
    const float scale = 1.0F + static_cast<float>(row) * light_row_step;
    quad.colour = {1.0F, 1.0F, 1.0F, std::clamp(1.0F - 1.0F / scale, 0.0F, 1.0F)};
    quad.blend = card::Blend::additive;
    return quad;
}

float pass_alpha(float alpha, float share, float later) noexcept {
    // Nothing is left for a pass whose later levels cover the corner wholly.
    const float left = 1.0F - alpha * later;
    if (!(left > 0.0F))
        return 0.0F;
    return std::clamp(alpha * share / left, 0.0F, 1.0F);
}

uint32_t append_unseen_terrain(
    card::CardFrame& frame,
    const wr::FogGrid& grid,
    const gw::TerrainAtlas& atlas,
    std::span<const card::PageHandle> greyed_pages,
    std::span<const GreyedLevel> levels,
    const FogPlacement& placement,
    card::TargetHandle target,
    const card::Rect* scissor
) {
    if (grid.tiles.empty() || levels.empty() || levels.size() > most_greyed_levels ||
        greyed_pages.size() < atlas.pages.size() || atlas.grid_width == 0 || atlas.grid_height == 0)
        return 0;
    for (const auto& level : levels)
        if (level.level >= gw::tile_level_count)
            return 0;
    std::vector<GreyedPiece> pieces;
    collect_greyed_pieces(grid, atlas, placement, pieces);
    if (pieces.empty())
        return 0;
    uint32_t quads = 0;
    for (std::size_t pass = 0; pass < levels.size(); ++pass) {
        const auto& drawn = levels[pass];
        float later = 0.0F;
        for (std::size_t after = pass + 1; after < levels.size(); ++after)
            later += levels[after].share;
        card::Batch* batch = nullptr;
        card::PageHandle batch_page{};
        for (const auto& piece : pieces) {
            const auto rect = gw::tile_rect(atlas, piece.slot, drawn.level);
            if (!rect || rect->page >= atlas.pages.size())
                continue;
            const auto& level = atlas.pages[rect->page].levels[drawn.level];
            const float level_width = static_cast<float>(level.width);
            const float level_height = static_cast<float>(level.height);
            // The texels of the piece: the tile's, or the quarter's of them.
            const uint32_t edge = piece.whole ? rect->edge : rect->edge / 2;
            const uint32_t texel_x = rect->x + (piece.whole ? 0 : piece.quarter_x * edge);
            const uint32_t texel_y = rect->y + (piece.whole ? 0 : piece.quarter_z * edge);
            open_batch(
                frame,
                batch,
                batch_page,
                greyed_pages[rect->page],
                drawn.level,
                drawn.sampling,
                target,
                scissor
            );
            std::array<card::Colour, fog_tile_corners> colours{};
            for (std::size_t corner = 0; corner < fog_tile_corners; ++corner)
                colours[corner] = {
                    1.0F, 1.0F, 1.0F, pass_alpha(piece.alphas[corner], drawn.share, later)
                };
            const float span =
                static_cast<float>(static_cast<double>(piece.span) * placement.scale);
            append_corner_quad(
                frame,
                placed(placement.origin_x, piece.map_x, placement.camera_x, placement.scale),
                placed(placement.origin_y, piece.map_z, placement.camera_z, placement.scale),
                span,
                span,
                static_cast<float>(texel_x) / level_width,
                static_cast<float>(texel_y) / level_height,
                static_cast<float>(texel_x + edge) / level_width,
                static_cast<float>(texel_y + edge) / level_height,
                colours
            );
            batch->index_count += quad_indices;
            ++quads;
        }
    }
    return quads;
}

uint32_t append_unseen_dither(
    card::CardFrame& frame,
    const wr::FogGrid& grid,
    const card::Colour& dither,
    const FogPlacement& placement,
    card::TargetHandle target,
    const card::Rect* scissor
) {
    return append_solid_pass(
        frame, grid, Layer::unseen, dither, dither_alpha, placement, target, scissor
    );
}

uint32_t append_unmapped(
    card::CardFrame& frame,
    const wr::FogGrid& grid,
    const card::Colour& colour,
    const FogPlacement& placement,
    card::TargetHandle target,
    const card::Rect* scissor
) {
    return append_solid_pass(
        frame, grid, Layer::unmapped, colour, 1.0F, placement, target, scissor
    );
}

} // namespace oa::app::full_fog
