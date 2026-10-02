// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Window-size textures as one texture or as tiles with gutters.
#include "oa/app/scaled_world.hpp"

#include <algorithm>

namespace oa::app {
namespace {

namespace policy = oa::app::render_policy;

/// Says whether two grids split a texture alike.
///
/// @param first one grid
/// @param second the other
/// @return true for the same size, tiles, columns and rows
bool same_grid(const policy::TileGrid& first, const policy::TileGrid& second) noexcept {
    return first.width == second.width && first.height == second.height &&
           first.tile_size == second.tile_size && first.columns == second.columns &&
           first.rows == second.rows;
}

} // namespace

void throw_present_error(const std::string& what) {
    throw PresentError(what + ": " + SDL_GetError());
}

bool TiledTexture::ensure(
    SDL_Renderer* renderer,
    SDL_PixelFormat format,
    int width,
    int height,
    uint32_t limit,
    SDL_BlendMode blend
) {
    const policy::TileGrid grid = policy::plan_tiles(
        static_cast<uint32_t>(std::max(width, 0)), static_cast<uint32_t>(std::max(height, 0)), limit
    );
    const policy::TileGrid held =
        policy::plan_tiles(static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), limit_);
    if (!tiles_.empty() && format_ == format && blend_ == blend && width_ == width &&
        height_ == height && same_grid(grid, held))
        return false;
    reset();
    if (grid.columns == 0 || grid.rows == 0)
        throw PresentError(
            "texture of " + std::to_string(width) + "x" + std::to_string(height) +
            " beyond the renderer's limit of " + std::to_string(limit)
        );
    tiles_.reserve(static_cast<std::size_t>(grid.columns) * grid.rows);
    for (uint32_t row = 0; row < grid.rows; ++row)
        for (uint32_t column = 0; column < grid.columns; ++column) {
            Piece piece;
            piece.tile = policy::tile_at(grid, column, row);
            piece.texture = SDL_CreateTexture(
                renderer,
                format,
                SDL_TEXTUREACCESS_STREAMING,
                static_cast<int>(piece.tile.texture.width),
                static_cast<int>(piece.tile.texture.height)
            );
            if (piece.texture == nullptr) {
                reset();
                throw_present_error("SDL_CreateTexture");
            }
            tiles_.push_back(piece);
            // SDL blends alpha formats unless told not to.
            if ((blend != SDL_BLENDMODE_NONE || SDL_ISPIXELFORMAT_ALPHA(format)) &&
                !SDL_SetTextureBlendMode(piece.texture, blend)) {
                reset();
                throw_present_error("SDL texture blend");
            }
            if (!SDL_SetTextureScaleMode(piece.texture, SDL_SCALEMODE_NEAREST)) {
                reset();
                throw_present_error("SDL texture scale");
            }
        }
    format_ = format;
    blend_ = blend;
    width_ = width;
    height_ = height;
    limit_ = limit;
    return true;
}

void TiledTexture::update(const uint8_t* pixels, int pitch, int bytes_per_pixel) {
    for (const auto& piece : tiles_) {
        const uint8_t* first = pixels + static_cast<std::ptrdiff_t>(piece.tile.texture.y) * pitch +
                               static_cast<std::ptrdiff_t>(piece.tile.texture.x) * bytes_per_pixel;
        if (!SDL_UpdateTexture(piece.texture, nullptr, first, pitch))
            throw_present_error("SDL_UpdateTexture");
    }
}

void TiledTexture::draw(
    SDL_Renderer* renderer, const SDL_FRect* source, const SDL_FRect* destination
) const {
    if (tiles_.size() == 1) {
        if (!SDL_RenderTexture(renderer, tiles_.front().texture, source, destination))
            throw_present_error("SDL_RenderTexture");
        return;
    }
    const SDL_FRect whole{0.0F, 0.0F, static_cast<float>(width_), static_cast<float>(height_)};
    const SDL_FRect from = source != nullptr ? *source : whole;
    const SDL_FRect to = destination != nullptr ? *destination : whole;
    if (from.w <= 0.0F || from.h <= 0.0F)
        return;
    const float scale_x = to.w / from.w;
    const float scale_y = to.h / from.h;
    for (const auto& piece : tiles_) {
        const auto& content = piece.tile.content;
        const float left = std::max(from.x, static_cast<float>(content.x));
        const float top = std::max(from.y, static_cast<float>(content.y));
        const float right =
            std::min(from.x + from.w, static_cast<float>(content.x + content.width));
        const float bottom =
            std::min(from.y + from.h, static_cast<float>(content.y + content.height));
        if (right <= left || bottom <= top)
            continue;
        const SDL_FRect part{
            left - static_cast<float>(piece.tile.texture.x),
            top - static_cast<float>(piece.tile.texture.y),
            right - left,
            bottom - top
        };
        const SDL_FRect onto{
            to.x + (left - from.x) * scale_x,
            to.y + (top - from.y) * scale_y,
            (right - left) * scale_x,
            (bottom - top) * scale_y
        };
        if (!SDL_RenderTexture(renderer, piece.texture, &part, &onto))
            throw_present_error("SDL_RenderTexture");
    }
}

void TiledTexture::reset() noexcept {
    for (const auto& piece : tiles_)
        SDL_DestroyTexture(piece.texture);
    tiles_.clear();
    format_ = SDL_PIXELFORMAT_UNKNOWN;
    blend_ = SDL_BLENDMODE_NONE;
    width_ = 0;
    height_ = 0;
    limit_ = 0;
}

} // namespace oa::app
