// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "screen_size.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/platform/machine.hpp"
#include "oa/platform/preferences.hpp"

#include <exception>
#include <filesystem>
#include <iostream>

namespace oa::app {

namespace settings = oa::ui::engine_settings;

settings::ScreenSize desktop_size() {
    const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    if (mode == nullptr || mode->w <= 0 || mode->h <= 0)
        return settings::desktop_screen_size;
    return {static_cast<uint16_t>(mode->w), static_cast<uint16_t>(mode->h)};
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

void take_screen_size(SDL_Window* window, settings::ScreenSize size, bool full_screen) {
    SDL_DisplayMode mode{};
    if (SDL_GetClosestFullscreenDisplayMode(
            SDL_GetDisplayForWindow(window), size.width, size.height, 0.0F, false, &mode
        ) &&
        !SDL_SetWindowFullscreenMode(window, &mode))
        std::cerr << "open-annihilation: the " << size.width << 'x' << size.height
                  << " display mode was refused: " << SDL_GetError() << '\n';
    if (full_screen && !SDL_SetWindowFullscreen(window, true))
        std::cerr << "open-annihilation: full screen was refused: " << SDL_GetError() << '\n';
}

} // namespace oa::app
