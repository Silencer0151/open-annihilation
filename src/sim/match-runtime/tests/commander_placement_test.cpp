// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// setup.commander-warp in the running match: the local player's placing of
// its commander opens on a live first unit, moves the whole part of its x
// and z to a map point while the fractions, height and layer stay, ends on
// done with a report to the multiplayer hooks, and closes as the game runs.
#include "combat_fixture.hpp"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

using namespace combat_fixture;
using sim::match_runtime::CommanderPlacement;

void commander_moves_while_placing() {
    Fixture f;
    auto& world = f.match->state();
    world.game.local_player_index = 0;
    CHECK(f.match->commander_placement() == CommanderPlacement::none);
    // No unit yet: nothing to place.
    f.match->begin_commander_placement();
    CHECK(f.match->commander_placement() == CommanderPlacement::none);
    CHECK(!f.match->place_commander(100, 100));

    auto& commander = f.spawn(0, 40, 60);
    uint32_t count = 0;
    CHECK(
        world_player_units(&world, &world.game.players[0], &count) ==
        &world.units[commander.unit_index]
    );
    commander.unit->position = {(40u << 16) | 0x8000u, 32u << 16, (60u << 16) | 0x4000u};
    const uint32_t layer = commander.unit->flags & OA_UNIT_FLAG_OCCUPANCY_MASK;
    f.match->begin_commander_placement();
    CHECK(f.match->commander_placement() == CommanderPlacement::placing);
    const auto start_x = commander.record.cell_x;
    const auto start_z = commander.record.cell_z;

    CHECK(f.match->place_commander(200, 100));
    const std::array<uint32_t, 3> moved = commander.unit->position;
    CHECK(moved[0] == ((200u << 16) | 0x8000u) && moved[2] == ((100u << 16) | 0x4000u));
    CHECK(moved[1] == (32u << 16));
    CHECK((commander.unit->flags & OA_UNIT_FLAG_OCCUPANCY_MASK) == layer);
    // Its footprint moved with it.
    CHECK(commander.record.cell_x > start_x && commander.record.cell_z > start_z);
    // A point off the map moves nothing.
    CHECK(!f.match->place_commander(world.game.map_pixel_width, 10));
    CHECK(!f.match->place_commander(10, world.game.map_pixel_height));
    CHECK(!f.match->place_commander(-1, 10));
    // As often as the player likes.
    CHECK(f.match->place_commander(40, 60));
    const std::array<uint32_t, 3> again = commander.unit->position;
    CHECK(again[0] == ((40u << 16) | 0x8000u) && again[2] == ((60u << 16) | 0x4000u));
    CHECK(commander.record.cell_x == start_x && commander.record.cell_z == start_z);

    int placed = 0;
    f.match->multiplayer.context = &placed;
    f.match->multiplayer.commander_placed = [](void* context) { ++*static_cast<int*>(context); };
    f.match->finish_commander_placement();
    CHECK(f.match->commander_placement() == CommanderPlacement::waiting && placed == 1);
    f.match->finish_commander_placement();
    CHECK(placed == 1);
    // Once done the commander stays where it is.
    CHECK(!f.match->place_commander(100, 100));
    // The game running closes the placing.
    f.run(1);
    CHECK(f.match->commander_placement() == CommanderPlacement::none);
    std::cout << "commander moves while placing passed\n";
}

} // namespace

int main() {
    try {
        commander_moves_while_placing();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
