// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Full tier's presentation: the terrain drawn by the graphics card from
// the map's terrain atlas, built and uploaded as the match loads as texture
// pages with their levels, by the level rule of full_terrain.hpp, and then
// the stages of the scene builder (runtime_full.hpp) that are switched on,
// the sprites so far; everything else the processor still draws, at the
// zoom over a terrain base filled by the nearest fill, and the card
// composes it over its own terrain through overlays of what differs from
// that base: with a stage on, what the bands drew and the fog's gray under
// the stages' batches, and what the fog's black and the painters changed
// over them. With Enhanced anti-aliasing on, the card draws its terrain and
// its stages into a world target at the row's supersample factor
// (full_supersampling.hpp), within the step-down's rung, the budget S and
// the texture limit, and reduces it to the window: by exact halvings from
// zoom 1 up, by the two-level blend of the view drawn at zoom 1 below; the
// processor's anti-aliasing never runs, and the processor's overlays go
// over the reduced picture. The planner, the HUD, the painters and the
// readers that keep a picture run as in the Basic tier. It is reached only
// when the tier decided for the frame is Full.
#include "oa/app/runtime.hpp"

#include "full_presentation.hpp"
#include "graphics_report.hpp"
#include "match_models.hpp"
#include "render_host.hpp"
#include "render_run.hpp"
#include "runtime_full.hpp"
#include "xrgb_conversion.hpp"

#include "oa/app/renderer_records.hpp"

#include "oa/base/float_precision.hpp"
#include "oa/platform/machine.hpp"
#include "oa/present/world_renderer/world_fog.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace oa::app {

namespace {

namespace gw = oa::present::gpu_world;
namespace ft = full_terrain;
namespace policy = render_policy;

/// What --render-fault card makes the card's frame fail with.
constexpr std::string_view forced_card_failure =
    "the card refused the terrain frame: forced by --render-fault card";
/// What the log says when Full's pages must wait for a shared game or a
/// replay to end.
constexpr std::string_view full_waits_message =
    "open-annihilation: graphics: the full tier's pages cannot be made during a shared game "
    "or a replay; the basic tier draws the battlefield until it ends";
/// Bytes a texel of a page or a target holds.
constexpr uint64_t bytes_per_texel = card::texel_bytes;

/// The edge of the page the Full function test draws, and of the target,
/// which holds the test's four batches: the page's level 0 at the top
/// left, the blended quad right of it, the page's level 1 enlarged below
/// it and the triangle over the rest.
constexpr uint32_t test_page_edge = 8;
constexpr uint32_t test_target_edge = 64;
/// Most a channel of the function test's blended quad may differ from the
/// blend computed exactly, and a channel of its level 1 drawn LINEAR from
/// the enlargement computed exactly: the renderer rounds its own way.
constexpr int test_blend_most_difference = 2;
/// The colour the function test clears its target to, and the alpha of the
/// quad it blends over it.
constexpr card::Colour test_clear_colour{0.2F, 0.4F, 0.6F, 1.0F};
constexpr float test_blend_alpha = 0.5F;
/// The corners of the function test's triangle, on whole pixels past the
/// page's and the blended quad's squares, with red at the first, green at
/// the second and blue at the third. The pixel at (test_centroid_x,
/// test_centroid_y) holds their centroid and is to show their mean, a
/// third of full in every channel, within test_centroid_most_difference:
/// the pixel's centre lies a sixth of a pixel from the centroid, so the
/// triangle's legs are long enough, 56 pixels, that the colours change by
/// under 2 levels over that, and a renderer that keeps whole levels, as
/// SDL's software renderer does, truncates one more at most.
constexpr std::array<std::array<float, 2>, 3> test_triangle{
    {{8.0F, 8.0F}, {64.0F, 8.0F}, {8.0F, 64.0F}}
};
constexpr uint32_t test_centroid_x = 26;
constexpr uint32_t test_centroid_y = 26;
constexpr int test_centroid_mean = 85;
constexpr int test_centroid_most_difference = 4;
/// Levels of the test page: level 0 and its exact box, level 1.
constexpr uint8_t test_page_levels = 2;
/// Channels of a texel the read-back compares: red, green and blue.
constexpr std::size_t compared_channels = 3;

/// Returns the nanoseconds since a moment of the steady clock.
///
/// @param since the moment
/// @return nanoseconds
[[nodiscard]] uint64_t nanoseconds_since(std::chrono::steady_clock::time_point since) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - since
    )
                                     .count());
}

/// Entries of the display gamma's table and of the gray table.
constexpr std::size_t table_entries = 256;

/// Rounds a count up to a multiple of the target's grain.
///
/// @param pixels the count
/// @return the multiple
[[nodiscard]] uint32_t target_extent(uint32_t pixels) noexcept {
    return (pixels + full_target_grain - 1U) / full_target_grain * full_target_grain;
}

/// Appends an untextured triangle, each corner in its own colour.
///
/// @param[in,out] frame the frame
/// @param corners the corners, in pixels of the target
/// @param colours the corners' colours
void append_triangle(
    card::CardFrame& frame,
    const std::array<std::array<float, 2>, 3>& corners,
    const std::array<card::Colour, 3>& colours
) {
    const auto first = static_cast<card::Index>(frame.vertices.size());
    for (std::size_t corner = 0; corner < corners.size(); ++corner)
        frame.vertices.push_back(
            {corners[corner][0], corners[corner][1], colours[corner], 0.0F, 0.0F}
        );
    frame.indices.push_back(first);
    frame.indices.push_back(first + 1);
    frame.indices.push_back(first + 2);
}

/// Returns a channel of a small picture enlarged twice as the card's LINEAR
/// sampling enlarges it: the pixel's centre lands a quarter of a texel
/// before the texel's centre or after it, and the two texels around it are
/// weighted by that quarter, clamped at the edge, rounded to the nearest.
///
/// @param texels the picture, texel_bytes a texel, row by row
/// @param edge the picture's texels a side
/// @param x the pixel's column in the enlargement, below 2 * edge
/// @param y the pixel's row in the enlargement
/// @param channel the channel
/// @return the level
[[nodiscard]] int enlarged_twice_linear(
    const uint8_t* texels, uint32_t edge, uint32_t x, uint32_t y, std::size_t channel
) {
    const auto last = static_cast<double>(edge - 1U);
    const auto axis = [&](uint32_t pixel, uint32_t& first, uint32_t& second, double& toward) {
        const double sample = std::clamp(static_cast<double>(pixel) / 2.0 - 0.25, 0.0, last);
        first = static_cast<uint32_t>(std::floor(sample));
        second = std::min(first + 1U, edge - 1U);
        toward = sample - static_cast<double>(first);
    };
    uint32_t x0 = 0;
    uint32_t x1 = 0;
    uint32_t y0 = 0;
    uint32_t y1 = 0;
    double fx = 0.0;
    double fy = 0.0;
    axis(x, x0, x1, fx);
    axis(y, y0, y1, fy);
    const auto at = [&](uint32_t tx, uint32_t ty) {
        return static_cast<double>(texels[(ty * edge + tx) * card::texel_bytes + channel]);
    };
    const double top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
    const double bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
    return static_cast<int>(std::lround(top + (bottom - top) * fy));
}

