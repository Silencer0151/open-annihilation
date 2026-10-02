// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The in-game menu's OA button under Resume and the settings dialog beside
// the darkened column: an overlay on the match takes their input, and a
// layer of their own goes over the match's layers.

#include "engine_settings_match_host.hpp"

#include "oa/app/runtime.hpp"
#include "oa/ui/frontend_dialogs.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

namespace oa::app {

namespace settings = oa::ui::engine_settings;
namespace layout = oa::ui::display_layout;

namespace {

/// The match overlay's z: over the extensions' overlays, as the main menu's dialog is.
constexpr int16_t kMatchOverlayZ = 100;

/// The layer's opacity where it darkens the screen, in 255ths.
constexpr uint32_t kBackdropAlpha = (settings::ingame_backdrop_opacity * 255U + 128U) / 256U;

/// The sound opening the dialog and OK play, as on the main menu.
constexpr std::string_view kOpenSound = "Options";
/// The sound Cancel plays, as on the main menu.
constexpr std::string_view kCancelSound = "Previous";

/// Scales a source length by the side column's scale.
///
/// @param value source pixels
/// @param scale the side column's scale
/// @return window pixels, at least 1
int32_t scaled_length(int32_t value, double scale) noexcept {
    return std::max(1, static_cast<int32_t>(std::lround(static_cast<double>(value) * scale)));
}

/// Tells whether a point lies in a rectangle.
///
/// @param rect the rectangle
/// @param x the point's column
/// @param y the point's row
/// @return true inside
bool rect_contains(const layout::Rect& rect, float x, float y) noexcept {
    return x >= static_cast<float>(rect.x) && y >= static_cast<float>(rect.y) &&
           x < static_cast<float>(rect.x + rect.width) &&
           y < static_cast<float>(rect.y + rect.height);
}

} // namespace

void Runtime::destroy_engine_settings_match_host(EngineSettingsMatchHost* host) noexcept {
    if (host != nullptr && host->layer != nullptr)
        SDL_DestroyTexture(host->layer);
    delete host;
}

Runtime::EngineSettingsMatchHost& Runtime::engine_settings_match_host() {
    if (!engine_settings_match_)
        engine_settings_match_.reset(new EngineSettingsMatchHost{});
    return *engine_settings_match_;
}

void Runtime::register_engine_settings_match_overlay() {
    OverlayDesc match{};
    match.name = "engine settings in a match";
    match.screen = screen_id(Screen::match);
    match.z = kMatchOverlayZ;
    match.event = EngineSettingsMatchHost::overlay_event;
    match.state = this;
    // A refused overlay is recorded in the registry, and register_screens
    // reports it once every screen and overlay is in.
    overlay_register(&screens_, &match);
    OverlayDesc cleanup{};
    cleanup.name = "engine settings left with the in-game menu";
    cleanup.screen = kScreenAny;
    cleanup.z = kMatchOverlayZ;
    cleanup.tick = EngineSettingsMatchHost::overlay_tick;
    cleanup.state = this;
    overlay_register(&screens_, &cleanup);
}

void Runtime::open_engine_settings_in_match() {
    if (screen_ != Screen::match || !match_ || match_finished_ ||
        engine_settings_dialog() != nullptr || engine_settings_fonts() == nullptr)
        return;
    if (!ingame_menu_column_shown()) {
        // From play only: a panel the menu opened, a team panel or a message
        // box keeps the column.
        if (pause_menu_shown() || oa::ui::frontend_dialogs::dialog_count() != 0)
            return;
        show_match_pause_menu();
        if (!ingame_menu_column_shown())
            return;
    }
    // The dialog is drawn and fed from engine_settings_dialog().
    open_engine_settings_dialog();
    play_ui_sound(kOpenSound, 0);
    auto& host = engine_settings_match_host();
    host.dialog_open = true;
    host.button_hovered = false;
    host.button_pressed = false;
    ++host.revision;
}

settings::Locks Runtime::engine_settings_locks() const {
    settings::GameState state{};
    state.in_game = static_cast<bool>(match_);
    const uint32_t extension = current_extension_state();
    state.shared_game = (extension & extension_state::shared_match) != 0;
    state.replay = (extension & extension_state::replay) != 0;
    state.frame_rate_from_command_line = options_.max_frames_per_second_given;
    return settings::settings_locks(state);
}

bool Runtime::EngineSettingsMatchHost::button_shown(Runtime& runtime) {
    return runtime.ingame_menu_column_shown() && runtime.engine_settings_fonts() != nullptr;
}

layout::Rect
Runtime::EngineSettingsMatchHost::button_rect(const layout::MatchLayout& match) noexcept {
    // The side column hangs from the window's top left corner at its scale.
    return {
        static_cast<int>(std::lround(static_cast<double>(button_source_x) * match.scale)),
        static_cast<int>(std::lround(static_cast<double>(button_source_y) * match.scale)),
        scaled_length(settings::ingame_button_side, match.scale),
        scaled_length(settings::ingame_button_side, match.scale)
    };
}

layout::Rect
Runtime::EngineSettingsMatchHost::dialog_rect(const layout::MatchLayout& match) noexcept {
    const int32_t width = scaled_length(settings::dialog_width, match.scale);
    const int32_t height = scaled_length(settings::dialog_height, match.scale);
    return {
        match.left + (match.width - match.left - width) / 2,
        (match.height - height) / 2,
        width,
        height
    };
}

layout::Point Runtime::EngineSettingsMatchHost::dialog_point(
    const layout::MatchLayout& match, float x, float y
) noexcept {
    const auto rect = dialog_rect(match);
    const auto source = [](float offset, int32_t shown, int32_t drawn) {
        return static_cast<int>(
            std::floor(static_cast<double>(offset) * static_cast<double>(drawn) / shown)
        );
    };
    return {
        source(x - static_cast<float>(rect.x), rect.width, settings::dialog_width),
        source(y - static_cast<float>(rect.y), rect.height, settings::dialog_height)
    };
}

bool Runtime::EngineSettingsMatchHost::take_action(
    Runtime& runtime, settings::DialogAction action
) {
    auto& host = runtime.engine_settings_match_host();
    if (action == settings::DialogAction::none)
        return false;
    ++host.revision;
    if (!runtime.take_engine_settings_action(action))
        return false;
    host.dialog_open = false;
    host.button_hovered = false;
    host.button_pressed = false;
    return true;
}

void Runtime::EngineSettingsMatchHost::take_dialog_input(
    Runtime& runtime, settings::Dialog& dialog, const ScreenInput& input
) {
    const auto point = dialog_point(runtime.match_layout_, input.x, input.y);
    // OK and Cancel sound as they do on the main menu.
    const auto take_sounded = [&runtime](settings::DialogAction action) {
        if (action == settings::DialogAction::accepted)
            runtime.play_ui_sound(kOpenSound, 0);
        else if (action == settings::DialogAction::cancelled)
            runtime.play_ui_sound(kCancelSound, 0);
        return take_action(runtime, action);
    };
    switch (input.kind) {
    case ScreenInputKind::key_down: {
        if (engine_settings_shortcut(input.key, input.modifiers))
            return;
        const auto key = engine_settings_dialog_key(input.key, input.modifiers);
        if (!key)
            return;
        // The key that closed the dialog does nothing more until it is
        // released, as on the main menu.
        if (take_sounded(settings::dialog_key(dialog, *key)))
            runtime.engine_settings_match_host().latched_key = input.key;
        return;
    }
    // Only a key latches when it closes the dialog; the pointer needs
    // nothing more once the dialog has taken its action.
    case ScreenInputKind::pointer_move:
        std::ignore = take_sounded(settings::dialog_pointer_move(dialog, point.x, point.y));
        return;
    case ScreenInputKind::pointer_down:
        if (input.button == SDL_BUTTON_LEFT)
            std::ignore = take_sounded(settings::dialog_pointer_down(dialog, point.x, point.y));
        return;
    case ScreenInputKind::pointer_up:
        if (input.button == SDL_BUTTON_LEFT)
            std::ignore = take_sounded(settings::dialog_pointer_up(dialog, point.x, point.y));
        return;
    case ScreenInputKind::key_up:
    case ScreenInputKind::text:
    case ScreenInputKind::wheel:
        return;
    }
}

bool Runtime::EngineSettingsMatchHost::take_input(Runtime& runtime, const ScreenInput& input) {
    auto& host = runtime.engine_settings_match_host();
    if (host.latched_key && input.key == *host.latched_key) {
        if (input.kind == ScreenInputKind::key_up) {
            host.latched_key.reset();
            return true;
        }
        if (input.kind == ScreenInputKind::key_down)
            return true;
    }
    if (host.dialog_open) {
        if (auto* dialog = runtime.engine_settings_dialog()) {
            take_dialog_input(runtime, *dialog, input);
            return true;
        }
        host.dialog_open = false;
    }
    if (input.kind == ScreenInputKind::key_down &&
        engine_settings_shortcut(input.key, input.modifiers)) {
        runtime.open_engine_settings_in_match();
        return true;
    }
    const bool pointer = input.kind == ScreenInputKind::pointer_move ||
                         input.kind == ScreenInputKind::pointer_down ||
                         input.kind == ScreenInputKind::pointer_up;
    if (!pointer)
        return false;
    if (!button_shown(runtime)) {
        if (host.button_hovered || host.button_pressed)
            ++host.revision;
        host.button_hovered = false;
        host.button_pressed = false;
        return false;
    }
    const bool over = rect_contains(button_rect(runtime.match_layout_), input.x, input.y);
    if (over != host.button_hovered)
        ++host.revision;
    host.button_hovered = over;
    if (input.kind == ScreenInputKind::pointer_down && input.button == SDL_BUTTON_LEFT && over) {
        host.button_pressed = true;
        ++host.revision;
        return true;
    }
    if (input.kind == ScreenInputKind::pointer_up && input.button == SDL_BUTTON_LEFT &&
        host.button_pressed) {
        host.button_pressed = false;
        ++host.revision;
        if (over)
            runtime.open_engine_settings_in_match();
        return true;
    }
    return false;
}

void Runtime::EngineSettingsMatchHost::close_when_column_hidden(Runtime& runtime) {
    auto& host = runtime.engine_settings_match_host();
    if (!host.dialog_open)
        return;
    if (runtime.engine_settings_dialog() == nullptr) {
        host.dialog_open = false;
        ++host.revision;
        return;
    }
    if (runtime.screen_ == Screen::match && runtime.ingame_menu_column_shown())
        return;
    // OK always closes the dialog; a failed save is reported where it
    // happens.
    std::ignore = take_action(runtime, settings::DialogAction::accepted);
}

int Runtime::EngineSettingsMatchHost::overlay_event(ScreenContext* context, void* state) {
    if (context == nullptr || context->input == nullptr)
        return 0;
    auto& runtime = *static_cast<Runtime*>(state);
    return take_input(runtime, *context->input) ? 1 : 0;
}

void Runtime::EngineSettingsMatchHost::overlay_tick(ScreenContext*, void* state) {
    close_when_column_hidden(*static_cast<Runtime*>(state));
}

void Runtime::EngineSettingsMatchHost::stamp(
    std::vector<uint8_t>& rgba,
    int32_t width,
    int32_t height,
    const oa::ui::frontend_renderer::Surface& source,
    const layout::Rect& rect
) {
    if (source.width == 0 || source.height == 0 || rect.width <= 0 || rect.height <= 0)
        return;
    const int32_t top = std::max(rect.y, 0);
    const int32_t bottom = std::min(rect.y + rect.height, height);
    const int32_t left = std::max(rect.x, 0);
    const int32_t right = std::min(rect.x + rect.width, width);
    for (int32_t row = top; row < bottom; ++row) {
        const auto source_row = static_cast<std::size_t>(
            static_cast<int64_t>(row - rect.y) * source.height / rect.height
        );
        for (int32_t column = left; column < right; ++column) {
            const auto source_column = static_cast<std::size_t>(
                static_cast<int64_t>(column - rect.x) * source.width / rect.width
            );
            const auto* from = source.rgb.data() + (source_row * source.width + source_column) * 3U;
            auto* to =
                rgba.data() + (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                               static_cast<std::size_t>(column)) *
                                  4U;
            to[0] = from[0];
            to[1] = from[1];
            to[2] = from[2];
            to[3] = 255U;
        }
    }
}

bool Runtime::EngineSettingsMatchHost::refresh_layer(Runtime& runtime) {
    auto& host = runtime.engine_settings_match_host();
    const auto* dialog = host.dialog_open ? runtime.engine_settings_dialog() : nullptr;
    const auto* fonts = runtime.engine_settings_fonts();
    const auto& match = runtime.match_layout_;
    LayerLook look{};
    look.width = match.width;
    look.height = match.height;
    look.scale = match.scale;
    look.button_shown =
        fonts != nullptr && runtime.screen_ == Screen::match && runtime.ingame_menu_column_shown();
    look.dialog_shown = fonts != nullptr && dialog != nullptr && look.button_shown;
    look.button_look = static_cast<uint8_t>(
        host.button_pressed && host.button_hovered ? settings::ButtonLook::pressed
        : host.button_hovered                      ? settings::ButtonLook::hovered
                                                   : settings::ButtonLook::idle
    );
    look.revision = host.revision;
    if (!look.button_shown || look.width <= 0 || look.height <= 0)
        return false;
    if (host.drawn == look)
        return true;
    const auto pixels =
        static_cast<std::size_t>(look.width) * static_cast<std::size_t>(look.height);
    host.layer_rgba.assign(pixels * 4U, 0);
    namespace renderer = oa::ui::frontend_renderer;
    const renderer::Placement unscaled{0, 0, 1};
    renderer::Surface button;
    button.width = static_cast<uint32_t>(settings::ingame_button_side);
    button.height = static_cast<uint32_t>(settings::ingame_button_side);
    button.rgb.assign(static_cast<std::size_t>(button.width) * button.height * 3U, 0);
    settings::draw_oa_button(
        button,
        unscaled,
        settings::ingame_button_side,
        static_cast<settings::ButtonLook>(look.button_look),
        *fonts
    );
    if (look.dialog_shown)
        renderer::blend_source_rect(
            button,
            unscaled,
            {0, 0, settings::ingame_button_side, settings::ingame_button_side},
            settings::backdrop_color,
            settings::ingame_backdrop_opacity
        );
    const auto button_at = button_rect(match);
    host.layer_bounds = button_at;
    if (look.dialog_shown) {
        // The whole screen darkens, the button with it; the dialog goes over.
        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
            auto* to = host.layer_rgba.data() + pixel * 4U;
            to[0] = settings::backdrop_color[0];
            to[1] = settings::backdrop_color[1];
            to[2] = settings::backdrop_color[2];
            to[3] = static_cast<uint8_t>(kBackdropAlpha);
        }
        host.layer_bounds = {0, 0, look.width, look.height};
    }
    stamp(host.layer_rgba, look.width, look.height, button, button_at);
    if (look.dialog_shown) {
        renderer::Surface drawn;
        drawn.width = static_cast<uint32_t>(settings::dialog_width);
        drawn.height = static_cast<uint32_t>(settings::dialog_height);
        drawn.rgb.assign(static_cast<std::size_t>(drawn.width) * drawn.height * 3U, 0);
        settings::draw_dialog(drawn, unscaled, *dialog, *fonts);
        stamp(host.layer_rgba, look.width, look.height, drawn, dialog_rect(match));
    }
    host.drawn = look;
    host.uploaded.reset();
    return true;
}

