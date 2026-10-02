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
//
// The accelerated tier's drawing on the card (scaled_world.hpp), on SDL's
// software renderer over a surface, with no window, read back and compared
// with what that renderer is asked to draw: nearest replication
// (scene_filter.hpp) and the renderer's own LINEAR, modelled on the
// processor (software_linear_rgb24). A seeded scene magnified by
// draw_scaled_world at 1.37 by sharp-bilinear and by LINEAR, and seeded
// sources drawn by sharp_draw at 0.5, 0.6, 0.75 and 1.37, keep within 2
// levels of it, and 0.5 on average; at 2 and 4 both draw NEAREST, equal to
// nearest replication. PIXELART is the renderer's NEAREST, as SDL's software
// renderer gives it, and the probe finds it missing. A scene split into
// tiles holds each neighbour's texels in its gutters and draws as the whole
// texture does. A prescale target split into tiles holds the enlarged
// picture, gutters included, from a scene of one tile or of several, and
// draws what one target draws. The overlay drawn over a picture keeps it
// where transparent and replaces it where opaque. sharp_draw makes no
// prescale target at a whole-number scale and does not draw its source into
// the target again while the source's revision is unchanged. A prescale
// target is checked by its read-back when made.
#include "oa/app/scaled_world.hpp"

#include "oa/present/world_renderer/scene_filter.hpp"
#include "oa/test/check.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

namespace {

/// Window-size textures as tiles beyond the renderer's limit.
namespace layer_tiles {
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

} // namespace layer_tiles

/// The accelerated tier's drawing on the card.
namespace card {
namespace wr = oa::present::world_renderer;
using oa::app::CardScale;
using oa::app::render_policy::ScaleFilter;

/// The surface the renderer draws on, and the battlefield on it.
constexpr int surface_width = 400;
constexpr int surface_height = 300;
constexpr SDL_Rect battlefield{10, 12, 300, 200};

/// The scene's texture: room for the scene at every zoom drawn here.
constexpr uint32_t scene_texture_width = 320;
constexpr uint32_t scene_texture_height = 240;

/// Seeds of the scene and the sources.
constexpr uint32_t seed_scene = 0x243F6A88U;
constexpr uint32_t seed_source = 0x85A308D3U;
constexpr uint32_t seed_overlay = 0x13198A2EU;

/// Most a channel of the renderer's picture may differ from the reference,
/// and the most the mean difference may be. SDL's software renderer draws
/// LINEAR exactly as software_linear_rgb24 models it on a processor with
/// SSE2 or NEON; a build of it without either truncates after each of its
/// two passes and can give one level less.
constexpr int most_difference = 2;
constexpr double most_mean_difference = 0.5;

/// The tile limit the tiled cases plan with: tiles of 64 texels, gutters included.
constexpr uint32_t small_limit = 64;

/// The tiled prescale cases: a target split at its 65th texel across and
/// down (tiles of 66 texels, gutters included), whose edge lands on a whole
/// pixel at a reduction by 0.8, where the software renderer, which places
/// each draw at whole pixels, steps through each tile as through the whole
/// target; and a scene split in two by tiles of 40 texels.
constexpr uint32_t prescale_tile_limit = 66;
constexpr uint32_t scene_tile_limit = 40;
/// The tiled scene's texture, and the corner of it magnified 1.6 times,
/// by a factor of 2, to whole pixels.
constexpr uint32_t tiled_scene_edge = 64;
constexpr uint32_t tiled_corner_edge = 50;
constexpr int tiled_corner_landed = 80;
/// The source sharp_draw enlarges 2.4 times, by a factor of 3, through a
/// tiled target whose tile edge cuts a block of 3 texels.
constexpr uint32_t tiled_source_edge = 40;
constexpr int tiled_source_landed = 96;

/// A 32-bit xorshift sequence, the same on every platform.
struct Random {
    uint32_t state{};

    /// Returns the next number of the sequence.
    ///
    /// @return 32 bits
    uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
};

/// RGB pixels, rows one after another.
struct Picture {
    uint32_t width{};
    uint32_t height{};
    std::vector<uint8_t> rgb;

    /// Returns the pixels as a scene.
    ///
    /// @return a read-only view
    [[nodiscard]] wr::RgbSource source() const { return {rgb.data(), width, height, width}; }

    /// Returns the top-left corner of the pixels as a scene.
    ///
    /// @param columns the corner's columns
    /// @param rows the corner's rows
    /// @return a read-only view
    [[nodiscard]] wr::RgbSource corner(uint32_t columns, uint32_t rows) const {
        return {rgb.data(), columns, rows, width};
    }

