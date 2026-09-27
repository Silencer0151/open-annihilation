// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/scenario/outcome.hpp"
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
using namespace oa::sim::scenario;

namespace {
/// Throws when a check fails.
///
/// @param v whether the check held
/// @param message what failed
void check(bool v, const char* message) {
    if (!v)
        throw std::runtime_error(message);
}

struct Definitions : DefinitionHost {
    /// Sets AnyUnitPassesZ to 80 world units.
    ///
    /// @param key key name
    /// @param fallback value for any other key
    /// @return the value
    int32_t integer(std::string_view key, int32_t fallback) override {
        return key == "AnyUnitPassesZ" ? 80 : fallback;
    }

    /// Names two ARMCOM kills for UnitTypeKilled.
    ///
    /// @param key key name
    /// @return the text, or nullopt for any other key
    std::optional<std::string> text(std::string_view key) override {
        if (key == "UnitTypeKilled")
            return "ARMCOM,2";
        return {};
    }
};

/// Checks the type-name constructors: the name stored, the type unresolved, the flags clear.
void constructors() {
    const auto build = build_unit_type_condition("ARMSOLAR");
    check(build.kind == Kind::build_unit_type, "build kind");
    check(std::string_view(build.type_name) == "ARMSOLAR", "BuildUnitType name");
    check(build.type_index == 0, "BuildUnitType starts unresolved");
    check(build.satisfied == 0 && build.celebrated == 0, "flags cleared");

    const auto kill = kill_all_of_type_condition("CORAK");
    check(kill.kind == Kind::kill_all_of_type, "KillAllOfType kind");
    check(std::string_view(kill.type_name) == "CORAK", "KillAllOfType name");
    check(kill.type_index == 0 && kill.units_counted == 0, "KillAllOfType starts unresolved");

    const auto lost = all_units_killed_of_type_condition("ARMGATE");
    check(lost.kind == Kind::all_units_killed_of_type, "AllUnitsKilledOfType kind");
    check(std::string_view(lost.type_name) == "ARMGATE", "AllUnitsKilledOfType name");

    bool threw = false;
    try {
        (void)build_unit_type_condition(std::string(type_name_capacity, 'X'));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "names longer than the 32-byte field are rejected");
}

/// Checks the registered UnitTypeKilled count and the AnyUnitPassesZ line and walk.
void registered_defeat_conditions() {
    Definitions definitions;
    Controller controller;
    register_conditions(controller, definitions);
    check(controller.defeat_count == 2, "two defeat conditions");
    auto& killed = *controller.defeat[0];
    check(killed.kind == Kind::unit_type_killed, "UnitTypeKilled registered first");
    check(killed.kills_left == 2, "two kills left");

    auto& line = *controller.defeat[1];
    check(
        line.kind == Kind::any_unit_passes_z && line.line == 5,
        "AnyUnitPassesZ stores the tile line"
    );
    const auto at_z = [](int16_t cell_z) {
        oa::Unit unit{};
        unit.cell_z = cell_z;
        return unit;
    };
    check(any_unit_passes_z_unit(line, at_z(8)), "three cells away continues the walk");
    check(line.satisfied == 0, "not met");
    check(!any_unit_passes_z_unit(line, at_z(7)), "two cells away stops the walk");
    check(line.satisfied == 1, "met");
    line.satisfied = 0;
    check(!any_unit_passes_z_unit(line, at_z(3)), "two cells below");
    line.satisfied = 0;
    line.line = 0x7fff8000;
    check(
        !any_unit_passes_z_unit(line, at_z(std::numeric_limits<int16_t>::min())),
        "the 0x80000000 difference stays negative through the absolute value"
    );

    line.satisfied = 0;
    line.line = 5;
    // Player 1 owns slots 1..3; player 0's slot 4 sits on the line.
    std::unique_ptr<oa::World, void (*)(oa::World*)> world(oa::world_create(), oa::world_destroy);
    const oa::WorldCapacity capacity{8, 1, 0};
    check(world && oa::world_alloc_tables(world.get(), &capacity) != 0, "world tables");
    world->game.players[1].first_unit = oa::oa_ref_from_index(1);
    world->game.players[1].last_unit = oa::oa_ref_from_index(3);
    world->game.players[0].first_unit = oa::oa_ref_from_index(4);
    world->game.players[0].last_unit = oa::oa_ref_from_index(4);
    const auto place = [&](uint32_t slot, uint16_t type, int16_t cell_z) {
        world->units[slot].type_index = type;
        world->units[slot].cell_z = cell_z;
    };
    place(1, 3, 9);
    place(2, 0, 5);
    place(3, 2, 1);
    place(4, 7, 5);
    check(!any_unit_passes_z(line, *world), "zero-type units are skipped, others too far");
    check(!condition_result(line), "result query reads the stored mark");
    place(3, 5, 6);
    check(any_unit_passes_z(line, *world) && condition_result(line), "a near unit meets it");
    const Condition frozen = line;
    world->game.players[1].first_unit = 0;
    world->game.players[1].last_unit = 0;
    check(any_unit_passes_z(line, *world), "a met condition skips the walk");
    check(line == frozen, "nothing written once met");
}

/// Checks the AllUnitsKilled walk: met until a unit that can take orders is found.
void all_units_killed_walk() {
    Condition c;
    c.kind = Kind::all_units_killed;
    // Type index, then the low byte of Unit.flags: 0x20 is OA_UNIT_FLAG_SELECTABLE.
    std::vector<UnitStatus> units{UnitStatus{0, 0x20}, UnitStatus{5, 0x00}};
    OutcomeView view;
    view.player_zero_units = units;
    check(all_units_killed(c, view), "no qualifying unit meets the condition");
    units[1] = UnitStatus{5, 0x20};
    check(!all_units_killed(c, view), "a live finished unit clears the result");
    check(c.satisfied == 0, "result cleared");
}
} // namespace

int main() {
    try {
        constructors();
        registered_defeat_conditions();
        all_units_killed_walk();
        std::cout << "campaign conditions passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
