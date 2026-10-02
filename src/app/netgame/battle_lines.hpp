// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The lines posted to a battle a launch started: to the running match's
// message log, or with no match into the battle room's chat ring, each cut
// into parts as the message log's notices are.
#pragma once

#include "oa/core/game_state.h"

namespace oa::app {

/// Posts a line wrapped as the message log's notices are.
///
/// The line is cut into 63-character parts at a blank within the last 12
/// characters, as the message log's notices are, each posted through
/// `post` (or into the battle room's chat ring without one), and bit 0 of
/// the Game's gui flags rises.
///
/// @param line the line
/// @param post posts one part to the battle's message log; null uses the chat ring
/// @param context passed back to `post`
/// @param[in,out] game Game whose gui flags rise
void post_wrapped_line(
    const char* line, void (*post)(void* context, const char* part), void* context, oa::Game& game
) noexcept;

/// Installs where post_battle_line posts while a match runs.
///
/// @param context passed back to `post_to_match`
/// @param post_to_match posts a line to the running match's message log;
///        false when no match runs, and the line goes to the chat ring
///        instead. Null never posts to a match.
void bind_battle_lines(
    void* context, bool (*post_to_match)(void* context, const char* line)
) noexcept;

/// Posts a line to the running match's message log, or with no match into the battle room's chat ring.
///
/// Into the chat ring the line goes wrapped as a notice (post_wrapped_line),
/// and the frontend Game's gui flags rise.
///
/// @param line the line
void post_battle_line(const char* line) noexcept;

/// Empties the frontend Game's chat ring (the battle room's chat).
void empty_chat_ring() noexcept;

} // namespace oa::app
