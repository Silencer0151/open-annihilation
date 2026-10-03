// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Map-placed units: owner numbers, the start picks per player, commander
// slots, creation on the cell height, timed entries and the computer
// player moved last.
#include "oa/sim/mission_units/map_units.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace oa;
using namespace oa::sim::mission_units;
namespace campaign = oa::data::campaign;

namespace {

int failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

/// A schema entry.
campaign::MissionUnit
entry(const char* name, uint8_t player, int32_t countdown = 0, uint8_t flags = 0) {
    campaign::MissionUnit unit{};
    unit.unit_name = name;
    unit.player = player;
    unit.creation_countdown = countdown;
    unit.flags = flags;
    unit.x = static_cast<int32_t>(40u << 16);
    unit.z = static_cast<int32_t>(24u << 16);
    unit.y = static_cast<int32_t>(7u << 16);
    return unit;
}

void test_owner_numbers() {
    CHECK(map_unit_owner(entry("A", 3)) == 3);
    CHECK(map_unit_owner(entry("A", 11)) == neutral_map_owner);
    // Any flag makes the entry nobody's.
    CHECK(map_unit_owner(entry("A", 11, 0, campaign::mission_unit_flag::immunity)) != 11);
    const campaign::MissionUnit units[] = {entry("A", 1), entry("B", 11)};
    CHECK(has_neutral_map_units(units, 2));
    CHECK(!has_neutral_map_units(units, 1));
    const campaign::MissionUnit flagged[] = {
        entry("B", 11, 0, campaign::mission_unit_flag::mission_critical)
    };
    CHECK(!has_neutral_map_units(flagged, 1));
}

void test_start_picks() {
    std::array<Side, 5> sides{};
    std::snprintf(sides[0].commander, sizeof sides[0].commander, "%s", "ARMCOM");
    std::snprintf(sides[1].commander, sizeof sides[1].commander, "%s", "CORCOM");
    const campaign::MissionUnit units[] = {
        entry("ARMSOLAR", 1),    // 0: start position 0
        entry("ARMCOM", 1),      // 1: a commander type
        entry("CORLLT", 2),      // 2: start position 1
        entry("ARMFLASH", 1, 5), // 3: timed
        entry("CORGATOR", 11),   // 4: neutral
        entry("armcom", 1),      // 5: names no commander exactly
    };
    std::array<MapUnitPick, 8> picks{};
    int32_t count = pick_start_map_units(units, 6, 0, false, sides, picks);
    CHECK(count == 3);
    CHECK(picks[0].entry == 0 && picks[0].slot_offset == 0);
    CHECK(picks[1].entry == 1 && picks[1].slot_offset == commander_slot_offset + 1);
    CHECK(picks[2].entry == 5 && picks[2].slot_offset == 0);
    count = pick_start_map_units(units, 6, 1, false, sides, picks);
    CHECK(count == 1 && picks[0].entry == 2);
    count = pick_start_map_units(units, 6, 0, true, sides, picks);
    CHECK(count == 1 && picks[0].entry == 4);
    // A short output keeps the first picks.
    std::array<MapUnitPick, 1> one{};
    CHECK(pick_start_map_units(units, 6, 0, false, sides, one) == 1 && one[0].entry == 0);
}

struct Created {
    uint8_t player{};
    uint16_t type{};
    FixedVec3 position{};
    uint16_t slot{};
};

void test_create() {
    World world{};
    std::vector<UnitDef> defs(3);
    std::snprintf(defs[1].unit_name, sizeof defs[1].unit_name, "%s", "ARMSOLAR");
    std::snprintf(defs[2].unit_name, sizeof defs[2].unit_name, "%s", "armsolar");
    world.unit_defs = defs.data();
    world.unit_def_count = 3;
    world.game.map_width = 4;
    world.game.map_height = 3;
    std::vector<MapPlot> plots(12);
    for (std::size_t i = 0; i < plots.size(); ++i)
        plots[i].height = static_cast<uint8_t>(10 + i);
    world.plots = plots.data();
    Created made{};
    Unit unit{};

    struct Context {
        Created* made;
        Unit* unit;
    } context{&made, &unit};

    Hooks hooks{};
    hooks.context = &context;
    hooks.create_unit = [](void* c,
                           uint8_t player,
                           uint16_t type,
                           const FixedVec3& position,
                           bool finished,
                           uint32_t,
                           uint16_t slot) -> Unit* {
        auto& ctx = *static_cast<Context*>(c);
        CHECK(finished);
        *ctx.made = {player, type, position, slot};
        return ctx.unit;
    };
    // Cell (x 40>>4 = 2, z 24>>4 = 1): index 1 * 4 + 2 = 6.
    auto solar = entry("ARMSOLAR", 1);
    CHECK(create_map_unit(world, solar, 3, 95, hooks) == &unit);
    CHECK(made.player == 3 && made.type == 1 && made.slot == 95);
    CHECK(made.position.y == static_cast<int32_t>(16u << 16));
    CHECK(made.position.x == solar.x && made.position.z == solar.z);
    // A column past the width reads the next row's cell.
    solar.x = static_cast<int32_t>(80u << 16); // column 5 of a 4-wide map: row 1 + 1, column 1
    (void)create_map_unit(world, solar, 3, 0, hooks);
    CHECK(made.position.y == static_cast<int32_t>((10u + 9u) << 16));
    // Past the last cell the entry's own height stands.
    solar.z = static_cast<int32_t>(200u << 16);
    (void)create_map_unit(world, solar, 3, 0, hooks);
    CHECK(made.position.y == solar.y);
    // The name matches exactly.
    made = {};
    CHECK(create_map_unit(world, entry("ARMSOLAR ", 1), 3, 0, hooks) == nullptr);
    CHECK(create_map_unit(world, entry("Armsolar", 1), 3, 0, hooks) == nullptr);
}

void test_timed() {
    const campaign::MissionUnit units[] = {
        entry("A", 1, 30),
        entry("B", 2, 0),
        entry("C", 1, 10),
        entry("", 1, 5),
        entry("D", 11, 10),
        entry("E", 1, -4),
    };
    std::array<int32_t, 8> order{};
    const int32_t count = order_timed_map_units(units, 6, order);
    CHECK(count == 3);
    CHECK(order[0] == 2 && order[1] == 4 && order[2] == 0);
    CHECK(!timed_map_unit_due(units[2], 299));
    CHECK(timed_map_unit_due(units[2], 300));
    CHECK(timed_map_unit_due(units[2], 329));

    std::array<int32_t, map_unit_players> at{};
    at.fill(-1);
    at[0] = 4; // start position 0 holds player 4
    at[1] = 7;
    CHECK(timed_map_unit_player(units[0], at, -1) == 4);
    CHECK(timed_map_unit_player(units[1], at, -1) == 7);
    CHECK(timed_map_unit_player(units[4], at, 6) == 6);
    CHECK(timed_map_unit_player(units[4], at, -1) == -1);
    // The neutral player takes no units of its own start position.
    CHECK(timed_map_unit_player(units[0], at, 4) == -1);
    const auto flagged = entry("F", 1, 10, campaign::mission_unit_flag::ai_ignore);
    CHECK(timed_map_unit_player(flagged, at, -1) == -1);
    at[0] = -1;
    CHECK(timed_map_unit_player(units[0], at, -1) == -1);
}

void test_computer_last() {
    std::array<int32_t, map_unit_players> positions{};
    positions.fill(-1);
    std::array<uint8_t, map_unit_players> statuses{};
    positions[0] = 2; // human
    positions[1] = 0; // computer
    positions[2] = 1; // computer
    statuses[0] = 1;
    statuses[1] = 2;
    statuses[2] = 2;
    // The human is placed: it takes the highest computer player's position.
    int32_t placing = 2;
    move_computer_last(positions, statuses, placing);
    CHECK(placing == 1 && positions[0] == 1 && positions[2] == 2);
    // Already last: nothing moves.
    placing = 0;
    move_computer_last(positions, statuses, placing);
    CHECK(placing == 0 && positions[1] == 0);
    // A player that is neither takes the human's position.
    positions[0] = 2;
    positions[2] = 1;
    placing = 0;
    move_computer_last(positions, statuses, placing);
    CHECK(placing == 2);
    // Without a computer player nothing moves.
    statuses[1] = statuses[2] = 3;
    positions[0] = 2;
    placing = 2;
    move_computer_last(positions, statuses, placing);
    CHECK(placing == 2 && positions[0] == 2);
}

} // namespace

int main() {
    test_owner_numbers();
    test_start_picks();
    test_create();
    test_timed();
    test_computer_last();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("map units tests passed");
    return 0;
}
