// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the automation endpoint reports of a match and of the battle room,
// as the match and room requests answer (serve_reports.hpp): the match is
// read from its world, as the automation host gives it, and the battle room
// from the multiplayer screens' own lobby; both are only read.
#pragma once

#include "oa/app/automation/json.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace oa {
struct Game;
struct Player;
struct World;
} // namespace oa

namespace oa::ui::frontend_multiplayer {
struct Lobby;
}

namespace oa::app::automation {

/// Writes a match's members of the match answer: whether a match runs, its
/// tick, pause and speed, its players, the local player, and its outcome.
///
/// @param[in,out] json the answer's object, open
/// @param match the running match's world; null while no match runs
/// @param digest the match's saved-state digest, written as state_hash; nothing writes none
void write_match(JsonWriter& json, const oa::World* match, std::optional<uint64_t> digest);

/// Writes the battle room's members of the room answer: whether the room
/// shows, the game's name, its host, its players, its map, the host's
/// options and the room's chat.
///
/// @param[in,out] json the answer's object, open
/// @param lobby the multiplayer screens' lobby, which is only read; null for none
/// @param in_room the battle room shows
void write_room(JsonWriter& json, oa::ui::frontend_multiplayer::Lobby* lobby, bool in_room);

/// Returns the text of one of the game's fixed text fields: its bytes up to
/// the first zero byte, as they are, in the game's code page. The JSON
/// writer writes text that is not UTF-8 as unicode_text (json.hpp) gives it.
///
/// @param bytes the field
/// @param size the field's size in bytes
/// @return the text
[[nodiscard]] std::string game_text(const char* bytes, size_t size);

/// Returns a player's name, as its record holds it.
///
/// @param player the player
/// @return the name, up to its end or the record's 30 bytes
[[nodiscard]] std::string_view player_name(const oa::Player& player) noexcept;

/// Tells whether a match's player record holds a player taking part: a
/// player of this machine, a computer player or one of another machine.
///
/// @param player the record
/// @return true for such a player
[[nodiscard]] bool player_seated(const oa::Player& player) noexcept;

/// Tells whether a match's local player has its outcome: won or lost.
///
/// @param game the match's Game block
/// @return true once the match is decided for the local player
[[nodiscard]] bool match_decided(const oa::Game& game) noexcept;

/// Returns the local player's outcome, once the match is decided for it.
///
/// @param game the match's Game block
/// @return "victory" or "defeat"; nothing while the match goes on
[[nodiscard]] std::optional<std::string_view> match_outcome(const oa::Game& game) noexcept;

/// Returns the slot of a match's winner, once the match is decided.
///
/// @param game the match's Game block
/// @return the local player when it won; otherwise the one player still
///         with units, when there is one; nothing else
[[nodiscard]] std::optional<uint8_t> match_winner(const oa::Game& game) noexcept;

} // namespace oa::app::automation
