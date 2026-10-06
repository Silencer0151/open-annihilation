// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings read before the window opens: the size the game's window
// opens at and the display mode full screen takes, from the Screen size
// setting the run reads and the defaults of the machine the game runs on,
// and Hardware acceleration, which the renderer's creation reads.
#pragma once

#include "oa/app/app.hpp"
#include "oa/platform/display_modes/sdl.hpp"
#include "oa/ui/engine_settings.hpp"
#include "screen_mode.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

namespace oa::app {

/// Returns what a display reports: the made-up monitor --display-modes
/// names, else the display's full-screen modes and desktop as SDL reports
/// them. SDL's video must be initialised.
///
/// @param options the parsed command line (Options::display_modes)
/// @param display SDL's id of the display; 0 reads an empty report
/// @return the report
[[nodiscard]] oa::platform::display_modes::DisplayReport
display_report(const Options& options, SDL_DisplayID display);

/// Returns how the Screen size setting's size shows from the next start:
/// the screen switched to it when the run starts full screen
/// (Options::start_full_screen), else a window of it.
///
/// @param options the parsed command line
/// @return full screen or a window
[[nodiscard]] oa::platform::display_modes::Use start_use(const Options& options) noexcept;

/// Returns the screen sizes a display offers for a way of showing them, each
/// once, the narrower first and neither side above
/// oa::ui::engine_settings::longest_screen_side: the sizes the options'
/// Screen Size and the settings' Screen size offer after Desktop.
///
/// @param options the parsed command line (Options::display_modes)
/// @param display SDL's id of the display
/// @param use full screen at a mode of the size, or a window of it, or a
///     frame drawn at it and scaled to the screen, which fits the desktop as
///     a window does
/// @param minimum_height the shortest size offered, in units: 480, or a
///     mod's taller floor
/// @return the sizes; never empty
[[nodiscard]] std::vector<oa::ui::engine_settings::ScreenSize> offered_screen_sizes(
    const Options& options,
    SDL_DisplayID display,
    oa::platform::display_modes::Use use,
    int32_t minimum_height
);

/// Returns how full screen shows a screen size in this run
/// (full_screen_method): from SDL's video driver, whether an X11 run is
/// within a Wayland session, and whether the run is in Steam's Game Mode.
/// SDL's video must be initialised.
///
/// @return the method
[[nodiscard]] FullScreenMethod run_full_screen_method();

/// Returns the steps that apply a screen size (apply_screen_size) on SDL's
/// window: its flags, size and full-screen mode; SDL_RestoreWindow,
/// SDL_SetWindowSize, keep_window_on_display, the display's mode of a size
/// (display_modes::take_full_screen_size) and the desktop's.
///
/// @param window the game's window, which the hooks take as their context
/// @return the hooks
[[nodiscard]] ScreenHooks sdl_screen_hooks(SDL_Window* window) noexcept;

/// Returns the size of the primary display's desktop: the made-up
/// monitor's first mode with --display-modes.
///
/// @param options the parsed command line (Options::display_modes)
/// @return the desktop's size; desktop_screen_size (zero by zero) when SDL
///     does not report it
[[nodiscard]] oa::ui::engine_settings::ScreenSize desktop_size(const Options& options);

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

/// Returns the screen size the game starts at on a display: the size, when
/// the display can show it as the run starts (start_use,
/// oa::platform::display_modes::can_show), else Desktop for this run, which
/// it says on stdout; the setting keeps the size, so that the display that
/// offers it brings it back.
///
/// @param options the parsed command line
/// @param size the screen size (starting_screen_size)
/// @return the size, or desktop_screen_size
[[nodiscard]] oa::ui::engine_settings::ScreenSize
shown_screen_size(const Options& options, oa::ui::engine_settings::ScreenSize size);

/// Returns the size the game's window opens at when neither --resolution nor
/// the Screen size setting names one: kDefaultWindowWidth by
/// kDefaultWindowHeight, each side held to the desktop's in Steam's Game
/// Mode. gamescope shows the whole of a larger window shrunk to the screen,
/// but its X server keeps the pointer within the desktop's size, which would
/// leave the window's lower and right parts out of the pointer's reach.
///
/// @param desktop the desktop's size (desktop_size); desktop_screen_size when
///     SDL does not report it, which leaves the default as it is
/// @param steam_game_mode whether the run is in Steam's Game Mode
///     (oa::platform::running_in_steam_game_mode)
/// @return the size
[[nodiscard]] oa::ui::engine_settings::ScreenSize
default_window_size(oa::ui::engine_settings::ScreenSize desktop, bool steam_game_mode) noexcept;

/// Logs on stdout the size the game's window opened at, the desktop's size
/// and whether the run is in Steam's Game Mode, for example
/// "open-annihilation: window: 1280x800 on a 1280x800 desktop, in Steam's
/// Game Mode". A window whose size SDL does not report logs nothing.
///
/// @param window the game's window
/// @param desktop the desktop's size (desktop_size)
/// @param steam_game_mode whether the run is in Steam's Game Mode
void report_window_size(
    SDL_Window* window, oa::ui::engine_settings::ScreenSize desktop, bool steam_game_mode
);

/// Sets the display mode a window takes in full screen to the display's
/// mode of a screen size (oa::platform::display_modes::mode_for: the
/// desktop's pixel density, then its refresh rate, then the highest), where
/// full screen switches the display's mode, and puts the window in full
/// screen when asked.
///
/// The window keeps the desktop's own mode in full screen where full screen
/// scales the frame, or the display offers no mode of the size; the match
/// is then drawn at the size and scaled to the screen.
///
/// @param window the game's window, opened at the size
/// @param size the screen size, not desktop_screen_size
/// @param full_screen true to put the window in full screen
/// @param method how full screen shows the size (run_full_screen_method)
void take_screen_size(
    SDL_Window* window,
    oa::ui::engine_settings::ScreenSize size,
    bool full_screen,
    FullScreenMethod method
);

} // namespace oa::app
