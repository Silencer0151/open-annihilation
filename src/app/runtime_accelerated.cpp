// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The accelerated presentation: the battlefield drawn at its draw scale and
// reduced by the exact area pass when zoomed out, or magnified by the
// graphics card with what the painters changed laid over it when zoomed in;
// the interface and the 640x480 screens scaled by the pixel-art or
// sharp-bilinear filter; and the standard tier's world drawn again for the
// readers that keep a picture. It is off unless its host switches it on.
#include "oa/app/runtime.hpp"

#include "render_host.hpp"
#include "render_run.hpp"
#include "xrgb_conversion.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace oa::app {

namespace {

namespace policy = render_policy;
namespace wr = oa::present::world_renderer;

/// Steps the zoom range above 1 is walked in to find the largest prescale
/// target a magnified scene needs.
constexpr int prescale_zoom_steps = 960;

/// The most a prescale factor is asked for, whatever the scale.
constexpr double most_prescale_factor = 64.0;

/// Returns the nanoseconds since a moment of the steady clock.
///
/// @param since the moment
/// @return nanoseconds
[[nodiscard]] int64_t nanoseconds_since(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now() - since
    )
        .count();
}

/// Returns the scene corner a magnified battlefield shows at a zoom: its map
/// pixels rounded up, within the scene.
///
/// @param battlefield the battlefield's columns or rows
/// @param zoom screen pixels per map pixel, above 1
/// @param scene the scene's columns or rows
/// @return the corner's columns or rows
[[nodiscard]] uint32_t magnified_corner(uint32_t battlefield, float zoom, uint32_t scene) {
    const auto shown = static_cast<uint32_t>(
        std::ceil(static_cast<double>(battlefield) / static_cast<double>(zoom))
    );
    return std::min(shown, scene);
}

} // namespace

void Runtime::switch_accelerated_presentation(bool on, const policy::LadderState& rung) {
    free_accelerated_presentation();
    accelerated_.on = on;
    accelerated_.rung = rung;
    // The renderer host's limit, which the standard tier's layers are tiled by too.
    accelerated_.texture_limit = on && sdl_.renderer != nullptr ? render_texture_limit() : 0;
}

bool Runtime::accelerated_presentation() const noexcept {
    return accelerated_.on && !accelerated_.suspended && sdl_.renderer != nullptr &&
           !options_.headless_check && director_ == nullptr;
}

void Runtime::drop_acceleration(const std::string& reason) {
    if (accelerated_.on)
        std::cout << "open-annihilation: graphics: the accelerated tier stopped (" << reason
                  << "); the processor draws everything from now on\n";
    free_accelerated_presentation();
    accelerated_.on = false;
    if (render_run_ && render_run_->host != nullptr) {
        auto& inputs = render_run_->host->tier_inputs();
        if (inputs.drop == render_policy::Drop::none)
            inputs.drop = render_policy::Drop::driver_failure;
    }
}

void Runtime::free_accelerated_layout_textures() noexcept {
    auto& state = accelerated_;
    state.scene.reset();
    state.overlay_texture.reset();
    state.world_prescale.destroy();
    state.hud_prescale.destroy();
    state.overlay = {};
    state.opaque_bands = {};
    state.uploaded_bands = {};
    state.overlay_uploaded = false;
}

void Runtime::free_accelerated_match_textures() noexcept {
    auto& state = accelerated_;
    free_accelerated_layout_textures();
    state.base = {};
    state.area_plan = {};
    state.layout_width = 0;
    state.layout_height = 0;
}

void Runtime::free_accelerated_presentation() noexcept {
    free_accelerated_match_textures();
    accelerated_.screen_prescale.destroy();
    accelerated_.frame = {};
    accelerated_.magnified = false;
}

