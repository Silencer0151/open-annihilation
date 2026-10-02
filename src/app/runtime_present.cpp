// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// SDL output textures, viewport sizing and frame presentation.
#include "oa/app/runtime.hpp"
#include "xrgb_conversion.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <tuple>

namespace oa::app {
namespace {

[[nodiscard]] int64_t elapsed_since(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now() - since
    )
        .count();
}

} // namespace

void Runtime::ensure_texture(int width, int height) {
    if (sdl_.texture != nullptr && output_texture_w_ == width && output_texture_h_ == height)
        return;
    if (sdl_.texture != nullptr) {
        SDL_DestroyTexture(sdl_.texture);
        sdl_.texture = nullptr;
    }
    sdl_.texture = SDL_CreateTexture(
        sdl_.renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height
    );
    if (sdl_.texture == nullptr)
        throw std::runtime_error(std::string("SDL_CreateTexture: ") + SDL_GetError());
    if (!SDL_SetTextureScaleMode(sdl_.texture, SDL_SCALEMODE_NEAREST))
        throw std::runtime_error(std::string("SDL texture scale: ") + SDL_GetError());
    output_texture_w_ = width;
    output_texture_h_ = height;
}

void Runtime::apply_output_mode() {
    if (sdl_.renderer == nullptr)
        return;
    if (screen_ == Screen::match) {
        int width = kCanvasWidth, height = kCanvasHeight;
        if (sdl_.window != nullptr)
            SDL_GetWindowSizeInPixels(sdl_.window, &width, &height);
        const auto laid_out = match_layout_;
        match_layout_ = oa::ui::display_layout::make_match_layout(width, height);
        // On a screen of another size the pointer's place is known again
        // only once SDL reports it.
        if (match_layout_.width != laid_out.width || match_layout_.height != laid_out.height)
            match_pointer_known_ = false;
        if (!SDL_SetRenderLogicalPresentation(
                sdl_.renderer,
                match_layout_.width,
                match_layout_.height,
                SDL_LOGICAL_PRESENTATION_DISABLED
            ))
            throw std::runtime_error(std::string("SDL logical presentation: ") + SDL_GetError());
        ensure_texture(match_layout_.width, match_layout_.height);
        // A build page laid out for a column of another height, or one that
        // no longer fits, is laid out again for this one.
        if (match_hud_ && match_build_page_ > 0 && selected_match_unit_ != 0 &&
            match_column_rows() != match_hud_fit_rows_) {
            const auto& gadgets = match_hud_->layout.gadgets;
            const bool overflows = std::any_of(
                gadgets.begin() + (gadgets.empty() ? 0 : 1),
                gadgets.end(),
                [&](const auto& gadget) {
                    return gadget.common.active != 0 && gadget.common.height > 0 &&
                           gadget.common.y + gadget.common.height > match_column_rows();
                }
            );
            if (match_hud_fitted_ || overflows)
                show_match_build_page(match_build_page_);
        }
    } else {
        // The load and save dialogs and the in-game briefing keep the size of the
        // frame they are drawn over, and the end screen the match's size while
        // it darkens the match's last frame.
        match_layout_ = {};
        match_pointer_known_ = false;
        int width = kCanvasWidth, height = kCanvasHeight;
        if (const auto* parent = panel_parent(); parent != nullptr) {
            width = static_cast<int>(parent->width);
            height = static_cast<int>(parent->height);
        } else if (const auto* battlefield = end_screen_battlefield_size()) {
            width = battlefield->width;
            height = battlefield->height;
        }
        if (!SDL_SetRenderLogicalPresentation(
                sdl_.renderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX
            ))
            throw std::runtime_error(std::string("SDL logical presentation: ") + SDL_GetError());
        ensure_texture(width, height);
    }
}

[[nodiscard]] float Runtime::match_zoom() const {
    return match_zoom_;
}

[[nodiscard]] int Runtime::visible_map_width() const {
    return std::max(
        1,
        static_cast<int>(std::lround(
            static_cast<double>(match_layout_.battlefield_width()) /
            static_cast<double>(match_zoom())
        ))
    );
}

[[nodiscard]] int Runtime::visible_map_height() const {
    return std::max(
        1,
        static_cast<int>(std::lround(
            static_cast<double>(match_layout_.battlefield_height()) /
            static_cast<double>(match_zoom())
        ))
    );
}