    /// Returns the pixels as a picture.
    ///
    /// @return a writable view
    [[nodiscard]] wr::RgbTarget target() { return {rgb.data(), width, height, width}; }
};

/// Returns a picture of seeded levels.
///
/// @param seed the sequence's seed
/// @param width columns
/// @param height rows
/// @return the picture
Picture seeded(uint32_t seed, uint32_t width, uint32_t height) {
    Random random{seed};
    Picture picture{width, height, std::vector<uint8_t>(std::size_t{width} * height * 3U)};
    for (auto& byte : picture.rgb)
        byte = static_cast<uint8_t>(random.next() >> 24);
    return picture;
}

/// The software renderer over a surface, made for each case.
struct Canvas {
    SDL_Surface* surface{};
    SDL_Renderer* renderer{};

    Canvas() {
        surface = SDL_CreateSurface(surface_width, surface_height, SDL_PIXELFORMAT_XRGB8888);
        renderer = surface != nullptr ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        OA_CHECK(renderer != nullptr);
    }

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    ~Canvas() {
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
    }

    /// Clears the surface to black.
    void clear() const {
        OA_CHECK(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
        OA_CHECK(SDL_RenderClear(renderer));
    }

    /// Reads the surface back.
    ///
    /// @return its pixels
    [[nodiscard]] Picture read() const {
        Picture picture{surface_width, surface_height, {}};
        SDL_Surface* read = SDL_RenderReadPixels(renderer, nullptr);
        SDL_Surface* rgb =
            read != nullptr ? SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGB24) : nullptr;
        SDL_DestroySurface(read);
        OA_CHECK(rgb != nullptr);
        if (rgb == nullptr)
            return picture;
        picture.rgb.resize(std::size_t{surface_width} * surface_height * 3U);
        for (int row = 0; row < surface_height; ++row)
            std::memcpy(
                picture.rgb.data() + static_cast<std::size_t>(row) * surface_width * 3U,
                static_cast<const uint8_t*>(rgb->pixels) +
                    static_cast<std::ptrdiff_t>(row) * rgb->pitch,
                std::size_t{surface_width} * 3U
            );
        SDL_DestroySurface(rgb);
        return picture;
    }
};

/// How far a read-back is from a reference over a rectangle.
struct Difference {
    int most{};
    double mean{};
};

/// Compares a read-back with a reference over a rectangle of both.
///
/// @param read the read-back
/// @param reference the reference, the same size
/// @param area the rectangle compared
/// @return the largest and the mean difference of a channel
Difference compare(const Picture& read, const Picture& reference, const SDL_Rect& area) {
    Difference difference;
    double sum = 0.0;
    std::size_t count = 0;
    for (int y = area.y; y < area.y + area.h; ++y)
        for (int x = area.x; x < area.x + area.w; ++x)
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const auto at = (static_cast<std::size_t>(y) * read.width + x) * 3U + channel;
                const int delta = std::abs(int{read.rgb[at]} - int{reference.rgb[at]});
                difference.most = std::max(difference.most, delta);
                sum += delta;
                ++count;
            }
    difference.mean = count != 0 ? sum / static_cast<double>(count) : 0.0;
    return difference;
}

/// The corner of the scene a zoom shows on the battlefield, and where it lands.
struct Corner {
    uint32_t width{};
    uint32_t height{};
    SDL_Rect destination{};
};

/// Returns the corner a zoom shows: the battlefield's map pixels rounded up,
/// landing from the battlefield's corner at that many times the zoom, rounded.
///
/// @param zoom screen pixels per scene pixel
/// @return the corner
Corner corner_at(double zoom) {
    Corner corner;
    corner.width = static_cast<uint32_t>(std::ceil(battlefield.w / zoom));
    corner.height = static_cast<uint32_t>(std::ceil(battlefield.h / zoom));
    corner.destination = {
        battlefield.x,
        battlefield.y,
        static_cast<int>(std::lround(corner.width * zoom)),
        static_cast<int>(std::lround(corner.height * zoom))
    };
    return corner;
}

/// Returns the reference of a corner drawn into the surface: black, and the
/// filter's reference within the battlefield.
///
/// @param scene the scene
/// @param corner the corner and where it lands
/// @param filter the reference's filter
/// @param factor the prescale factor for sharp-bilinear
/// @return the reference picture of the surface
Picture
reference_of(const Picture& scene, const Corner& corner, ScaleFilter filter, uint32_t factor) {
    Picture reference{
        surface_width,
        surface_height,
        std::vector<uint8_t>(std::size_t{surface_width} * surface_height * 3U, 0)
    };
    Picture drawn{surface_width, surface_height, reference.rgb};
    const wr::ScenePlacement placement{
        static_cast<double>(corner.destination.w) / corner.width,
        static_cast<double>(corner.destination.h) / corner.height,
        static_cast<double>(corner.destination.x),
        static_cast<double>(corner.destination.y)
    };
    const auto source = scene.corner(corner.width, corner.height);
    switch (filter) {
    case ScaleFilter::nearest:
    case ScaleFilter::pixelart:
        wr::nearest_rgb24(source, placement, drawn.target());
        break;
    case ScaleFilter::linear:
        oa::app::software_linear_rgb24(source, 1, corner.destination, drawn.target());
        break;
    case ScaleFilter::sharp_bilinear:
        oa::app::software_linear_rgb24(source, factor, corner.destination, drawn.target());
        break;
    }
    for (int y = battlefield.y; y < battlefield.y + battlefield.h; ++y)
        std::memcpy(
            reference.rgb.data() +
                (static_cast<std::size_t>(y) * surface_width + battlefield.x) * 3U,
            drawn.rgb.data() + (static_cast<std::size_t>(y) * surface_width + battlefield.x) * 3U,
            static_cast<std::size_t>(battlefield.w) * 3U
        );
    return reference;
}

/// Uploads the scene into a tiled texture of the largest size.
///
/// @param canvas the renderer
/// @param scene the scene
/// @param limit the texture limit to tile at
/// @param counts the counts the texture adds to
/// @param[out] texture the texture
void upload_scene(
    const Canvas& canvas,
    const Picture& scene,
    uint32_t limit,
    oa::app::ScaledWorldCounts& counts,
    oa::app::TiledTexture& texture
) {
    texture.create(
        canvas.renderer,
        scene_texture_width,
        scene_texture_height,
        limit,
        SDL_BLENDMODE_NONE,
        counts
    );
    texture.upload_rgb24(
        scene.rgb.data(), scene.width * 3U, scene.width, scene.height, nullptr, nullptr
    );
}

/// Magnifies a seeded scene into the battlefield by each filter and zoom
/// and compares the read-back with the references.
void test_draw_scaled_world_matches_the_references() {
    const Picture scene = seeded(seed_scene, 240, 160);

    struct Case {
        double zoom{};
        CardScale scale{};
    };

    const Case cases[] = {
        {1.37, {ScaleFilter::sharp_bilinear, 2}},
        {1.37, {ScaleFilter::linear, 1}},
        {2.0, {ScaleFilter::nearest, 1}},
        {4.0, {ScaleFilter::nearest, 1}},
    };
    for (const auto& item : cases) {
        Canvas canvas;
        oa::app::ScaledWorldCounts counts;
        oa::app::TiledTexture texture;
        oa::app::PrescaleTarget prescale;
        upload_scene(canvas, scene, 0, counts, texture);
        const Corner corner = corner_at(item.zoom);
        if (item.scale.filter == ScaleFilter::sharp_bilinear)
            prescale.ensure(
                canvas.renderer,
                nullptr,
                (corner.width + 1) * item.scale.factor,
                (corner.height + 1) * item.scale.factor,
                0,
                counts
            );
        canvas.clear();
        oa::app::draw_scaled_world(
            canvas.renderer,
            nullptr,
            texture,
            scene.width,
            scene.height,
            corner.width,
            corner.height,
            corner.destination,
            battlefield,
            item.scale,
            prescale,
            counts
        );
        const Picture read = canvas.read();
        const Picture reference = reference_of(scene, corner, item.scale.filter, item.scale.factor);
        // The whole surface: outside the battlefield the clip leaves black.
        const SDL_Rect whole{0, 0, surface_width, surface_height};
        const auto difference = compare(read, reference, whole);
        std::printf(
            "draw_scaled_world at %.2f by filter %d: most %d, mean %.3f\n",
            item.zoom,
            static_cast<int>(item.scale.filter),
            difference.most,
            difference.mean
        );
        if (item.scale.filter == ScaleFilter::nearest)
            OA_CHECK(difference.most == 0);
        OA_CHECK(difference.most <= most_difference);
        OA_CHECK(difference.mean <= most_mean_difference);
        OA_CHECK(
            counts.prescale_draws == (item.scale.filter == ScaleFilter::sharp_bilinear ? 1U : 0U)
        );
    }
}

/// Draws a seeded source by sharp_draw at each scale, through the prescale
/// target where the scale is not whole and above 1, and compares.
void test_sharp_draw_matches_the_references() {
    const Picture source = seeded(seed_source, 160, 120);
    const double scales[] = {0.5, 0.6, 0.75, 1.37, 2.0, 4.0};
    for (const double scale : scales) {
        Canvas canvas;
        oa::app::ScaledWorldCounts counts;
        oa::app::PrescaleTarget prescale;
        SDL_Texture* texture = SDL_CreateTexture(
            canvas.renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 160, 120
        );
        OA_CHECK(texture != nullptr);
        OA_CHECK(SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST));
        OA_CHECK(SDL_UpdateTexture(texture, nullptr, source.rgb.data(), 160 * 3));
        const bool whole = std::floor(scale) == scale;
        const auto factor = static_cast<uint32_t>(std::ceil(scale));
        const CardScale card{
            whole ? ScaleFilter::nearest
                  : (factor > 1 ? ScaleFilter::sharp_bilinear : ScaleFilter::linear),
            whole ? 1U : factor
        };
        if (card.filter == ScaleFilter::sharp_bilinear)
            prescale.ensure(canvas.renderer, nullptr, 160 * factor, 120 * factor, 0, counts);
        const int width = static_cast<int>(std::lround(160 * scale));
        const int height = static_cast<int>(std::lround(120 * scale));
        const Corner corner{
            160,
            120,
            {7, 5, std::min(width, surface_width - 7), std::min(height, surface_height - 5)}
        };
        // Draw the source whole, clipped by the surface.
        const oa::app::SharpPart part{
            {0.0F, 0.0F, 160.0F, 120.0F},
            {7.0F, 5.0F, static_cast<float>(width), static_cast<float>(height)}
        };
        canvas.clear();
        oa::app::sharp_draw(
            canvas.renderer, nullptr, texture, 160, 120, {&part, 1}, card, 1, prescale, counts
        );
        const Picture read = canvas.read();
        Picture reference{
            surface_width,
            surface_height,
            std::vector<uint8_t>(std::size_t{surface_width} * surface_height * 3U, 0)
        };
        const wr::ScenePlacement placement{
            static_cast<double>(width) / 160.0, static_cast<double>(height) / 120.0, 7.0, 5.0
        };
        Picture drawn = reference;
        const SDL_Rect landed{7, 5, width, height};
        switch (card.filter) {
        case ScaleFilter::nearest:
            wr::nearest_rgb24(source.source(), placement, drawn.target());
            break;
        case ScaleFilter::linear:
            oa::app::software_linear_rgb24(source.source(), 1, landed, drawn.target());
            break;
        default:
            oa::app::software_linear_rgb24(source.source(), factor, landed, drawn.target());
            break;
        }
        for (int y = 5; y < 5 + corner.destination.h; ++y)
            std::memcpy(
                reference.rgb.data() + (static_cast<std::size_t>(y) * surface_width + 7) * 3U,
                drawn.rgb.data() + (static_cast<std::size_t>(y) * surface_width + 7) * 3U,
                static_cast<std::size_t>(corner.destination.w) * 3U
            );
        const auto difference = compare(read, reference, {0, 0, surface_width, surface_height});
        std::printf(
            "sharp_draw at %.2f: most %d, mean %.3f\n", scale, difference.most, difference.mean
        );
        if (whole)
            OA_CHECK(difference.most == 0);
        OA_CHECK(difference.most <= most_difference);
        OA_CHECK(difference.mean <= most_mean_difference);
        // A whole-number scale makes no prescale target and draws none.
        if (whole)
            OA_CHECK(!prescale.made() && counts.prescale_draws == 0);
        // The source left NEAREST, as the standard tier draws it.
        SDL_ScaleMode mode = SDL_SCALEMODE_LINEAR;
        OA_CHECK(SDL_GetTextureScaleMode(texture, &mode) && mode == SDL_SCALEMODE_NEAREST);
        SDL_DestroyTexture(texture);
    }
}

