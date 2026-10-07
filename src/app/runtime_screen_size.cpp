// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Screen size applied at once: the window resized, full screen at a
// display mode of the size or drawn at it and scaled to the screen, and the
// size the pickers show; and the window's frame as Window frame says.
#include "engine_settings_state.hpp"
#include "oa/app/runtime.hpp"
#include "screen_mode.hpp"
#include "screen_size.hpp"

#include <SDL3/SDL.h>

#include <iostream>

namespace oa::app {

namespace settings = oa::ui::engine_settings;
namespace display_modes = oa::platform::display_modes;

SDL_Point Runtime::scaled_frame_size() const {
    if (OA_TOUCH_FIRST || sdl_.window == nullptr)
        return {};
    const ScreenHooks hooks = sdl_screen_hooks(sdl_.window);
    const display_modes::Size frame = scaled_frame(
        hooks.window(hooks.context),
        {full_screen_switch_.screen_width, full_screen_switch_.screen_height}
    );
    return {frame.width, frame.height};
}

SDL_Point Runtime::screen_size_now() const {
    if (const SDL_Point scaled = scaled_frame_size(); scaled.x > 0 && scaled.y > 0)
        return scaled;
    SDL_Point size{};
    if (sdl_.window == nullptr || !SDL_GetWindowSize(sdl_.window, &size.x, &size.y))
        return {};
    return size;
}

void Runtime::apply_screen_size(settings::ScreenSize size) {
    if (OA_TOUCH_FIRST || sdl_.window == nullptr)
        return;
    const AppliedScreen applied = oa::app::apply_screen_size(
        sdl_screen_hooks(sdl_.window), {size.width, size.height}, run_full_screen_method()
    );
    full_screen_switch_.screen_width = applied.screen.width;
    full_screen_switch_.screen_height = applied.screen.height;
    full_screen_switch_.window_width = applied.window_after_full_screen.width;
    full_screen_switch_.window_height = applied.window_after_full_screen.height;
    if (size == settings::desktop_screen_size)
        std::cout << "open-annihilation: screen size: the desktop's own\n";
    else
        std::cout << "open-annihilation: screen size: " << size.width << 'x' << size.height
                  << (applied.window_after_full_screen.width == 0 ? ", the window's; " : "; ")
                  << (applied.switched ? "full screen at a display mode of it\n"
                                       : "full screen drawn at it and scaled\n");
    // A window's new size reaches the game as any resize does; a frame
    // scaled in full screen changes no window, so the screen is laid out
    // again now, and the match drawn again at its new size.
    apply_output_mode();
    if (screen_ == Screen::match && match_ && selected_tnt_)
        render_match_surface();
}

void Runtime::apply_window_frame() {
    if (OA_TOUCH_FIRST || sdl_.window == nullptr || !engine_settings_)
        return;
    const SDL_WindowFlags flags = SDL_GetWindowFlags(sdl_.window);
    // A window still switching to or from full screen is changed once it
    // has.
    const bool switching =
        full_screen_switch_.awaiting_shown &&
        SDL_GetTicks() - full_screen_switch_.requested_ms < full_screen_settle_ms;
    const WindowFramePlace place{
        .windowed = (flags & SDL_WINDOW_FULLSCREEN) == 0 && !switching,
        .playing = screen_ == Screen::match && match_ && !match_finished_ && !match_paused_,
        .bordered = (flags & SDL_WINDOW_BORDERLESS) == 0,
    };
    const auto request = window_frame_request(
        engine_settings_->current.window_frame == settings::WindowFrame::hidden_in_play, place
    );
    if (request != WindowFrameRequest::none &&
        !set_window_frame(sdl_.window, request == WindowFrameRequest::show))
        std::cerr << "open-annihilation: the window's frame did not change: " << SDL_GetError()
                  << '\n';
}

void Runtime::EngineSettingsState::take_screen_size(Runtime& runtime, settings::ScreenSize size) {
    choose_screen_size(runtime, size);
    runtime.apply_screen_size(runtime.engine_settings_state().current.screen_size);
}

} // namespace oa::app
