// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The condition methods over a hand-built World: owner, name, count and
// compare outcomes as the game decides them, and the kind tables
// that dispatch the unit events and the queries.
#include "oa/sim/scenario/conditions.hpp"
#include "oa/sim/scenario/outcome.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

using namespace oa;
using namespace oa::sim::scenario;

namespace {
int failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

constexpr uint16_t tank = 1; // "ARMSTUMP"
constexpr uint16_t gate = 2; // "CORGATE"
constexpr uint32_t slots_per_player = 4;

struct Header : DefinitionHost {
    const char* key{};
    const char* value{};

    /// Returns 1 for the one integer key, when it has no text.
    ///
    /// @param name key name
    /// @param fallback value for any other key
    /// @return the value
    int32_t integer(std::string_view name, int32_t fallback) override {
        return value == nullptr && name == key ? 1 : fallback;
    }

    /// Returns the one text key's value.
    ///
    /// @param name key name
    /// @return the text, or nullopt for any other key
    std::optional<std::string> text(std::string_view name) override {
        if (value != nullptr && name == key)
            return std::string(value);
        return std::nullopt;
    }
};

/// Registers one condition from a one-key GlobalHeader and returns it.
///
/// @param key the kind's GlobalHeader key
/// @param value its text, or null for an integer key set to 1
/// @return the condition
Condition registered(const char* key, const char* value = nullptr) {
    Header header;
    header.key = key;
    header.value = value;
    Controller controller;
    register_conditions(controller, header);
    for (auto& entry : controller.victory)
        if (entry && descriptor(entry->kind).key == key)
            return *entry;
    for (auto& entry : controller.defeat)
        if (entry && descriptor(entry->kind).key == key)
            return *entry;
    return {};
}

// Two players of four unit slots each; the catalog is sorted by name.
struct Match {
    std::unique_ptr<World> world = std::make_unique<World>();
    std::vector<Unit> units = std::vector<Unit>(1 + 2 * slots_per_player);
    std::vector<UnitDef> defs = std::vector<UnitDef>(3);
    std::vector<bool> movement = std::vector<bool>(units.size());
    int cues = 0;
    int radius_queries = 0;
    bool radius_met = true; // what the MoveUnitToRadius hook answers

    /// Builds the two players' slots, the two unit types and the sides' commanders.
    Match() {
        world->units = units.data();
        world->unit_slot_count = static_cast<uint32_t>(units.size());
        world->unit_defs = defs.data();
        world->unit_def_count = static_cast<uint32_t>(defs.size());
        std::strcpy(defs[tank].unit_name, "ARMSTUMP");
        std::strcpy(defs[gate].unit_name, "CORGATE");
        defs[tank].type_id = tank;
        defs[gate].type_id = gate;
        for (uint8_t index = 0; index < 2; ++index) {
            Player& player = world->game.players[index];
            player.index = index;
            player.info = oa_ref_from_index(index);
            world->player_info[index].side = index;
            player.first_unit = oa_unit_ref_from_slot(1 + index * slots_per_player);
            player.last_unit = oa_unit_ref_from_slot((index + 1) * slots_per_player);
        }
        std::strcpy(world->game.sides[0].commander, "ARMSTUMP");
        std::strcpy(world->game.sides[1].commander, "CORGATE");
    }

    /// Places a unit in the owner's next free slot.
    ///
    /// @param owner player index
    /// @param type unit type index
    /// @param mobile whether the unit holds a movement object
    /// @return the unit, or slot 0 when the owner's slots are full
    Unit& place(uint8_t owner, uint16_t type, bool mobile = false) {
        for (uint32_t slot = 1 + owner * slots_per_player; slot <= (owner + 1u) * slots_per_player;
             ++slot) {
            Unit& unit = units[slot];
            if (unit.type_index != 0)
                continue;
            unit = Unit{};
            unit.type_index = type;
            unit.def = oa_ref_from_index(type);
            unit.owner = oa_ref_from_index(owner);
            unit.owner_index = owner;
            unit.flags = OA_UNIT_FLAG_SELECTABLE;
            movement[slot] = mobile;
            return unit;
        }
        return units[0];
    }

