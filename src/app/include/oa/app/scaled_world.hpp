// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Textures whose size follows the window, made as one texture within the
// renderer's texture limit and as tiles beyond it, each tile carrying a
// one-texel gutter copied from its neighbour; and the error a failed SDL call
// of the standard tier's presenting throws.
#pragma once

#include "oa/app/render_policy.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace oa::app {

/// A failed SDL call while presenting a frame of the standard tier: the
/// renderer is rebuilt for the rest of the run (Runtime::render).
class PresentError : public std::runtime_error {
  public:

    using std::runtime_error::runtime_error;
};

/// Throws PresentError with SDL's error after what failed.
///
/// @param what the call or texture that failed
[[noreturn]] void throw_present_error(const std::string& what);

/// A streaming texture as one texture within the renderer's limit, or as
/// tiles beyond it (render_policy::plan_tiles).
///
/// Within the limit it makes and uses exactly the one texture today's code
/// makes, and passes the caller's rectangles to SDL unchanged, so a renderer
/// with no limit, such as SDL's software renderer, draws as before. Beyond
/// it, each tile is uploaded from the part of the picture it holds, its
/// gutters included, and drawn without its gutters, so tiles drawn 1:1 at
/// whole-pixel places give the same pixels as one texture would. Every SDL
/// failure throws PresentError.
class TiledTexture {
  public:

    TiledTexture() = default;
    TiledTexture(const TiledTexture&) = delete;
    TiledTexture& operator=(const TiledTexture&) = delete;

    /// Destroys the texture or its tiles.
    ~TiledTexture() { reset(); }

    /// Makes the texture or its tiles unless they already match: a format,
    /// size, limit or blend mode that differs makes them again. Each is a
    /// streaming texture scaled NEAREST with the given blend mode; an
    /// alpha format with SDL_BLENDMODE_NONE has its blending turned off,
    /// which SDL turns on for alpha formats.
    ///
    /// Throws PresentError when SDL cannot make one.
    ///
    /// @param renderer the renderer
    /// @param format the pixel format
    /// @param width the picture's width in pixels, above 0
    /// @param height the picture's height in pixels, above 0
    /// @param limit the renderer's texture limit in texels; 0 for none
    /// @param blend how the texture is drawn over what is under it
    /// @return true when the texture or its tiles were made anew
    bool ensure(
        SDL_Renderer* renderer,
        SDL_PixelFormat format,
        int width,
        int height,
        uint32_t limit,
        SDL_BlendMode blend
    );

    /// Fills the texture through its tiles: each is locked and handed to
    /// fill with the part of the picture it holds, gutters included.
    ///
    /// Throws PresentError when a tile cannot be locked.
    ///
    /// @param fill called as fill(const SDL_Rect& source, uint8_t* pixels,
    ///     int pitch) with the part of the picture in its pixels, the tile's
    ///     locked pixels and their pitch in bytes
    template <typename Fill>
    void upload(Fill&& fill) {
        for (const auto& piece : tiles_) {
            void* pixels = nullptr;
            int pitch = 0;
            if (!SDL_LockTexture(piece.texture, nullptr, &pixels, &pitch))
                throw_present_error("SDL_LockTexture");
            fill(source_rect(piece), static_cast<uint8_t*>(pixels), pitch);
            SDL_UnlockTexture(piece.texture);
        }
    }

    /// Copies a picture into the texture's tiles with SDL_UpdateTexture.
    ///
    /// Throws PresentError when SDL refuses a tile.
    ///
    /// @param pixels the picture, as large as the texture, in its format
    /// @param pitch bytes from one row of the picture to the next
    /// @param bytes_per_pixel bytes of one pixel of the format
    void update(const uint8_t* pixels, int pitch, int bytes_per_pixel);

    /// Draws a rectangle of the texture onto one of the render target.
    ///
    /// With one texture it is one SDL_RenderTexture with the rectangles
    /// unchanged, null included. With tiles, each tile's part of `source`
    /// is drawn onto its part of `destination`.
    ///
    /// Throws PresentError when SDL refuses a draw.
    ///
    /// @param renderer the renderer the texture was made on
    /// @param source the part of the picture to draw, in its pixels; null for all of it
    /// @param destination where it goes on the render target; null for the
    ///     whole target with one texture, and the picture's own rectangle
    ///     with tiles
    void draw(SDL_Renderer* renderer, const SDL_FRect* source, const SDL_FRect* destination) const;

    /// Destroys the texture or its tiles; the next ensure makes them again.
    void reset() noexcept;

    /// Returns how many textures hold the picture.
    ///
    /// @return 0 before ensure, 1 within the limit, more beyond it
    [[nodiscard]] std::size_t tile_count() const noexcept { return tiles_.size(); }

    /// Returns the one texture that holds the whole picture.
    ///
    /// @return the texture; null when there is none or the picture is tiled
    [[nodiscard]] SDL_Texture* single() const noexcept {
        return tiles_.size() == 1 ? tiles_.front().texture : nullptr;
    }

    /// Returns the picture's width.
    ///
    /// @return pixels; 0 before ensure
    [[nodiscard]] int width() const noexcept { return width_; }

    /// Returns the picture's height.
    ///
    /// @return pixels; 0 before ensure
    [[nodiscard]] int height() const noexcept { return height_; }

    /// Returns the textures' pixel format.
    ///
    /// @return the format; SDL_PIXELFORMAT_UNKNOWN before ensure
    [[nodiscard]] SDL_PixelFormat format() const noexcept { return format_; }

  private:

    /// One texture of the picture and the part it holds.
    struct Piece {
        SDL_Texture* texture{};
        render_policy::Tile tile{};
    };

    /// Returns the part of the picture a tile holds, gutters included.
    ///
    /// @param piece the tile
    /// @return the rectangle in the picture's pixels
    [[nodiscard]] static SDL_Rect source_rect(const Piece& piece) noexcept {
        return {
            static_cast<int>(piece.tile.texture.x),
            static_cast<int>(piece.tile.texture.y),
            static_cast<int>(piece.tile.texture.width),
            static_cast<int>(piece.tile.texture.height)
        };
    }

    std::vector<Piece> tiles_;
    SDL_PixelFormat format_{SDL_PIXELFORMAT_UNKNOWN};
    SDL_BlendMode blend_{SDL_BLENDMODE_NONE};
    int width_{};
    int height_{};
    uint32_t limit_{}; ///< the texture limit the tiles were planned for; 0 for none
};

} // namespace oa::app
