// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/sim/scenario/conditions.hpp"
#include "oa/core/world.h"
#include "oa/sim/scenario/state.hpp"
#include <cstdint>
#include <span>

namespace oa::sim::scenario {
inline constexpr std::size_t player_count = 10;

// The fields the can-take-orders test reads, not a generic 'alive' flag.
struct UnitStatus {
    uint16_t type_index{};        // Unit.type_index; the player-unit walk skips zero slots
    uint8_t flags{};              // the low byte of Unit.flags
    float build_remaining{};      // Unit.build_remaining
    uint32_t capture_cooldown{};  // Unit.capture_cooldown
    bool has_attachment_parent{}; // whether Unit.attach_parent is set
    uint8_t carrier_flags{};      // the top byte of the carrier's Unit.flags
};

struct OutcomeView {
    uint8_t local_player{};
    std::array<int16_t, player_count> live_units{};   // Player.unit_count
    std::array<uint8_t, player_count> local_allies{}; // the local player's Player.alliance
    std::span<const UnitStatus> player_zero_units;
};

// What the condition queries and outcome tests read from the running match.
struct QueryContext {
    oa::World& world; // writable for the unit walks
    const OutcomeView& view;
    uint32_t tick{};      // match tick, for the timers and the disc-check loss
    ConditionHost host{}; // victory cue, movement query and MoveUnitToRadius
};

/// Tests whether a unit can take orders, which keeps AllUnitsKilled from being met.
///
/// @param unit the unit's status fields
/// @return true with OA_UNIT_FLAG_SELECTABLE, a finished (or unordered) build, no capture
///         cooldown, and no carrier unless the carrier has OA_UNIT_FLAG_AIR_BASE
bool qualifies_for_all_units_killed(const UnitStatus& unit);

// The queries that tell whether a condition is met. The eight kinds whose
// unit events decide them share stored_result, and the two timers share timer;
// every other kind has its own.
enum class Query {
    stored_result,
    destroy_all_units,
    build_unit_type,
    move_unit_to_radius,
    unit_type_passes_x,
    unit_type_passes_z,
    timer,
    all_units_killed,
    any_unit_passes_x,
    any_unit_passes_z
};
// One per Query; the query runner table is indexed by Query.
inline constexpr std::size_t query_count = 10;
static_assert(query_count == static_cast<std::size_t>(Query::any_unit_passes_z) + 1);

/// Returns the query a condition kind runs.
///
/// Throws std::invalid_argument for a value outside Kind.
///
/// @param kind condition kind
/// @return its query
Query query_of(Kind kind);

/// Runs one condition's query: the one query_of names for its kind.
///
/// Throws std::invalid_argument for a kind outside Kind, or for MoveUnitToRadius without
/// the host's hook.
///
/// @param[in,out] condition the condition
/// @param context the match's world, view, tick and hooks
/// @return true when met
bool evaluate_condition(Condition& condition, const QueryContext& context);
/// Runs the AllUnitsKilled query: met unless one of player 0's units still qualifies.
///
/// The condition is marked met before the walk and cleared by the first qualifying unit.
///
/// @param[in,out] condition the condition
/// @param view player 0's units
/// @return true when met
bool all_units_killed(Condition& condition, const OutcomeView& view);
/// Returns whether the condition is marked met, the query of eight condition kinds.
///
/// @param condition the condition
/// @return true when met
bool condition_result(const Condition& condition);
/// Runs the AnyUnitPassesZ query.
///
/// Unless already met, walks player 1's units, skipping empty slots, until one is
/// within two cells of the line.
///
/// @param[in,out] condition the condition
/// @param world units and players
/// @return true when met
bool any_unit_passes_z(Condition& condition, World& world);
/// Tests one unit for AnyUnitPassesZ.
///
/// A unit whose Z cell is within two cells of the line meets the condition.
///
/// @param[in,out] condition the condition
/// @param unit walked unit
/// @return true while unmet (the walk goes on)
bool any_unit_passes_z_unit(Condition& condition, const Unit& unit);
/// Runs the DestroyAllUnits query: met while player 1 has no units.
///
/// The victory cue plays the first time.
///
/// @param[in,out] condition the condition
/// @param view live-unit counts
/// @param host victory cue
/// @return true when met
bool destroy_all_units_met(
    Condition& condition, const OutcomeView& view, const ConditionHost& host
);
/// Tests one unit in the AllUnitsKilled walk.
///
/// A unit that can take orders clears the mark the query set.
///
/// @param[in,out] condition the condition
/// @param unit walked unit's status
/// @return true while the condition is still marked met (the walk goes on)
bool all_units_killed_unit(Condition& condition, const UnitStatus& unit);
/// Runs the timer query of VictoryTimerRunsOut and DeathTimerRunsOut.
///
/// The query stores nothing.
///
/// @param condition the condition
/// @param tick match tick
/// @return true once the tick (unsigned) has reached the deadline, a zero deadline included
bool timer_elapsed(const Condition& condition, uint32_t tick);
/// Runs the AnyUnitPassesX query.
///
/// Unless already met, walks player 1's units, skipping empty slots, until one is on
/// the line.
///
/// @param[in,out] condition the condition
/// @param world units and players
/// @return true when met
bool any_unit_passes_x(Condition& condition, World& world);
/// Tests one unit for AnyUnitPassesX.
///
/// A unit whose cell column is within two of the line meets the condition.
///
/// @param[in,out] condition the condition
/// @param cell_x the unit's cell column
/// @return true while unmet (the walk goes on)
/// @quirk A difference whose negation overflows counts as on the line.
bool any_unit_passes_x_unit(Condition& condition, int16_t cell_x);
/// Tests campaign victory: every victory condition holds.
///
/// With none registered a DestroyAllUnits condition is added first.
///
/// Throws std::logic_error before registration or for a corrupt condition array.
///
/// @param[in,out] controller registered conditions
/// @param context the match's world, view, tick and hooks
/// @return true when won
bool campaign_victory(Controller& controller, const QueryContext& context);
/// Tests campaign defeat: the defeat conditions in order until one is met.
///
/// With none registered an AllUnitsKilled condition is added first.
///
/// Throws std::logic_error before registration or for a corrupt condition array.
///
/// @param[in,out] controller registered conditions
/// @param context the match's world, view, tick and hooks
/// @return true when lost
bool campaign_defeat(Controller& controller, const QueryContext& context);
/// Tests skirmish victory: no player other than the local one and its allies still has units.
///
/// @param controller enabled word
/// @param view live-unit counts and the local alliance row
/// @return true when won; false while the controller is disabled
bool offline_victory(const Controller& controller, const OutcomeView& view);
/// Tests skirmish defeat: the local player has no units left.
///
/// The disc-check loss is tested separately (diagnostic_defeat).
///
/// @param controller enabled word
/// @param view live-unit counts
/// @return true when lost; false while the controller is disabled
bool offline_defeat(const Controller& controller, const OutcomeView& view);

// The loss the game imposes when its disc check fails (?).
struct DiagnosticLoss {
    bool active{};       // the disc check failed
    uint32_t deadline{}; // tick the loss falls due; 0 until drawn
};

class DiagnosticRandom {
  public:

