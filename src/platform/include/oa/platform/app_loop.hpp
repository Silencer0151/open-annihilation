// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Pacing rules of the application loop: the event loop that runs the idle
// tick, sweeps finished sound streams and parks the music player while the
// application is inactive.

#include <cstdint>

namespace oa::platform {

// The sweep runs after an idle tick once more than this many milliseconds
// passed since the previous one.
inline constexpr int32_t finished_stream_sweep_after_ms = 99;

/// Tests whether the finished-stream sweep is due after an idle tick.
///
/// @param now_ms host millisecond tick count
/// @param last_sweep_ms tick count of the previous sweep
/// @return true when more than finished_stream_sweep_after_ms passed; the
///         difference is signed, so a clock that steps back waits
[[nodiscard]] constexpr bool
finished_stream_sweep_due(uint32_t now_ms, uint32_t last_sweep_ms) noexcept {
    return static_cast<int32_t>(now_ms - last_sweep_ms) > finished_stream_sweep_after_ms;
}

/// Tests whether the loop should block waiting for events.
///
/// An inactive application only waits for events, unless a live multiplayer
/// game or an online session has to keep running.
///
/// @param active whether the application has focus
/// @param live_multiplayer_game whether a multiplayer game is running
/// @param online_session whether an online session is open
/// @return true when the loop may block
[[nodiscard]] constexpr bool application_waits_for_events(
    bool active, bool live_multiplayer_game, bool online_session
) noexcept {
    return !active && !live_multiplayer_game && !online_session;
}

enum class MusicFocusAction : uint8_t {
    none,
    park,   // store the disc's track kinds, remember the music kind, close the player
    resume, // reopen the player and restore its options and music kind
};

/// Chooses what the loop does with the music player on a focus change.
///
/// Only a player the loop parked itself is reopened on activation.
///
/// @param active whether the application has focus
/// @param player_open whether the music player is open
/// @param parked_by_loop whether the loop closed the player on deactivation
/// @return park when losing focus with the player open, resume when regaining
///         it with a parked player, otherwise none
[[nodiscard]] constexpr MusicFocusAction
music_focus_action(bool active, bool player_open, bool parked_by_loop) noexcept {
    if (!active)
        return player_open ? MusicFocusAction::park : MusicFocusAction::none;
    return !player_open && parked_by_loop ? MusicFocusAction::resume : MusicFocusAction::none;
}

} // namespace oa::platform