void Runtime::area_filter_scene(const wr::Surface& scene) {
    const uint32_t width = match_world_cpu_.width;
    const uint32_t height = match_world_cpu_.height;
    const uint32_t scale = area_scale(match_zoom(), accelerated_.frame.draw_scale);
    auto& plan = accelerated_.area_plan;
    if (plan.scale() != scale || plan.picture_width() != width || plan.picture_height() != height)
        if (wr::plan_area_filter(plan, scale, width, height) != wr::AreaError::none)
            throw std::runtime_error("cannot weigh the area pass of the battlefield's scene");
    const wr::RgbSource source{scene.rgb.data(), scene.width, scene.height, scene.width};
    const wr::RgbTarget picture{match_world_cpu_.rgb.data(), width, height, width};
    if (wr::area_filter_rgb24(plan, source, picture, draw_pool_.get()) != wr::AreaError::none)
        throw std::runtime_error("cannot reduce the battlefield's scene by the area pass");
}

uint32_t Runtime::accelerated_prescale_factor(
    double scale, uint32_t width, uint32_t height, uint64_t charged
) const {
    // A target beyond the renderer's texture limit is split into tiles, so
    // the budget alone bounds the factor.
    const policy::PrescaleBudget budget{policy::prescale_budget(accelerated_.rung.card), charged};
    return policy::prescale_factor(budget, width, height, scale);
}

CardScale Runtime::accelerated_card_scale(
    double scale, uint32_t width, uint32_t height, const PrescaleTarget& target
) const {
    const auto filter = policy::chrome_filter(accelerated_.rung, scale);
    if (filter != policy::ScaleFilter::sharp_bilinear)
        return {filter, 1};
    // The factor the scale asks for, falling to what the target holds.
    auto factor = static_cast<uint32_t>(std::min(std::ceil(scale), most_prescale_factor));
    while (factor > 1 && (target.width() < factor * width || target.height() < factor * height))
        --factor;
    if (factor <= 1)
        return {policy::ScaleFilter::linear, 1};
    return {filter, factor};
}