    virtual ~DiagnosticRandom() = default;
    /// Draws from the match's linear congruential stream.
    ///
    /// @return a value in 0..32767
    virtual uint16_t rand15() = 0;
};

/// Tests the loss a failed disc check imposes.
///
/// The first call draws a deadline 9000..17999 ticks (5 to 10 minutes) ahead; the loss
/// is due once the tick reaches it, and the deadline is then cleared.
///
/// Throws std::invalid_argument for a draw outside 15 bits.
///
/// @param controller enabled word; nothing while disabled
/// @param[in,out] diagnostic whether the check failed, and the deadline
/// @param tick match tick
/// @param random the match's linear congruential stream
/// @return true when the loss is due
bool diagnostic_defeat(
    const Controller& controller,
    DiagnosticLoss& diagnostic,
    uint32_t tick,
    DiagnosticRandom& random
);

// The session kind CampaignFile.kind holds, which picks the
// victory and defeat tests.
enum class GameKind : int32_t { campaign = 1, skirmish = 2, multiplayer = 3 };

/// Runs the local player's victory test.
///
/// None while Controller.enabled is clear, else the campaign's conditions, the
/// skirmish test or the multiplayer game's.
///
/// @param[in,out] controller registered conditions and enabled word
/// @param kind session kind
/// @param context the match's world, view, tick and hooks
/// @return true when won
bool victory_check(Controller& controller, GameKind kind, const QueryContext& context);

/// Runs the local player's defeat test.
///
/// None while the controller is disabled; the disc-check loss once its deadline
/// passes; else the campaign's conditions, or in a skirmish or multiplayer game the
/// local player left with no units.
///
/// @param[in,out] controller registered conditions and enabled word
/// @param kind session kind
/// @param context the match's world, view, tick and hooks
/// @param[in,out] diagnostic disc-check loss state
/// @param random the match's linear congruential stream
/// @return true when lost
bool defeat_check(
    Controller& controller,
    GameKind kind,
    const QueryContext& context,
    DiagnosticLoss& diagnostic,
    DiagnosticRandom& random
);
// respawn: in deathmatch the local player's defeat countdown ran out and it
// gets a new commander instead of losing; no outcome flags change.
// watch: in a multiplayer game it goes on as a watcher instead; no outcome
// flags change either.
enum class Outcome { ongoing, victory, defeat, respawn, watch };

namespace outcome_flag {
inline constexpr uint16_t finished = 0x04, won = 0x10, victory_transition = 0x20,
                          defeat_transition = 0x40;
}

struct OutcomeState {
    int16_t countdown = -1; // as Game.outcome_countdown; -1 when the game loads
    uint16_t flags{};       // as Game.outcome_flags
};

/// Runs the local player's once-per-30-tick outcome branch.
///
/// A met condition starts a countdown of five calls; it is not reset when the condition
/// goes false. A campaign prioritizes victory; a skirmish or multiplayer game
/// prioritizes defeat. Call after the tick's unit and player work, once per due interval.
///
/// @param[in,out] state countdown and outcome flags
/// @param campaign true in a campaign
/// @param victory result of victory_check
/// @param defeat result of defeat_check
/// @param defeat_allowed false ignores defeat outside a campaign
/// @param deathmatch true under the deathmatch commander rule: an expired defeat asks for
///        a respawn instead
/// @param watches_instead true when the player may watch instead of losing
/// @return the outcome; victory and defeat also set the flags
Outcome advance_outcome(
    OutcomeState& state,
    bool campaign,
    bool victory,
    bool defeat,
    bool defeat_allowed = true,
    bool deathmatch = false,
    bool watches_instead = false
);

/// Runs the per-tick defeat of a multiplayer game no human is left in.
///
/// The defeat countdown runs on the same state, except under the deathmatch commander rule.
///
/// @param[in,out] state countdown and outcome flags
/// @param deathmatch true under the deathmatch commander rule
/// @param connected_participants human players still in the game
/// @return defeat once the countdown expires, else ongoing
Outcome
advance_abandoned_outcome(OutcomeState& state, bool deathmatch, int32_t connected_participants);
} // namespace oa::sim::scenario