void Runtime::compose_engine_settings_layer(renderer::Surface& frame) {
    if (!EngineSettingsMatchHost::refresh_layer(*this))
        return;
    const auto& host = engine_settings_match_host();
    const auto width = static_cast<int32_t>(frame.width);
    const auto height = static_cast<int32_t>(frame.height);
    if (!host.drawn || host.drawn->width != width || host.drawn->height != height)
        return;
    const auto& bounds = host.layer_bounds;
    const int32_t top = std::max(bounds.y, 0);
    const int32_t bottom = std::min(bounds.y + bounds.height, height);
    const int32_t left = std::max(bounds.x, 0);
    const int32_t right = std::min(bounds.x + bounds.width, width);
    // As the frontend dialogs' layer goes over the composed frame.
    for (int32_t row = top; row < bottom; ++row)
        for (int32_t column = left; column < right; ++column) {
            const auto pixel = static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                               static_cast<std::size_t>(column);
            const auto* source = host.layer_rgba.data() + pixel * 4U;
            const unsigned alpha = source[3];
            if (alpha == 0)
                continue;
            auto* target = frame.rgb.data() + pixel * 3U;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const unsigned shown =
                    gamma_identity_ ? source[channel] : gamma_table_[source[channel]];
                target[channel] =
                    static_cast<uint8_t>((shown * alpha + target[channel] * (255U - alpha)) / 255U);
            }
        }
}

