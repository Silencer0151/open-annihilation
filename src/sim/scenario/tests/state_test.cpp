// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/scenario/conditions.hpp"
#include "oa/sim/scenario/state.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>
using namespace oa::sim::scenario;

namespace {
/// Throws when a check fails.
///
/// @param value whether the check held
/// @param message what failed
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

struct Definition final : DefinitionHost {
    std::map<std::string, int32_t> integers;
    std::map<std::string, std::string> strings;
    std::vector<std::string> reads;

    /// Records the key and returns its integer.
    ///
    /// @param key key name
    /// @param fallback value for a missing key
    /// @return the value
    int32_t integer(std::string_view key, int32_t fallback) override {
        reads.emplace_back(key);
        auto found = integers.find(std::string(key));
        return found == integers.end() ? fallback : found->second;
    }

    /// Records the key and returns its text.
    ///
    /// @param key key name
    /// @return the text, or nullopt when the key is missing
    std::optional<std::string> text(std::string_view key) override {
        reads.emplace_back(key);
        auto found = strings.find(std::string(key));
        if (found == strings.end())
            return {};
        return found->second;
    }
};

/// Returns a GlobalHeader that names every condition kind once.
///
/// @return the definition
Definition all() {
    Definition d;
    for (auto key :
         {"KillEnemyCommander",
          "DestroyAllUnits",
          "KillAllMobileUnits",
          "CommanderKilled",
          "AllUnitsKilled"})
        d.integers[key] = 1;
    d.integers["VictoryTimerRunsOut"] = 4;
    d.integers["DeathTimerRunsOut"] = 5;
    d.integers["AnyUnitPassesX"] = 48;
    d.integers["AnyUnitPassesZ"] = 64;
    for (auto key : {"BuildUnitType", "CaptureUnitType", "KillAllOfType", "AllUnitsKilledOfType"})
        d.strings[key] = "armcom";
    for (auto key : {"KillUnitType", "UnitTypeKilled", "UnitTypePassesX", "UnitTypePassesZ"})
        d.strings[key] = "armcom,48";
    d.strings["MoveUnitToRadius"] = "ANYTYPE,16,32,64";
    return d;
}

// A visitor that appends a copy of the first victory condition once, while the walk runs.
struct Appending {
    Controller* controller{};
    bool append{};
    int visited{};
};

/// Visits a condition, appending once when asked.
///
/// @param[in,out] state the controller, whether to append, and the visits counted
void visit_and_append(Appending& state) {
    ++state.visited;
    if (state.append) {
        state.append = false;
        auto& c = *state.controller;
        c.victory[static_cast<std::size_t>(c.victory_count)] = c.victory[0];
        ++c.victory_count;
    }
}

/// Checks which unit events each kind reacts to.
void unit_event_table() {
    constexpr std::array<Kind, 7> destroyed{
        Kind::kill_enemy_commander,
        Kind::kill_all_mobile_units,
        Kind::kill_all_of_type,
        Kind::kill_unit_type,
        Kind::commander_killed,
        Kind::all_units_killed_of_type,
        Kind::unit_type_killed,
    };
    for (std::size_t i = 0; i < kind_count; ++i) {
        const auto kind = static_cast<Kind>(i);
        bool listed = false;
        for (const auto k : destroyed)
            listed = listed || k == kind;
        check(reacts_to(kind, Event::unit_destroyed) == listed, "destroyed handlers");
        check(
            reacts_to(kind, Event::unit_captured) == (kind == Kind::capture_unit_type),
            "captured handler"
        );
        check(!reacts_to(kind, Event::unit_created), "no created handler");
    }
    check(
        !reacts_to(static_cast<Kind>(kind_count), Event::unit_destroyed),
        "kind outside the table reacts to nothing"
    );
}

/// Checks registration, the constructors' fields, the condition walk and the dispatch table.
void tests() {
    Controller c;
    Definition defaults;
    check(register_conditions(c, defaults) == DefinitionError::none, "defaults register");
    check(
        c.victory_count == 1 && c.defeat_count == 1 &&
            c.victory[0]->kind == Kind::destroy_all_units &&
            c.defeat[0]->kind == Kind::all_units_killed,
        "actual default registrations"
    );
    check(
        defaults.reads.size() == 18 && defaults.reads.front() == "KillEnemyCommander" &&
            defaults.reads.back() == "AnyUnitPassesZ",
        "definition query order"
    );
    const auto before = *c.victory[0];
    notify_unit_created(c, {17});
    check(*c.victory[0] == before, "unit creation changes no condition");
    disable(c);
    notify_unit_created(c, {17});
    check(c.enabled == 0, "notification does not gate enabled");
    destroy(c);
    check(!c.registration_complete && c.victory_count == 0, "destroy ownership");
    notify_unit_created(c, {17});
    check(!visit_conditions(c, [](Condition&) {}), "unregistered conditions visit nothing");
    construct(c);
    auto d = all();
    check(register_conditions(c, d) == DefinitionError::none, "every condition registers");
    check(
        c.victory_count == 11 && c.defeat_count == 7 && c.enabled == 1,
        "all condition registrations"
    );
    const Condition& radius = *c.victory[7];
    check(
        radius.kind == Kind::move_unit_to_radius &&
            radius.point == ConditionPoint{16, unplaced_point_height, 32} &&
            radius.radius == 0x400000 && radius.type_name[0] == '\0' && radius.satisfied == 0 &&
            radius.celebrated == 0,
        "radius constructor ANYTYPE"
    );
    check(
        c.victory[8]->line == 3 && c.victory[10]->deadline == 120 && c.defeat[5]->line == 3,
        "threshold/timer conversions"
    );
    check(
        c.victory[8]->kind == Kind::unit_type_passes_x &&
            std::string_view(c.victory[8]->type_name) == "armcom" &&
            c.victory[9]->kind == Kind::unit_type_passes_z && c.victory[9]->line == 3,
        "line constructors"
    );
    const auto any_line = *unit_type_passes_condition(Kind::unit_type_passes_z, "AnyType", -40);
    check(
        any_line.line == -3 && any_line.type_name[0] == '\0',
        "line in cells shifts arithmetically; ANYTYPE matches without case"
    );
    const auto named_radius = *move_unit_to_radius_condition("corgate", 992, 656, -2);
    check(
        named_radius.point.x == 992 && named_radius.point.z == 656 &&
            named_radius.radius == -0x20000 &&
            std::string_view(named_radius.type_name) == "corgate",
        "radius constructor fields"
    );
    check(
        c.victory[5]->type_index == 0 && c.victory[5]->units_counted == 0 &&
            c.defeat[2]->type_index == 0 && c.defeat[2]->units_counted == 0,
        "a type left unresolved at registration keeps its initial values"
    );

    // Created and unhandled events change nothing.
    const auto world = std::make_unique<oa::World>();
    const oa::Unit unit{};
    std::vector<Condition> registered;
    for (int32_t i = 0; i < c.victory_count; ++i)
        registered.push_back(*c.victory[static_cast<std::size_t>(i)]);
    dispatch(c, Event::unit_created, *world, unit, ConditionHost{});
    for (int32_t i = 0; i < c.victory_count; ++i)
        check(
            *c.victory[static_cast<std::size_t>(i)] == registered[static_cast<std::size_t>(i)],
            "all known created handlers inert"
        );
    for (auto value : {-1, 3})
        check(
            !dispatch(c, static_cast<Event>(value), *world, unit, ConditionHost{}),
            "event outside the handler table rejected"
        );

    Appending walk{&c, true};
    check(
        visit_conditions(c, [&walk](Condition&) { visit_and_append(walk); }),
        "the walk visits every condition"
    );
    check(
        walk.visited == 19 && c.victory_count == 12, "handler count reread after callback append"
    );
    unit_event_table();

    Definition unusual;
    unusual.integers["KillEnemyCommander"] = -1;
    unusual.integers["VictoryTimerRunsOut"] = -1;
    unusual.integers["DeathTimerRunsOut"] = 0;
    unusual.strings["BuildUnitType"] = "";
    unusual.strings["UnitTypePassesX"] = "ANYTYPE,-17";
    unusual.strings["KillUnitType"] = "armcom,0x10 trailing";
    construct(c);
    check(register_conditions(c, unusual) == DefinitionError::none, "unusual text registers");
    check(
        c.victory_count == 4 && c.victory[2]->kills_left == 16 && c.victory[3]->line == -2,
        "presence, signed shifts, scanf prefix"
    );
    Definition malformed;
    malformed.strings["KillUnitType"] = "ARM_COM,4";
    construct(c);
    check(
        register_conditions(c, malformed) == DefinitionError::missing_comma &&
            !c.registration_complete,
        "malformed parser rejected"
    );
}
} // namespace

int main() {
    try {
        tests();
        std::cout << "scenario state passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
