// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Shared declarations between the computer-player sources.
#pragma once

#include "oa/sim/ai.hpp"

namespace oa::sim::ai {

inline constexpr uint32_t standing_move_manoeuvre = 1;
inline constexpr uint32_t standing_move_roam = 2;
inline constexpr uint32_t standing_fire_at_will = 2;
inline constexpr uint32_t max_recruits = 5000;

/// Returns a type's computer-player fields.
///
/// @param state computer players and their type table
/// @param type unit type index
/// @return the fields, or null for type 0, an index past the table or a null state
[[nodiscard]] const ComputerType*
computer_type(const ComputerPlayers* state, uint16_t type) noexcept;

/// Builds a controller with one task per squad.
///
/// Structures, a land strike group with its rally, construction, an idle task for armed
/// structures (the structures task under ai.squad5-factory-tick), a naval strike group
/// with its rally, air raids and the siege, whose target, search point and search step
/// all start at the map's centre. Both strike groups may attack from 4 members and always
/// attack from 6 (ai.attack-wave-size).
///
/// @param[out] ai controller to build
/// @param player player index
/// @param game map size in world units (Game.map_width_world, Game.map_height_world)
/// @param rules the match's ai.* rules
void computer_player_create(
    ComputerPlayer& ai,
    uint8_t player,
    const oa::Game& game,
    const data::match_rules::AiRules& rules
) noexcept;

/// Converts a double to a 32-bit integer, truncating toward zero.
///
/// @param value value to convert
/// @return the integer; a value that is not finite or does not fit gives INT32_MIN
[[nodiscard]] int32_t truncate_to_int32(double value) noexcept;

/// Chooses a construction site for a type near the builder and the base.
///
/// The search radius grows by 160 each call up to the map's width and resets after a
/// placement. The search centre is the base, or the point that far from the builder
/// toward it. Extractors try the metal spots first unless a random draw below 255 is
/// under the surface metal.
///
/// @param state computer players and their type table
/// @param host random stream, site and metal queries
/// @param[in,out] ai controller; its placement radius changes
/// @param builder builder position, 16.16 world coordinates
/// @param type unit type to place
/// @param[out] site receives the site centre, 16.16, at the builder's height
/// @return false when no site was found
[[nodiscard]] bool computer_place_build(
    const ComputerPlayers* state,
    const ComputerHost& host,
    ComputerPlayer& ai,
    const oa::FixedVec3& builder,
    uint16_t type,
    oa::FixedVec3* site
) noexcept;

} // namespace oa::sim::ai