void Runtime::ensure_accelerated_match_textures() {
    auto& state = accelerated_;
    // A new layout, or a display of another density, remakes the card's
    // textures and the overlay; the base and the area pass's weights belong
    // to the frame just drawn for it, and stay.
    const double density = match_display_density();
    if (state.layout_width != match_layout_.width || state.layout_height != match_layout_.height ||
        state.layout_density != density) {
        free_accelerated_layout_textures();
        state.layout_width = match_layout_.width;
        state.layout_height = match_layout_.height;
        state.layout_density = density;
    }
    const int battlefield_width = match_layout_.battlefield_width();
    const int battlefield_height = match_layout_.battlefield_height();
    if (battlefield_width <= 0 || battlefield_height <= 0)
        return;
    const auto bf_w = static_cast<uint32_t>(battlefield_width);
    const auto bf_h = static_cast<uint32_t>(battlefield_height);
    const uint32_t limit = state.texture_limit;
    // The HUD layer's prescale target, at the chrome's scale on the
    // display, made larger only when a taller build page needs it.
    const double chrome = match_layout_.scale * density;
    const uint32_t hud_w = match_hud_cpu_.width;
    const uint32_t hud_h = match_hud_cpu_.height;
    if (hud_w != 0 && hud_h != 0 &&
        policy::chrome_filter(state.rung, chrome) == policy::ScaleFilter::sharp_bilinear) {
        const uint32_t factor = accelerated_prescale_factor(chrome, hud_w, hud_h, 0);
        if (factor > 1 && (state.hud_prescale.width() < factor * hud_w ||
                           state.hud_prescale.height() < factor * hud_h))
            state.hud_prescale.ensure(
                sdl_.renderer,
                nullptr,
                std::max(state.hud_prescale.width(), factor * hud_w),
                std::max(state.hud_prescale.height(), factor * hud_h),
                limit,
                state.counts
            );
    }
    if (!state.rung.magnify)
        return;
    // The scene, at the largest a magnified frame draws, and the overlay.
    const auto largest = largest_magnified_scene(battlefield_width, battlefield_height);
    const auto scene_w = static_cast<uint32_t>(largest.width);
    const auto scene_h = static_cast<uint32_t>(largest.height);
    if (!state.scene.made())
        state.scene.create(
            sdl_.renderer, scene_w, scene_h, limit, SDL_BLENDMODE_NONE, state.counts
        );
    if (!state.overlay_texture.made()) {
        state.overlay_texture.create(
            sdl_.renderer, bf_w, bf_h, limit, SDL_BLENDMODE_BLEND, state.counts
        );
        state.overlay_uploaded = false;
        const uint32_t bands = platform::job_pool::bands_of_rows(bf_h, xrgb_band_rows);
        state.opaque_bands.assign(bands, 0);
        state.uploaded_bands.assign(bands, 0);
        state.overlay.assign(std::size_t{bf_w} * bf_h * 4U, 0);
    }
    // The scene's prescale target, at the largest any zoom above 1 needs on
    // the display within what the HUD's target leaves of the budget, in
    // tiles beyond the renderer's limit as the scene is.
    const bool prescaled = state.rung.card == policy::CardFilter::prescale_full ||
                           state.rung.card == policy::CardFilter::prescale_quarter;
    if (!prescaled || state.world_prescale.made())
        return;
    uint32_t widest = 0;
    uint32_t tallest = 0;
    for (int step = 1; step <= prescale_zoom_steps; ++step) {
        const float zoom = 1.0F + (kMaxBattlefieldZoom - 1.0F) * static_cast<float>(step) /
                                      static_cast<float>(prescale_zoom_steps);
        const double scale = static_cast<double>(zoom) * density;
        if (std::floor(scale) == scale)
            continue;
        const uint32_t region_w = std::min(magnified_corner(bf_w, zoom, scene_w) + 1, scene_w);
        const uint32_t region_h = std::min(magnified_corner(bf_h, zoom, scene_h) + 1, scene_h);
        const uint32_t factor =
            accelerated_prescale_factor(scale, region_w, region_h, state.hud_prescale.pixels());
        if (factor <= 1)
            continue;
        widest = std::max(widest, factor * region_w);
        tallest = std::max(tallest, factor * region_h);
    }
    const policy::PrescaleBudget left{
        policy::prescale_budget(state.rung.card), state.hud_prescale.pixels()
    };
    if (widest != 0 && uint64_t{widest} * tallest + left.charged <= left.limit)
        state.world_prescale.ensure(sdl_.renderer, nullptr, widest, tallest, limit, state.counts);
}

