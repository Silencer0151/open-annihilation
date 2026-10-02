// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings read before the window opens: the size the game's window
// opens at and the display mode full screen takes, from the Screen size
// setting the run reads and the defaults of the machine the game runs on,
// and Hardware acceleration, which the renderer's creation reads.
#pragma once

#include "oa/app/app.hpp"
#include "oa/ui/engine_settings.hpp"

#include <SDL3/SDL.h>

namespace oa::app {

/// Returns the size of the primary display's desktop.
///
/// @return the desktop's size; desktop_screen_size (zero by zero) when SDL
///     does not report it
[[nodiscard]] oa::ui::engine_settings::ScreenSize desktop_size();

/// Returns what the settings' defaults depend on at start, before the
/// runtime reads them: the platform, whether the preferences file is the
/// player's own, and whether the machine is a Raspberry Pi or a light
/// machine. The installation's totala.ini is not read.
///
/// @param options the parsed command line
/// @param desktop the desktop's size (desktop_size)
/// @return the inputs
[[nodiscard]] oa::ui::engine_settings::Inputs
start_inputs(const Options& options, oa::ui::engine_settings::ScreenSize desktop);

/// Returns the settings the run's preferences file holds before the window
/// opens, each at its default on this machine where the file has none. A
/// file that cannot be read gives every default, and says so on stderr; the
/// runtime reads it again and reports what is wrong with it.
///
/// @param options the parsed command line (Options::preferences_file)
/// @param desktop the desktop's size (desktop_size)
/// @return the settings
[[nodiscard]] oa::ui::engine_settings::EngineSettings
start_settings(const Options& options, oa::ui::engine_settings::ScreenSize desktop);

/// Returns the screen size the game starts at: the Screen size setting the
/// run's preferences file holds, or its default on this machine.
///
/// @param options the parsed command line; with --resolution the window
///     takes that size and the setting is not read
/// @param start the settings read before the window opens (start_settings)
/// @return the size; desktop_screen_size for the desktop's own
[[nodiscard]] oa::ui::engine_settings::ScreenSize starting_screen_size(
    const Options& options, const oa::ui::engine_settings::EngineSettings& start
) noexcept;

/// Sets the display mode a window takes in full screen to the one nearest a
/// screen size, and puts the window in full screen when asked.
///
/// The window keeps the desktop's own mode in full screen when the display
/// offers no mode of the size or larger.
///
/// @param window the game's window, opened at the size
/// @param size the screen size, not desktop_screen_size
/// @param full_screen true to put the window in full screen
void take_screen_size(
    SDL_Window* window, oa::ui::engine_settings::ScreenSize size, bool full_screen
);

} // namespace oa::app
