// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What a display reports, read through SDL's display mode functions, which
// every platform the game runs on has, Windows XP's among them; and the
// full-screen mode a window takes for a size.
#pragma once

#include "oa/platform/display_modes.hpp"

#include <cstdint>

struct SDL_Window;

namespace oa::platform::display_modes {

/// Reads what a display reports through SDL: its full-screen modes and its
/// desktop's mode, in the window system's units (Size). SDL's video must be
/// initialised.
///
/// @param display SDL's id of the display; 0 reads an empty report
/// @return the report; empty, its desktop unknown, when SDL reports nothing
[[nodiscard]] DisplayReport read_display(uint32_t display);

/// Sets the mode a window takes in full screen to its display's mode of a
/// size, chosen as mode_for chooses it.
///
/// @param window the window; null takes nothing
/// @param size the size
/// @return true when the display has a mode of the size and the window
///     took it
bool take_full_screen_size(SDL_Window* window, Size size);

} // namespace oa::platform::display_modes
