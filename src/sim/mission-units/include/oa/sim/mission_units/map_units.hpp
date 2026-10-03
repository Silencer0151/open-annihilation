// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Units a skirmish or multiplayer map places from its OTA schema when a mod
// profile turns setup.map-scripted-units on: the schema's units entries
// belong to start positions by their player number (1 for the first start
// position), number 11 to a neutral computer player. Each machine places
// them for the players it runs: at the start in place of the commander,
// and an entry with a CreationCountdown once that many seconds of game
// time have passed.
#pragma once

#include "oa/core/side.h"
#include "oa/core/world.h"
#include "oa/data/campaign/campaign_file.hpp"
#include "oa/sim/mission_units.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace oa::sim::mission_units {

/// The owner number of map units that go to the neutral computer player.
inline constexpr int32_t neutral_map_owner = 11;
/// A start entry naming a commander type takes the slot this far past its
/// player's first slot, plus the entry's index.
inline constexpr uint16_t commander_slot_offset = 90;
/// Ticks in a second of game time, which CreationCountdown counts in.
inline constexpr uint32_t map_unit_ticks_per_second = 30;
/// The player slots.
inline constexpr std::size_t map_unit_players = 10;

/// Returns a map unit entry's owner number: its player number, with its
/// flags as the high byte, so an entry with any flag set belongs to no
/// start position and to no neutral player.
///
/// @param entry the schema entry
/// @return the owner number, read as a signed 16-bit value
[[nodiscard]] int32_t map_unit_owner(const data::campaign::MissionUnit& entry) noexcept;

/// Tells whether a schema places units for the neutral player.
///
/// @param units the schema's units entries
/// @param count number of entries
/// @return true when an entry's owner number is neutral_map_owner
[[nodiscard]] bool
has_neutral_map_units(const data::campaign::MissionUnit* units, int32_t count) noexcept;

/// One start entry a player takes.
struct MapUnitPick {
    int32_t entry{};        ///< the schema entry's index
    uint16_t slot_offset{}; ///< past the player's first slot; 0 takes any free slot
};

/// Picks the start entries a player takes.
///
/// Entries with a CreationCountdown above 0 are left to the timed pass.
/// The neutral player takes the entries of owner neutral_map_owner and no
/// other; any other player the entries whose owner number is its start
/// position + 1. An entry naming one of the sides' commanders (exactly)
/// takes the slot commander_slot_offset + its index past the player's
/// first slot.
///
/// @param units the schema's units entries
/// @param count number of entries
/// @param start_position the player's start position, 0-based
/// @param neutral whether the player takes the neutral units
/// @param sides the sides whose commander names are reserved
/// @param[out] out the picks, in schema order
/// @return the number of picks written; picks past out's size are dropped
int32_t pick_start_map_units(
    const data::campaign::MissionUnit* units,
    int32_t count,
    int32_t start_position,
    bool neutral,
    std::span<const Side> sides,
    std::span<MapUnitPick> out
) noexcept;

/// Creates one map unit for a player through the hooks.
///
/// The unit is of the type whose UnitName equals the entry's Unitname
/// exactly (none: no unit), finished, at the entry's x and z; its height
/// is the height of the map cell it stands on, or the entry's own when
/// that cell is past the map's last.
///
/// @param world the world, for its unit types and map cells
/// @param entry the schema entry
/// @param player the owner, 0..9
/// @param requested_slot the slot to take; 0 for any free one
/// @param hooks the spawn service (create_unit)
/// @return the unit, or null when none was made
Unit* create_map_unit(
    World& world,
    const data::campaign::MissionUnit& entry,
    uint8_t player,
    uint16_t requested_slot,
    const Hooks& hooks
) noexcept;

/// Runs the InitialMission scripts of map units created together.
///
/// @param units the schema's units entries
/// @param count number of entries
/// @param created the unit made for each entry, null for none
/// @param[in,out] point the map point the scripts share
/// @param hooks order, definition and carry services
void run_map_unit_scripts(
    const data::campaign::MissionUnit* units,
    int32_t count,
    Unit* const* created,
    ScriptPoint& point,
    const Hooks& hooks
);

/// Orders the timed entries: those with a Unitname and a CreationCountdown
/// above 0, by countdown, entries of equal countdown in schema order.
///
/// @param units the schema's units entries
/// @param count number of entries
/// @param[out] out entry indices
/// @return the number written; entries past out's size are dropped
int32_t order_timed_map_units(
    const data::campaign::MissionUnit* units, int32_t count, std::span<int32_t> out
) noexcept;

/// Tells whether a timed entry is due: its countdown, in seconds, is at
/// most the whole seconds of game time.
///
/// @param entry the schema entry
/// @param tick the game tick
/// @return true when it is due
[[nodiscard]] bool
timed_map_unit_due(const data::campaign::MissionUnit& entry, uint32_t tick) noexcept;

/// Finds the player a timed entry goes to.
///
/// An entry of owner neutral_map_owner goes to the neutral player; one of
/// owner 1..10 to the player this machine placed at that start position,
/// unless that player takes the neutral units.
///
/// @param entry the schema entry
/// @param player_at_position the player placed at each start position here, -1 for none
/// @param neutral_player the player taking the neutral units, -1 for none
/// @return the player slot, or -1 for none
[[nodiscard]] int32_t timed_map_unit_player(
    const data::campaign::MissionUnit& entry,
    const std::array<int32_t, map_unit_players>& player_at_position,
    int32_t neutral_player
) noexcept;

/// Moves this machine's highest-placed computer player to the start position
/// of its highest-placed human when the human's is higher, on a map with
/// neutral units whose start positions are not fixed.
///
/// The players compared are the humans and the computer players this
/// machine runs, the first of the highest position counting; nothing moves
/// without one of each. When the two swap, the player being placed takes
/// the computer player's position if it holds the human's, and the human's
/// otherwise.
///
/// @param[in,out] positions the start position of each player slot
/// @param statuses each slot's Player.status
/// @param[in,out] placing the start position of the player being placed
void move_computer_last(
    std::array<int32_t, map_unit_players>& positions,
    const std::array<uint8_t, map_unit_players>& statuses,
    int32_t& placing
) noexcept;

} // namespace oa::sim::mission_units
