// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A screen size applied to the game's window at once: a window resized to
// it, or full screen switched to a display mode of it, or drawn at it and
// scaled to the screen, as each window system allows. The steps reach the
// window through a table of hooks, so that a test can follow them on a
// made-up window; the game fills the table with SDL's calls.
#pragma once

#include "oa/platform/display_modes.hpp"

#include <cstdint>
#include <string_view>

namespace oa::app {

/// How full screen shows a screen size other than the desktop's own.
enum class FullScreenMethod : uint8_t {
    /// The display switches to a mode of the size, as 3.1c's full screen
    /// did; a size the display has no mode of is drawn as scale_frame
    /// draws it.
    switch_mode,
    /// The display keeps its desktop mode: the game draws the match at the
    /// size and scales the frame to the screen.
    scale_frame,
};

/// Returns how full screen shows a screen size on a window system.
///
/// Windows switches the display's mode, which the system puts back when the
/// game leaves full screen, is switched away from or ends, even by a
/// crash; on Windows XP the monitor then scales the frame at no cost to the
/// processor. An X server of its own (X11 outside a Wayland session and
/// outside Steam's Game Mode) switches through its own modes too. Every
/// other window system scales the frame: macOS, whose switch fades the
/// screen and moves other programs' windows, and whose high-density
/// displays show every mode scaled anyway; Wayland and X11 within a
/// Wayland session, which have no modes of their own to switch to; Steam's
/// Game Mode, which offers one; and the dummy and offscreen drivers, so
/// that checks run the scaled frame.
///
/// @param video_driver SDL's name for the video driver (SDL_GetCurrentVideoDriver)
/// @param wayland_session the X11 driver runs within a Wayland session
///     (WAYLAND_DISPLAY is set)
/// @param steam_game_mode the run is in Steam's Game Mode
/// @return how full screen shows a size
[[nodiscard]] FullScreenMethod full_screen_method(
    std::string_view video_driver, bool wayland_session, bool steam_game_mode
) noexcept;

/// The game's window as a screen size is applied to it.
struct ScreenWindow {
    bool full_screen{}; ///< the window is full screen
    /// In full screen, at a display mode of the game's own rather than the
    /// desktop's.
    bool exclusive{};
    bool maximised{}; ///< the window fills the desktop as the window system maximised it
    /// The window's size, in the window system's units (display_modes::Size).
    oa::platform::display_modes::Size size{};
};

/// Returns the frame a match is drawn at and scaled to the screen: the
/// screen size applied, while the window is full screen on the display's
/// desktop mode and the size is not the window's own.
///
/// @param window the window
/// @param applied the screen size applied; 0 by 0 for the desktop's own
/// @return the frame; 0 by 0 where the match is drawn at the window's size
[[nodiscard]] oa::platform::display_modes::Size
scaled_frame(const ScreenWindow& window, oa::platform::display_modes::Size applied) noexcept;

/// The game's window, as the steps that apply a screen size reach it.
struct ScreenHooks {
    void* context{}; ///< handed back to each hook
    /// Returns the window's state now; null reads a window of no size, not
    /// full screen.
    ScreenWindow (*window)(void* context){};
    /// Brings a maximised window back to a size of its own; null leaves it
    /// maximised.
    void (*restore)(void* context){};
    /// Asks the window system for a window of a size, in its units; null
    /// asks for none.
    void (*set_window_size)(void* context, oa::platform::display_modes::Size size){};
    /// Moves the window so that it lies on its display at a size, which it
    /// keeps; null leaves the window where it is.
    void (*keep_on_display)(void* context, oa::platform::display_modes::Size size){};
    /// Sets the display mode full screen takes to the display's mode of a
    /// size (display_modes::mode_for), which a window in full screen takes at
    /// once; null takes none.
    ///
    /// @return false when the display has no mode of the size or refuses it
    bool (*take_mode)(void* context, oa::platform::display_modes::Size size){};
    /// Sets the display mode full screen takes back to the desktop's, which
    /// a window in full screen takes at once; null leaves the mode as it is.
    void (*take_desktop_mode)(void* context){};
};

/// What applying a screen size leaves for the game to follow.
struct AppliedScreen {
    /// The screen size full screen shows, now and at every switch to full
    /// screen: the size, or 0 by 0 for the desktop's own.
    oa::platform::display_modes::Size screen{};
    /// The size the window takes once it leaves full screen; 0 by 0 keeps
    /// its own.
    oa::platform::display_modes::Size window_after_full_screen{};
    /// Full screen took a display mode of the size.
    bool switched{};
};

/// Applies a screen size to the game's window at once.
///
/// Desktop (0 by 0) sets full screen back to the desktop's own mode, and a
/// window keeps the size it has. A size in a window brings a maximised
/// window back to a size of its own, asks for a window of the size, keeps
/// it on its display at that size, and sets the mode full screen will take:
/// the display's mode of the size where the method switches modes and the
/// display has one, else the desktop's. A size in full screen takes the
/// display's mode of the size where the method switches modes and the
/// display has one, else the desktop's mode, on which the match is drawn at
/// the size and scaled to the screen (scaled_frame); the window takes the
/// size once it leaves full screen.
///
/// @param hooks the window
/// @param size the screen size; 0 by 0 for the desktop's own
/// @param method how full screen shows a size (full_screen_method)
/// @return what the game follows
AppliedScreen apply_screen_size(
    const ScreenHooks& hooks, oa::platform::display_modes::Size size, FullScreenMethod method
);

} // namespace oa::app