/// Runs the Full function test on an executor, four batches into one
/// target read back once: (i) an opaque page of two levels drawn 1:1
/// NEAREST reads back equal to the page; (ii) the page's level 1 drawn
/// twice its size LINEAR reads back within test_blend_most_difference of
/// the enlargement computed exactly, or, on SDL's software renderer, which
/// may sample geometry NEAREST, exactly as NEAREST; (iii) a white quad at
/// alpha one half blended over a known colour reads back within
/// test_blend_most_difference of the blend; and (iv) a triangle with red,
/// green and blue corners shows their mean at its centroid pixel within
/// test_centroid_most_difference. Its page and target are destroyed and the
/// render target set back to the window when it ends.
///
/// @param[in,out] executor the executor, open
/// @param renderer the renderer the executor is open on
/// @param software_renderer the renderer is SDL's software renderer
/// @return what failed; empty when it passed
std::string
run_full_function_test(card::Executor& executor, SDL_Renderer* renderer, bool software_renderer) {
    card::PageDescription description;
    description.width = test_page_edge;
    description.height = test_page_edge;
    description.level_count = test_page_levels;
    const card::PageHandle page = executor.create_page(description);
    if (page == card::PageHandle{})
        return "the test page: " + executor.error();
    const card::TargetHandle target = executor.create_target(test_target_edge, test_target_edge, 1);
    if (target == card::TargetHandle{}) {
        executor.destroy_page(page);
        return "the test target: " + executor.error();
    }
    const auto finish = [&](std::string failure) {
        executor.destroy_target(target);
        executor.destroy_page(page);
        return failure;
    };
    // Level 0: a pattern of channels that differ in every texel; level 1 its
    // exact box.
    std::array<uint8_t, test_page_edge * test_page_edge * card::texel_bytes> level_0{};
    for (uint32_t y = 0; y < test_page_edge; ++y)
        for (uint32_t x = 0; x < test_page_edge; ++x) {
            uint8_t* texel = &level_0[(y * test_page_edge + x) * card::texel_bytes];
            texel[0] = static_cast<uint8_t>(x * 32U);
            texel[1] = static_cast<uint8_t>(y * 32U);
            texel[2] = static_cast<uint8_t>((x ^ y) * 16U + 8U);
            texel[3] = 255;
        }
    constexpr uint32_t half_edge = test_page_edge / 2;
    std::array<uint8_t, half_edge * half_edge * card::texel_bytes> level_1{};
    for (uint32_t y = 0; y < half_edge; ++y)
        for (uint32_t x = 0; x < half_edge; ++x)
            for (uint32_t channel = 0; channel < card::texel_bytes; ++channel) {
                const auto at = [&](uint32_t dx, uint32_t dy) {
                    return static_cast<uint32_t>(
                        level_0
                            [((2U * y + dy) * test_page_edge + 2U * x + dx) * card::texel_bytes +
                             channel]
                    );
                };
                level_1[(y * half_edge + x) * card::texel_bytes + channel] =
                    static_cast<uint8_t>((at(0, 0) + at(1, 0) + at(0, 1) + at(1, 1) + 2U) / 4U);
            }
    if (!executor.update_page(
            page, 0, nullptr, level_0.data(), test_page_edge * card::texel_bytes
        ) ||
        !executor.update_page(page, 1, nullptr, level_1.data(), half_edge * card::texel_bytes))
        return finish("filling the test page: " + executor.error());
    card::CardFrame frame;
    card::Batch clear;
    clear.operation = card::Operation::clear;
    clear.target = target;
    clear.colour = test_clear_colour;
    frame.batches.push_back(clear);
    // (i) The page 1:1 NEAREST, at the top left.
    card::Batch quad;
    quad.target = target;
    quad.page = page;
    quad.level = 0;
    quad.sampling = card::Sampling::nearest;
    quad.first_index = 0;
    card::append_quad(
        frame,
        0.0F,
        0.0F,
        static_cast<float>(test_page_edge),
        static_cast<float>(test_page_edge),
        0.0F,
        0.0F,
        1.0F,
        1.0F,
        card::Colour{}
    );
    quad.index_count = static_cast<uint32_t>(frame.indices.size());
    frame.batches.push_back(quad);
    // (iii) The blended quad, at the top right.
    card::Batch blended;
    blended.target = target;
    blended.blend = card::Blend::alpha;
    blended.first_index = static_cast<card::Index>(frame.indices.size());
    card::append_quad(
        frame,
        static_cast<float>(test_page_edge),
        0.0F,
        static_cast<float>(test_page_edge),
        static_cast<float>(test_page_edge),
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        card::Colour{1.0F, 1.0F, 1.0F, test_blend_alpha}
    );
    blended.index_count = static_cast<uint32_t>(frame.indices.size()) - blended.first_index;
    frame.batches.push_back(blended);
    // (ii) Level 1 twice its size LINEAR, at the bottom left.
    card::Batch enlarged;
    enlarged.target = target;
    enlarged.page = page;
    enlarged.level = 1;
    enlarged.sampling = card::Sampling::linear;
    enlarged.first_index = static_cast<card::Index>(frame.indices.size());
    card::append_quad(
        frame,
        0.0F,
        static_cast<float>(test_page_edge),
        static_cast<float>(test_page_edge),
        static_cast<float>(test_page_edge),
        0.0F,
        0.0F,
        1.0F,
        1.0F,
        card::Colour{}
    );
    enlarged.index_count = static_cast<uint32_t>(frame.indices.size()) - enlarged.first_index;
    frame.batches.push_back(enlarged);
    // (iv) The triangle, over the rest of the target.
    card::Batch triangle;
    triangle.target = target;
    triangle.first_index = static_cast<card::Index>(frame.indices.size());
    append_triangle(
        frame,
        test_triangle,
        {card::Colour{1.0F, 0.0F, 0.0F, 1.0F},
         card::Colour{0.0F, 1.0F, 0.0F, 1.0F},
         card::Colour{0.0F, 0.0F, 1.0F, 1.0F}}
    );
    triangle.index_count = static_cast<uint32_t>(frame.indices.size()) - triangle.first_index;
    frame.batches.push_back(triangle);
    if (!executor.execute(frame, nullptr))
        return finish("the test frame: " + executor.error());
    // The target read back once: the page's texels, the enlargement, the
    // blend and the centroid.
    SDL_Texture* texture = executor.target_texture(target);
    if (texture == nullptr || !SDL_SetRenderTarget(renderer, texture))
        return finish(std::string("SDL_SetRenderTarget of the test target: ") + SDL_GetError());
    SDL_Surface* read = SDL_RenderReadPixels(renderer, nullptr);
    const bool back = SDL_SetRenderTarget(renderer, nullptr);
    SDL_Surface* pixels =
        read != nullptr ? SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA32) : nullptr;
    SDL_DestroySurface(read);
    if (pixels == nullptr || !back) {
        SDL_DestroySurface(pixels);
        return finish(std::string("reading the test target back: ") + SDL_GetError());
    }
    std::string failure;
    const auto pixel = [&](uint32_t x, uint32_t y) {
        return static_cast<const uint8_t*>(pixels->pixels) +
               static_cast<std::ptrdiff_t>(y) * pixels->pitch +
               static_cast<std::ptrdiff_t>(x) * card::texel_bytes;
    };
    for (uint32_t y = 0; y < test_page_edge && failure.empty(); ++y)
        for (uint32_t x = 0; x < test_page_edge && failure.empty(); ++x)
            if (std::memcmp(
                    pixel(x, y),
                    &level_0[(y * test_page_edge + x) * card::texel_bytes],
                    compared_channels
                ) != 0)
                failure = "the page drawn 1:1 did not read back as its texels";
    // The enlargement: LINEAR within the tolerance, or NEAREST exactly where
    // the software renderer samples geometry so.
    bool linear_within = true;
    bool nearest_exact = true;
    for (uint32_t y = 0; y < test_page_edge; ++y)
        for (uint32_t x = 0; x < test_page_edge; ++x) {
            const uint8_t* shown = pixel(x, test_page_edge + y);
            const uint8_t* nearest = &level_1[((y / 2U) * half_edge + x / 2U) * card::texel_bytes];
            if (std::memcmp(shown, nearest, compared_channels) != 0)
                nearest_exact = false;
            for (std::size_t channel = 0; channel < compared_channels; ++channel)
                if (std::abs(
                        int{shown[channel]} -
                        enlarged_twice_linear(level_1.data(), half_edge, x, y, channel)
                    ) > test_blend_most_difference)
                    linear_within = false;
        }
    if (failure.empty() && !linear_within && !(software_renderer && nearest_exact))
        failure = "the page's level 1 drawn twice its size LINEAR did not read back as the "
                  "enlargement";
    const std::array<float, compared_channels> cleared{
        test_clear_colour.red, test_clear_colour.green, test_clear_colour.blue
    };
    for (uint32_t y = 0; y < test_page_edge && failure.empty(); ++y)
        for (uint32_t x = test_page_edge; x < 2U * test_page_edge && failure.empty(); ++x)
            for (std::size_t channel = 0; channel < compared_channels; ++channel) {
                const double expected =
                    255.0 * (test_blend_alpha + (1.0 - test_blend_alpha) * cleared[channel]);
                if (std::abs(static_cast<double>(pixel(x, y)[channel]) - expected) >
                    test_blend_most_difference) {
                    failure = "the quad at alpha one half did not read back as the blend";
                    break;
                }
            }
    if (failure.empty()) {
        const uint8_t* centroid = pixel(test_centroid_x, test_centroid_y);
        for (std::size_t channel = 0; channel < compared_channels; ++channel)
            if (std::abs(int{centroid[channel]} - test_centroid_mean) >
                test_centroid_most_difference) {
                failure = "the triangle's centroid pixel is not the mean of its corners' "
                          "colours: it read back " +
                          std::to_string(centroid[0]) + ", " + std::to_string(centroid[1]) + ", " +
                          std::to_string(centroid[2]);
                break;
            }
    }
    SDL_DestroySurface(pixels);
    return finish(failure);
}