void Runtime::present_engine_settings_layer() {
    if (sdl_.renderer == nullptr || !EngineSettingsMatchHost::refresh_layer(*this))
        return;
    auto& host = engine_settings_match_host();
    const int width = host.drawn->width;
    const int height = host.drawn->height;
    if (host.layer == nullptr || host.layer_width != width || host.layer_height != height) {
        if (host.layer != nullptr)
            SDL_DestroyTexture(host.layer);
        host.layer = SDL_CreateTexture(
            sdl_.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height
        );
        if (host.layer == nullptr || !SDL_SetTextureBlendMode(host.layer, SDL_BLENDMODE_BLEND) ||
            !SDL_SetTextureScaleMode(host.layer, SDL_SCALEMODE_NEAREST))
            throw std::runtime_error(std::string("SDL settings layer: ") + SDL_GetError());
        host.layer_width = width;
        host.layer_height = height;
        host.uploaded.reset();
    }
    if (host.uploaded != host.drawn || host.uploaded_gamma != gamma_table_) {
        const uint8_t* pixels = host.layer_rgba.data();
        std::vector<uint8_t> corrected;
        if (!gamma_identity_) {
            corrected = host.layer_rgba;
            apply_gamma_rgb(corrected.data(), corrected.size() / 4U, 4);
            pixels = corrected.data();
        }
        if (!SDL_UpdateTexture(host.layer, nullptr, pixels, width * 4))
            throw std::runtime_error(std::string("SDL settings layer upload: ") + SDL_GetError());
        host.uploaded = host.drawn;
        host.uploaded_gamma = gamma_table_;
    }
    const SDL_FRect bounds{
        static_cast<float>(host.layer_bounds.x),
        static_cast<float>(host.layer_bounds.y),
        static_cast<float>(host.layer_bounds.width),
        static_cast<float>(host.layer_bounds.height)
    };
    if (!SDL_RenderTexture(sdl_.renderer, host.layer, &bounds, &bounds))
        throw std::runtime_error(std::string("SDL_RenderTexture: ") + SDL_GetError());
}

void Runtime::destroy_engine_settings_textures() {
    if (!engine_settings_match_ || engine_settings_match_->layer == nullptr)
        return;
    SDL_DestroyTexture(engine_settings_match_->layer);
    engine_settings_match_->layer = nullptr;
    engine_settings_match_->layer_width = 0;
    engine_settings_match_->layer_height = 0;
    engine_settings_match_->uploaded.reset();
}

} // namespace oa::app