[[nodiscard]] oa::present::world_renderer::BattlefieldViewport
Runtime::live_viewport(uint32_t camera_x, uint32_t camera_y) const {
    return {
        camera_x,
        camera_y,
        match_layout_.left,
        match_layout_.top,
        static_cast<uint32_t>(match_layout_.battlefield_width()),
        static_cast<uint32_t>(match_layout_.battlefield_height()),
        static_cast<uint32_t>(match_layout_.width),
        static_cast<uint32_t>(match_layout_.height),
        match_zoom()
    };
}

void Runtime::initialize_sdl() {
    if (sdl_.window == nullptr || sdl_.renderer == nullptr) {
        if (!SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1"))
            throw std::runtime_error("SDL mouse focus click-through hint was rejected");
        // Closing the window reaches the game as a close request, which a
        // running match answers with its surrender confirmation, rather than
        // as a quit SDL adds on its own.
        if (!SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0"))
            throw std::runtime_error("SDL last-window quit hint was rejected");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
            throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
        sdl_.window = SDL_CreateWindow(
            "Open Annihilation",
            kDefaultWindowWidth,
            kDefaultWindowHeight,
            game_window_flags(options_.start_full_screen)
        );
        if (sdl_.window == nullptr)
            throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
        sdl_.renderer = SDL_CreateRenderer(sdl_.window, nullptr);
        if (sdl_.renderer == nullptr)
            throw std::runtime_error(std::string("SDL_CreateRenderer: ") + SDL_GetError());
    }
    apply_output_mode();
    load_game_cursors();
    if (cursors_loaded_)
        SDL_HideCursor();
    install_engine_settings_menu_item();
}

bool Runtime::take_full_screen_event(const SDL_Event& event) {
    return oa::app::take_full_screen_event(sdl_.window, full_screen_switch_, event);
}

void Runtime::take_full_screen_switch(const FullScreenSwitch& full_screen) noexcept {
    full_screen_switch_ = full_screen;
}

SDL_Texture* Runtime::ensure_streaming_texture(
    SDL_Texture* existing,
    SDL_PixelFormat format,
    int width,
    int height,
    int& stored_w,
    int& stored_h
) {
    if (existing != nullptr && existing->format == format && stored_w == width &&
        stored_h == height)
        return existing;
    if (existing != nullptr)
        SDL_DestroyTexture(existing);
    auto* texture =
        SDL_CreateTexture(sdl_.renderer, format, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (texture == nullptr)
        throw std::runtime_error(std::string("SDL_CreateTexture: ") + SDL_GetError());
    if (!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST))
        throw std::runtime_error(std::string("SDL texture scale: ") + SDL_GetError());
    stored_w = width;
    stored_h = height;
    return texture;
}

SDL_PixelFormat Runtime::frame_texture_format() const {
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        return SDL_PIXELFORMAT_XRGB8888;
    const char* name = SDL_GetRendererName(sdl_.renderer);
    const bool software = name != nullptr && std::strcmp(name, SDL_SOFTWARE_RENDERER) == 0;
    return software && SDL_GetWindowPixelFormat(sdl_.window) == SDL_PIXELFORMAT_RGB565
               ? SDL_PIXELFORMAT_RGB565
               : SDL_PIXELFORMAT_XRGB8888;
}

void Runtime::upload_rgb24_frame(SDL_Texture* texture, const renderer::Surface& source) {
    if (texture == nullptr || source.rgb.empty())
        return;
    void* pixels = nullptr;
    int pitch = 0;
    if (!SDL_LockTexture(texture, nullptr, &pixels, &pitch))
        throw std::runtime_error(std::string("SDL_LockTexture: ") + SDL_GetError());
    const auto convert =
        texture->format == SDL_PIXELFORMAT_RGB565 ? convert_rgb24_rgb565 : convert_rgb24_xrgb;
    convert(
        source.rgb.data(),
        source.width,
        source.height,
        static_cast<uint8_t*>(pixels),
        static_cast<std::size_t>(pitch),
        gamma_identity_ ? nullptr : &gamma_table_,
        draw_pool_.get()
    );
    SDL_UnlockTexture(texture);
}

void Runtime::destroy_match_layer_textures() {
    if (match_hud_tex_ != nullptr) {
        SDL_DestroyTexture(match_hud_tex_);
        match_hud_tex_ = nullptr;
    }
    if (match_world_tex_ != nullptr) {
        SDL_DestroyTexture(match_world_tex_);
        match_world_tex_ = nullptr;
    }
    if (match_cursor_tex_ != nullptr) {
        SDL_DestroyTexture(match_cursor_tex_);
        match_cursor_tex_ = nullptr;
    }
    if (match_dialog_tex_ != nullptr) {
        SDL_DestroyTexture(match_dialog_tex_);
        match_dialog_tex_ = nullptr;
    }
    if (match_dialog_side_tex_ != nullptr) {
        SDL_DestroyTexture(match_dialog_side_tex_);
        match_dialog_side_tex_ = nullptr;
    }
    destroy_engine_settings_textures();
    match_dialog_tex_w_ = match_dialog_tex_h_ = 0;
    match_dialog_side_tex_w_ = match_dialog_side_tex_h_ = 0;
    match_hud_tex_w_ = match_hud_tex_h_ = 0;
    match_world_tex_w_ = match_world_tex_h_ = 0;
    match_cursor_tex_w_ = match_cursor_tex_h_ = 0;
}

std::array<Runtime::HudStrip, 3> Runtime::match_hud_strips() const {
    const int left = match_layout_.left;
    const int bar_w = match_layout_.hud_width - left;
    // A build page laid out past 480 rows grows the HUD; the side column then
    // shows those rows too, at the chrome's scale.
    int column_rows = kCanvasHeight;
    int column_height = match_layout_.hud_height;
    if (const auto rows = static_cast<int>(match_hud_cpu_.height); rows > kCanvasHeight) {
        column_rows = rows;
        column_height = std::min(
            match_layout_.height, static_cast<int>(std::lround(rows * match_layout_.scale))
        );
    }
    return {{
        {0, 0, kBattlefieldLeft, column_rows, 0, 0, left, column_height},
        {kBattlefieldLeft,
         0,
         kBattlefieldWidth,
         kBattlefieldTop,
         left,
         0,
         bar_w,
         match_layout_.top},
        {kBattlefieldLeft,
         kCanvasHeight - kBattlefieldBottom,
         kBattlefieldWidth,
         kBattlefieldBottom,
         left,
         match_layout_.bottom_bar_y(),
         bar_w,
         match_layout_.bottom},
    }};
}

void Runtime::compose_match_layers(renderer::Surface& frame) {
    frame.width = static_cast<uint32_t>(match_layout_.width);
    frame.height = static_cast<uint32_t>(match_layout_.height);
    frame.rgb.assign(static_cast<std::size_t>(frame.width) * frame.height * 3U, 0);
    if (match_hud_cpu_.rgb.empty() || match_world_cpu_.rgb.empty())
        return;
    for (const auto& strip : match_hud_strips())
        scale_blit(
            frame,
            match_hud_cpu_,
            strip.x,
            strip.y,
            strip.w,
            strip.h,
            strip.source_x,
            strip.source_y,
            strip.source_w,
            strip.source_h
        );
    blit_rect(
        frame,
        match_world_cpu_,
        match_layout_.left,
        match_layout_.top,
        0,
        0,
        static_cast<int>(match_world_cpu_.width),
        static_cast<int>(match_world_cpu_.height)
    );
    if (!match_dialog_side_.rgb.empty() && placed_panel_area())
        blit_rect(
            frame,
            match_dialog_side_,
            match_dialog_side_at_.x,
            match_dialog_side_at_.y,
            0,
            0,
            static_cast<int>(match_dialog_side_.width),
            static_cast<int>(match_dialog_side_.height)
        );
}

void Runtime::compose_match_frame(renderer::Surface& frame) {
    compose_match_layers(frame);
    if (match_hud_cpu_.rgb.empty() || match_world_cpu_.rgb.empty())
        return;
    const auto pixels = static_cast<std::size_t>(frame.width) * frame.height;
    apply_gamma_rgb(frame.rgb.data(), pixels, 3);
    compose_engine_settings_layer(frame);
    if (!match_use_layers_ || oa::ui::frontend_dialogs::dialog_count() == 0 ||
        match_dialog_rgba_.size() != pixels * 4U)
        return;
    for (std::size_t i = 0; i < pixels; ++i) {
        const auto* source = match_dialog_rgba_.data() + i * 4U;
        const unsigned alpha = source[3];
        auto* target = frame.rgb.data() + i * 3U;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const unsigned shown =
                gamma_identity_ ? source[channel] : gamma_table_[source[channel]];
            target[channel] =
                static_cast<uint8_t>((shown * alpha + target[channel] * (255U - alpha)) / 255U);
        }
    }
}

bool Runtime::compose_match_dialog_layer() {
    if (oa::ui::frontend_dialogs::dialog_count() == 0)
        return false;
    if (match_hud_ && !match_hud_->layout.gadgets.empty()) {
        const auto& root = match_hud_->layout.gadgets.front().common;
        oa::ui::frontend_dialogs::dialog_shade_below(
            match_hud_cpu_, root.x, root.y, root.width, root.height, match_palette_
        );
    }
    oa::ui::frontend_dialogs::dialog_draw_layer(
        static_cast<uint32_t>(match_layout_.width),
        static_cast<uint32_t>(match_layout_.height),
        match_dialog_rgba_
    );
    if (match_dialog_tex_ == nullptr || match_dialog_tex_w_ != match_layout_.width ||
        match_dialog_tex_h_ != match_layout_.height) {
        if (match_dialog_tex_ != nullptr)
            SDL_DestroyTexture(match_dialog_tex_);
        match_dialog_tex_ = SDL_CreateTexture(
            sdl_.renderer,
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STREAMING,
            match_layout_.width,
            match_layout_.height
        );
        if (match_dialog_tex_ == nullptr ||
            !SDL_SetTextureBlendMode(match_dialog_tex_, SDL_BLENDMODE_BLEND) ||
            !SDL_SetTextureScaleMode(match_dialog_tex_, SDL_SCALEMODE_NEAREST))
            throw std::runtime_error(std::string("SDL dialog layer: ") + SDL_GetError());
        match_dialog_tex_w_ = match_layout_.width;
        match_dialog_tex_h_ = match_layout_.height;
    }
    const uint8_t* dialog_pixels = match_dialog_rgba_.data();
    std::vector<uint8_t> corrected;
    if (!gamma_identity_) {
        corrected = match_dialog_rgba_;
        apply_gamma_rgb(corrected.data(), corrected.size() / 4U, 4);
        dialog_pixels = corrected.data();
    }
    if (!SDL_UpdateTexture(match_dialog_tex_, nullptr, dialog_pixels, match_layout_.width * 4))
        throw std::runtime_error(std::string("SDL dialog layer upload: ") + SDL_GetError());
    return true;
}

void Runtime::present_match_layers() {
    if (match_hud_cpu_.rgb.empty() || match_world_cpu_.rgb.empty())
        return;
    const bool dialogs = compose_match_dialog_layer();
    const auto frame_format = frame_texture_format();
    match_hud_tex_ = ensure_streaming_texture(
        match_hud_tex_,
        frame_format,
        static_cast<int>(match_hud_cpu_.width),
        static_cast<int>(match_hud_cpu_.height),
        match_hud_tex_w_,
        match_hud_tex_h_
    );
    match_world_tex_ = ensure_streaming_texture(
        match_world_tex_,
        frame_format,
        static_cast<int>(match_world_cpu_.width),
        static_cast<int>(match_world_cpu_.height),
        match_world_tex_w_,
        match_world_tex_h_
    );
    const auto upload_start = std::chrono::steady_clock::now();
    upload_rgb24_frame(match_hud_tex_, match_hud_cpu_);
    upload_rgb24_frame(match_world_tex_, match_world_cpu_);
    const auto present_start = std::chrono::steady_clock::now();
    phase_times_.upload += elapsed_since(upload_start);
    // The clear is the blank fill for strip area beyond the chrome's largest
    // (1280x1024) size: right of the bars and under the side column.
    if (!SDL_SetRenderDrawColor(sdl_.renderer, 0, 0, 0, 255) || !SDL_RenderClear(sdl_.renderer))
        throw std::runtime_error(std::string("SDL_RenderClear: ") + SDL_GetError());
    for (const auto& strip : match_hud_strips()) {
        const SDL_FRect source{
            static_cast<float>(strip.source_x),
            static_cast<float>(strip.source_y),
            static_cast<float>(strip.source_w),
            static_cast<float>(strip.source_h)
        };
        const SDL_FRect destination{
            static_cast<float>(strip.x),
            static_cast<float>(strip.y),
            static_cast<float>(strip.w),
            static_cast<float>(strip.h)
        };
        if (!SDL_RenderTexture(sdl_.renderer, match_hud_tex_, &source, &destination))
            throw std::runtime_error(std::string("SDL_RenderTexture: ") + SDL_GetError());
    }
    const SDL_FRect world{
        static_cast<float>(match_layout_.left),
        static_cast<float>(match_layout_.top),
        static_cast<float>(match_world_cpu_.width),
        static_cast<float>(match_world_cpu_.height)
    };
    if (!SDL_RenderTexture(sdl_.renderer, match_world_tex_, nullptr, &world))
        throw std::runtime_error(std::string("SDL_RenderTexture: ") + SDL_GetError());
    // A placed dialog's part over the side column goes over the HUD layer.
    if (!match_dialog_side_.rgb.empty() && placed_panel_area()) {
        match_dialog_side_tex_ = ensure_streaming_texture(
            match_dialog_side_tex_,
            frame_format,
            static_cast<int>(match_dialog_side_.width),
            static_cast<int>(match_dialog_side_.height),
            match_dialog_side_tex_w_,
            match_dialog_side_tex_h_
        );
        upload_rgb24_frame(match_dialog_side_tex_, match_dialog_side_);
        const SDL_FRect side{
            static_cast<float>(match_dialog_side_at_.x),
            static_cast<float>(match_dialog_side_at_.y),
            static_cast<float>(match_dialog_side_.width),
            static_cast<float>(match_dialog_side_.height)
        };
        if (!SDL_RenderTexture(sdl_.renderer, match_dialog_side_tex_, nullptr, &side))
            throw std::runtime_error(std::string("SDL_RenderTexture: ") + SDL_GetError());
    }
    present_engine_settings_layer();
    if (dialogs && !SDL_RenderTexture(sdl_.renderer, match_dialog_tex_, nullptr, nullptr))
        throw std::runtime_error(std::string("SDL_RenderTexture: ") + SDL_GetError());
    present_software_cursor();
    capture_render_target();
    if (!SDL_RenderPresent(sdl_.renderer))
        throw std::runtime_error(std::string("SDL_RenderPresent: ") + SDL_GetError());
    phase_times_.present += elapsed_since(present_start);
    frame_pacing::note_frame_measure(
        frame_stats_,
        frame_pacing::FrameMeasure::present,
        static_cast<uint64_t>(elapsed_since(upload_start))
    );
}

void Runtime::capture_render_target() {
    if (video_capture_)
        video_capture_->add_frame(sdl_.renderer);
    if (capture_frame_ == nullptr)
        return;
    SDL_Surface* target = SDL_RenderReadPixels(sdl_.renderer, nullptr);
    SDL_Surface* rgb =
        target != nullptr ? SDL_ConvertSurface(target, SDL_PIXELFORMAT_RGB24) : nullptr;
    SDL_DestroySurface(target);
    if (rgb == nullptr)
        throw std::runtime_error(std::string("SDL_RenderReadPixels: ") + SDL_GetError());
    const auto width = static_cast<std::size_t>(rgb->w);
    capture_frame_->width = static_cast<uint32_t>(rgb->w);
    capture_frame_->height = static_cast<uint32_t>(rgb->h);
    capture_frame_->rgb.resize(width * static_cast<std::size_t>(rgb->h) * 3U);
    for (int row = 0; row < rgb->h; ++row)
        std::memcpy(
            capture_frame_->rgb.data() + static_cast<std::size_t>(row) * width * 3U,
            static_cast<const uint8_t*>(rgb->pixels) +
                static_cast<std::ptrdiff_t>(row) * rgb->pitch,
            width * 3U
        );
    SDL_DestroySurface(rgb);
}

void Runtime::present_software_cursor() {
    if (!cursors_loaded_ || cursor_image_ == nullptr)
        return;
    const auto rendered = oa::formats::gaf::render_normal(*cursor_image_);
    if (!rendered.ok())
        return;
    const auto& frame = *rendered.frame;
    if (match_cursor_tex_ == nullptr || match_cursor_tex_w_ != static_cast<int>(frame.width) ||
        match_cursor_tex_h_ != static_cast<int>(frame.height)) {
        if (match_cursor_tex_ != nullptr)
            SDL_DestroyTexture(match_cursor_tex_);
        match_cursor_tex_ = SDL_CreateTexture(
            sdl_.renderer,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            static_cast<int>(frame.width),
            static_cast<int>(frame.height)
        );
        if (match_cursor_tex_ == nullptr)
            return;
        // Unblended, the cursor would cover the battlefield with its clear
        // pixels; a texture that cannot blend is dropped as one that cannot
        // be made is, and the next frame tries again.
        if (!SDL_SetTextureBlendMode(match_cursor_tex_, SDL_BLENDMODE_BLEND)) {
            SDL_DestroyTexture(match_cursor_tex_);
            match_cursor_tex_ = nullptr;
            return;
        }
        match_cursor_tex_w_ = static_cast<int>(frame.width);
        match_cursor_tex_h_ = static_cast<int>(frame.height);
    }
    void* pixels = nullptr;
    int pitch = 0;
    if (!SDL_LockTexture(match_cursor_tex_, nullptr, &pixels, &pitch))
        return;
    const auto& pal = match_palette_.size() >= 1024 ? match_palette_ : resources_.gui_palette;
    for (uint32_t row = 0; row < frame.height; ++row) {
        auto* dst = reinterpret_cast<uint32_t*>(
            static_cast<uint8_t*>(pixels) + static_cast<std::size_t>(row) * pitch
        );
        for (uint32_t column = 0; column < frame.width; ++column) {
            const auto offset = static_cast<std::size_t>(row) * frame.width + column;
            if (offset >= frame.coverage.size() || frame.coverage[offset] == 0) {
                dst[column] = 0;
                continue;
            }
            const auto pal_i = static_cast<std::size_t>(frame.pixels[offset]) * 4U;
            if (pal_i + 2 >= pal.size()) {
                dst[column] = 0;
                continue;
            }
            dst[column] = 0xff000000u | (static_cast<uint32_t>(pal[pal_i]) << 16) |
                          (static_cast<uint32_t>(pal[pal_i + 1]) << 8) |
                          static_cast<uint32_t>(pal[pal_i + 2]);
        }
        if (!gamma_identity_)
            gamma_xrgb_row(dst, static_cast<int>(frame.width), gamma_table_);
    }
    SDL_UnlockTexture(match_cursor_tex_);
    const SDL_FRect dest{
        pointer_x_ - static_cast<float>(frame.origin_x),
        pointer_y_ - static_cast<float>(frame.origin_y),
        static_cast<float>(frame.width),
        static_cast<float>(frame.height)
    };
    // A cursor the renderer refuses is missing from this frame alone: the
    // next frame draws it again.
    std::ignore = SDL_RenderTexture(sdl_.renderer, match_cursor_tex_, nullptr, &dest);
}

void Runtime::render() {
    const auto compose_start = std::chrono::steady_clock::now();
    rebuild_surface();
    const auto composed = elapsed_since(compose_start);
    phase_times_.compose += composed;
    frame_pacing::note_frame_measure(
        frame_stats_, frame_pacing::FrameMeasure::draw, static_cast<uint64_t>(composed)
    );
    // The loading screen went out through the display sink as it was drawn.
    if (screen_ == Screen::loading)
        return;
    if (screen_ == Screen::match && match_use_layers_) {
        present_match_layers();
        return;
    }
    // A match's surface_ is composed at the display gamma already.
    const auto present_start = std::chrono::steady_clock::now();
    const uint8_t* frame = surface_.rgb.data();
    std::vector<uint8_t> corrected;
    if (!gamma_identity_ && screen_ != Screen::match) {
        corrected = surface_.rgb;
        apply_gamma_rgb(corrected.data(), corrected.size() / 3U, 3);
        frame = corrected.data();
    }
    if (!SDL_UpdateTexture(sdl_.texture, nullptr, frame, static_cast<int>(surface_.width * 3U)) ||
        !SDL_RenderClear(sdl_.renderer) ||
        !SDL_RenderTexture(sdl_.renderer, sdl_.texture, nullptr, nullptr))
        throw std::runtime_error(std::string("SDL render: ") + SDL_GetError());
    capture_render_target();
    if (!SDL_RenderPresent(sdl_.renderer))
        throw std::runtime_error(std::string("SDL render: ") + SDL_GetError());
    frame_pacing::note_frame_measure(
        frame_stats_,
        frame_pacing::FrameMeasure::present,
        static_cast<uint64_t>(elapsed_since(present_start))
    );
}

[[nodiscard]] oa::ui::gui_input::MenuObject Runtime::input_menu() const {
    return {resources_.layout.gadgets, selected_};
}

void write_ppm(const fs::path& path, const renderer::Surface& surface) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot create snapshot: " + path.string());
    output << "P6\n" << surface.width << ' ' << surface.height << "\n255\n";
    output.write(
        reinterpret_cast<const char*>(surface.rgb.data()),
        static_cast<std::streamsize>(surface.rgb.size())
    );
    if (!output)
        throw std::runtime_error("cannot finish snapshot: " + path.string());
}

} // namespace oa::app