/// Finds what a picture holds over a base, as ARGB8888 words, and uploads
/// the bands that hold it now or held it at the last upload, so that the
/// texture holds this picture's overlay whatever came before.
///
/// @param renderer the game's renderer
/// @param picture the picture, RGB24 at the battlefield's size
/// @param base the base it is found against, the same size
/// @param gamma the display gamma's table; null for none
/// @param pool the drawing threads
/// @param[in,out] overlay the words, the battlefield's size
/// @param[in,out] opaque_bands the bands that hold an opaque pixel now
/// @param[in,out] uploaded_bands the bands uploaded last
/// @param[in,out] uploaded the texture holds an uploaded overlay
/// @param[in,out] texture the overlay's texture, made for the size
void upload_overlay(
    const uint8_t* picture,
    const uint8_t* base,
    uint32_t bf_w,
    uint32_t bf_h,
    const std::array<uint8_t, 256>* gamma,
    oa::platform::job_pool::Pool* pool,
    std::vector<uint8_t>& overlay,
    std::vector<uint8_t>& opaque_bands,
    std::vector<uint8_t>& uploaded_bands,
    bool& uploaded,
    TiledTexture& texture
) {
    const std::size_t pitch = std::size_t{bf_w} * card::texel_bytes;
    convert_rgb24_overlay_argb(
        picture, base, bf_w, bf_h, overlay.data(), pitch, gamma, opaque_bands, pool
    );
    const auto bands = static_cast<uint32_t>(opaque_bands.size());
    const auto stale = [&](uint32_t band) {
        return !uploaded || opaque_bands[band] != 0 || uploaded_bands[band] != 0;
    };
    for (uint32_t band = 0; band < bands;) {
        if (!stale(band)) {
            ++band;
            continue;
        }
        uint32_t end = band + 1;
        while (end < bands && stale(end))
            ++end;
        texture.update(
            overlay.data(),
            static_cast<int>(pitch),
            static_cast<int>(card::texel_bytes),
            band * xrgb_band_rows,
            std::min(bf_h, end * xrgb_band_rows)
        );
        band = end;
    }
    uploaded_bands = opaque_bands;
    uploaded = true;
}

namespace supersampling = full_supersampling;

static_assert(
    policy::largest_supersample_factor == card::largest_supersampling_factor,
    "the policy's largest supersample factor is one a render target takes"
);

/// Returns the Enhanced anti-aliasing row's level that a unit
/// supersampling level is in effect for, which the Full tier reads its
/// factor from (render_policy::supersample_factor): the two name the same
/// six levels.
///
/// @param level the level units draw at outside Full
/// @return the row's level
[[nodiscard]] oa::ui::engine_settings::AntiAliasing
anti_aliasing_level_of(oa::present::model::UnitSupersampling level) noexcept {
    using oa::present::model::UnitSupersampling;
    using oa::ui::engine_settings::AntiAliasing;
    switch (level) {
    case UnitSupersampling::off:
        return AntiAliasing::off;
    case UnitSupersampling::x2:
        return AntiAliasing::x2;
    case UnitSupersampling::x3:
        return AntiAliasing::x3;
    case UnitSupersampling::x4:
        return AntiAliasing::x4;
    case UnitSupersampling::x8:
        return AntiAliasing::x8;
    case UnitSupersampling::x16:
        return AntiAliasing::x16;
    }
    return AntiAliasing::off;
}

/// Returns the supersample factor a Full frame asks of the world target:
/// the Enhanced anti-aliasing row's, 1, 2 or 4, capped at the step-down's
/// rung once it has lowered Full's anti-aliasing for slow frames
/// (runtime_tier_watch.cpp), which never rises again within the run.
///
/// @param level the row's level, as units draw at it outside Full
/// @param rung the rung the accelerated presentation draws at
/// @param rung_lowered the step-down has lowered Full's anti-aliasing
/// @return the factor
[[nodiscard]] uint32_t supersample_asked(
    oa::present::model::UnitSupersampling level, const policy::LadderState& rung, bool rung_lowered
) noexcept {
    const uint32_t asked = policy::supersample_factor(anti_aliasing_level_of(level));
    return rung.full && rung_lowered ? std::min<uint32_t>(asked, rung.supersample) : asked;
}

/// Returns a count of bytes in whole mebibytes, for the log.
///
/// @param bytes the count
/// @return the mebibytes
[[nodiscard]] uint64_t mebibytes(uint64_t bytes) noexcept {
    return bytes / (uint64_t{1024} * 1024);
}

} // namespace

void Runtime::destroy_full_presentation(FullPresentation* full) noexcept {
    delete full;
}

void Runtime::FullPresentation::ensure_sprite_palette(
    const oa::PaletteBytes& palette_bytes, float gamma
) {
    auto& pages = sprite_pages;
    const oa::Palette palette = oa::present::palette_from_bytes(palette_bytes);
    pages.set_palette(palette, gamma);
    if (pages.has_gray_table() && gray_generation == pages.palette_generation())
        return;
    // The fog grays a pixel by its brightness: the entry nearest the grey
    // of its colour's mean channel, so a frame's greyed cell takes that
    // entry for each of its indices.
    std::array<uint8_t, table_entries> levels{};
    oa::present::world_renderer::build_gray_levels(palette, levels);
    std::array<uint8_t, gw::gray_table_entries> gray{};
    for (std::size_t index = 0; index < gray.size(); ++index) {
        const auto& entry = palette.entries[index];
        const auto level =
            static_cast<std::size_t>((static_cast<unsigned>(entry.r) + entry.g + entry.b) / 3U);
        gray[index] = levels[level];
    }
    pages.set_gray_table(gray);
    gray_generation = pages.palette_generation();
}

card::PageHandle Runtime::FullPresentation::card_page(uint32_t page) {
    const auto pages = sprite_pages.pages();
    if (page >= pages.size())
        return {};
    if (card_pages.size() <= page)
        card_pages.resize(std::size_t{page} + 1);
    FullCardPage& slot = card_pages[page];
    const uint32_t size = pages[page].size;
    if (slot.handle != card::PageHandle{} && slot.size == size && executor.page_alive(slot.handle))
        return slot.handle;
    if (slot.handle != card::PageHandle{})
        executor.destroy_page(slot.handle);
    slot = {};
    // In a shared game or a replay after the loading screen no page is
    // made: the stages wait for the match to end.
    if (!creation_allowed) {
        waiting = true;
        throw FullCardError("a sprite page waits for the match to end");
    }
    card::PageDescription description;
    description.width = size;
    description.height = size;
    description.level_count = 1;
    slot.handle = executor.create_page(description);
    if (slot.handle == card::PageHandle{})
        throw FullCardError(
            "a sprite page of " + std::to_string(size) +
            " texels a side could not be made: " + executor.error()
        );
    slot.size = size;
    return slot.handle;
}

card::PageHandle Runtime::FullPresentation::card_page_hook(void* context, uint32_t page) {
    return static_cast<FullPresentation*>(context)->card_page(page);
}