void Runtime::present_accelerated_match_layers(bool dialogs) {
    auto& state = accelerated_;
    // The front end's prescale target is not kept during a match.
    state.screen_prescale.destroy();
    const auto frame_format = opaque_layer_format();
    ensure_streaming_texture(
        match_hud_tex_,
        frame_format,
        static_cast<int>(match_hud_cpu_.width),
        static_cast<int>(match_hud_cpu_.height),
        match_hud_tex_w_,
        match_hud_tex_h_
    );
    ensure_accelerated_match_textures();
    const auto& frame = state.frame;
    const uint32_t bf_w = match_world_cpu_.width;
    const uint32_t bf_h = match_world_cpu_.height;
    const bool magnified = frame.method == SceneMethod::magnify && state.scene.made() &&
                           state.overlay_texture.made_for(bf_w, bf_h, state.texture_limit) &&
                           match_scene_cpu_.width == static_cast<uint32_t>(frame.scene_width) &&
                           match_scene_cpu_.height == static_cast<uint32_t>(frame.scene_height) &&
                           match_scene_cpu_.width <= state.scene.grid().width &&
                           match_scene_cpu_.height <= state.scene.grid().height &&
                           state.base.size() == match_world_cpu_.rgb.size();
    state.magnified = magnified;
    if (!magnified)
        match_world_tex_.ensure(
            sdl_.renderer,
            frame_format,
            static_cast<int>(bf_w),
            static_cast<int>(bf_h),
            render_texture_limit(),
            SDL_BLENDMODE_NONE
        );
    const auto* gamma = gamma_identity_ ? nullptr : &gamma_table_;
    const auto upload_start = std::chrono::steady_clock::now();
    upload_rgb24_frame(match_hud_tex_, match_hud_cpu_);
    if (magnified) {
        state.scene.upload_rgb24(
            match_scene_cpu_.rgb.data(),
            std::size_t{match_scene_cpu_.width} * 3U,
            match_scene_cpu_.width,
            match_scene_cpu_.height,
            gamma,
            draw_pool_.get()
        );
        // What the painters changed, uploaded in the bands that hold it now
        // or held it at the last upload, so the texture holds this frame's
        // overlay whatever frames came before.
        const std::size_t pitch = std::size_t{bf_w} * 4U;
        convert_rgb24_overlay_argb(
            match_world_cpu_.rgb.data(),
            state.base.data(),
            bf_w,
            bf_h,
            state.overlay.data(),
            pitch,
            gamma,
            state.opaque_bands,
            draw_pool_.get()
        );
        const auto bands = static_cast<uint32_t>(state.opaque_bands.size());
        const auto stale = [&](uint32_t band) {
            return !state.overlay_uploaded || state.opaque_bands[band] != 0 ||
                   state.uploaded_bands[band] != 0;
        };
        for (uint32_t band = 0; band < bands;) {
            if (!stale(band)) {
                ++band;
                continue;
            }
            uint32_t end = band + 1;
            while (end < bands && stale(end))
                ++end;
            state.overlay_texture.update(
                state.overlay.data(),
                static_cast<int>(pitch),
                4,
                band * xrgb_band_rows,
                std::min(bf_h, end * xrgb_band_rows)
            );
            band = end;
        }
        state.uploaded_bands = state.opaque_bands;
        state.overlay_uploaded = true;
    } else {
        upload_rgb24_tiles(match_world_tex_, match_world_cpu_, gamma);
    }
    const auto present_start = std::chrono::steady_clock::now();
    phase_times_.upload += nanoseconds_since(upload_start);
    if (!SDL_SetRenderDrawColor(sdl_.renderer, 0, 0, 0, 255) || !SDL_RenderClear(sdl_.renderer))
        throw_present_error("SDL_RenderClear");
    // Every scale is the one at the display's pixels: on a window at native
    // density the layout's scale times the density.
    const double density = match_display_density();
    // The HUD strips by the chrome's filter.
    std::vector<SharpPart> strips;
    for (const auto& strip : match_hud_strips())
        strips.push_back(
            {{static_cast<float>(strip.source_x),
              static_cast<float>(strip.source_y),
              static_cast<float>(strip.source_w),
              static_cast<float>(strip.source_h)},
             {static_cast<float>(strip.x),
              static_cast<float>(strip.y),
              static_cast<float>(strip.w),
              static_cast<float>(strip.h)}}
        );
    sharp_draw(
        sdl_.renderer,
        nullptr,
        match_hud_tex_,
        match_hud_cpu_.width,
        match_hud_cpu_.height,
        strips,
        accelerated_card_scale(
            match_layout_.scale * density,
            match_hud_cpu_.width,
            match_hud_cpu_.height,
            state.hud_prescale
        ),
        state.hud_revision,
        state.hud_prescale,
        state.counts
    );
    const SDL_Rect battlefield{
        match_layout_.left, match_layout_.top, static_cast<int>(bf_w), static_cast<int>(bf_h)
    };
    const SDL_FRect world{
        static_cast<float>(battlefield.x),
        static_cast<float>(battlefield.y),
        static_cast<float>(bf_w),
        static_cast<float>(bf_h)
    };
    if (!magnified) {
        // 1:1 in layout pixels, at the density on the display; below zoom
        // 1, where the area pass runs, its result, made at the layout's
        // size at every density.
        draw_one_to_one(sdl_.renderer, match_world_tex_, nullptr, &world, one_to_one_scale_mode());
    } else {
        const float zoom = match_zoom();
        const uint32_t scene_w = match_scene_cpu_.width;
        const uint32_t scene_h = match_scene_cpu_.height;
        const uint32_t width = magnified_corner(bf_w, zoom, scene_w);
        const uint32_t height = magnified_corner(bf_h, zoom, scene_h);
        const SDL_Rect destination{
            battlefield.x,
            battlefield.y,
            static_cast<int>(std::lround(static_cast<double>(width) * zoom)),
            static_cast<int>(std::lround(static_cast<double>(height) * zoom))
        };
        draw_scaled_world(
            sdl_.renderer,
            nullptr,
            state.scene,
            scene_w,
            scene_h,
            width,
            height,
            destination,
            battlefield,
            accelerated_card_scale(
                world_display_scale(frame, zoom, density),
                std::min(width + 1, scene_w),
                std::min(height + 1, scene_h),
                state.world_prescale
            ),
            state.world_prescale,
            state.counts
        );
        draw_one_to_one(
            sdl_.renderer, state.overlay_texture, nullptr, &world, one_to_one_scale_mode()
        );
    }
    finish_match_layers(frame_format, dialogs, upload_start, present_start);
}

