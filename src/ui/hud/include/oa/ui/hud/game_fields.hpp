// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Accessors for the Game and Player fields the HUD panels read and write.
#pragma once

#include "oa/core/game_state.h"

#include <cstddef>
#include <cstdint>

namespace oa::ui::hud {

/// Game.chat_flags bit: the chat panel sends to the team (TALK2.GUI SENDTO).
inline constexpr uint8_t kChatFlagTeam = 0x01;
/// Bytes of Game.mission_panel_name, the panel file the mission start panel
/// loads.
inline constexpr size_t kMissionPanelNameBytes = sizeof(Game::mission_panel_name);

/// Reads whether the chat panel sends to the team.
///
/// @param game game record read
/// @return whether Game.chat_flags holds kChatFlagTeam
inline bool chat_to_team(const Game& game) noexcept {
    return (game.chat_flags & kChatFlagTeam) != 0;
}

/// Sets or clears the chat panel's send-to-team bit.
///
/// @param[in,out] game game record whose Game.chat_flags change
/// @param team whether kChatFlagTeam is set; the other bits are kept
inline void set_chat_to_team(Game& game, bool team) noexcept {
    game.chat_flags =
        static_cast<uint8_t>((game.chat_flags & ~kChatFlagTeam) | (team ? kChatFlagTeam : 0));
}

/// Reads the panel file name the mission start panel loads.
///
/// @param game game record read
/// @return Game.mission_panel_name, kMissionPanelNameBytes bytes that need not
///         end in a NUL
inline const char* mission_panel_name(const Game& game) noexcept {
    return reinterpret_cast<const char*>(game.mission_panel_name);
}

/// Reads which message-log lines are shown.
///
/// @param game game record read
/// @return Game.message_filter: 1 chat only, 2 all but kind 8, 3 kinds 1, 4
///         and 8; anything else hides the log
inline int32_t message_filter(const Game& game) noexcept {
    return game.message_filter;
}

/// Reads the show-all switch of message-log filter 3.
///
/// @param game game record read
/// @return Game.screen_chat; nonzero shows every line under filter 3
inline int32_t message_show_all(const Game& game) noexcept {
    return static_cast<int32_t>(game.screen_chat);
}

/// Reads the number of message-log lines shown on screen.
///
/// @param game game record read
/// @return Game.text_lines; 0 turns the log off
inline int32_t message_lines(const Game& game) noexcept {
    return game.text_lines;
}

/// Sets the number of message-log lines shown on screen.
///
/// @param[in,out] game game record whose Game.text_lines is set
/// @param lines lines shown; 0 turns the log off
inline void set_message_lines(Game& game, int32_t lines) noexcept {
    game.text_lines = lines;
}

/// Reads whether the debug display of every unit range is on.
///
/// @param game game record read
/// @return whether Game.show_ranges is nonzero
inline bool show_ranges(const Game& game) noexcept {
    return game.show_ranges != 0;
}

/// Reads the player's row on the kills/losses board.
///
/// @param player player record read
/// @return Player.board_row, 0 for the top row
inline uint8_t board_row(const Player& player) noexcept {
    return player.board_row;
}

/// Sets the player's row on the kills/losses board.
///
/// @param[in,out] player player record whose board_row is set
/// @param row new Player.board_row, 0 for the top row
inline void set_board_row(Player& player, uint8_t row) noexcept {
    player.board_row = row;
}

/// Returns the value a saved game keeps as the player's "WinLoseTime".
///
/// @param player player record read
/// @return Player.win_lose_time
inline int32_t win_lose_time(const Player& player) noexcept {
    return player.win_lose_time;
}

/// Sets the value a saved game keeps as the player's "WinLoseTime".
///
/// @param[in,out] player player record whose win_lose_time is set
/// @param value new Player.win_lose_time
inline void set_win_lose_time(Player& player, int32_t value) noexcept {
    player.win_lose_time = value;
}

/// Returns the tick after which the resource bar next refreshes its income
/// figures; a saved game keeps it as "DisplayTimer". Each refresh advances
/// it by the refresh interval.
///
/// @param player player record read
/// @return Player.display_timer
inline int32_t display_timer(const Player& player) noexcept {
    return player.display_timer;
}

/// Sets the tick after which the resource bar next refreshes its income
/// figures.
///
/// @param[in,out] player player record whose display_timer is set
/// @param value new Player.display_timer, a tick
inline void set_display_timer(Player& player, int32_t value) noexcept {
    player.display_timer = value;
}

} // namespace oa::ui::hud