void Runtime::FullPresentation::upload_sprite_pages() {
    auto& pages = sprite_pages;
    const auto held = pages.pages();
    for (uint32_t index = 0; index < card_pages.size() && index < held.size(); ++index) {
        FullCardPage& slot = card_pages[index];
        if (slot.handle == card::PageHandle{})
            continue;
        const gw::Page& page = held[index];
        if (page.size == 0 || page.size != slot.size) {
            executor.destroy_page(slot.handle);
            slot = {};
            continue;
        }
        if (slot.revision == page.revision)
            continue;
        const uint32_t pitch = page.size * card::texel_bytes;
        bool uploaded = true;
        if (slot.revision == 0) {
            uploaded = executor.update_page(slot.handle, 0, nullptr, page.texels.data(), pitch);
        } else if (!page.dirty.empty()) {
            const card::Rect part{page.dirty.x, page.dirty.y, page.dirty.width, page.dirty.height};
            const uint8_t* first =
                page.texels.data() +
                (std::size_t{page.dirty.y} * page.size + page.dirty.x) * card::texel_bytes;
            uploaded = executor.update_page(slot.handle, 0, &part, first, pitch);
        }
        if (!uploaded)
            throw FullCardError("a sprite page could not be filled: " + executor.error());
        pages.clear_dirty(index);
        slot.revision = page.revision;
    }
}

void Runtime::FullPresentation::destroy_sprite_card_pages() noexcept {
    for (FullCardPage& slot : card_pages)
        if (slot.handle != card::PageHandle{})
            executor.destroy_page(slot.handle);
    card_pages.clear();
}

void Runtime::FullPresentation::destroy_world_target() noexcept {
    executor.destroy_target(world_target);
    world_target = {};
    world_target_width = 0;
    world_target_height = 0;
    world_target_factor = 0;
    world_target_bytes = 0;
}

void Runtime::set_full_stages(uint8_t stages) {
    if (!full_)
        full_.reset(new FullPresentation);
    // The terrain is the Full tier's whatever the stages asked for.
    const auto left = static_cast<uint8_t>(stages & ~full::stages_built & ~full::stage_terrain);
    if (left != 0 && (full_->stages | stages) != full_->stages)
        std::cout << graphics_log_prefix << "the graphics card does not draw the "
                  << full::stage_text(left)
                  << " of the battlefield in this version; the processor draws them\n"
                  << std::flush;
    full_->stages = static_cast<uint8_t>(stages & full::stages_built);
}

uint8_t Runtime::full_stages() const noexcept {
    return full_ ? full_->stages : uint8_t{0};
}

uint16_t Runtime::take_full_card_kinds() {
    const uint16_t kinds = full_presentation() ? full::card_kinds(full_->stages) : uint16_t{0};
    if (full_)
        full_->frame_drawn = kinds != 0;
    return kinds;
}

bool Runtime::full_frame_drawn() const noexcept {
    return full_ && full_->frame_drawn;
}

void Runtime::capture_full_base(const oa::present::world_renderer::Surface& world) {
    if (!full_)
        return;
    full_->base.width = world.width;
    full_->base.height = world.height;
    full_->base.rgb.assign(world.rgb.begin(), world.rgb.end());
}

full::SpriteStageResult Runtime::full_sprite_result() const noexcept {
    return full_ ? full_->sprites : full::SpriteStageResult{};
}

renderer::Surface Runtime::full_base() const {
    return full_ ? full_->base : renderer::Surface{};
}

void Runtime::free_full_match_state() noexcept {
    if (!full_)
        return;
    free_full_match_textures();
    full_->sprite_pages.clear();
    full_->gray_generation = 0;
    full_->base = {};
    full_->frame_drawn = false;
    full_->sprites = {};
}

bool Runtime::full_presentation() const noexcept {
    return accelerated_presentation() && full_ && full_->on;
}

void Runtime::set_full_presentation(bool on) {
    if (!on) {
        if (full_ && full_->on) {
            full_->on = false;
            free_full_presentation();
        }
        return;
    }
    if (!full_)
        full_.reset(new FullPresentation);
    full_->on = true;
}

void Runtime::free_full_match_textures() noexcept {
    if (!full_)
        return;
    auto& full = *full_;
    for (const card::PageHandle page : full.pages)
        full.executor.destroy_page(page);
    full.pages.clear();
    full.page_bytes = 0;
    full.pages_from_load = false;
    full.executor.destroy_target(full.target);
    full.target = {};
    full.target_width = 0;
    full.target_height = 0;
    full.target_refused = false;
    full.destroy_world_target();
    full.refused_world_width = 0;
    full.refused_world_height = 0;
    full.refused_world_factor = 0;
    full.supersample = 1;
    full.drawn_plan = {};
    full.atlas = {};
    full.atlas_source = {};
    full.frame = {};
    full.overlay_texture.reset();
    full.overlay = {};
    full.opaque_bands = {};
    full.uploaded_bands = {};
    full.overlay_uploaded = false;
    // The stages' pages and the second overlay go with the match too.
    full.destroy_sprite_card_pages();
    full.stage_frame = {};
    full.painted_texture.reset();
    full.painted = {};
    full.painted_opaque_bands = {};
    full.painted_uploaded_bands = {};
    full.painted_uploaded = false;
    full.drawn = false;
}

void Runtime::free_full_presentation() noexcept {
    if (!full_)
        return;
    free_full_match_textures();
    full_->executor.close();
    full_->function_tested = false;
}

void Runtime::drop_full(const std::string& reason, policy::FullDrop drop) {
    if (full_ && full_->on)
        std::cout << "open-annihilation: graphics: the full tier stopped (" << reason
                  << "); the basic tier draws the battlefield from now on\n"
                  << std::flush;
    free_full_presentation();
    if (full_)
        full_->on = false;
    if (!render_run_ || render_run_->host == nullptr)
        return;
    auto& host = *render_run_->host;
    auto& inputs = host.tier_inputs();
    if (inputs.full_drop == policy::FullDrop::none)
        inputs.full_drop = drop;
    // In a shared game or a replay Full stays away until the match ends.
    policy::note_match_frame(inputs.match, policy::RenderTier::accelerated, false);
    // Full's first frames, where they stood under their own sentinel, stop
    // with it; another path's stage stands.
    const auto& sentinel = host.records().sentinel();
    if (sentinel && sentinel->stage == renderer_state::SentinelStage::path &&
        sentinel->path == renderer_state::AcceleratedPath::full)
        host.end_path_stage();
}

void Runtime::take_full_failure(const std::string& error) {
    if (render_run_ && render_run_->host != nullptr) {
        auto& host = *render_run_->host;
        // A failure noted already, as the function test's is before it is
        // thrown, is not a second one; any other card call that failed is
        // struck against the driver.
        if (host.tier_inputs().full_drop == policy::FullDrop::none) {
            renderer_state::Strike failure;
            failure.stage = renderer_state::StrikeStage::card;
            failure.call = failing_call_name(error);
            std::ignore = host.note_running_failure(failure);
        }
    }
    drop_full(error, policy::FullDrop::card_failure);
}

bool Runtime::begin_full_path() {
    if (!render_run_ || render_run_->host == nullptr)
        return true;
    if (render_run_->host->begin_path(renderer_state::AcceleratedPath::full))
        return true;
    drop_full(std::string(path_trial_unwritten_reason), policy::FullDrop::trial_unwritten);
    return false;
}

bool Runtime::full_creation_allowed() {
    if (!render_run_ || render_run_->host == nullptr || !full_)
        return true;
    if (policy::first_use_allowed(render_run_->host->tier_inputs().match, full_->loading_screen))
        return true;
    full_->waiting = true;
    wait_full_for_match_end();
    return false;
}

void Runtime::wait_full_for_match_end() {
    if (!render_run_ || render_run_->host == nullptr || !full_)
        return;
    policy::note_match_frame(
        render_run_->host->tier_inputs().match, policy::RenderTier::accelerated, false
    );
    if (!full_->wait_logged) {
        full_->wait_logged = true;
        std::cout << full_waits_message << '\n' << std::flush;
    }
}

