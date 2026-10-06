// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "screen_size.hpp"

#include "oa/app/full_screen.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/platform/machine.hpp"
#include "oa/platform/preferences.hpp"
#include "oa/platform/system.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string_view>
#include <tuple>

namespace oa::app {

namespace settings = oa::ui::engine_settings;
namespace display_modes = oa::platform::display_modes;

namespace {

/// Tells whether a display's size fits a screen size's 16-bit sides.
///
/// @param size the display's size
/// @return true when both sides are above 0 and fit
bool fits_screen_size(display_modes::Size size) noexcept {
    constexpr int32_t widest = std::numeric_limits<uint16_t>::max();
    return size.width > 0 && size.height > 0 && size.width <= widest && size.height <= widest;
}

/// Tells whether the Screen size setting keeps a display's size: neither
/// side above settings::longest_screen_side.
///
/// @param size the display's size
/// @return true when both sides are above 0 and the setting keeps them
bool kept_as_setting(display_modes::Size size) noexcept {
    return size.width > 0 && size.height > 0 && size.width <= settings::longest_screen_side &&
           size.height <= settings::longest_screen_side;
}

/// Returns a display's size as the Screen size setting keeps one.
///
/// @param size the display's size, one fits_screen_size accepts
/// @return the size
settings::ScreenSize screen_size_of(display_modes::Size size) noexcept {
    return {static_cast<uint16_t>(size.width), static_cast<uint16_t>(size.height)};
}

} // namespace

display_modes::DisplayReport display_report(const Options& options, SDL_DisplayID display) {
    if (!options.display_modes.empty())
        if (auto made_up = display_modes::report_from_text(options.display_modes))
            return *made_up;
    return display_modes::read_display(display);
}

display_modes::Use start_use(const Options& options) noexcept {
    return options.start_full_screen ? display_modes::Use::full_screen : display_modes::Use::window;
}

std::vector<settings::ScreenSize> offered_screen_sizes(
    const Options& options, SDL_DisplayID display, display_modes::Use use, int32_t minimum_height
) {
    std::vector<settings::ScreenSize> sizes;
    for (const display_modes::Size size :
         display_modes::offered_sizes(display_report(options, display), use, minimum_height))
        if (kept_as_setting(size))
            sizes.push_back(screen_size_of(size));
    if (sizes.empty())
        sizes.push_back(
            {static_cast<uint16_t>(display_modes::smallest_size.width),
             static_cast<uint16_t>(display_modes::smallest_size.height)}
        );
    return sizes;
}

settings::ScreenSize desktop_size(const Options& options) {
    const display_modes::Size desktop =
        display_report(options, SDL_GetPrimaryDisplay()).desktop.size;
    if (!fits_screen_size(desktop))
        return settings::desktop_screen_size;
    return screen_size_of(desktop);
}

settings::Inputs start_inputs(const Options& options, settings::ScreenSize desktop) {
    settings::Inputs inputs{};
    inputs.players_own_profile = !options.preferences_file.has_value();
#if defined(SDL_PLATFORM_MACOS)
    inputs.macos = true;
#endif
    inputs.raspberry_pi = oa::platform::running_on_raspberry_pi();
    inputs.light_machine = oa::platform::light_machine(oa::platform::read_machine_traits());
    inputs.desktop = desktop;
    inputs.steam_deck_panel_hz =
        oa::platform::steam_deck_refresh_hz(oa::platform::running_steam_deck_model());
    inputs.native_density_windows = options.native_density_windows;
    return inputs;
}

settings::EngineSettings start_settings(const Options& options, settings::ScreenSize desktop) {
    oa::platform::preferences::Values values;
    try {
        const auto file = preference_file(options.preferences_file);
        if (std::filesystem::exists(file))
            values = oa::platform::preferences::load(file);
    } catch (const std::exception& error) {
        // The runtime reads the file again and reports what is wrong with it.
        std::cerr << "open-annihilation: the settings start at their defaults: " << error.what()
                  << '\n';
    }
    return settings::read_settings(values, start_inputs(options, desktop), false);
}

settings::ScreenSize
starting_screen_size(const Options& options, const settings::EngineSettings& start) noexcept {
    if (options.window_resolution)
        return settings::desktop_screen_size;
    return start.screen_size;
}

settings::ScreenSize shown_screen_size(const Options& options, settings::ScreenSize size) {
    if (size == settings::desktop_screen_size)
        return size;
    if (display_modes::can_show(
            display_report(options, SDL_GetPrimaryDisplay()),
            {size.width, size.height},
            start_use(options)
        ))
        return size;
    std::cout << "open-annihilation: the display does not offer the " << size.width << 'x'
              << size.height << " screen size; this run shows the desktop's\n";
    return settings::desktop_screen_size;
}

settings::ScreenSize
default_window_size(settings::ScreenSize desktop, bool steam_game_mode) noexcept {
    settings::ScreenSize size{
        static_cast<uint16_t>(kDefaultWindowWidth), static_cast<uint16_t>(kDefaultWindowHeight)
    };
    if (steam_game_mode && desktop != settings::desktop_screen_size) {
        size.width = std::min(size.width, desktop.width);
        size.height = std::min(size.height, desktop.height);
    }
    return size;
}

void report_window_size(SDL_Window* window, settings::ScreenSize desktop, bool steam_game_mode) {
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSize(window, &width, &height))
        return;
    std::cout << "open-annihilation: window: " << width << 'x' << height;
    if (desktop == settings::desktop_screen_size)
        std::cout << " on a desktop of unknown size";
    else
        std::cout << " on a " << desktop.width << 'x' << desktop.height << " desktop";
    std::cout << (steam_game_mode ? ", in Steam's Game Mode" : ", outside Steam's Game Mode")
              << '\n';
}

