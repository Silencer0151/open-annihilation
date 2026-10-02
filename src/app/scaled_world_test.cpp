// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Window-size textures beyond the renderer's texture limit, on SDL's
// software renderer over a surface larger than two 1024-texel tiles each
// way: the same picture drawn as tiles under a limit of 1024 and as one
// texture with no limit reads back pixel for pixel alike, for an XRGB8888
// upload drawn 1:1 at an offset, ARGB8888 drawn with no blending, an RGBA32
// layer blended over a background with no rectangles given, and part of the
// picture drawn onto the same part of the target. Each tile is filled from the part of the picture
// it holds, so its gutters hold its neighbours' edges.
#include "oa/app/scaled_world.hpp"

#include "oa/test/check.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace {

using oa::app::TiledTexture;

/// The render target, larger than two 1024-texel tiles in each direction.
constexpr int kTargetWidth = 2600;
constexpr int kTargetHeight = 1500;
/// The tiled run's texture limit, in texels.
constexpr uint32_t kTiledLimit = 1024;
/// The picture's size: a window-size layer of the target's size, which
/// drawn at an offset runs off the target's right and bottom edges.
constexpr int kPictureWidth = kTargetWidth;
constexpr int kPictureHeight = kTargetHeight;
/// Where the picture is drawn on the target.
constexpr float kOffsetX = 128.0F;
constexpr float kOffsetY = 32.0F;
/// Bytes of one pixel of the 32-bit formats.
constexpr int kPixelBytes = 4;
/// Seed of the pictures.
constexpr uint32_t kSeed = 2600;

/// A picture of random 32-bit pixels.
std::vector<uint8_t> random_picture(uint32_t seed) {
    std::mt19937 random(seed);
    std::vector<uint8_t> picture(static_cast<std::size_t>(kPictureWidth) * kPictureHeight * 4U);
    for (auto& byte : picture)
        byte = static_cast<uint8_t>(random());
    return picture;
}

/// SDL's software renderer over its own surface.
struct SoftwareTarget {
    SDL_Surface* surface{};
    SDL_Renderer* renderer{};

    SoftwareTarget() {
        surface = SDL_CreateSurface(kTargetWidth, kTargetHeight, SDL_PIXELFORMAT_XRGB8888);
        if (surface != nullptr)
            renderer = SDL_CreateSoftwareRenderer(surface);
    }

    SoftwareTarget(const SoftwareTarget&) = delete;
    SoftwareTarget& operator=(const SoftwareTarget&) = delete;

    ~SoftwareTarget() {
        if (renderer != nullptr)
            SDL_DestroyRenderer(renderer);
        if (surface != nullptr)
            SDL_DestroySurface(surface);
    }

    /// Clears the target to a colour of its own.
    void clear() const {
        OA_CHECK(SDL_SetRenderDrawColor(renderer, 20, 40, 60, 255));
        OA_CHECK(SDL_RenderClear(renderer));
    }

    /// Reads the target back.
    std::vector<uint8_t> read_back() const {
        std::vector<uint8_t> pixels;
        SDL_Surface* read = SDL_RenderReadPixels(renderer, nullptr);
        OA_CHECK(read != nullptr);
        if (read == nullptr)
            return pixels;
        SDL_Surface* rgb = SDL_ConvertSurface(read, SDL_PIXELFORMAT_XRGB8888);
        SDL_DestroySurface(read);
        OA_CHECK(rgb != nullptr);
        if (rgb == nullptr)
            return pixels;
        const auto row_bytes = static_cast<std::size_t>(rgb->w) * 4U;
        pixels.resize(row_bytes * static_cast<std::size_t>(rgb->h));
        for (int row = 0; row < rgb->h; ++row)
            std::memcpy(
                pixels.data() + static_cast<std::size_t>(row) * row_bytes,
                static_cast<const uint8_t*>(rgb->pixels) +
                    static_cast<std::ptrdiff_t>(row) * rgb->pitch,
                row_bytes
            );
        SDL_DestroySurface(rgb);
        return pixels;
    }
};

