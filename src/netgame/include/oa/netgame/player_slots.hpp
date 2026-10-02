// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The session's view of the ten player records (Game.players): which slot a
// transport id occupies, the id a slot answers to, and the free slot a
// joining player takes. A slot answers to its Player.player_id while its
// status is set, whatever Player.in_use says.

#include "oa/core/game_state.h"

namespace oa::netgame {

inline constexpr uint8_t no_player_slot = OA_PLAYER_COUNT;
inline constexpr uint32_t no_player_id = 0xffffffffu;

/// Returns the transport id a player slot answers to.
///
/// @param game Game whose player records are read.
/// @param slot Player slot, 0..9.
/// @return The slot's Player.player_id, or no_player_id when the slot is out of range or its status is free.
[[nodiscard]] uint32_t player_slot_id(const Game& game, uint8_t slot) noexcept;

/// Finds the slot answering to a transport id.
///
/// @param game Game whose player records are searched.
/// @param player_id Transport id; no_player_id never matches.
/// @return The lowest matching slot, or no_player_slot.
[[nodiscard]] uint8_t player_slot_of(const Game& game, uint32_t player_id) noexcept;

/// Finds the player record answering to a transport id.
///
/// @param game Game whose player records are searched.
/// @param player_id Transport id; no_player_id never matches.
/// @return The record of the lowest matching slot, or null.
[[nodiscard]] Player* player_of_id(Game& game, uint32_t player_id) noexcept;

/// Finds the player record answering to a transport id.
///
/// @param game Game whose player records are searched.
/// @param player_id Transport id; no_player_id never matches.
/// @return The record of the lowest matching slot, or null.
[[nodiscard]] const Player* player_of_id(const Game& game, uint32_t player_id) noexcept;

/// Returns the transport id of the first slot a human plays on this machine (status local).
///
/// @param game Game whose player records are searched.
/// @return That slot's Player.player_id, or no_player_id when no slot is local.
[[nodiscard]] uint32_t first_local_player_id(const Game& game) noexcept;

/// Finds the slot a joining player takes: the first record neither in use nor closed.
///
/// @param game Game whose player records are searched.
/// @return That slot, or no_player_slot when every record is taken or closed.
[[nodiscard]] uint8_t free_player_slot(const Game& game) noexcept;

} // namespace oa::netgame
