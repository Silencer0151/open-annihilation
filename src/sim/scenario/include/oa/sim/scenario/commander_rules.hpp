// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/data/campaign/campaign_file.hpp"
#include "oa/core/world.h"

#include <cstdint>

// The deathmatch branches of the commander rule (Game.session_rules, the
// game setup's commander option) outside the commander-death self-destruct.
namespace oa::sim::scenario {

enum class CommanderRule : int32_t {
    game_continues = 0,
    game_ends = 1,
    deathmatch = 2,
};

/// Tests whether the commander rule is deathmatch.
///
/// @param game game record
/// @return true when Game.session_rules is CommanderRule::deathmatch
[[nodiscard]] inline bool deathmatch(const oa::Game& game) noexcept {
    return game.session_rules == static_cast<int32_t>(CommanderRule::deathmatch);
}

/// Returns what the kills board ranks a player by.
///
/// @param game game record, for the commander rule
/// @param player player record
/// @return commander kills in deathmatch, kills otherwise
[[nodiscard]] int16_t board_score(const oa::Game& game, const oa::Player& player) noexcept;

/// Moves a killer up the kills board after a kill is counted in a skirmish or multiplayer game.
///
/// An active killer below the top row moves up to the best row held by a listed player
/// (status set, not a watcher) with a lower score; the rows from there down to the
/// killer's old row move down one.
///
/// @param[in,out] world players and board rows
/// @param killer player who scored
/// @return true when the killer took the top row, which the game announces in chat
bool promote_on_kill_board(oa::World& world, oa::Player& killer) noexcept;

/// Tests for multiplayer victory.
///
/// Never in deathmatch. Otherwise each other active, non-watching player must have
/// created units, and while still in the game be in a mutual alliance with the local
/// player that both sides count for allied victory, and be allied with every player
/// still in the game.
///
/// @param world players and alliances
/// @return true when the local player has won
[[nodiscard]] bool multiplayer_victory(const oa::World& world) noexcept;

/// Counts the human players still in the game and not watching.
///
/// Local players count, and players simulated elsewhere whose setup state is human.
///
/// @param world players
/// @return the count
[[nodiscard]] int32_t connected_participants(const oa::World& world) noexcept;

/// Counts the computer players still in the game.
///
/// @param world players
/// @return the count
[[nodiscard]] int32_t computer_participants(const oa::World& world) noexcept;

/// Tests whether a multiplayer player whose defeat countdown ran out watches instead of losing.
///
/// It watches while nothing rejected it and the host allows watching, or computer
/// players still play (this machine hosts them).
///
/// @param world players and game options
/// @return true when the player watches
[[nodiscard]] bool defeated_player_watches(const oa::World& world) noexcept;

/// Finds the first player slot with a status whose player-info record carries the host role.
///
/// @param world players
/// @return the slot, or OA_PLAYER_COUNT when none does
[[nodiscard]] uint8_t host_player_index(const oa::World& world) noexcept;

/// Decides whether a session's chat line may run cheat commands, as its mission start sets it.
///
/// Always in a single-player game, a campaign or a skirmish, and in a
/// multiplayer game as the host's CHEATING option says, for every player
/// alike: the host starts through the same branch and reads its own record
/// (on when no player holds the host role). 3.1c refuses cheats in a
/// campaign; the engine runs them there as in a skirmish (VARIANCES.md).
///
/// @param kind session kind
/// @param world players and their options
/// @param previous current setting, kept for any other kind
/// @return whether cheats are allowed
[[nodiscard]] bool session_cheats_allowed(
    oa::data::campaign::SessionKind kind, const oa::World& world, bool previous
) noexcept;

} // namespace oa::sim::scenario