/// What one way of drawing a picture does.
enum class Drawing : uint8_t {
    locked_offset,  ///< filled through locked tiles, drawn 1:1 at an offset
    updated_argb,   ///< updated as ARGB8888, drawn with no blending
    blended_layer,  ///< an RGBA32 layer blended over a background
    part_onto_part, ///< part of the picture drawn onto the same part of the target
};

/// Draws a picture one way, under a texture limit, and reads the target back.
///
/// @param drawing the way
/// @param limit the texture limit; 0 for none
/// @param[out] tiles how many textures held the picture
/// @return the target's pixels
std::vector<uint8_t> draw(Drawing drawing, uint32_t limit, std::size_t& tiles) {
    SoftwareTarget target;
    OA_CHECK(target.renderer != nullptr);
    if (target.renderer == nullptr)
        return {};
    const auto picture = random_picture(kSeed + static_cast<uint32_t>(drawing));
    const int picture_pitch = kPictureWidth * kPixelBytes;
    target.clear();
    TiledTexture texture;
    const SDL_FRect at{
        kOffsetX, kOffsetY, static_cast<float>(kPictureWidth), static_cast<float>(kPictureHeight)
    };
    switch (drawing) {
    case Drawing::locked_offset: {
        OA_CHECK(texture.ensure(
            target.renderer,
            SDL_PIXELFORMAT_XRGB8888,
            kPictureWidth,
            kPictureHeight,
            limit,
            SDL_BLENDMODE_NONE
        ));
        texture.upload([&](const SDL_Rect& source, uint8_t* pixels, int pitch) {
            for (int row = 0; row < source.h; ++row)
                std::memcpy(
                    pixels + static_cast<std::ptrdiff_t>(row) * pitch,
                    picture.data() + static_cast<std::ptrdiff_t>(source.y + row) * picture_pitch +
                        static_cast<std::ptrdiff_t>(source.x) * kPixelBytes,
                    static_cast<std::size_t>(source.w) * kPixelBytes
                );
        });
        texture.draw(target.renderer, nullptr, &at);
        break;
    }
    case Drawing::updated_argb:
        OA_CHECK(texture.ensure(
            target.renderer,
            SDL_PIXELFORMAT_ARGB8888,
            kPictureWidth,
            kPictureHeight,
            limit,
            SDL_BLENDMODE_NONE
        ));
        texture.update(picture.data(), picture_pitch, kPixelBytes);
        texture.draw(target.renderer, nullptr, &at);
        break;
    case Drawing::blended_layer: {
        // A background of its own, as one texture in both runs, then the
        // layer blended over the whole target.
        const auto background = random_picture(kSeed);
        TiledTexture under;
        OA_CHECK(under.ensure(
            target.renderer,
            SDL_PIXELFORMAT_XRGB8888,
            kPictureWidth,
            kPictureHeight,
            0,
            SDL_BLENDMODE_NONE
        ));
        under.update(background.data(), picture_pitch, kPixelBytes);
        under.draw(target.renderer, nullptr, nullptr);
        OA_CHECK(texture.ensure(
            target.renderer,
            SDL_PIXELFORMAT_RGBA32,
            kPictureWidth,
            kPictureHeight,
            limit,
            SDL_BLENDMODE_BLEND
        ));
        texture.update(picture.data(), picture_pitch, kPixelBytes);
        texture.draw(target.renderer, nullptr, nullptr);
        break;
    }
    case Drawing::part_onto_part: {
        OA_CHECK(texture.ensure(
            target.renderer,
            SDL_PIXELFORMAT_ARGB8888,
            kPictureWidth,
            kPictureHeight,
            limit,
            SDL_BLENDMODE_NONE
        ));
        texture.update(picture.data(), picture_pitch, kPixelBytes);
        // A part that crosses a tile's edge in each direction.
        const SDL_FRect part{1000.0F, 1000.0F, 700.0F, 250.0F};
        texture.draw(target.renderer, &part, &part);
        break;
    }
    }
    tiles = texture.tile_count();
    return target.read_back();
}

void tiles_draw_as_one_texture() {
    for (const Drawing drawing :
         {Drawing::locked_offset,
          Drawing::updated_argb,
          Drawing::blended_layer,
          Drawing::part_onto_part}) {
        std::size_t whole_tiles = 0;
        std::size_t split_tiles = 0;
        const auto whole = draw(drawing, 0, whole_tiles);
        const auto split = draw(drawing, kTiledLimit, split_tiles);
        OA_CHECK(whole_tiles == 1);
        OA_CHECK(split_tiles > 1);
        OA_CHECK(!whole.empty());
        OA_CHECK(whole == split);
    }
}

