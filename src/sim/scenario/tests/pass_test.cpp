// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/scenario/outcome.hpp"
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
using namespace oa::sim::scenario;

namespace {
constexpr uint32_t unit_slots = 8; // slot 0 is reserved

struct Definitions : DefinitionHost {
    int32_t line = -1;

    /// Returns the AnyUnitPassesX line.
    ///
    /// @param key key name
    /// @param fallback value for any other key
    /// @return the value
    int32_t integer(std::string_view key, int32_t fallback) override {
        return key == "AnyUnitPassesX" ? line : fallback;
    }

    /// Names no text key.
    ///
    /// @param key key name
    /// @return nullopt
    std::optional<std::string> text(std::string_view key) override {
        (void)key;
        return {};
    }
};

// A unit of player 1 in the walk: its type index (0 for an empty slot) and cell column.
struct PlayerOneUnit {
    uint16_t type_index{};
    int16_t cell_x{};
};

/// Throws when a check fails.
///
/// @param v whether the check held
void check(bool v) {
    if (!v)
        throw std::runtime_error("pass assertion failed");
}

/// Registers an AnyUnitPassesX condition.
///
/// @param line the line, world units
/// @return the condition
Condition registered(int32_t line) {
    Definitions definitions;
    definitions.line = line;
    Controller controller;
    register_conditions(controller, definitions);
    check(
        controller.defeat_count == 1 && controller.defeat[0] &&
        controller.defeat[0]->kind == Kind::any_unit_passes_x
    );
    return *controller.defeat[0];
}

// A world whose player 1 owns the first slots, in order.
struct Walk {
    std::unique_ptr<oa::World, void (*)(oa::World*)> world{oa::world_create(), oa::world_destroy};

    /// Allocates the world's unit table.
    Walk() {
        const oa::WorldCapacity capacity{unit_slots, 1, 0};
        check(world && oa::world_alloc_tables(world.get(), &capacity) != 0);
    }

    /// Gives player 1 exactly these units, from slot 1 on.
    ///
    /// @param units the units, in slot order
    /// @return the world
    oa::World& with(std::initializer_list<PlayerOneUnit> units) {
        check(units.size() < unit_slots);
        uint32_t slot = 1;
        for (const auto& unit : units) {
            world->units[slot].type_index = unit.type_index;
            world->units[slot].cell_x = unit.cell_x;
            ++slot;
        }
        auto& player = world->game.players[1];
        player.first_unit = units.size() == 0 ? 0 : oa::oa_ref_from_index(1);
        player.last_unit = units.size() == 0 ? 0 : oa::oa_ref_from_index(slot - 1);
        return *world;
    }
};
} // namespace

int main() {
    try {
        Walk walk;
        auto condition = registered(48);
        check(condition.line == 3);
        check(condition.satisfied == 0);
        const auto celebrated = condition.celebrated;
        check(any_unit_passes_x(condition, walk.with({{0, 3}, {1, 0}, {1, 1}, {4, 100}})));
        check(condition.satisfied == 1);
        check(condition.celebrated == celebrated);
        check(condition.line == 3);

        const Condition frozen = condition;
        check(any_unit_passes_x(condition, walk.with({{0, 3}, {1, 0}, {1, 100}, {4, 100}})));
        check(condition == frozen);

        condition = registered(64);
        check(condition.line == 4);
        check(!any_unit_passes_x(condition, walk.with({{1, 1}, {2, 7}, {0, 4}})));
        check(condition.satisfied == 0);
        check(any_unit_passes_x(condition, walk.with({{1, 6}})));
        check(condition.satisfied == 1);
        check(any_unit_passes_x(condition, walk.with({{1, 0}})));
        condition.satisfied = 0;
        check(any_unit_passes_x(condition, walk.with({{1, 2}})));
        condition.satisfied = 0;
        check(!any_unit_passes_x(condition, walk.with({{1, 7}})));
        check(!any_unit_passes_x(condition, walk.with({})));
        check(!any_unit_passes_x(condition, walk.with({{0, 4}})));

        condition.line = -5;
        check(any_unit_passes_x(condition, walk.with({{1, -7}})));
        condition.satisfied = 0;
        check(!any_unit_passes_x(condition, walk.with({{1, -8}})));
        condition.satisfied = 0;
        check(any_unit_passes_x(condition, walk.with({{1, -3}})));

        // Cell -32768 minus line 0x7FFF8000 wraps to 0x80000000, whose negation stays
        // negative, so the unit counts as on the line.
        condition.satisfied = 0;
        condition.line = 0x7fff8000;
        check(any_unit_passes_x(condition, walk.with({{1, std::numeric_limits<int16_t>::min()}})));

        // A condition never registered reads its initial values: line 0, unmet.
        Condition blank;
        blank.kind = Kind::any_unit_passes_x;
        check(!any_unit_passes_x(blank, walk.with({})));
        check(!any_unit_passes_x(blank, walk.with({{0, 1}})));
        check(any_unit_passes_x(blank, walk.with({{1, 0}})));
        std::cout << "scenario pass passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