void Runtime::draw_accelerated_screen(
    SDL_Texture* texture, int width, int height, uint64_t revision
) {
    if (texture == nullptr || width <= 0 || height <= 0)
        return;
    // The letterbox's scale: window pixels per frame pixel.
    SDL_FRect area{};
    if (!SDL_GetRenderLogicalPresentationRect(sdl_.renderer, &area) || !(area.w > 0.0F))
        throw AccelerationError(
            std::string("SDL_GetRenderLogicalPresentationRect: ") + SDL_GetError()
        );
    const double scale = static_cast<double>(area.w) / static_cast<double>(width);
    const auto w = static_cast<uint32_t>(width);
    const auto h = static_cast<uint32_t>(height);
    auto& state = accelerated_;
    if (policy::chrome_filter(state.rung, scale) == policy::ScaleFilter::sharp_bilinear) {
        const uint32_t factor = accelerated_prescale_factor(scale, w, h, 0);
        if (factor > 1)
            state.screen_prescale.ensure(
                sdl_.renderer, nullptr, factor * w, factor * h, state.texture_limit, state.counts
            );
    }
    const SharpPart whole{
        {0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height)},
        {0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height)}
    };
    sharp_draw(
        sdl_.renderer,
        nullptr,
        texture,
        w,
        h,
        {&whole, 1},
        accelerated_card_scale(scale, w, h, state.screen_prescale),
        revision,
        state.screen_prescale,
        state.counts
    );
}

void Runtime::ensure_screen_world() {
    if (!match_ || screen_ != Screen::match)
        return;
    const auto method = accelerated_.frame.method;
    if (method != SceneMethod::area && method != SceneMethod::magnify)
        return;

    // The world drawn again as the standard tier draws it, at the moment
    // the last frame showed; its units counted for this draw alone, and the
    // resource readout, which the frame's draw eased, not eased again.
    struct Restore {
        Runtime& runtime;
        frame_pacing::FrameDrawCounts draws;
        float alpha{};
        std::optional<uint32_t> readout_eases;

        ~Restore() {
            runtime.accelerated_.suspended = false;
            runtime.set_presentation_alpha(alpha);
            runtime.frame_draws_ = draws;
            runtime.readout_eases_ = readout_eases;
        }
    } restore{*this, frame_draws_, presentation_alpha(), readout_eases_};

    frame_draws_ = {};
    readout_eases_ = 0U;
    set_presentation_alpha(accelerated_.frame_alpha);
    accelerated_.suspended = true;
    refresh_filtered_terrain();
    render_match_surface();
}

} // namespace oa::app