void Runtime::preallocate_full_match_textures() {
    if (!render_run_ || render_run_->host == nullptr || !full_ || !full_->on ||
        sdl_.renderer == nullptr)
        return;
    const auto& gate = render_run_->host->tier_inputs().match;
    if (gate.kind == policy::MatchKind::none || !gate.full)
        return;
    auto& full = *full_;
    full.loading_screen = true;
    full.wait_logged = false;
    try {
        // The palette the atlas is built from: the game's, which the match
        // view reads again as it opens.
        match_palette_ = load_active_palette(assets_);
        if (begin_full_path()) {
            ensure_full_match_textures();
            full.pages_from_load = true;
            const auto bf_w = static_cast<uint32_t>(std::max(0, match_layout_.battlefield_width()));
            const auto bf_h = static_cast<uint32_t>(std::max(0, match_layout_.battlefield_height()));
            ensure_full_target(bf_w, bf_h);
            if (bf_w != 0 && bf_h != 0)
                ensure_full_world_target(
                    supersample_asked(
                        unit_supersampling_,
                        accelerated_.rung,
                        render_run_->watch && render_run_->watch->full_slowed
                    ),
                    bf_w,
                    bf_h,
                    render_texture_limit()
                );
        }
    } catch (const FullCardError& error) {
        take_full_failure(error.what());
    } catch (const AccelerationError& error) {
        take_acceleration_error(error);
    }
    full.loading_screen = false;
    full.waiting = false;
}

void Runtime::ensure_full_target(uint32_t bf_w, uint32_t bf_h) {
    auto& full = *full_;
    if (full.target != card::TargetHandle{} || full.target_refused || bf_w == 0 || bf_h == 0)
        return;
    // The target, made once at the largest a zoom just above 1 needs, two
    // map pixels of room for every battlefield pixel, in whole map pixels at
    // every whole-number zoom; where the renderer cannot make it, the
    // terrain is drawn LINEAR straight.
    const uint32_t width = target_extent(2U * bf_w);
    const uint32_t height = target_extent(2U * bf_h);
    if (!full_creation_allowed())
        throw FullCardError("the full tier's zoom-in target waits for the match to end");
    if (!accelerated_buffer_allowed(
            policy::AcceleratedBuffer::card_targets, uint64_t{width} * height * bytes_per_texel
        ))
        throw FullCardError("the full tier's zoom-in target: too little memory");
    full.target_width = width;
    full.target_height = height;
    full.target = full.executor.create_target(full.target_width, full.target_height, 1);
    if (full.target == card::TargetHandle{}) {
        full.target_refused = true;
        full.target_width = 0;
        full.target_height = 0;
        std::cout << "open-annihilation: graphics: the full tier's zoom-in target "
                     "could not be made ("
                  << full.executor.error() << "); the terrain between whole zooms is drawn LINEAR\n"
                  << std::flush;
    }
}

uint32_t Runtime::full_supersample() const noexcept {
    return full_presentation() ? full_->supersample : 0;
}

void Runtime::ensure_full_world_target(
    uint32_t asked, uint32_t battlefield_width, uint32_t battlefield_height, uint32_t texture_limit
) {
    auto& full = *full_;
    if (full.supersample_budget == 0) {
        const oa::platform::MachineTraits machine = oa::platform::read_machine_traits();
        // The memory the tier was decided from (render_policy::TierInputs::memory).
        const uint64_t memory = render_run_ && render_run_->host != nullptr
                                    ? render_run_->host->tier_inputs().memory
                                    : uint64_t{0};
        full.supersample_budget = policy::supersample_budget(
            memory, oa::platform::light_machine(machine), oa::platform::running_on_raspberry_pi()
        );
    }
    const uint32_t size_width =
        supersampling::rounded_up(battlefield_width, supersampling::target_grain);
    const uint32_t size_height =
        supersampling::rounded_up(battlefield_height, supersampling::target_grain);
    uint32_t fitted = policy::fit_supersample_factor(
        asked, size_width, size_height, full.supersample_budget, texture_limit
    );
    // A target the renderer or the memory guard refused is not asked for
    // again at that size and factor.
    if (fitted > 1 && full.refused_world_factor == fitted &&
        full.refused_world_width == size_width && full.refused_world_height == size_height)
        fitted = 1;
    const bool kept = full.world_target != card::TargetHandle{} &&
                      full.world_target_width == size_width &&
                      full.world_target_height == size_height && full.world_target_factor == fitted;
    bool waits = false;
    if (fitted > 1 && !kept) {
        // In a shared game or a replay after the loading screen no target is
        // made: the target the loading screen made draws on at its factor,
        // else the tier draws straight, until the match ends.
        const bool may_make =
            !render_run_ || render_run_->host == nullptr ||
            policy::first_use_allowed(render_run_->host->tier_inputs().match, full.loading_screen);
        const uint64_t bytes =
            policy::supersample_target_pixels(size_width, size_height, fitted) * bytes_per_texel;
        if (!may_make) {
            waits = true;
            const bool same_size = full.world_target != card::TargetHandle{} &&
                                   full.world_target_width == size_width &&
                                   full.world_target_height == size_height;
            fitted = same_size ? full.world_target_factor : 1;
        } else {
            full.destroy_world_target();
            if (!accelerated_buffer_fits(bytes)) {
                // The guard's refusal costs the anti-aliasing, not the tier:
                // the battlefield is drawn straight.
                std::cout << graphics_log_prefix << "full tier: the world target of " << size_width
                          << "x" << size_height << " at factor " << fitted
                          << " would leave too little memory; the battlefield is drawn without "
                          << "anti-aliasing\n"
                          << std::flush;
                full.refused_world_width = size_width;
                full.refused_world_height = size_height;
                full.refused_world_factor = fitted;
                fitted = 1;
            } else {
                const uint64_t bytes_before = full.executor.counts().texture_bytes;
                full.world_target =
                    full.executor.create_target(size_width, size_height, fitted, true);
                if (full.world_target == card::TargetHandle{}) {
                    std::cout << graphics_log_prefix << "full tier: the world target of "
                              << size_width << "x" << size_height << " at factor " << fitted
                              << " could not be made (" << full.executor.error()
                              << "); the battlefield is drawn without anti-aliasing\n"
                              << std::flush;
                    full.refused_world_width = size_width;
                    full.refused_world_height = size_height;
                    full.refused_world_factor = fitted;
                    fitted = 1;
                } else {
                    full.world_target_width = size_width;
                    full.world_target_height = size_height;
                    full.world_target_factor = fitted;
                    full.world_target_bytes = full.executor.counts().texture_bytes - bytes_before;
                    std::cout << graphics_log_prefix << "full tier: anti-aliasing " << fitted
                              << "x: the world target of " << size_width << "x" << size_height
                              << " at factor " << fitted << " and its half hold "
                              << mebibytes(full.world_target_bytes) << " MiB\n"
                              << std::flush;
                }
            }
        }
    }
    if (fitted == 1 && full.world_target != card::TargetHandle{})
        full.destroy_world_target();
    if (!waits && fitted < asked &&
        (fitted != full.supersample || asked != full.supersample_asked))
        std::cout << graphics_log_prefix << "full tier: anti-aliasing "
                  << (fitted > 1 ? std::to_string(fitted) + "x" : std::string("off"))
                  << ": the Enhanced anti-aliasing row asks for " << asked
                  << "x, but the budget of "
                  << mebibytes(full.supersample_budget * bytes_per_texel)
                  << " MiB and the renderer allow no larger world target at " << size_width << "x"
                  << size_height << '\n'
                  << std::flush;
    full.supersample = fitted;
    full.supersample_asked = asked;
}

void Runtime::ensure_full_executor() {
    auto& full = *full_;
    if (!full.executor.is_open()) {
        if (!full_creation_allowed())
            throw FullCardError("the full tier's executor waits for the match to end");
        if (!full.executor.open(sdl_.renderer, render_texture_limit()))
            throw FullCardError("the card could not be opened: " + full.executor.error());
        full.function_tested = false;
        full.pages.clear();
        full.pages_from_load = false;
        full.atlas_source = {};
        full.target = {};
        full.target_refused = false;
        // The executor's targets went with its last hold on the renderer.
        full.world_target = {};
        full.world_target_width = 0;
        full.world_target_height = 0;
        full.world_target_factor = 0;
        full.world_target_bytes = 0;
        full.refused_world_width = 0;
        full.refused_world_height = 0;
        full.refused_world_factor = 0;
        full.supersample = 1;
    }
    if (!full.function_tested) {
        const bool software =
            render_run_ && render_run_->host != nullptr &&
            render_run_->host->facts().renderer == oa::platform::render_probe::software_renderer;
        const std::string failure = run_full_function_test(full.executor, sdl_.renderer, software);
        oa::base::float_precision::restore_program_float_control();
        if (!failure.empty()) {
            // Not capable of Full: no strike, since the card drew nothing
            // wrong that a driver's failure would explain.
            drop_full("the Full function test failed: " + failure, policy::FullDrop::function_test);
            throw FullCardError("the Full function test failed: " + failure);
        }
        full.function_tested = true;
    }
}

