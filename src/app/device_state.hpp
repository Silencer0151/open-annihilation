// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The keys, pointer buttons and modifier keys held down, as the game reads
// them between their events: what the keyboard and mouse hold, and what the
// automation endpoint holds (automation_host.hpp). The events the endpoint
// hands the game come through SDL's event queue alone, which leaves SDL's
// own device state as it was, so every read of what is held goes through
// here. Without the endpoint each read gives exactly what SDL gives.
//
// What the endpoint holds is kept for the process, as SDL keeps the
// devices' state, and is read and changed on the thread that runs main().
// The functions are defined in runtime_input_modifiers.cpp.
#pragma once

#include <SDL3/SDL.h>

namespace oa::app::device_state {

/// Tells whether a key is held down: on the keyboard, or by the automation
/// endpoint.
///
/// @param scancode the key
/// @return true while either holds it; false for a scancode SDL does not count
[[nodiscard]] bool key_held(SDL_Scancode scancode) noexcept;

/// Returns the pointer buttons held down: the mouse's, and the automation
/// endpoint's.
///
/// @return SDL_BUTTON_MASK bits of every button either holds
[[nodiscard]] SDL_MouseButtonFlags buttons_held() noexcept;

/// Returns the modifier keys held down: SDL_GetModState(), with the bits of
/// the modifier keys the automation endpoint holds.
///
/// @return SDL_Keymod bits
[[nodiscard]] SDL_Keymod modifiers_held() noexcept;

/// Holds a key down for the automation endpoint, or lets it go.
///
/// @param scancode the key; one SDL does not count is ignored
/// @param down true to hold it, false to let it go
void hold_key(SDL_Scancode scancode, bool down) noexcept;

/// Sets the pointer buttons the automation endpoint holds down.
///
/// @param buttons SDL_BUTTON_MASK bits of the buttons held; 0 for none
void hold_buttons(SDL_MouseButtonFlags buttons) noexcept;

} // namespace oa::app::device_state