/// Checks that sharp_draw draws its source into the prescale target only
/// when the source's revision or the factor changes.
void test_sharp_draw_redraws_only_a_new_revision() {
    Canvas canvas;
    const Picture source = seeded(seed_source, 64, 48);
    oa::app::ScaledWorldCounts counts;
    oa::app::PrescaleTarget prescale;
    SDL_Texture* texture = SDL_CreateTexture(
        canvas.renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 64, 48
    );
    OA_CHECK(texture != nullptr && SDL_UpdateTexture(texture, nullptr, source.rgb.data(), 64 * 3));
    prescale.ensure(canvas.renderer, nullptr, 64 * 3, 48 * 3, 0, counts);
    OA_CHECK(counts.textures_created == 1);
    const oa::app::SharpPart part{{0.0F, 0.0F, 64.0F, 48.0F}, {0.0F, 0.0F, 160.0F, 120.0F}};
    const CardScale card{ScaleFilter::sharp_bilinear, 3};
    for (const uint64_t revision : {5U, 5U, 5U, 6U, 6U})
        oa::app::sharp_draw(
            canvas.renderer, nullptr, texture, 64, 48, {&part, 1}, card, revision, prescale, counts
        );
    OA_CHECK(counts.prescale_draws == 2);
    // A smaller factor within the target draws again.
    oa::app::sharp_draw(
        canvas.renderer,
        nullptr,
        texture,
        64,
        48,
        {&part, 1},
        {ScaleFilter::sharp_bilinear, 2},
        6,
        prescale,
        counts
    );
    OA_CHECK(counts.prescale_draws == 3);
    // Asking for the target at its size again makes nothing new.
    prescale.ensure(canvas.renderer, nullptr, 64 * 3, 48 * 3, 0, counts);
    OA_CHECK(counts.textures_created == 1 && counts.textures_destroyed == 0);
    prescale.destroy();
    OA_CHECK(counts.textures_destroyed == 1);
    SDL_DestroyTexture(texture);
}