void Runtime::ensure_full_terrain_pages(const oa::PaletteBytes& palette) {
    auto& full = *full_;
    if (!selected_tnt_)
        throw FullCardError("the match has no map");
    const auto& map = *selected_tnt_;
    const uint32_t limit = render_texture_limit();
    const uint32_t page_edge = gw::fit_page_edge(
        limit == render_policy::unlimited_texture_size ? gw::page_edge_limit : limit
    );
    const bool gamma = !gamma_identity_;
    const FullPresentation::AtlasSource source = FullPresentation::AtlasSource::of(map);
    const bool same_source = full.atlas_source == source && !full.pages.empty() &&
                             full.pages.size() == full.atlas.pages.size() &&
                             full.atlas_page_edge == page_edge && full.atlas_palette == palette &&
                             full.atlas_gamma == gamma &&
                             (!gamma || full.atlas_gamma_table == gamma_table_);
    if (same_source)
        return;
    if (!full_creation_allowed())
        throw FullCardError("the full tier's terrain pages wait for the match to end");
    for (const card::PageHandle page : full.pages)
        full.executor.destroy_page(page);
    full.pages.clear();
    full.page_bytes = 0;
    full.pages_from_load = false;
    full.atlas_source = {};
    const auto build_start = std::chrono::steady_clock::now();
    const gw::TerrainAtlasError error = gw::build_terrain_atlas(
        map, palette, gamma ? &gamma_table_ : nullptr, page_edge, full.atlas
    );
    full.atlas_build_ns = nanoseconds_since(build_start);
    if (error != gw::TerrainAtlasError::none)
        throw FullCardError(
            std::string("the terrain atlas could not be built: ") +
            gw::terrain_atlas_error_text(error)
        );
    // The pages' memory, counted by the memory guard before they are made.
    uint64_t page_bytes = 0;
    for (const auto& atlas_page : full.atlas.pages)
        for (std::size_t level = 0;
             level < std::min<std::size_t>(full_terrain_levels, atlas_page.levels.size());
             ++level)
            page_bytes += uint64_t{atlas_page.levels[level].width} *
                          atlas_page.levels[level].height * bytes_per_texel;
    if (!accelerated_buffer_allowed(policy::AcceleratedBuffer::card_pages, page_bytes))
        throw FullCardError("the terrain pages: too little memory");
    const auto upload_start = std::chrono::steady_clock::now();
    for (auto& atlas_page : full.atlas.pages) {
        card::PageDescription description;
        description.width = atlas_page.width;
        description.height = atlas_page.height;
        description.level_count = static_cast<uint8_t>(
            std::min<std::size_t>(full_terrain_levels, atlas_page.levels.size())
        );
        const card::PageHandle page = full.executor.create_page(description);
        if (page == card::PageHandle{})
            throw FullCardError("a terrain page could not be made: " + full.executor.error());
        full.pages.push_back(page);
        for (uint8_t level = 0; level < description.level_count; ++level) {
            const auto& atlas_level = atlas_page.levels[level];
            const uint32_t pitch = atlas_level.width * card::texel_bytes;
            if (!full.executor.update_page(
                    page, level, nullptr, atlas_page.texels.data() + atlas_level.offset, pitch
                ))
                throw FullCardError("a terrain page could not be filled: " + full.executor.error());
            full.page_bytes += uint64_t{pitch} * atlas_level.height;
        }
        // The card holds the page now; the processor keeps the page's
        // sizes and levels, which the builder reads, and lets its texels go.
        std::vector<uint8_t>().swap(atlas_page.texels);
    }
    full.page_upload_ns = nanoseconds_since(upload_start);
    full.atlas_source = source;
    full.atlas_page_edge = page_edge;
    full.atlas_palette = palette;
    full.atlas_gamma = gamma;
    full.atlas_gamma_table = gamma_table_;
}

void Runtime::make_full_match_pages() {
    if (!full_presentation() || !selected_tnt_)
        return;
    auto& full = *full_;
    try {
        // Full's first card calls of the run stand under its trial, as its
        // first frame's do; a trial that cannot be written keeps Full off
        // with nothing struck.
        if (!begin_full_path())
            return;
        ensure_full_executor();
        // The loading screen's palette is the match's: both are the game's
        // active palette, which the match view loads again as it is entered.
        ensure_full_terrain_pages(load_active_palette(assets_));
        full.pages_from_load = true;
    } catch (const FullCardError& error) {
        // A page that must wait for a shared game to end, which the pages
        // made as its loading screen began leave none of
        // (preallocate_full_match_textures), drops and strikes nothing.
        if (full.waiting) {
            full.waiting = false;
            return;
        }
        take_full_failure(error.what());
    }
}

void Runtime::ensure_full_match_textures() {
    auto& full = *full_;
    ensure_full_executor();
    ensure_full_terrain_pages(match_palette_);
    // The overlay, at the battlefield's size: the world layer's, or before
    // the first frame the match layout's.
    const uint32_t bf_w =
        match_world_cpu_.width != 0
            ? match_world_cpu_.width
            : static_cast<uint32_t>(std::max(0, match_layout_.battlefield_width()));
    const uint32_t bf_h =
        match_world_cpu_.height != 0
            ? match_world_cpu_.height
            : static_cast<uint32_t>(std::max(0, match_layout_.battlefield_height()));
    if (bf_w == 0 || bf_h == 0)
        return;
    const uint32_t limit = render_texture_limit();
    if (!full.overlay_texture.made_for(bf_w, bf_h, limit)) {
        if (!full_creation_allowed())
            throw FullCardError("the full tier's overlay waits for the match to end");
        full.overlay_texture.create(
            sdl_.renderer, bf_w, bf_h, limit, SDL_BLENDMODE_BLEND, full.counts
        );
        full.overlay_uploaded = false;
        const uint32_t bands = platform::job_pool::bands_of_rows(bf_h, xrgb_band_rows);
        full.opaque_bands.assign(bands, 0);
        full.uploaded_bands.assign(bands, 0);
        full.overlay.assign(std::size_t{bf_w} * bf_h * 4U, 0);
        // The second overlay, of what is painted over the stages' batches.
        full.painted_texture.reset();
        full.painted_texture.create(
            sdl_.renderer, bf_w, bf_h, limit, SDL_BLENDMODE_BLEND, full.counts
        );
        full.painted_uploaded = false;
        full.painted_opaque_bands.assign(bands, 0);
        full.painted_uploaded_bands.assign(bands, 0);
        full.painted.assign(std::size_t{bf_w} * bf_h * 4U, 0);
        // A new battlefield size needs new targets.
        full.executor.destroy_target(full.target);
        full.target = {};
        full.target_width = 0;
        full.target_height = 0;
        full.target_refused = false;
        full.destroy_world_target();
    }
}