void gutters_hold_neighbours_edges() {
    SoftwareTarget target;
    OA_CHECK(target.renderer != nullptr);
    if (target.renderer == nullptr)
        return;
    TiledTexture texture;
    OA_CHECK(texture.ensure(
        target.renderer,
        SDL_PIXELFORMAT_XRGB8888,
        kPictureWidth,
        kPictureHeight,
        kTiledLimit,
        SDL_BLENDMODE_NONE
    ));
    std::vector<SDL_Rect> sources;
    texture.upload([&](const SDL_Rect& source, uint8_t*, int) { sources.push_back(source); });
    OA_CHECK(sources.size() == texture.tile_count());
    // Every pair of neighbours overlaps by two texels, a gutter on each side
    // of their seam, so each gutter is filled with its neighbour's edge.
    std::size_t seams = 0;
    for (const auto& first : sources)
        for (const auto& second : sources) {
            if (first.y == second.y && first.h == second.h && second.x > first.x &&
                second.x < first.x + first.w) {
                OA_CHECK(first.x + first.w - second.x == 2);
                ++seams;
            }
            if (first.x == second.x && first.w == second.w && second.y > first.y &&
                second.y < first.y + first.h) {
                OA_CHECK(first.y + first.h - second.y == 2);
                ++seams;
            }
        }
    OA_CHECK(seams > 0);
    // Every tile within the limit, and a picture within it in one texture.
    for (const auto& source : sources)
        OA_CHECK(
            source.w <= static_cast<int>(kTiledLimit) && source.h <= static_cast<int>(kTiledLimit)
        );
    TiledTexture small;
    OA_CHECK(small.ensure(
        target.renderer, SDL_PIXELFORMAT_XRGB8888, 640, 480, kTiledLimit, SDL_BLENDMODE_NONE
    ));
    OA_CHECK(small.tile_count() == 1 && small.single() != nullptr);
    // The same request makes nothing again; another size does.
    OA_CHECK(!small.ensure(
        target.renderer, SDL_PIXELFORMAT_XRGB8888, 640, 480, kTiledLimit, SDL_BLENDMODE_NONE
    ));
    OA_CHECK(small.ensure(
        target.renderer, SDL_PIXELFORMAT_XRGB8888, 800, 600, kTiledLimit, SDL_BLENDMODE_NONE
    ));
    small.reset();
    OA_CHECK(small.tile_count() == 0 && small.width() == 0);
}

void alpha_layers_keep_their_blending() {
    SoftwareTarget target;
    OA_CHECK(target.renderer != nullptr);
    if (target.renderer == nullptr)
        return;
    TiledTexture opaque;
    OA_CHECK(
        opaque.ensure(target.renderer, SDL_PIXELFORMAT_ARGB8888, 64, 64, 0, SDL_BLENDMODE_NONE)
    );
    SDL_BlendMode mode = SDL_BLENDMODE_INVALID;
    OA_CHECK(SDL_GetTextureBlendMode(opaque.single(), &mode) && mode == SDL_BLENDMODE_NONE);
    TiledTexture layer;
    OA_CHECK(layer.ensure(target.renderer, SDL_PIXELFORMAT_RGBA32, 64, 64, 0, SDL_BLENDMODE_BLEND));
    OA_CHECK(SDL_GetTextureBlendMode(layer.single(), &mode) && mode == SDL_BLENDMODE_BLEND);
    SDL_ScaleMode scale = SDL_SCALEMODE_LINEAR;
    OA_CHECK(SDL_GetTextureScaleMode(layer.single(), &scale) && scale == SDL_SCALEMODE_NEAREST);
}

} // namespace

int main() {
    tiles_draw_as_one_texture();
    gutters_hold_neighbours_edges();
    alpha_layers_keep_their_blending();
    const int status = oa::test::check_exit_status();
    if (status == 0)
        std::puts("scaled world: tiles beyond the limit draw as one texture would");
    return status;
}