/// Checks that PIXELART draws as NEAREST on SDL's software renderer, which
/// lacks the filter, and that the probe finds it missing there.
void test_pixelart_is_nearest_here() {
    Canvas canvas;
    OA_CHECK(!oa::app::probe_pixelart(canvas.renderer, nullptr));
    const Picture scene = seeded(seed_scene, 240, 160);
    const Corner corner = corner_at(1.37);
    std::vector<Picture> reads;
    for (const auto filter : {ScaleFilter::pixelart, ScaleFilter::nearest}) {
        oa::app::ScaledWorldCounts counts;
        oa::app::TiledTexture texture;
        oa::app::PrescaleTarget prescale;
        upload_scene(canvas, scene, 0, counts, texture);
        canvas.clear();
        oa::app::draw_scaled_world(
            canvas.renderer,
            nullptr,
            texture,
            scene.width,
            scene.height,
            corner.width,
            corner.height,
            corner.destination,
            battlefield,
            {filter, 1},
            prescale,
            counts
        );
        reads.push_back(canvas.read());
    }
    OA_CHECK(reads[0].rgb == reads[1].rgb);
}

/// Checks a scene split into tiles: each tile holds the texels of its part
/// of the scene, gutters included, and the tiles draw what the whole
/// texture draws, at 1 and at 2.
void test_tiles_hold_their_gutters() {
    const Picture scene = seeded(seed_scene, 240, 160);
    Canvas canvas;
    oa::app::ScaledWorldCounts counts;
    oa::app::TiledTexture tiled;
    upload_scene(canvas, scene, small_limit, counts, tiled);
    const auto& grid = tiled.grid();
    OA_CHECK(grid.columns > 1 && grid.rows > 1);
    OA_CHECK(counts.textures_created == uint64_t{grid.columns} * grid.rows);
    // Each tile drawn alone 1:1 shows the scene's texels its texture rectangle covers.
    bool gutters = true;
    for (uint32_t row = 0; row < grid.rows; ++row)
        for (uint32_t column = 0; column < grid.columns; ++column) {
            const auto tile = oa::app::render_policy::tile_at(grid, column, row);
            if (tile.texture.x + tile.texture.width > scene.width ||
                tile.texture.y + tile.texture.height > scene.height)
                continue;
            canvas.clear();
            const SDL_FRect whole{
                0.0F,
                0.0F,
                static_cast<float>(tile.texture.width),
                static_cast<float>(tile.texture.height)
            };
            OA_CHECK(
                SDL_RenderTexture(canvas.renderer, tiled.tile_texture(column, row), nullptr, &whole)
            );
            const Picture read = canvas.read();
            for (uint32_t y = 0; y < tile.texture.height; ++y)
                gutters =
                    gutters &&
                    std::memcmp(
                        read.rgb.data() + std::size_t{y} * surface_width * 3U,
                        scene.rgb.data() +
                            ((std::size_t{tile.texture.y} + y) * scene.width + tile.texture.x) * 3U,
                        std::size_t{tile.texture.width} * 3U
                    ) == 0;
        }
    OA_CHECK(gutters);
    oa::app::TiledTexture whole;
    upload_scene(canvas, scene, 0, counts, whole);
    OA_CHECK(whole.grid().columns == 1 && whole.grid().rows == 1);
    for (const double zoom : {1.0, 2.0}) {
        const Corner corner = corner_at(zoom);
        std::vector<Picture> reads;
        for (auto* texture : {&tiled, &whole}) {
            oa::app::PrescaleTarget prescale;
            canvas.clear();
            oa::app::draw_scaled_world(
                canvas.renderer,
                nullptr,
                *texture,
                scene.width,
                scene.height,
                corner.width,
                corner.height,
                corner.destination,
                battlefield,
                {ScaleFilter::nearest, 1},
                prescale,
                counts
            );
            reads.push_back(canvas.read());
        }
        OA_CHECK(reads[0].rgb == reads[1].rgb);
    }
    const uint64_t made = counts.textures_created;
    tiled.reset();
    whole.reset();
    OA_CHECK(counts.textures_destroyed == made);
}

