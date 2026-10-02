// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/scenario/outcome.hpp"
#include "oa/sim/scenario/commander_rules.hpp"
#include "oa/sim/scenario/conditions.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace oa::sim::scenario {
namespace {
constexpr uint8_t enemy_player = 1;
// OA_UNIT_FLAG_AIR_BASE within the top byte of Unit.flags.
constexpr uint8_t carrier_air_base = static_cast<uint8_t>(OA_UNIT_FLAG_AIR_BASE >> 24);

using QueryRunner = bool (*)(Condition& condition, const QueryContext& context);

// The query one kind runs.
struct KindQuery {
    Kind kind{};
    Query query{};
};

// The function that runs one query.
struct QueryRun {
    Query query{};
    QueryRunner run{};
};

/// Answers the query of the kinds whose unit events decide them: the stored mark.
///
/// @param c the condition
/// @param context unused
/// @return true when met
bool stored_result_query(Condition& c, const QueryContext& context) {
    (void)context;
    return condition_result(c);
}

/// Runs the DestroyAllUnits query from the query table.
///
/// @param[in,out] c the condition
/// @param context outcome view and victory cue
/// @return true when met
bool destroy_all_units_query(Condition& c, const QueryContext& context) {
    return destroy_all_units_met(c, context.view, context.host);
}

/// Runs the BuildUnitType query from the query table.
///
/// @param[in,out] c the condition
/// @param context world and victory cue
/// @return true when met
bool build_unit_type_query(Condition& c, const QueryContext& context) {
    return build_unit_type_met(c, context.world, context.host);
}

/// Runs the MoveUnitToRadius query through the host, which holds the terrain and unit positions.
///
/// @param[in,out] c the condition
/// @param context the host's hook
/// @return true when met; false when the host has no MoveUnitToRadius hook
bool move_unit_to_radius_query(Condition& c, const QueryContext& context) {
    if (context.host.move_unit_to_radius_met == nullptr)
        return false;
    return context.host.move_unit_to_radius_met(context.host.context, c);
}

/// Runs the UnitTypePassesX query from the query table.
///
/// @param[in,out] c the condition
/// @param context world and victory cue
/// @return true when met
bool unit_type_passes_x_query(Condition& c, const QueryContext& context) {
    return unit_type_passes_x_met(c, context.world, context.host);
}

/// Runs the UnitTypePassesZ query from the query table.
///
/// @param[in,out] c the condition
/// @param context world and victory cue
/// @return true when met
bool unit_type_passes_z_query(Condition& c, const QueryContext& context) {
    return unit_type_passes_z_met(c, context.world, context.host);
}

/// Runs the timer query of VictoryTimerRunsOut and DeathTimerRunsOut from the query table.
///
/// @param c the condition
/// @param context match tick
/// @return true once the deadline is reached
bool timer_query(Condition& c, const QueryContext& context) {
    return timer_elapsed(c, context.tick);
}

/// Runs the AllUnitsKilled query from the query table.
///
/// @param[in,out] c the condition
/// @param context outcome view
/// @return true when met
bool all_units_killed_query(Condition& c, const QueryContext& context) {
    return all_units_killed(c, context.view);
}

/// Runs the AnyUnitPassesX query from the query table.
///
/// @param[in,out] c the condition
/// @param context world
/// @return true when met
bool any_unit_passes_x_query(Condition& c, const QueryContext& context) {
    return any_unit_passes_x(c, context.world);
}

/// Runs the AnyUnitPassesZ query from the query table.
///
/// @param[in,out] c the condition
/// @param context world
/// @return true when met
bool any_unit_passes_z_query(Condition& c, const QueryContext& context) {
    return any_unit_passes_z(c, context.world);
}

constexpr std::array<KindQuery, kind_count> queries{{
    {Kind::kill_enemy_commander, Query::stored_result},
    {Kind::destroy_all_units, Query::destroy_all_units},
    {Kind::kill_all_mobile_units, Query::stored_result},
    {Kind::build_unit_type, Query::build_unit_type},
    {Kind::capture_unit_type, Query::stored_result},
    {Kind::kill_all_of_type, Query::stored_result},
    {Kind::kill_unit_type, Query::stored_result},
    {Kind::move_unit_to_radius, Query::move_unit_to_radius},
    {Kind::unit_type_passes_x, Query::unit_type_passes_x},
    {Kind::unit_type_passes_z, Query::unit_type_passes_z},
    {Kind::victory_timer, Query::timer},
    {Kind::commander_killed, Query::stored_result},
    {Kind::all_units_killed, Query::all_units_killed},
    {Kind::all_units_killed_of_type, Query::stored_result},
    {Kind::unit_type_killed, Query::stored_result},
    {Kind::death_timer, Query::timer},
    {Kind::any_unit_passes_x, Query::any_unit_passes_x},
    {Kind::any_unit_passes_z, Query::any_unit_passes_z},
}};

/// Tells whether every query sits at its kind's index, which query_of relies on.
///
/// @return true when the table is in Kind order
constexpr bool queries_in_kind_order() {
    for (std::size_t i = 0; i < queries.size(); ++i)
        if (static_cast<std::size_t>(queries[i].kind) != i)
            return false;
    return true;
}

static_assert(queries_in_kind_order());

constexpr std::array<QueryRun, query_count> query_runs{{
    {Query::stored_result, stored_result_query},
    {Query::destroy_all_units, destroy_all_units_query},
    {Query::build_unit_type, build_unit_type_query},
    {Query::move_unit_to_radius, move_unit_to_radius_query},
    {Query::unit_type_passes_x, unit_type_passes_x_query},
    {Query::unit_type_passes_z, unit_type_passes_z_query},
    {Query::timer, timer_query},
    {Query::all_units_killed, all_units_killed_query},
    {Query::any_unit_passes_x, any_unit_passes_x_query},
    {Query::any_unit_passes_z, any_unit_passes_z_query},
}};

/// Tells whether every runner sits at its query's index, which evaluate_condition relies on.
///
/// @return true when the table is in Query order
constexpr bool query_runs_in_query_order() {
    for (std::size_t i = 0; i < query_runs.size(); ++i)
        if (static_cast<std::size_t>(query_runs[i].query) != i)
            return false;
    return true;
}

static_assert(query_runs_in_query_order());

/// Advances the outcome countdown.
///
/// A negative countdown starts at 4; each later call counts down and the result is due
/// once it drops below zero.
///
/// @param[in,out] state countdown
/// @return true when the result is due
bool countdown_expired(OutcomeState& state) {
    if (state.countdown < 0) {
        state.countdown = 4;
        return false;
    }
    --state.countdown;
    return state.countdown < 0;
}

/// Marks the local player finished and defeated.
///
/// @param[in,out] state outcome flags
void finish_defeated(OutcomeState& state) {
    state.flags |= outcome_flag::finished;
    state.flags &= static_cast<uint16_t>(~outcome_flag::won);
    state.flags |= outcome_flag::defeat_transition;
}

/// Tells whether a controller was registered and the view names a player of the table.
///
/// @param c the controller
/// @param view the outcome view
/// @return false before registration or for a local player outside the table
bool registered_for(const Controller& c, const OutcomeView& view) noexcept {
    return c.registration_complete && view.local_player < player_count;
}

/// Tests one group: victory needs every condition met, defeat any one.
///
/// An empty group first gets its default condition.
///
/// @param[in,out] c the controller
/// @param group victory or defeat
/// @param context the match's world, view, tick and hooks
/// @return true when the group decides the game
bool evaluate_group(Controller& c, Group group, const QueryContext& context) {
    if (!registered_for(c, context.view))
        return false;
    auto& count = group == Group::victory ? c.victory_count : c.defeat_count;
    auto& entries = group == Group::victory ? c.victory : c.defeat;
    if (count == 0) {
        Condition condition;
        condition.kind = group == Group::victory ? Kind::destroy_all_units : Kind::all_units_killed;
        entries[0] = condition;
        count = 1;
    }
    for (int32_t i = 0; i < count; ++i) {
        if (count < 0 || count > static_cast<int32_t>(handler_capacity) ||
            !entries[static_cast<std::size_t>(i)])
            return false;
        bool satisfied = evaluate_condition(*entries[static_cast<std::size_t>(i)], context);
        if (group == Group::victory && !satisfied)
            return false;
        if (group == Group::defeat && satisfied)
            return true;
    }
    return group == Group::victory;
}
} // namespace

