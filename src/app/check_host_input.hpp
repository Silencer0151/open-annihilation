// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The parts of the check host (check_host.hpp) that need no running game:
// the pointer and wheel events it makes, the gadget it finds by name and the
// text it hands back. app-check-host tests them.
#pragma once

#include "oa/ui/gui_layout.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace oa::app::check_host_input {

/// Makes a left-button pointer event at a point of the 640x480 canvas.
///
/// With a renderer the point is placed in the window as the renderer shows
/// the canvas there; without one the canvas point is the event's point. The
/// event names the window when there is one, and window id 0 otherwise.
/// Throws std::runtime_error for a type other than a motion, a press or a
/// release, or when the renderer cannot place the point.
///
/// @param renderer the game's renderer; null in a run without a window
/// @param window the game's window; null in a run without a window
/// @param type SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN or
///        SDL_EVENT_MOUSE_BUTTON_UP
/// @param x canvas column
/// @param y canvas row
/// @param clicks the presses in a row a press or release counts; a motion
///        ignores it
/// @return the event
[[nodiscard]] SDL_Event pointer_event(
    SDL_Renderer* renderer, SDL_Window* window, uint32_t type, int32_t x, int32_t y, uint8_t clicks
);

/// Makes a mouse wheel event at a point of the 640x480 canvas.
///
/// The point is placed in the window as pointer_event places it, and the
/// event names the window as pointer_event's does. Throws
/// std::runtime_error when the renderer cannot place the point.
///
/// @param renderer the game's renderer; null in a run without a window
/// @param window the game's window; null in a run without a window
/// @param x canvas column
/// @param y canvas row
/// @param notches the wheel's turn, positive away from the player
/// @return the event
[[nodiscard]] SDL_Event
wheel_event(SDL_Renderer* renderer, SDL_Window* window, int32_t x, int32_t y, float notches);

/// Finds a gadget of a layout by its name, compared ignoring ASCII case.
///
/// @param layout the layout
/// @param name the gadget's name
/// @return the first gadget so named, or null
[[nodiscard]] const oa::ui::gui_layout::Gadget*
find_gadget(const oa::ui::gui_layout::Layout& layout, std::string_view name);

/// Copies text and a zero byte after it into a buffer.
///
/// @param text the text
/// @param[out] out receives the text and its zero byte; left as it is when
///        they do not fit
/// @param size the bytes `out` holds
/// @return true when they fit
[[nodiscard]] bool copy_text(std::string_view text, char* out, std::size_t size);

} // namespace oa::app::check_host_input