/// Reads back what a target texture holds.
///
/// @param canvas the renderer
/// @param texture a render target of it
/// @param width the target's texels across
/// @param height its texels down
/// @return its pixels
Picture read_target(const Canvas& canvas, SDL_Texture* texture, uint32_t width, uint32_t height) {
    Picture picture{width, height, std::vector<uint8_t>(std::size_t{width} * height * 3U, 0)};
    OA_CHECK(SDL_SetRenderTarget(canvas.renderer, texture));
    SDL_Surface* read = SDL_RenderReadPixels(canvas.renderer, nullptr);
    SDL_Surface* rgb = read != nullptr ? SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGB24) : nullptr;
    SDL_DestroySurface(read);
    OA_CHECK(SDL_SetRenderTarget(canvas.renderer, nullptr));
    OA_CHECK(
        rgb != nullptr && rgb->w == static_cast<int>(width) && rgb->h == static_cast<int>(height)
    );
    if (rgb == nullptr || rgb->w != static_cast<int>(width) || rgb->h != static_cast<int>(height)) {
        SDL_DestroySurface(rgb);
        return picture;
    }
    for (uint32_t row = 0; row < height; ++row)
        std::memcpy(
            picture.rgb.data() + std::size_t{row} * width * 3U,
            static_cast<const uint8_t*>(rgb->pixels) +
                static_cast<std::ptrdiff_t>(row) * rgb->pitch,
            std::size_t{width} * 3U
        );
    SDL_DestroySurface(rgb);
    return picture;
}

