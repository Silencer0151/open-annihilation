// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Full tier's presentation: the terrain drawn by the graphics card from
// the map's terrain atlas, built and uploaded as the match loads as texture
// pages with their levels, by the level rule of full_terrain.hpp;
// everything else the processor still draws, at the zoom over a terrain
// base filled by the nearest fill, and the card composes it over its own
// terrain through the overlay of what differs from that base. The planner,
// the HUD, the painters and the readers that keep a picture run as in the
// Basic tier. It is reached only when the tier decided for the frame is
// Full.
#include "oa/app/runtime.hpp"

#include "full_presentation.hpp"
#include "render_host.hpp"
#include "render_run.hpp"
#include "xrgb_conversion.hpp"

#include "oa/base/float_precision.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace oa::app {

namespace {

namespace gw = oa::present::gpu_world;
namespace ft = full_terrain;

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

} // namespace

void Runtime::destroy_full_presentation(FullPresentation* full) noexcept {
    delete full;
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
    full.atlas = {};
    full.atlas_source = {};
    full.frame = {};
    full.overlay_texture.reset();
    full.overlay = {};
    full.opaque_bands = {};
    full.uploaded_bands = {};
    full.overlay_uploaded = false;
    full.drawn = false;
}

void Runtime::free_full_presentation() noexcept {
    if (!full_)
        return;
    free_full_match_textures();
    full_->executor.close();
    full_->function_tested = false;
}

void Runtime::drop_full(const std::string& reason) {
    if (full_ && full_->on)
        std::cout << "open-annihilation: graphics: the full tier stopped (" << reason
                  << "); the basic tier draws the battlefield from now on\n"
                  << std::flush;
    free_full_presentation();
    if (full_)
        full_->on = false;
    if (render_run_ && render_run_->host != nullptr)
        render_run_->host->tier_inputs().full_drop = true;
}