bool qualifies_for_all_units_killed(const UnitStatus& unit) {
    return (unit.flags & OA_UNIT_FLAG_SELECTABLE) != 0 &&
           (unit.build_remaining == 0.0F || std::isnan(unit.build_remaining)) &&
           unit.capture_cooldown == 0 &&
           (!unit.has_attachment_parent || (unit.carrier_flags & carrier_air_base) != 0);
}

bool timer_elapsed(const Condition& c, uint32_t tick) {
    // Met iff tick >= deadline, including when the deadline is 0.
    return tick >= c.deadline;
}

bool any_unit_passes_x(Condition& c, World& world) {
    if (c.satisfied == 0)
        scan_player_units(world, enemy_player, [&](const Unit& unit) {
            return any_unit_passes_x_unit(c, unit.cell_x);
        });
    return c.satisfied != 0;
}

bool any_unit_passes_x_unit(Condition& c, int16_t cell_x) {
    if (cell_on_line(cell_x, c.line))
        c.satisfied = 1;
    return c.satisfied == 0;
}

bool destroy_all_units_met(Condition& c, const OutcomeView& view, const ConditionHost& host) {
    // Player 1's live-unit count alone, not a sum over every enemy.
    if (view.live_units[enemy_player] != 0)
        return false;
    if (c.celebrated == 0) {
        if (host.victory_cue != nullptr)
            host.victory_cue(host.context);
        c.celebrated = 1;
    }
    return true;
}