/// Checks that each tile of a prescale target holds the corner of a
/// picture enlarged by nearest replication, gutters included.
///
/// @param canvas the renderer
/// @param prescale the target, its corner filled
/// @param picture the picture
/// @param factor target texels per picture texel
/// @param filled the target's texels filled across and down
void check_prescale_tiles(
    const Canvas& canvas,
    const oa::app::PrescaleTarget& prescale,
    const Picture& picture,
    uint32_t factor,
    uint32_t filled
) {
    const auto& grid = prescale.grid();
    bool held = true;
    for (uint32_t row = 0; row < grid.rows; ++row)
        for (uint32_t column = 0; column < grid.columns; ++column) {
            const auto tile = oa::app::render_policy::tile_at(grid, column, row);
            const Picture read = read_target(
                canvas, prescale.tile_texture(column, row), tile.texture.width, tile.texture.height
            );
            for (uint32_t y = 0; y < tile.texture.height; ++y)
                for (uint32_t x = 0; x < tile.texture.width; ++x) {
                    const uint32_t texel_x = tile.texture.x + x;
                    const uint32_t texel_y = tile.texture.y + y;
                    if (texel_x >= filled || texel_y >= filled)
                        continue;
                    held = held &&
                           std::memcmp(
                               read.rgb.data() + (std::size_t{y} * read.width + x) * 3U,
                               picture.rgb.data() + (std::size_t{texel_y / factor} * picture.width +
                                                     texel_x / factor) *
                                                        3U,
                               3
                           ) == 0;
                }
        }
    OA_CHECK(held);
}

