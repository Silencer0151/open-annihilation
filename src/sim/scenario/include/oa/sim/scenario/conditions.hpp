// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The campaign conditions' unit events and queries over the canonical World:
// each function handles one event or query of a condition kind, reading and
// writing the condition's named fields (oa/sim/scenario/state.hpp). dispatch() runs each
// kind's handler from a table keyed by kind.

#include "oa/core/world.h"
#include "oa/sim/scenario/state.hpp"

#include <cstdint>

namespace oa::sim::scenario {

// The unit events a condition kind can react to.
enum class Event { unit_destroyed, unit_captured, unit_created };

// What the condition methods need from the running match.
struct ConditionHost {
    void* context{};
    // The "Victory Condition" voice cue of a victory condition met for the
    // first time.
    void (*victory_cue)(void* context){};
    // Whether the unit holds a movement object (Unit.movement), which only
    // types of bmcode 1 get.
    bool (*has_movement)(void* context, const oa::Unit&){};
    // The MoveUnitToRadius query, which needs the match's terrain and the
    // positions of its units; without it the condition is never met.
    bool (*move_unit_to_radius_met)(void* context, Condition& condition){};
};

/// Tells whether a condition kind has a handler for a unit event.
///
/// @param kind condition kind
/// @param event unit event
/// @return true when the kind reacts to the event; false for a value outside
///         Kind or Event
[[nodiscard]] bool reacts_to(Kind kind, Event event) noexcept;

/// Calls a unit event's handler of every victory condition, then of every defeat condition.
///
/// Kinds that ignore the event are skipped; each group's count is re-read as it goes.
///
/// @param[in,out] controller registered conditions
/// @param event unit event to dispatch
/// @param world units, players and types
/// @param unit unit the event is about
/// @param host victory cue and movement query
/// @return false, having called nothing, for a value outside Event; false when
///         visit_conditions stops early or the conditions were never registered
bool dispatch(
    Controller& controller,
    Event event,
    oa::World& world,
    const oa::Unit& unit,
    const ConditionHost& host
);

/// Tests whether a cell is within two cells of a condition's line.
///
/// @param cell cell column or row
/// @param line the condition's line, cells
/// @return true when |cell - line| <= 2
/// @quirk A difference whose negation overflows stays negative and so counts as on the line.
bool cell_on_line(int16_t cell, int32_t line);

/// Visits one player's unit range in slot order, skipping empty slots, until the matcher returns false.
///
/// @param world units and players
/// @param player player index
/// @param matcher called with each occupied unit; false stops the walk
template <typename Matcher>
void scan_player_units(oa::World& world, uint8_t player, Matcher&& matcher) {
    uint32_t count = 0;
    oa::Unit* first = oa::world_player_units(&world, &world.game.players[player], &count);
    for (uint32_t i = 0; i < count; ++i)
        if (first[i].type_index != 0 && !matcher(first[i]))
            return;
}

/// Runs KillEnemyCommander on a destroyed unit.
///
/// A player-1 unit named as its owner's side commander meets the condition.
///
/// @param[in,out] condition the condition
/// @param world units, players and types
/// @param destroyed unit destroyed
/// @param host victory cue and movement query
void kill_enemy_commander_destroyed(
    Condition& condition,
    const oa::World& world,
    const oa::Unit& destroyed,
    const ConditionHost& host
);

/// Runs KillAllMobileUnits on a destroyed unit.
///
/// A player-1 unit with a movement object meets the condition when fewer than two such
/// units of player 1, itself included, are left.
///
/// @param[in,out] condition the condition
/// @param world units, players and types
/// @param destroyed unit destroyed
/// @param host victory cue and movement query
void kill_all_mobile_units_destroyed(
    Condition& condition, oa::World& world, const oa::Unit& destroyed, const ConditionHost& host
);

/// Counts a unit with a movement object in the KillAllMobileUnits walk.
///
/// @param[in,out] condition the condition; units_counted goes up by one for a mobile unit
/// @param has_movement whether the walked unit has a movement object
/// @return true while fewer than two are counted (the walk goes on)
bool count_mobile_unit(Condition& condition, bool has_movement);

/// Runs the BuildUnitType query.
///
/// Once met it stays met; otherwise the named type is resolved while unresolved and
/// player 0's units are searched for a finished one of it.
///
/// @param[in,out] condition the condition
/// @param world units, players and types
/// @param host victory cue and movement query
/// @return true when met
bool build_unit_type_met(Condition& condition, oa::World& world, const ConditionHost& host);

/// Tests one unit in the BuildUnitType walk.
///
/// A finished unit of the resolved type meets the condition.
///
/// @param[in,out] condition the condition
/// @param unit walked unit
/// @param host victory cue and movement query
/// @return true while unmet (the walk goes on)
bool finished_unit_of_type(Condition& condition, const oa::Unit& unit, const ConditionHost& host);

/// Runs CaptureUnitType on a captured unit.
///
/// A unit of the named type that player 1 owned before the capture meets the condition.
///
/// @param[in,out] condition the condition
/// @param world units, players and types
/// @param captured unit captured
/// @param host victory cue and movement query
void capture_unit_type_captured(
    Condition& condition,
    const oa::World& world,
    const oa::Unit& captured,
    const ConditionHost& host
);

/// Runs KillAllOfType on a destroyed unit.
///
/// While unmet, a player-1 unit of the named type meets it when fewer than two units of
/// the type, itself included, are left to player 1.
///
/// @param[in,out] condition the condition
/// @param world units, players and types
/// @param destroyed unit destroyed
/// @param host victory cue and movement query
void kill_all_of_type_destroyed(
    Condition& condition, oa::World& world, const oa::Unit& destroyed, const ConditionHost& host
);

/// Counts a unit of the resolved type in the KillAllOfType and AllUnitsKilledOfType walks.
///
/// @param[in,out] condition the condition; units_counted goes up by one for a match
/// @param unit walked unit
/// @return true while fewer than two are counted (the walk goes on)
bool count_type_match(Condition& condition, const oa::Unit& unit);

/// Runs KillUnitType on a destroyed unit.
///
/// While kills are left, a player-1 unit of the named type counts one down; none left
/// meets the condition.
///
/// @param[in,out] condition the condition
/// @param world units, players and types
/// @param destroyed unit destroyed
/// @param host victory cue and movement query
void kill_unit_type_destroyed(
    Condition& condition,
    const oa::World& world,
    const oa::Unit& destroyed,
    const ConditionHost& host
);

/// Tests one unit inside the MoveUnitToRadius radius.
///
/// A player-0 unit of the named type, or of any type when no name is stored, that can
/// take orders meets the condition.
///
/// @param[in,out] condition the condition
/// @param world units, players and types
/// @param unit unit inside the radius
/// @param host victory cue and movement query
void move_unit_to_radius_unit(
    Condition& condition, const oa::World& world, const oa::Unit& unit, const ConditionHost& host
);

/// Runs the UnitTypePassesX query: while unmet, player 0's units are searched for one on the line.
///
/// @param[in,out] condition the condition
/// @param world units, players and types
/// @param host victory cue and movement query
/// @return true when met
bool unit_type_passes_x_met(Condition& condition, oa::World& world, const ConditionHost& host);

/// Tests one unit for UnitTypePassesX.
///
/// A unit of the named type, or of any type when no name is stored, whose cell column
/// (Unit.cell_x) is within two of the line meets the condition.
///
/// @param[in,out] condition the condition
/// @param world units, players and types
/// @param unit walked unit
/// @param host victory cue and movement query
/// @return true while unmet (the walk goes on)
bool unit_type_passes_x_unit(
    Condition& condition, const oa::World& world, const oa::Unit& unit, const ConditionHost& host
);

/// Runs the UnitTypePassesZ query, as the X one with the cell row.
///
/// @param[in,out] condition the condition
/// @param world units, players and types
/// @param host victory cue and movement query
/// @return true when met
bool unit_type_passes_z_met(Condition& condition, oa::World& world, const ConditionHost& host);

/// Tests one unit for UnitTypePassesZ on the cell row (Unit.cell_z).
///
/// @param[in,out] condition the condition
/// @param world units, players and types
/// @param unit walked unit
/// @param host victory cue and movement query
/// @return true while unmet (the walk goes on)
bool unit_type_passes_z_unit(
    Condition& condition, const oa::World& world, const oa::Unit& unit, const ConditionHost& host
);

/// Runs CommanderKilled on a destroyed unit.
///
/// A player-0 unit named as its owner's side commander meets the condition, with no cue.
///
/// @param[in,out] condition the condition
/// @param world units, players and types
/// @param destroyed unit destroyed
void commander_killed_destroyed(
    Condition& condition, const oa::World& world, const oa::Unit& destroyed
);

/// Runs UnitTypeKilled on a destroyed unit of the named type.
///
/// Counts one down from the kills left; none left meets the condition.
///
/// @param[in,out] condition the condition
/// @param world units, players and types
/// @param destroyed unit destroyed
/// @quirk Every death of the type counts, whoever owned the unit, and once met the count
///        goes on falling, wrapping from the lowest 32-bit value to the highest.
void unit_type_killed_destroyed(
    Condition& condition, const oa::World& world, const oa::Unit& destroyed
);

/// Runs AllUnitsKilledOfType on a destroyed unit of the named type.
///
/// It meets the condition when fewer than two units of the type, itself included, are
/// left to players 0 and 1 together, whoever owned it.
///
/// @param[in,out] condition the condition
/// @param world units, players and types
/// @param destroyed unit destroyed
void all_units_killed_of_type_destroyed(
    Condition& condition, oa::World& world, const oa::Unit& destroyed
);

} // namespace oa::sim::scenario