    /// Returns the condition host: a counted cue, the movement table and a counted
    /// MoveUnitToRadius query that meets the condition while radius_met is set.
    ///
    /// @return the host, bound to this match
    ConditionHost host() {
        return {
            this,
            [](void* context) { ++static_cast<Match*>(context)->cues; },
            [](void* context, const Unit& unit) {
                auto& match = *static_cast<Match*>(context);
                return match.movement[static_cast<std::size_t>(&unit - match.units.data())] !=
                       false;
            },
            [](void* context, Condition& condition) {
                auto& match = *static_cast<Match*>(context);
                ++match.radius_queries;
                if (!match.radius_met)
                    return false;
                condition.satisfied = 1;
                return true;
            },
        };
    }
};

/// Tells whether a condition is met.
///
/// @param c the condition
/// @return true once satisfied
bool result(const Condition& c) {
    return c.satisfied != 0;
}

/// Checks that the player-unit walk skips empty slots and stops when told.
void scan_skips_empty_slots_and_stops() {
    Match m;
    m.place(1, tank);
    m.place(1, gate);
    m.place(1, tank);
    m.units[1 + slots_per_player + 1].type_index = 0;
    int visited = 0;
    scan_player_units(*m.world, 1, [&](const Unit&) { return ++visited < 1; });
    CHECK(visited == 1);
    visited = 0;
    scan_player_units(*m.world, 1, [&](const Unit&) { return ++visited != 0; });
    CHECK(visited == 2);
}

/// Checks that KillAllMobileUnits counts only player 1's units with a movement object.
void kill_all_mobile_units_counts_movement_objects() {
    Match m;
    auto c = registered("KillAllMobileUnits");
    Unit& structure = m.place(1, gate);
    Unit& first = m.place(1, tank, true);
    Unit& second = m.place(1, tank, true);
    kill_all_mobile_units_destroyed(c, *m.world, structure, m.host());
    CHECK(!result(c));
    kill_all_mobile_units_destroyed(c, *m.world, first, m.host());
    CHECK(!result(c) && c.units_counted == 2);
    second.type_index = 0;
    kill_all_mobile_units_destroyed(c, *m.world, first, m.host());
    CHECK(result(c) && m.cues == 1 && c.units_counted == 1);
    kill_all_mobile_units_destroyed(c, *m.world, first, m.host());
    CHECK(m.cues == 1);
    Unit& own = m.place(0, tank, true);
    auto other = registered("KillAllMobileUnits");
    kill_all_mobile_units_destroyed(other, *m.world, own, m.host());
    CHECK(!result(other));
}

/// Checks KillUnitType's signed countdown of player 1's deaths of the type.
void kill_unit_type_counts_signed() {
    Match m;
    auto c = registered("KillUnitType", "CORGATE,2");
    Unit& enemy = m.place(1, gate);
    Unit& own = m.place(0, gate);
    kill_unit_type_destroyed(c, *m.world, own, m.host());
    CHECK(c.kills_left == 2);
    kill_unit_type_destroyed(c, *m.world, enemy, m.host());
    CHECK(c.kills_left == 1 && !result(c));
    kill_unit_type_destroyed(c, *m.world, enemy, m.host());
    CHECK(c.kills_left == 0 && result(c) && m.cues == 1);
    // A count at or below zero takes no more kills.
    kill_unit_type_destroyed(c, *m.world, enemy, m.host());
    CHECK(c.kills_left == 0);
    auto negative = registered("KillUnitType", "CORGATE,-1");
    kill_unit_type_destroyed(negative, *m.world, enemy, m.host());
    CHECK(negative.kills_left == -1 && !result(negative));
}

/// Checks that KillAllOfType counts player 1's units of the type after a death.
void kill_all_of_type_counts_player_one() {
    Match m;
    auto c = registered("KillAllOfType", "corgate");
    Unit& first = m.place(1, gate);
    Unit& second = m.place(1, gate);
    m.place(0, gate);
    kill_all_of_type_destroyed(c, *m.world, first, m.host());
    CHECK(!result(c) && c.type_index == gate && c.units_counted == 2);
    first.type_index = 0;
    kill_all_of_type_destroyed(c, *m.world, second, m.host());
    CHECK(result(c) && m.cues == 1 && c.units_counted == 1);
}

/// Checks that KillAllOfType resolves nothing for a player 0 unit or another type,
/// takes no death once met and cues only while the cue has not played.
void kill_all_of_type_gates_the_death() {
    Match m;
    auto c = registered("KillAllOfType", "CORGATE");
    Unit& own = m.place(0, gate);
    Unit& stump = m.place(1, tank);
    Unit& first = m.place(1, gate);
    Unit& last = m.place(1, gate);
    kill_all_of_type_destroyed(c, *m.world, own, m.host());
    kill_all_of_type_destroyed(c, *m.world, stump, m.host());
    CHECK(!result(c) && c.type_index == 0 && c.units_counted == 0);
    kill_all_of_type_destroyed(c, *m.world, first, m.host());
    CHECK(!result(c) && c.units_counted == 2);
    first.type_index = 0;
    c.satisfied = 1;
    kill_all_of_type_destroyed(c, *m.world, last, m.host());
    CHECK(c.units_counted == 2 && m.cues == 0);
    c.satisfied = 0;
    c.celebrated = 1;
    kill_all_of_type_destroyed(c, *m.world, last, m.host());
    CHECK(result(c) && c.units_counted == 1 && m.cues == 0);
}

/// Checks that UnitTypeKilled counts every death of the type, whoever owned the unit,
/// and that its 32-bit decrement goes on past the met condition.
void unit_type_killed_counts_every_death() {
    Match m;
    auto c = registered("UnitTypeKilled", "corgate,2");
    Unit& enemy = m.place(1, gate);
    Unit& own = m.place(0, gate);
    Unit& other = m.place(1, tank);
    unit_type_killed_destroyed(c, *m.world, other);
    CHECK(c.kills_left == 2);
    unit_type_killed_destroyed(c, *m.world, own);
    CHECK(c.kills_left == 1 && !result(c));
    unit_type_killed_destroyed(c, *m.world, enemy);
    CHECK(c.kills_left == 0 && result(c) && m.cues == 0);
    unit_type_killed_destroyed(c, *m.world, enemy);
    CHECK(c.kills_left == -1 && result(c));
    auto prefix = registered("UnitTypeKilled", "CORGAT,1");
    unit_type_killed_destroyed(prefix, *m.world, enemy);
    CHECK(prefix.kills_left == 1 && !result(prefix));
    c.kills_left = std::numeric_limits<int32_t>::min();
    c.satisfied = 0;
    unit_type_killed_destroyed(c, *m.world, enemy);
    CHECK(c.kills_left == std::numeric_limits<int32_t>::max() && !result(c));
}

/// Checks that AllUnitsKilledOfType counts both players' units of the type.
void all_units_killed_of_type_counts_both_players() {
    Match m;
    auto c = registered("AllUnitsKilledOfType", "CORGATE");
    Unit& enemy = m.place(1, gate);
    Unit& own = m.place(0, gate);
    all_units_killed_of_type_destroyed(c, *m.world, enemy);
    CHECK(!result(c) && c.units_counted == 2);
    enemy.type_index = 0;
    all_units_killed_of_type_destroyed(c, *m.world, own);
    CHECK(result(c) && m.cues == 0);
}

/// Checks that BuildUnitType resolves its type and takes an unordered build as finished.
void build_unit_type_takes_unordered_as_finished() {
    Match m;
    auto c = registered("BuildUnitType", "ARMSTUMP");
    CHECK(c.type_index == 0);
    Unit& unit = m.place(0, tank);
    unit.build_remaining = 0.5F;
    m.place(1, tank);
    CHECK(!build_unit_type_met(c, *m.world, m.host()));
    CHECK(c.type_index == tank);
    unit.build_remaining = std::numeric_limits<float>::quiet_NaN();
    CHECK(build_unit_type_met(c, *m.world, m.host()) && m.cues == 1);
}

/// Checks that the commander kinds name the commander of the owner's side.
void commanders_follow_the_owner_side() {
    Match m;
    auto kill = registered("KillEnemyCommander");
    auto lost = registered("CommanderKilled");
    Unit& enemy_tank = m.place(1, tank);
    Unit& enemy_gate = m.place(1, gate);
    Unit& own_tank = m.place(0, tank);
    kill_enemy_commander_destroyed(kill, *m.world, enemy_tank, m.host());
    CHECK(!result(kill));
    kill_enemy_commander_destroyed(kill, *m.world, enemy_gate, m.host());
    CHECK(result(kill) && m.cues == 1);
    commander_killed_destroyed(lost, *m.world, enemy_gate);
    CHECK(!result(lost));
    commander_killed_destroyed(lost, *m.world, own_tank);
    CHECK(result(lost) && m.cues == 1);
}

/// Checks that CaptureUnitType takes only a unit player 1 owned.
void capture_takes_player_one() {
    Match m;
    auto c = registered("CaptureUnitType", "CORGATE");
    Unit& own = m.place(0, gate);
    Unit& enemy = m.place(1, gate);
    capture_unit_type_captured(c, *m.world, own, m.host());
    CHECK(!result(c));
    capture_unit_type_captured(c, *m.world, enemy, m.host());
    CHECK(result(c) && m.cues == 1);
}

/// Checks that UnitTypePassesX and Z read the unit's cell column and row.
void passing_lines_read_the_unit_cells() {
    Match m;
    auto c = registered("UnitTypePassesZ", "ANYTYPE,64");
    CHECK(c.line == 4);
    Unit& enemy = m.place(1, gate);
    enemy.cell_z = 4;
    Unit& own = m.place(0, gate);
    own.cell_z = 7;
    CHECK(!unit_type_passes_z_met(c, *m.world, m.host()));
    own.cell_z = 6;
    CHECK(unit_type_passes_z_met(c, *m.world, m.host()) && m.cues == 1);
    // A difference whose negation overflows reads as on the line.
    auto overflow = registered("UnitTypePassesX", "ARMSTUMP,0");
    overflow.line = std::numeric_limits<int32_t>::min();
    Unit& tank_unit = m.place(0, tank);
    tank_unit.cell_x = 0;
    CHECK(!unit_type_passes_x_unit(overflow, *m.world, tank_unit, m.host()) && result(overflow));
}

/// Checks that MoveUnitToRadius takes only player 0's units that can take orders.
void move_unit_to_radius_takes_selectable_local_units() {
    Match m;
    auto c = registered("MoveUnitToRadius", "ANYTYPE,992,656,64");
    Unit& enemy = m.place(1, tank);
    move_unit_to_radius_unit(c, *m.world, enemy, m.host());
    CHECK(!result(c));
    Unit& own = m.place(0, tank);
    own.flags = 0;
    move_unit_to_radius_unit(c, *m.world, own, m.host());
    CHECK(!result(c));
    own.flags = OA_UNIT_FLAG_SELECTABLE;
    move_unit_to_radius_unit(c, *m.world, own, m.host());
    CHECK(result(c) && m.cues == 1);
    auto named = registered("MoveUnitToRadius", "CORGATE,1,1,1");
    move_unit_to_radius_unit(named, *m.world, own, m.host());
    CHECK(!result(named));
}

/// Checks that dispatch() runs each registered kind's handler for the event and
/// skips the kinds that ignore it.
void dispatch_runs_each_kinds_handler() {
    struct Every final : DefinitionHost {
        /// Sets CommanderKilled and KillEnemyCommander.
        ///
        /// @param key key name
        /// @param fallback value for any other key
        /// @return the value
        int32_t integer(std::string_view key, int32_t fallback) override {
            return key == "CommanderKilled" || key == "KillEnemyCommander" ? 1 : fallback;
        }

        /// Names CORGATE for the counting and capture kinds.
        ///
        /// @param key key name
        /// @return the text, or nullopt for any other key
        std::optional<std::string> text(std::string_view key) override {
            if (key == "KillUnitType" || key == "UnitTypeKilled")
                return std::string("CORGATE,1");
            if (key == "CaptureUnitType")
                return std::string("CORGATE");
            return std::nullopt;
        }
    } header;

    Match m;
    Controller controller;
    register_conditions(controller, header);
    CHECK(controller.victory_count == 3 && controller.defeat_count == 2);
    Unit& enemy = m.place(1, gate);
    dispatch(controller, Event::unit_destroyed, *m.world, enemy, m.host());
    // KillEnemyCommander (CORGATE is side 1's commander), KillUnitType and
    // UnitTypeKilled take the death; CaptureUnitType and CommanderKilled do not.
    CHECK(controller.victory[0]->kind == Kind::kill_enemy_commander);
    CHECK(result(*controller.victory[0]));
    CHECK(controller.victory[1]->kind == Kind::capture_unit_type);
    CHECK(!result(*controller.victory[1]));
    CHECK(controller.victory[2]->kind == Kind::kill_unit_type && result(*controller.victory[2]));
    CHECK(controller.defeat[0]->kind == Kind::commander_killed && !result(*controller.defeat[0]));
    CHECK(controller.defeat[1]->kind == Kind::unit_type_killed && result(*controller.defeat[1]));
    CHECK(m.cues == 2);
    dispatch(controller, Event::unit_captured, *m.world, enemy, m.host());
    CHECK(result(*controller.victory[1]) && m.cues == 3);
    CHECK(controller.victory[2]->kills_left == 0 && controller.defeat[1]->kills_left == 0);
}

/// Checks that evaluate_condition() runs each kind's query from its table: the
/// stored mark, the timer against the tick, and MoveUnitToRadius through the host.
void queries_follow_the_kind() {
    Match m;
    OutcomeView view;
    const QueryContext context{*m.world, view, 60, m.host()};
    auto stored = registered("KillAllMobileUnits");
    CHECK(!evaluate_condition(stored, context));
    stored.satisfied = 1;
    CHECK(evaluate_condition(stored, context));
    auto timer = registered("DeathTimerRunsOut");
    CHECK(timer.deadline == 30);
    CHECK(evaluate_condition(timer, context));
    timer.deadline = 61;
    CHECK(!evaluate_condition(timer, context));
    auto radius = registered("MoveUnitToRadius", "ANYTYPE,1,2,3");
    CHECK(evaluate_condition(radius, context) && m.radius_queries == 1 && result(radius));
    QueryContext without_hook = context;
    without_hook.host.move_unit_to_radius_met = nullptr;
    bool threw = false;
    try {
        (void)evaluate_condition(radius, without_hook);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

// The inputs a query can read. Each run of query_with turns one of them to
// the side that meets the query reading it; none turns none.
enum class Input {
    none,
    satisfied,          // the stored mark is set
    deadline_reached,   // the tick has reached the deadline
    no_enemy_units,     // player 1's live-unit count is zero
    no_qualifying_unit, // player 0 has no unit that can take orders
    radius_hook_met,    // the host's MoveUnitToRadius hook answers met
    local_unit_built,   // player 0's ARMSTUMP is finished
    local_unit_on_x,    // player 0's unit is on the column
    local_unit_on_z,    // player 0's unit is on the row
    enemy_unit_on_x,    // player 1's unit is on the column
    enemy_unit_on_z     // player 1's unit is on the row
};
constexpr size_t input_count = 11;
static_assert(input_count == static_cast<size_t>(Input::enemy_unit_on_z) + 1);

constexpr int16_t line_cell = 10; // the column and row of the passing kinds
constexpr int16_t far_cell = 40;  // more than two cells from both
constexpr uint32_t deadline = 90; // ticks

// Each kind's query and the inputs that meet it; every other input leaves it
// unmet. Input::none pads a kind that one input alone meets.
struct KindQueryCase {
    Kind kind{};
    Query query{};
    std::array<Input, 2> met_by{};
};

constexpr std::array<KindQueryCase, kind_count> kind_queries{{
    {Kind::kill_enemy_commander, Query::stored_result, {Input::satisfied, Input::none}},
    {Kind::destroy_all_units, Query::destroy_all_units, {Input::no_enemy_units, Input::none}},
    {Kind::kill_all_mobile_units, Query::stored_result, {Input::satisfied, Input::none}},
    {Kind::build_unit_type, Query::build_unit_type, {Input::satisfied, Input::local_unit_built}},
    {Kind::capture_unit_type, Query::stored_result, {Input::satisfied, Input::none}},
    {Kind::kill_all_of_type, Query::stored_result, {Input::satisfied, Input::none}},
    {Kind::kill_unit_type, Query::stored_result, {Input::satisfied, Input::none}},
    {Kind::move_unit_to_radius, Query::move_unit_to_radius, {Input::radius_hook_met, Input::none}},
    {Kind::unit_type_passes_x,
     Query::unit_type_passes_x,
     {Input::satisfied, Input::local_unit_on_x}},
    {Kind::unit_type_passes_z,
     Query::unit_type_passes_z,
     {Input::satisfied, Input::local_unit_on_z}},
    {Kind::victory_timer, Query::timer, {Input::deadline_reached, Input::none}},
    {Kind::commander_killed, Query::stored_result, {Input::satisfied, Input::none}},
    {Kind::all_units_killed, Query::all_units_killed, {Input::no_qualifying_unit, Input::none}},
    {Kind::all_units_killed_of_type, Query::stored_result, {Input::satisfied, Input::none}},
    {Kind::unit_type_killed, Query::stored_result, {Input::satisfied, Input::none}},
    {Kind::death_timer, Query::timer, {Input::deadline_reached, Input::none}},
    {Kind::any_unit_passes_x, Query::any_unit_passes_x, {Input::satisfied, Input::enemy_unit_on_x}},
    {Kind::any_unit_passes_z, Query::any_unit_passes_z, {Input::satisfied, Input::enemy_unit_on_z}},
}};

/// Runs a kind's query through evaluate_condition with one input on the side that meets it.
///
/// Player 0 and player 1 each hold an ARMSTUMP far from the line, player 0's
/// unfinished; player 1 has a live unit, player 0 a unit that can take orders,
/// and the tick is one short of the deadline. The condition names ARMSTUMP
/// whatever its kind, so a kind run through another kind's query meets an
/// input that its own query ignores.
///
/// @param kind condition kind
/// @param input the input turned
/// @return what the query answers
bool query_with(Kind kind, Input input) {
    Match m;
    m.radius_met = input == Input::radius_hook_met;
    Unit& own = m.place(0, tank);
    own.build_remaining = input == Input::local_unit_built ? 0.0F : 0.5F;
    own.cell_x = input == Input::local_unit_on_x ? line_cell : far_cell;
    own.cell_z = input == Input::local_unit_on_z ? line_cell : far_cell;
    Unit& enemy = m.place(1, tank);
    enemy.cell_x = input == Input::enemy_unit_on_x ? line_cell : far_cell;
    enemy.cell_z = input == Input::enemy_unit_on_z ? line_cell : far_cell;
    UnitStatus qualifying;
    qualifying.type_index = tank;
    qualifying.flags = static_cast<uint8_t>(OA_UNIT_FLAG_SELECTABLE);
    OutcomeView view;
    view.live_units[1] = input == Input::no_enemy_units ? 0 : 1;
    if (input != Input::no_qualifying_unit)
        view.player_zero_units = std::span(&qualifying, 1);
    Condition c;
    c.kind = kind;
    c.satisfied = input == Input::satisfied ? 1 : 0;
    std::strcpy(c.type_name, "ARMSTUMP");
    c.line = line_cell;
    c.deadline = deadline;
    const uint32_t tick = input == Input::deadline_reached ? deadline : deadline - 1;
    const QueryContext context{*m.world, view, tick, m.host()};
    return evaluate_condition(c, context);
}

/// Checks that every kind runs its own query: query_of names it, and only the inputs
/// that query reads meet the condition.
void every_kind_runs_its_own_query() {
    for (const KindQueryCase& expected : kind_queries) {
        const auto kind = static_cast<unsigned>(expected.kind);
        if (query_of(expected.kind) != expected.query) {
            std::fprintf(
                stderr,
                "kind %u runs query %u\n",
                kind,
                static_cast<unsigned>(query_of(expected.kind))
            );
            ++failures;
        }
        for (size_t i = 0; i < input_count; ++i) {
            const auto input = static_cast<Input>(i);
            const bool met = input != Input::none &&
                             (input == expected.met_by[0] || input == expected.met_by[1]);
            if (query_with(expected.kind, input) != met) {
                std::fprintf(
                    stderr, "kind %u with input %zu: expected %s\n", kind, i, met ? "met" : "unmet"
                );
                ++failures;
            }
        }
    }
    bool threw = false;
    try {
        (void)query_of(static_cast<Kind>(kind_count));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

struct Random : DiagnosticRandom {
    /// Draws zero.
    ///
    /// @return 0
    uint16_t rand15() override { return 0; }
};

/// Checks that a disabled controller runs no query: the AllUnitsKilled query,
/// which marks its condition met before walking, leaves it alone.
void the_disabled_controller_tests_nothing() {
    Match m;
    Header header;
    header.key = "KillAllMobileUnits";
    Controller controller;
    register_conditions(controller, header);
    controller.victory[0]->satisfied = 1;
    OutcomeView view;
    const QueryContext context{*m.world, view, 0, m.host()};
    Random random;
    DiagnosticLoss loss;
    CHECK(victory_check(controller, GameKind::campaign, context));
    CHECK(defeat_check(controller, GameKind::campaign, context, loss, random));
    CHECK(controller.defeat[0]->kind == Kind::all_units_killed && result(*controller.defeat[0]));
    disable(controller);
    controller.defeat[0]->satisfied = 7;
    CHECK(!victory_check(controller, GameKind::campaign, context));
    CHECK(!defeat_check(controller, GameKind::campaign, context, loss, random));
    CHECK(!defeat_check(controller, GameKind::skirmish, context, loss, random));
    CHECK(controller.defeat[0]->satisfied == 7);
}

} // namespace

int main() {
    scan_skips_empty_slots_and_stops();
    kill_all_mobile_units_counts_movement_objects();
    kill_unit_type_counts_signed();
    kill_all_of_type_counts_player_one();
    kill_all_of_type_gates_the_death();
    unit_type_killed_counts_every_death();
    all_units_killed_of_type_counts_both_players();
    build_unit_type_takes_unordered_as_finished();
    commanders_follow_the_owner_side();
    capture_takes_player_one();
    passing_lines_read_the_unit_cells();
    move_unit_to_radius_takes_selectable_local_units();
    dispatch_runs_each_kinds_handler();
    queries_follow_the_kind();
    every_kind_runs_its_own_query();
    the_disabled_controller_tests_nothing();
    if (failures != 0)
        return 1;
    std::puts("scenario conditions passed");
    return 0;
}