/// Checks prescale targets split into tiles: a seeded scene, itself split
/// into tiles, magnified 1.6 times by sharp-bilinear through a target of
/// tiles holds the enlarged scene in every tile and draws what one target
/// draws; and a seeded source drawn 2.4 times by sharp_draw, whose tile
/// edges cut a block of the enlargement, likewise. Both are the renderer's
/// own LINEAR of the enlarged picture.
void test_prescale_tiles_draw_as_one_target() {
    {
        const Picture scene = seeded(seed_scene, tiled_scene_edge, tiled_scene_edge);
        const Corner corner{
            tiled_corner_edge,
            tiled_corner_edge,
            {battlefield.x, battlefield.y, tiled_corner_landed, tiled_corner_landed}
        };
        constexpr uint32_t factor = 2;
        const uint32_t filled = (tiled_corner_edge + 1) * factor;
        std::vector<Picture> reads;
        for (const uint32_t limit : {prescale_tile_limit, 0U}) {
            Canvas canvas;
            oa::app::ScaledWorldCounts counts;
            oa::app::TiledTexture texture;
            texture.create(
                canvas.renderer,
                tiled_scene_edge,
                tiled_scene_edge,
                scene_tile_limit,
                SDL_BLENDMODE_NONE,
                counts
            );
            OA_CHECK(texture.grid().columns == 2 && texture.grid().rows == 2);
            texture.upload_rgb24(
                scene.rgb.data(), scene.width * 3U, scene.width, scene.height, nullptr, nullptr
            );
            oa::app::PrescaleTarget prescale;
            prescale.ensure(canvas.renderer, nullptr, filled, filled, limit, counts);
            const uint32_t tiles = limit == 0 ? 1U : 2U;
            OA_CHECK(prescale.grid().columns == tiles && prescale.grid().rows == tiles);
            canvas.clear();
            oa::app::draw_scaled_world(
                canvas.renderer,
                nullptr,
                texture,
                scene.width,
                scene.height,
                corner.width,
                corner.height,
                corner.destination,
                battlefield,
                {ScaleFilter::sharp_bilinear, factor},
                prescale,
                counts
            );
            OA_CHECK(counts.prescale_draws == 1);
            reads.push_back(canvas.read());
            check_prescale_tiles(canvas, prescale, scene, factor, filled);
        }
        OA_CHECK(reads[0].rgb == reads[1].rgb);
        const auto difference = compare(
            reads[0],
            reference_of(scene, corner, ScaleFilter::sharp_bilinear, factor),
            {0, 0, surface_width, surface_height}
        );
        std::printf(
            "draw_scaled_world through prescale tiles: most %d, mean %.3f\n",
            difference.most,
            difference.mean
        );
        OA_CHECK(difference.most <= most_difference);
        OA_CHECK(difference.mean <= most_mean_difference);
    }
    {
        const Picture source = seeded(seed_source, tiled_source_edge, tiled_source_edge);
        constexpr uint32_t factor = 3;
        const uint32_t filled = tiled_source_edge * factor;
        const SDL_Rect landed{7, 5, tiled_source_landed, tiled_source_landed};
        const oa::app::SharpPart part{
            {0.0F,
             0.0F,
             static_cast<float>(tiled_source_edge),
             static_cast<float>(tiled_source_edge)},
            {static_cast<float>(landed.x),
             static_cast<float>(landed.y),
             static_cast<float>(landed.w),
             static_cast<float>(landed.h)}
        };
        std::vector<Picture> reads;
        for (const uint32_t limit : {prescale_tile_limit, 0U}) {
            Canvas canvas;
            oa::app::ScaledWorldCounts counts;
            SDL_Texture* texture = SDL_CreateTexture(
                canvas.renderer,
                SDL_PIXELFORMAT_RGB24,
                SDL_TEXTUREACCESS_STREAMING,
                static_cast<int>(tiled_source_edge),
                static_cast<int>(tiled_source_edge)
            );
            OA_CHECK(texture != nullptr);
            OA_CHECK(SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST));
            OA_CHECK(SDL_UpdateTexture(
                texture, nullptr, source.rgb.data(), static_cast<int>(tiled_source_edge * 3U)
            ));
            oa::app::PrescaleTarget prescale;
            prescale.ensure(canvas.renderer, nullptr, filled, filled, limit, counts);
            const uint32_t tiles = limit == 0 ? 1U : 2U;
            OA_CHECK(prescale.grid().columns == tiles && prescale.grid().rows == tiles);
            canvas.clear();
            oa::app::sharp_draw(
                canvas.renderer,
                nullptr,
                texture,
                tiled_source_edge,
                tiled_source_edge,
                {&part, 1},
                {ScaleFilter::sharp_bilinear, factor},
                1,
                prescale,
                counts
            );
            reads.push_back(canvas.read());
            check_prescale_tiles(canvas, prescale, source, factor, filled);
            SDL_DestroyTexture(texture);
        }
        OA_CHECK(reads[0].rgb == reads[1].rgb);
        Picture reference{
            surface_width,
            surface_height,
            std::vector<uint8_t>(std::size_t{surface_width} * surface_height * 3U, 0)
        };
        oa::app::software_linear_rgb24(source.source(), factor, landed, reference.target());
        const auto difference = compare(reads[0], reference, {0, 0, surface_width, surface_height});
        std::printf(
            "sharp_draw through prescale tiles: most %d, mean %.3f\n",
            difference.most,
            difference.mean
        );
        OA_CHECK(difference.most <= most_difference);
        OA_CHECK(difference.mean <= most_mean_difference);
    }
}

