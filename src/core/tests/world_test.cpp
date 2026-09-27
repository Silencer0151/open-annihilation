// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Compiled as C++20: World helpers in namespace oa, including const overloads.
#include "oa/core/world.h"

#include <cstdio>
#include <memory>
#include <type_traits>

static_assert(std::is_standard_layout_v<oa::World>);
static_assert(
    std::is_same_v<decltype(oa::world_unit(std::declval<const oa::World*>(), 1u)), const oa::Unit*>
);
static_assert(std::is_same_v<decltype(oa::world_unit(std::declval<oa::World*>(), 1u)), oa::Unit*>);

int main() {
    int failures = 0;
    const auto check = [&](bool condition, const char* what) {
        if (!condition) {
            std::fprintf(stderr, "world: %s\n", what);
            ++failures;
        }
    };
    auto world = std::make_unique<oa::World>();
    oa::Unit units[11]{};
    oa::UnitDef defs[3]{};
    world->units = units;
    world->unit_slot_count = 11;
    world->unit_defs = defs;
    world->unit_def_count = 3;

    units[10].def = oa::oa_ref_from_index(2);
    units[10].owner = oa::world_player_ref_of(world.get(), &world->game.players[0]);
    const oa::World* view = world.get();
    check(oa::world_unit_def_of(view, &units[10]) == &defs[2], "const unit def");
    check(oa::world_unit_owner(view, &units[10]) == &world->game.players[0], "const owner");
    check(oa::world_unit(view, oa::world_unit_ref(view, &units[10])) == &units[10], "round trip");
    check(oa::world_unit_at(view, 11) == nullptr, "const slot bound");

    world->game.weapon_defs[4].flags = OA_WEAPON_FLAG_STOCKPILE;
    check(oa::world_weapon_def(view, 5)->flags == OA_WEAPON_FLAG_STOCKPILE, "weapon def index");
    return failures == 0 ? 0 : 1;
}
