// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The handler a request to end the program runs, as the game keeps it
// installed through its application modes and matches, and the disconnect
// reason a leave shows.
#pragma once

#include "oa/core/game_state.h"

#include <cstdint>

namespace oa::app {

/// Returns the text of the local player's disconnect reason, which the game shows as it leaves.
///
/// @param game the frontend Game block
/// @return the text for the local player's reject reason; null when it has none
[[nodiscard]] const char* leave_reason(const oa::Game& game) noexcept;

// The handler a request to end the program runs (the window's close button,
// or the system's quit). The game keeps one installed: each application mode
// it sets installs that mode's handler, and a launch and a leave install
// theirs; nothing else changes it. An extension built on network play may
// answer a request first (Extension::close_requested); this handler answers
// the requests it leaves.
enum class CloseHandler : uint8_t {
    none,         // no handler: the run ends at once
    exit_confirm, // the exit confirmation, "Surrender this battle and exit to the system?"
    leave,        // the game leaves its session and ends, showing the disconnect reason
};

// The installed handler and what it follows. The game starts with the leave,
// which its first application mode installs.
struct CloseHandlers {
    CloseHandler installed{CloseHandler::leave};
    int32_t mode{};  // the application mode the game was last seen in
    bool in_match{}; // a match has run, or loaded, since it started, and has not ended
};

// What the installed handler follows, seen at a frame or at a request.
struct CloseObservation {
    int32_t frontend_mode{}; // the frontend Game's application mode (Game.mode)
    bool match_runs{};       // a match runs or loads
};

/// Returns the handler setting an application mode installs.
///
/// @param mode the application mode (oa::ui::frontend_state::mode_id values)
/// @return the exit confirmation in a match (mode_id::in_match) and the
///         leave in every other mode
[[nodiscard]] CloseHandler app_mode_close_handler(int32_t mode) noexcept;

/// Follows the game into the installed handler, as the game installs one.
///
/// A match that runs or loads puts the game in mode_id::in_match until
/// close_handlers_match_ended; otherwise the mode is the frontend Game's.
/// A change of mode installs that mode's handler (app_mode_close_handler);
/// anything else keeps the handler, so a launch's exit confirmation holds
/// until the game next sets a mode (close_handlers_mode_set) or changes it.
///
/// @param[in,out] handlers the installed handler and what it last saw
/// @param now the game's state
void close_handlers_observe(CloseHandlers& handlers, const CloseObservation& now) noexcept;

/// Installs the handler of an application mode the game sets, as each mode it sets installs one.
///
/// Every mode set installs that mode's handler (app_mode_close_handler), a
/// mode set again to the one the game already runs included: a launch's
/// exit confirmation gives way to the leave when the game sets the mode it
/// runs again. Outside a match the mode set becomes the one
/// close_handlers_observe compares with; a match keeps mode_id::in_match
/// until close_handlers_match_ended.
///
/// @param[in,out] handlers the installed handler and what it last saw
/// @param mode the application mode set (oa::ui::frontend_state::mode_id values)
/// @param now the game's state as the mode is set, the mode already written
void close_handlers_mode_set(
    CloseHandlers& handlers, int32_t mode, const CloseObservation& now
) noexcept;

/// Ends the match mode close_handlers_observe entered: the match finished, was left or was torn down.
///
/// The next observation installs the handler of the frontend Game's mode.
///
/// @param[in,out] handlers the installed handler and what it last saw
void close_handlers_match_ended(CloseHandlers& handlers) noexcept;

} // namespace oa::app