/// Checks the overlay drawn 1:1 with blending over a picture: it replaces
/// the picture where opaque and leaves it where transparent, as the overlay
/// rule says; also split into tiles.
void test_overlay_over_a_picture() {
    const Picture picture = seeded(seed_scene, battlefield.w, battlefield.h);
    Random random{seed_overlay};
    std::vector<uint32_t> overlay(
        std::size_t{static_cast<uint32_t>(battlefield.w)} * battlefield.h, 0
    );
    for (auto& word : overlay)
        if (random.next() % 7U == 0)
            word = 0xFF000000U | (random.next() & 0xFFFFFFU);
    Picture expected = picture;
    wr::overlay_rgb24(expected.target(), overlay.data(), static_cast<uint32_t>(battlefield.w));
    for (const uint32_t limit : {0U, small_limit}) {
        Canvas canvas;
        oa::app::ScaledWorldCounts counts;
        oa::app::TiledTexture under;
        under.create(canvas.renderer, battlefield.w, battlefield.h, 0, SDL_BLENDMODE_NONE, counts);
        under.upload_rgb24(
            picture.rgb.data(), picture.width * 3U, picture.width, picture.height, nullptr, nullptr
        );
        oa::app::TiledTexture over;
        over.create(
            canvas.renderer, battlefield.w, battlefield.h, limit, SDL_BLENDMODE_BLEND, counts
        );
        // Uploaded in two runs of rows, as dirty bands are.
        over.update(reinterpret_cast<const uint8_t*>(overlay.data()), battlefield.w * 4, 4, 0, 64);
        over.update(
            reinterpret_cast<const uint8_t*>(overlay.data()),
            battlefield.w * 4,
            4,
            64,
            static_cast<uint32_t>(battlefield.h)
        );
        canvas.clear();
        const SDL_FRect landed{
            static_cast<float>(battlefield.x),
            static_cast<float>(battlefield.y),
            static_cast<float>(battlefield.w),
            static_cast<float>(battlefield.h)
        };
        under.draw(canvas.renderer, nullptr, &landed);
        over.draw(canvas.renderer, nullptr, &landed);
        const Picture read = canvas.read();
        bool same = true;
        for (int y = 0; y < battlefield.h; ++y)
            same =
                same && std::memcmp(
                            read.rgb.data() +
                                ((static_cast<std::size_t>(y) + battlefield.y) * surface_width +
                                 battlefield.x) *
                                    3U,
                            expected.rgb.data() + static_cast<std::size_t>(y) * battlefield.w * 3U,
                            static_cast<std::size_t>(battlefield.w) * 3U
                        ) == 0;
        OA_CHECK(same);
    }
}

} // namespace card

} // namespace

int main() {
    layer_tiles::tiles_draw_as_one_texture();
    layer_tiles::gutters_hold_neighbours_edges();
    layer_tiles::alpha_layers_keep_their_blending();
    if (!SDL_Init(0)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }
    card::test_draw_scaled_world_matches_the_references();
    card::test_sharp_draw_matches_the_references();
    card::test_sharp_draw_redraws_only_a_new_revision();
    card::test_pixelart_is_nearest_here();
    card::test_tiles_hold_their_gutters();
    card::test_prescale_tiles_draw_as_one_target();
    card::test_overlay_over_a_picture();
    SDL_Quit();
    const int status = oa::test::check_exit_status();
    if (status == 0)
        std::puts("scaled world: tiles beyond the limit draw as one texture would");
    return status;
}