bool Runtime::present_full_match_layers(bool dialogs) {
    auto& full = *full_;
    // Full's first match frame of the run stands under its own trial and
    // sentinel; where the trial cannot be written Full is dropped, with
    // nothing struck, and Basic presents the frame.
    if (!begin_full_path()) {
        full.drawn = false;
        return false;
    }
    try {
        // The front end's prescale target is not kept during a match.
        accelerated_.screen_prescale.destroy();
        const auto frame_format = opaque_layer_format();
        ensure_streaming_texture(
            match_hud_tex_,
            frame_format,
            static_cast<int>(match_hud_cpu_.width),
            static_cast<int>(match_hud_cpu_.height),
            match_hud_tex_w_,
            match_hud_tex_h_
        );
        // The HUD layer's prescale target and the layout's bookkeeping, as
        // Basic's frame makes them, so that the HUD strips are drawn by
        // sharp_draw from that target at every chrome scale as Basic draws
        // them; the magnified world's part makes nothing, since a Full frame
        // is drawn at the zoom with no split.
        ensure_accelerated_match_textures();
        ensure_full_match_textures();
        const uint32_t bf_w = match_world_cpu_.width;
        const uint32_t bf_h = match_world_cpu_.height;
        const float zoom = match_zoom();
        const auto& base = match_terrain_cache_;
        const uint32_t limit = render_texture_limit();
        // The terrain base is the frame's own nearest fill at the zoom and
        // camera, the battlefield's size; a frame drawn otherwise, as the
        // standard tier's scaling draws it, is Basic's to present.
        const bool base_ready =
            base.width == bf_w && base.height == bf_h &&
            base.rgb.size() == match_world_cpu_.rgb.size() &&
            std::abs(terrain_cache_zoom_ - zoom) <= 1.0e-4F && !accelerated_.frame.apart &&
            full.pages.size() == full.atlas.pages.size() && !full.pages.empty() &&
            full.overlay_texture.made_for(bf_w, bf_h, limit);
        if (!base_ready) {
            full.drawn = false;
            return false;
        }
        const auto* gamma = gamma_identity_ ? nullptr : &gamma_table_;
        const auto upload_start = std::chrono::steady_clock::now();
        upload_rgb24_frame(match_hud_tex_, match_hud_cpu_);
        // With a stage on, the frame kept the world layer without the
        // card's kinds, after the fog's gray and before its black and the
        // painters: the stages draw over it, and the black and the painters
        // over them.
        const bool staged = full.stages != 0 && full.frame_drawn && full.base.width == bf_w &&
                            full.base.height == bf_h &&
                            full.base.rgb.size() == match_world_cpu_.rgb.size();
        // What the processor drew over the terrain base, by difference from
        // it, uploaded in the bands that hold it now or held it at the last
        // upload, so the texture holds this frame's overlay whatever came
        // before; with a stage on, what it drew under the stages, and then
        // what the fog's black and the painters changed over them.
        const auto overlay_start = std::chrono::steady_clock::now();
        upload_overlay(
            staged ? full.base.rgb.data() : match_world_cpu_.rgb.data(),
            base.rgb.data(),
            bf_w,
            bf_h,
            gamma,
            draw_pool_.get(),
            full.overlay,
            full.opaque_bands,
            full.uploaded_bands,
            full.overlay_uploaded,
            full.overlay_texture
        );
        if (staged)
            upload_overlay(
                match_world_cpu_.rgb.data(),
                full.base.rgb.data(),
                bf_w,
                bf_h,
                gamma,
                draw_pool_.get(),
                full.painted,
                full.painted_opaque_bands,
                full.painted_uploaded_bands,
                full.painted_uploaded,
                full.painted_texture
            );
        full.overlay_ns = nanoseconds_since(overlay_start);
        const auto present_start = std::chrono::steady_clock::now();
        phase_times_.upload += static_cast<int64_t>(nanoseconds_since(upload_start));
        if (!SDL_SetRenderDrawColor(sdl_.renderer, 0, 0, 0, 255) || !SDL_RenderClear(sdl_.renderer))
            throw_present_error("SDL_RenderClear");
        draw_accelerated_hud_strips();

        // The terrain, from the pages, by the level rule at the zoom.
        const auto build_start = std::chrono::steady_clock::now();
        auto& frame = full.frame;
        frame.reset();
        const card::Rect battlefield{
            match_layout_.left,
            match_layout_.top,
            static_cast<int32_t>(bf_w),
            static_cast<int32_t>(bf_h)
        };
        // The pixel-art sampling mode where the start-up probe found it
        // scales as that filter does, which the rung's card filter carries
        // (SDL's software renderer takes the mode and draws it NEAREST),
        // and the executor sets it on the pages.
        const bool pixel_art = accelerated_.rung.card == render_policy::CardFilter::pixelart &&
                               full.executor.capabilities().pixel_art_sampling;
        full.plan = ft::plan_terrain_draw(zoom, pixel_art);
        ft::TerrainView view;
        view.camera_x = terrain_cache_cam_x_;
        view.camera_y = terrain_cache_cam_y_;
        view.origin_x = static_cast<float>(match_layout_.left);
        view.origin_y = static_cast<float>(match_layout_.top);
        view.scale = zoom;
        view.width = bf_w;
        view.height = bf_h;
        uint32_t quads = 0;
        bool through_target = false;
        // The stages' batches go on the last list the planner built; a page
        // a stage would make now waits in a shared game or a replay after
        // the loading screen (FullPresentation::card_page).
        auto& stage_frame = full.stage_frame;
        stage_frame.reset();
        full.sprites = {};
        full.stage_ns = 0;
        full.creation_allowed =
            !render_run_ || render_run_->host == nullptr ||
            policy::first_use_allowed(render_run_->host->tier_inputs().match, full.loading_screen);
        const bool sprites_on = staged && (full.stages & full::stage_sprites) != 0 && match_;
        // The sprite stage: the frame's sprites, particle squares and lines
        // from the sprite pages, in the list's order, appended to a frame
        // through a view: straight to the window at the battlefield's
        // corner, or into the world target at its draw scale. The pages'
        // texels the stage changed go up before the frame runs.
        const auto emit_sprite_stage = [&](card::CardFrame& into,
                                           float origin_x,
                                           float origin_y,
                                           float view_scale,
                                           card::TargetHandle target) {
            full.ensure_sprite_palette(match_palette_, display_gamma_);
            full::SpriteStageInputs inputs;
            inputs.list = &match_models().draws;
            inputs.view.origin_x = origin_x;
            inputs.view.origin_y = origin_y;
            inputs.view.scale = view_scale;
            inputs.view.zoom = zoom;
            inputs.view.offset = accelerated_.frame_offset;
            inputs.view.width = static_cast<int32_t>(bf_w);
            inputs.view.height = static_cast<int32_t>(bf_h);
            inputs.view.target = target;
            // The camera the sight is read at: the planner's, map pixels.
            inputs.view.camera_x = match_camera_x_;
            inputs.view.camera_y = match_camera_z_;
            // The viewer's sight, as the fog reads it; none when the match
            // cannot say, which draws every sprite in colour.
            std::span<const uint8_t> coverage;
            try {
                coverage = match_->player_coverage(match_view_player());
            } catch (const std::exception&) {
                coverage = {};
            }
            const auto& sight = match_->sight();
            if (!coverage.empty() && sight.width > 0 && sight.height > 0) {
                inputs.sight.coverage = coverage;
                inputs.sight.player_bits = sight.player_bits;
                inputs.sight.width = sight.width;
                inputs.sight.height = sight.height;
                inputs.sight.viewer_bit =
                    static_cast<uint16_t>(1U << (sight.viewpoint_player & 0x1fU));
                inputs.sight.line_of_sight = match_line_of_sight_on();
                inputs.sight.mapping = match_mapping_on();
            }
            inputs.palette = &match_palette_;
            inputs.gamma = gamma;
            const full::SpritePageHooks hooks{&full, &FullPresentation::card_page_hook};
            full.sprites = full::emit_sprites(inputs, full.sprite_pages, hooks, into);
            if (full.sprites.pages_overflowed && !full.overflow_logged) {
                full.overflow_logged = true;
                std::cout << graphics_log_prefix
                          << "the frame's sprites do not fit the sprite pages; the card draws "
                             "none of them this frame\n"
                          << std::flush;
            }
            full.upload_sprite_pages();
            if (!full.stages_logged) {
                full.stages_logged = true;
                std::cout << graphics_log_prefix << "full tier: the graphics card also draws the "
                          << full::stage_text(full.stages) << " of the battlefield\n"
                          << std::flush;
            }
        };
        // Anti-aliasing: the factor the Enhanced anti-aliasing row asks for,
        // within the step-down's rung, fitted to this battlefield, and the
        // world target at it.
        ensure_full_world_target(
            supersample_asked(
                unit_supersampling_,
                accelerated_.rung,
                render_run_ && render_run_->watch && render_run_->watch->full_slowed
            ),
            bf_w,
            bf_h,
            limit
        );
        const supersampling::WorldTargetPlan supersampled =
            supersampling::plan_world_target(zoom, full.supersample, bf_w, bf_h);
        full.drawn_plan = supersampled;
        // The stage's batches inside the terrain's frame, which the terrain's
        // count leaves out.
        uint32_t sprite_batches = 0;
        if (supersampled.factor > 1) {
            // The terrain into the world target, level 0 NEAREST at the
            // plan's draw scale: from zoom 1 up the texture holds the
            // factor's texels a window pixel, and below it one a map pixel;
            // the sprite stage into it at the same scale; then the target
            // reduced into the battlefield, by the factor's halvings or by
            // the two-level blend of the part drawn.
            through_target = true;
            ft::TerrainPass pass;
            pass.level = 0;
            pass.sampling = card::Sampling::nearest;
            pass.blend = card::Blend::none;
            pass.alpha = 1.0F;
            full.plan = {};
            full.plan.passes[0] = pass;
            full.plan.pass_count = 1;
            full.plan.through_target = false;
            card::Batch clear;
            clear.operation = card::Operation::clear;
            clear.target = full.world_target;
            clear.colour = card::Colour{0.0F, 0.0F, 0.0F, 1.0F};
            frame.batches.push_back(clear);
            ft::TerrainView target_view = view;
            target_view.origin_x = 0.0F;
            target_view.origin_y = 0.0F;
            target_view.scale = supersampled.draw_scale;
            target_view.width =
                static_cast<uint32_t>(supersampled.source_part.width) / supersampled.factor;
            target_view.height =
                static_cast<uint32_t>(supersampled.source_part.height) / supersampled.factor;
            quads += ft::append_terrain_tiles(
                frame, full.atlas, full.pages, target_view, pass, full.world_target, nullptr
            );
            if (sprites_on) {
                // Size pixels per layout pixel, so that the texture holds the
                // sprites at the factor's pixels a window pixel, or at zoom 1
                // below zoom 1.
                const auto stage_start = std::chrono::steady_clock::now();
                const auto before = static_cast<uint32_t>(frame.batches.size());
                emit_sprite_stage(
                    frame, 0.0F, 0.0F, supersampled.draw_scale / zoom, full.world_target
                );
                sprite_batches = static_cast<uint32_t>(frame.batches.size()) - before;
                full.stage_ns = nanoseconds_since(stage_start);
            }
            card::Batch reduce;
            reduce.operation =
                supersampled.two_level ? card::Operation::blend_reduce : card::Operation::resolve;
            reduce.source = full.world_target;
            reduce.source_part = supersampled.source_part;
            reduce.destination = {
                battlefield.x + supersampled.destination.x,
                battlefield.y + supersampled.destination.y,
                supersampled.destination.width,
                supersampled.destination.height
            };
            reduce.scissored = true;
            reduce.scissor = battlefield;
            frame.batches.push_back(reduce);
        } else if (full.plan.through_target) {
            ensure_full_target(bf_w, bf_h);
            const uint32_t factor = full.plan.target_zoom;
            if (full.target != card::TargetHandle{} && factor != 0 &&
                full.target_width / factor * zoom >= static_cast<float>(bf_w) &&
                full.target_height / factor * zoom >= static_cast<float>(bf_h)) {
                through_target = true;
                card::Batch clear;
                clear.operation = card::Operation::clear;
                clear.target = full.target;
                clear.colour = card::Colour{0.0F, 0.0F, 0.0F, 1.0F};
                frame.batches.push_back(clear);
                ft::TerrainView target_view = view;
                target_view.origin_x = 0.0F;
                target_view.origin_y = 0.0F;
                target_view.scale = static_cast<float>(factor);
                target_view.width = full.target_width;
                target_view.height = full.target_height;
                quads += ft::append_terrain_tiles(
                    frame,
                    full.atlas,
                    full.pages,
                    target_view,
                    full.plan.passes[0],
                    full.target,
                    nullptr
                );
                card::Batch resolve;
                resolve.operation = card::Operation::resolve;
                resolve.source = full.target;
                resolve.scissored = true;
                resolve.scissor = battlefield;
                resolve.destination = {
                    battlefield.x,
                    battlefield.y,
                    static_cast<int32_t>(std::lround(
                        static_cast<double>(full.target_width) * zoom / static_cast<double>(factor)
                    )),
                    static_cast<int32_t>(std::lround(
                        static_cast<double>(full.target_height) * zoom / static_cast<double>(factor)
                    ))
                };
                frame.batches.push_back(resolve);
            } else {
                ft::TerrainPass pass = full.plan.passes[0];
                pass.sampling = card::Sampling::linear;
                quads += ft::append_terrain_tiles(
                    frame, full.atlas, full.pages, view, pass, card::TargetHandle{}, &battlefield
                );
            }
        } else {
            for (uint32_t index = 0; index < full.plan.pass_count; ++index)
                quads += ft::append_terrain_tiles(
                    frame,
                    full.atlas,
                    full.pages,
                    view,
                    full.plan.passes[index],
                    card::TargetHandle{},
                    &battlefield
                );
        }
        full.build_ns = nanoseconds_since(build_start) - full.stage_ns;
        // Drawn straight, the stage's batches go into a frame of their own,
        // run over the first overlay.
        if (sprites_on && supersampled.factor == 1) {
            const auto stage_start = std::chrono::steady_clock::now();
            emit_sprite_stage(
                stage_frame,
                static_cast<float>(match_layout_.left),
                static_cast<float>(match_layout_.top),
                1.0F,
                card::TargetHandle{}
            );
            full.stage_ns = nanoseconds_since(stage_start);
        }
        const auto execute_start = std::chrono::steady_clock::now();
        // The failure --check-renderer-ladder forces stands in for the
        // card's own.
        if (render_fault_due(RenderFaultPoint::card))
            throw FullCardError(std::string(forced_card_failure));
        if (!full.executor.execute(frame, nullptr))
            throw FullCardError("the card refused the terrain frame: " + full.executor.error());
        oa::base::float_precision::restore_program_float_control();
        full.execute_ns = nanoseconds_since(execute_start);
        // The frame counts towards the stage of Full's first frames.
        if (render_run_)
            render_run_->paths_drawn = static_cast<PathSet>(
                render_run_->paths_drawn | path_bit(renderer_state::AcceleratedPath::full)
            );
        full.drawn = true;
        full.drawn_zoom = zoom;
        full.drawn_quads = quads;
        // The terrain's batches, with the clear and the reduction of a
        // target it drew through; the stage counts its own.
        full.drawn_batches = static_cast<uint32_t>(frame.batches.size()) - sprite_batches;
        full.drawn_through_target = through_target;
        ++full.frames;

        // What the processor drew, over the card's terrain, 1:1: under the
        // stages' kinds where the frame was drawn straight, over the whole
        // reduced picture where it was drawn through the world target; then
        // the stages' batches where the target did not hold them, and what
        // was painted over them.
        const SDL_FRect world{
            static_cast<float>(battlefield.x),
            static_cast<float>(battlefield.y),
            static_cast<float>(bf_w),
            static_cast<float>(bf_h)
        };
        draw_one_to_one(
            sdl_.renderer, full.overlay_texture, nullptr, &world, one_to_one_scale_mode()
        );
        if (staged) {
            if (supersampled.factor == 1) {
                const auto stage_run_start = std::chrono::steady_clock::now();
                if (!full.executor.execute(stage_frame, nullptr))
                    throw FullCardError(
                        "the card refused the stages' frame: " + full.executor.error()
                    );
                oa::base::float_precision::restore_program_float_control();
                full.stage_ns += nanoseconds_since(stage_run_start);
            }
            draw_one_to_one(
                sdl_.renderer, full.painted_texture, nullptr, &world, one_to_one_scale_mode()
            );
        }
        // The tier's own passes, timed for the step-down as Basic's are.
        accelerated_.passes_ns += full.overlay_ns + full.build_ns + full.execute_ns + full.stage_ns;
        finish_match_layers(frame_format, dialogs, upload_start, present_start);
        return true;
    } catch (const FullCardError& error) {
        // A page or target that must wait for a shared game to end leaves
        // the frame to Basic with nothing dropped or struck.
        if (full.waiting) {
            full.waiting = false;
            full.drawn = false;
            wait_full_for_match_end();
            return false;
        }
        take_full_failure(error.what());
        return false;
    }
}

} // namespace oa::app