bool all_units_killed_unit(Condition& c, const UnitStatus& unit) {
    if (qualifies_for_all_units_killed(unit))
        c.satisfied = 0;
    return c.satisfied != 0;
}

Query query_of(Kind kind) noexcept {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= queries.size())
        return Query::stored_result;
    return queries[index].query;
}

bool evaluate_condition(Condition& c, const QueryContext& context) {
    return query_runs[static_cast<std::size_t>(query_of(c.kind))].run(c, context);
}

bool all_units_killed(Condition& c, const OutcomeView& view) {
    c.satisfied = 1;
    for (const auto& unit : view.player_zero_units)
        if (unit.type_index != 0 && !all_units_killed_unit(c, unit))
            break;
    return c.satisfied != 0;
}

bool condition_result(const Condition& c) {
    return c.satisfied != 0;
}

bool any_unit_passes_z(Condition& c, World& world) {
    if (c.satisfied == 0)
        scan_player_units(world, enemy_player, [&](const Unit& unit) {
            return any_unit_passes_z_unit(c, unit);
        });
    return c.satisfied != 0;
}

bool any_unit_passes_z_unit(Condition& c, const Unit& unit) {
    if (cell_on_line(unit.cell_z, c.line))
        c.satisfied = 1;
    return c.satisfied == 0;
}

bool campaign_victory(Controller& c, const QueryContext& context) {
    return evaluate_group(c, Group::victory, context);
}

bool campaign_defeat(Controller& c, const QueryContext& context) {
    return evaluate_group(c, Group::defeat, context);
}

bool victory_check(Controller& c, GameKind kind, const QueryContext& context) {
    if (c.enabled == 0)
        return false;
    switch (kind) {
    case GameKind::campaign:
        return campaign_victory(c, context);
    case GameKind::skirmish:
        return offline_victory(c, context.view);
    case GameKind::multiplayer:
        return multiplayer_victory(context.world);
    }
    return false;
}

bool defeat_check(
    Controller& c,
    GameKind kind,
    const QueryContext& context,
    DiagnosticLoss& diagnostic,
    DiagnosticRandom& random
) {
    if (c.enabled == 0)
        return false;
    if (diagnostic_defeat(c, diagnostic, context.tick, random))
        return true;
    switch (kind) {
    case GameKind::campaign:
        return campaign_defeat(c, context);
    case GameKind::skirmish:
    case GameKind::multiplayer:
        return offline_defeat(c, context.view);
    }
    return false;
}

bool offline_victory(const Controller& c, const OutcomeView& v) {
    if (!registered_for(c, v))
        return false;
    if (c.enabled == 0)
        return false;
    for (std::size_t i = 0; i < player_count; ++i)
        if (i != v.local_player && v.local_allies[i] == 0 && v.live_units[i] != 0)
            return false;
    return true;
}

bool offline_defeat(const Controller& c, const OutcomeView& v) {
    if (!registered_for(c, v))
        return false;
    return c.enabled != 0 && v.live_units[v.local_player] == 0;
}

bool diagnostic_defeat(
    const Controller& c, DiagnosticLoss& diagnostic, uint32_t tick, DiagnosticRandom& random
) {
    if (c.enabled == 0 || !diagnostic.active)
        return false;
    if (diagnostic.deadline == 0) {
        auto value = random.rand15();
        if (value > 32767)
            return false;
        diagnostic.deadline = 9000U + static_cast<uint32_t>(value) * 9000U / 32768U;
    }
    if (diagnostic.deadline <= tick) {
        diagnostic.deadline = 0;
        return true;
    }
    return false;
}

Outcome advance_outcome(
    OutcomeState& state,
    bool campaign,
    bool victory,
    bool defeat,
    bool defeat_allowed,
    bool deathmatch,
    bool watches_instead
) {
    Outcome candidate = Outcome::ongoing;
    if (campaign) {
        if (victory)
            candidate = Outcome::victory;
        else if (defeat)
            candidate = Outcome::defeat;
    } else {
        if (defeat_allowed && defeat)
            candidate = Outcome::defeat;
        else if (victory)
            candidate = Outcome::victory;
    }
    if (candidate == Outcome::ongoing || !countdown_expired(state))
        return Outcome::ongoing;
    if (!campaign && deathmatch && candidate == Outcome::defeat)
        return Outcome::respawn;
    if (!campaign && watches_instead && candidate == Outcome::defeat)
        return Outcome::watch;
    if (candidate == Outcome::victory)
        state.flags |=
            outcome_flag::finished | outcome_flag::won | outcome_flag::victory_transition;
    else
        finish_defeated(state);
    return candidate;
}

Outcome
advance_abandoned_outcome(OutcomeState& state, bool deathmatch, int32_t connected_participants) {
    if (deathmatch || connected_participants != 0 || !countdown_expired(state))
        return Outcome::ongoing;
    finish_defeated(state);
    return Outcome::defeat;
}
} // namespace oa::sim::scenario
