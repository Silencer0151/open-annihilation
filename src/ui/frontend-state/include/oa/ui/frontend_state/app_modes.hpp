// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Frontend state resets and the application-mode handlers that run the
// frontend dispatcher.
#pragma once

#include "oa/ui/frontend_state/dispatcher.hpp"

namespace oa::ui::frontend_state {

/// Writes the dispatcher state under the checksum guard and restarts both signal bytes at initialize.
///
/// @param[in,out] s Dispatcher state.
/// @param[in,out] h Host that runs the checksum step.
/// @param state New state_id value.
void set_frontend_state(State& s, Host& h, uint8_t state);

/// Sets the current and pending signal once the state checksum is checked.
///
/// @param[in,out] s Dispatcher state.
/// @param[in,out] h Host that runs the checksum step.
/// @param signal New signal.
void set_frontend_signal(State& s, Host& h, uint8_t signal);

/// Returns to the main menu under the default palette.
///
/// @param[in,out] s Dispatcher state.
/// @param[in,out] h Host that runs the checksum and palette steps.
void reset_to_main_menu(State& s, Host& h);

/// Runs app mode 0: leaves a game for the frontend (mode 2) without touching the dispatcher state.
///
/// Restores the normal cursor, clears the in-game flags and selects the
/// main-menu map list.
///
/// @param[in,out] s Dispatcher state.
/// @param[in,out] h Host that runs the cursor, map-list and mode calls.
void enter_frontend_mode(State& s, Host& h);

/// Runs app mode 1: as mode 0, but also returns the dispatcher to the main menu.
///
/// @param[in,out] s Dispatcher state.
/// @param[in,out] h Host that runs the palette, map-list and mode calls.
void reset_to_frontend_mode(State& s, Host& h);

/// Runs app mode 2: steps the dispatcher, then hands over to another mode when the frontend has reached it.
///
/// The next mode is skirmish setup (4) or game load (5) once the map list is
/// ready, otherwise the one the extension's handover names. The panel gadgets
/// and cursor are drawn every tick.
///
/// @param[in,out] s Dispatcher state.
/// @param[in,out] h Host that runs the dispatcher's routines.
/// @param extension States an extension runs in place of the engine's and the
///        modes they hand over to; the default leaves every state to the engine.
void tick_frontend_mode(State& s, Host& h, const StateHandler& extension = {});

} // namespace oa::ui::frontend_state