void Runtime::ensure_full_executor() {
    auto& full = *full_;
    if (!full.executor.is_open()) {
        if (!full.executor.open(sdl_.renderer, render_texture_limit()))
            throw FullCardError("the card could not be opened: " + full.executor.error());
        full.function_tested = false;
        full.pages.clear();
        full.pages_from_load = false;
        full.atlas_source = {};
        full.target = {};
        full.target_refused = false;
    }
    if (!full.function_tested) {
        const bool software =
            render_run_ && render_run_->host != nullptr &&
            render_run_->host->facts().renderer == oa::platform::render_probe::software_renderer;
        const std::string failure = run_full_function_test(full.executor, sdl_.renderer, software);
        oa::base::float_precision::restore_program_float_control();
        if (!failure.empty())
            throw FullCardError("the Full function test failed: " + failure);
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
    try {
        ensure_full_executor();
        // The loading screen's palette is the match's: both are the game's
        // active palette, which the match view loads again as it is entered.
        ensure_full_terrain_pages(load_active_palette(assets_));
        full_->pages_from_load = true;
    } catch (const FullCardError& error) {
        drop_full(error.what());
    }
}

void Runtime::ensure_full_match_textures() {
    auto& full = *full_;
    ensure_full_executor();
    ensure_full_terrain_pages(match_palette_);
    // The overlay, at the battlefield's size.
    const uint32_t bf_w = match_world_cpu_.width;
    const uint32_t bf_h = match_world_cpu_.height;
    if (bf_w == 0 || bf_h == 0)
        return;
    const uint32_t limit = render_texture_limit();
    if (!full.overlay_texture.made_for(bf_w, bf_h, limit)) {
        full.overlay_texture.create(
            sdl_.renderer, bf_w, bf_h, limit, SDL_BLENDMODE_BLEND, full.counts
        );
        full.overlay_uploaded = false;
        const uint32_t bands = platform::job_pool::bands_of_rows(bf_h, xrgb_band_rows);
        full.opaque_bands.assign(bands, 0);
        full.uploaded_bands.assign(bands, 0);
        full.overlay.assign(std::size_t{bf_w} * bf_h * 4U, 0);
        // A new battlefield size needs a new target.
        full.executor.destroy_target(full.target);
        full.target = {};
        full.target_width = 0;
        full.target_height = 0;
        full.target_refused = false;
    }
}

bool Runtime::present_full_match_layers(bool dialogs) {
    auto& full = *full_;
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
        // What the processor drew over the terrain base, by difference from
        // it, uploaded in the bands that hold it now or held it at the last
        // upload, so the texture holds this frame's overlay whatever came
        // before.
        const auto overlay_start = std::chrono::steady_clock::now();
        const std::size_t pitch = std::size_t{bf_w} * 4U;
        convert_rgb24_overlay_argb(
            match_world_cpu_.rgb.data(),
            base.rgb.data(),
            bf_w,
            bf_h,
            full.overlay.data(),
            pitch,
            gamma,
            full.opaque_bands,
            draw_pool_.get()
        );
        const auto bands = static_cast<uint32_t>(full.opaque_bands.size());
        const auto stale = [&](uint32_t band) {
            return !full.overlay_uploaded || full.opaque_bands[band] != 0 ||
                   full.uploaded_bands[band] != 0;
        };
        for (uint32_t band = 0; band < bands;) {
            if (!stale(band)) {
                ++band;
                continue;
            }
            uint32_t end = band + 1;
            while (end < bands && stale(end))
                ++end;
            full.overlay_texture.update(
                full.overlay.data(),
                static_cast<int>(pitch),
                4,
                band * xrgb_band_rows,
                std::min(bf_h, end * xrgb_band_rows)
            );
            band = end;
        }
        full.uploaded_bands = full.opaque_bands;
        full.overlay_uploaded = true;
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
        if (full.plan.through_target) {
            // The target, made once at the largest a zoom just above 1 needs,
            // two map pixels of room for every battlefield pixel, in whole
            // map pixels at every whole-number zoom; where the renderer
            // cannot make it, the terrain is drawn LINEAR straight.
            if (full.target == card::TargetHandle{} && !full.target_refused) {
                full.target_width = target_extent(2U * bf_w);
                full.target_height = target_extent(2U * bf_h);
                full.target = full.executor.create_target(full.target_width, full.target_height, 1);
                if (full.target == card::TargetHandle{}) {
                    full.target_refused = true;
                    full.target_width = 0;
                    full.target_height = 0;
                    std::cout << "open-annihilation: graphics: the full tier's zoom-in target "
                                 "could not be made ("
                              << full.executor.error()
                              << "); the terrain between whole zooms is drawn LINEAR\n"
                              << std::flush;
                }
            }
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
        full.build_ns = nanoseconds_since(build_start);
        const auto execute_start = std::chrono::steady_clock::now();
        if (!full.executor.execute(frame, nullptr))
            throw FullCardError("the card refused the terrain frame: " + full.executor.error());
        oa::base::float_precision::restore_program_float_control();
        full.execute_ns = nanoseconds_since(execute_start);
        full.drawn = true;
        full.drawn_zoom = zoom;
        full.drawn_quads = quads;
        full.drawn_batches = static_cast<uint32_t>(frame.batches.size());
        full.drawn_through_target = through_target;
        ++full.frames;
        // The tier's own passes, timed for the step-down as Basic's are.
        accelerated_.passes_ns += full.overlay_ns + full.build_ns + full.execute_ns;

        // What the processor drew, over the card's terrain, 1:1.
        const SDL_FRect world{
            static_cast<float>(battlefield.x),
            static_cast<float>(battlefield.y),
            static_cast<float>(bf_w),
            static_cast<float>(bf_h)
        };
        draw_one_to_one(
            sdl_.renderer, full.overlay_texture, nullptr, &world, one_to_one_scale_mode()
        );
        finish_match_layers(frame_format, dialogs, upload_start, present_start);
        return true;
    } catch (const FullCardError& error) {
        drop_full(error.what());
        return false;
    }
}

} // namespace oa::app