FullScreenMethod run_full_screen_method() {
    const char* const driver = SDL_GetCurrentVideoDriver();
    return full_screen_method(
        driver != nullptr ? std::string_view(driver) : std::string_view(),
        oa::platform::environment_value("WAYLAND_DISPLAY").has_value(),
        oa::platform::running_in_steam_game_mode()
    );
}

ScreenHooks sdl_screen_hooks(SDL_Window* window) noexcept {
    ScreenHooks hooks{};
    hooks.context = window;
    hooks.window = [](void* context) {
        auto* const shown = static_cast<SDL_Window*>(context);
        ScreenWindow state{};
        const SDL_WindowFlags flags = SDL_GetWindowFlags(shown);
        state.full_screen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
        state.exclusive = state.full_screen && SDL_GetWindowFullscreenMode(shown) != nullptr;
        state.maximised = (flags & SDL_WINDOW_MAXIMIZED) != 0;
        if (!SDL_GetWindowSize(shown, &state.size.width, &state.size.height))
            state.size = {};
        return state;
    };
    hooks.restore = [](void* context) {
        auto* const shown = static_cast<SDL_Window*>(context);
        // The size that follows needs the window back to a size of its own;
        // a window system that refuses leaves it maximised.
        if (SDL_RestoreWindow(shown))
            std::ignore = SDL_SyncWindow(shown);
    };
    hooks.set_window_size = [](void* context, display_modes::Size size) {
        // The new size reaches the game as any resize does; a window system
        // that refuses it leaves the window as it was.
        if (!SDL_SetWindowSize(static_cast<SDL_Window*>(context), size.width, size.height))
            std::cerr << "open-annihilation: the window's size of " << size.width << 'x'
                      << size.height << " was refused: " << SDL_GetError() << '\n';
    };
    hooks.keep_on_display = [](void* context, display_modes::Size size) {
        keep_window_on_display(static_cast<SDL_Window*>(context), size.width, size.height);
    };
    hooks.take_mode = [](void* context, display_modes::Size size) {
        return display_modes::take_full_screen_size(static_cast<SDL_Window*>(context), size);
    };
    hooks.take_desktop_mode = [](void* context) {
        if (!SDL_SetWindowFullscreenMode(static_cast<SDL_Window*>(context), nullptr))
            std::cerr << "open-annihilation: the desktop's display mode was refused: "
                      << SDL_GetError() << '\n';
    };
    return hooks;
}

void take_screen_size(
    SDL_Window* window, settings::ScreenSize size, bool full_screen, FullScreenMethod method
) {
    if (method == FullScreenMethod::switch_mode &&
        !display_modes::take_full_screen_size(window, {size.width, size.height}))
        std::cout << "open-annihilation: the display has no " << size.width << 'x' << size.height
                  << " mode; full screen draws at that size and scales it\n";
    if (full_screen && !SDL_SetWindowFullscreen(window, true))
        std::cerr << "open-annihilation: full screen was refused: " << SDL_GetError() << '\n';
}

} // namespace oa::app
