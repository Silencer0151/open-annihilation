// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "fixture.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)

int main() {
    Player inactive{};
    CHECK(!player_active(inactive));
    Player human{};
    human.in_use = 1;
    human.status = 1;
    human.index = 0;
    std::array<int32_t, 3> centroid{7, 7, 7};
    CHECK(!selection_centroid({}, centroid));
    CHECK(centroid[0] == 7);
    const std::array<std::array<uint32_t, 3>, 2> positions{{
        {0x00020000u, 0xffff0000u, 0x00010000u},
        {0x00040000u, 0x00010000u, 0x00030000u},
    }};
    CHECK(selection_centroid(positions, centroid));
    CHECK(centroid[0] == 0x00030000 && centroid[1] == 0 && centroid[2] == 0x00020000);
    std::array<int32_t, 3> picked{9, 9, 9};
    CHECK(!priority_selection_centroid({}, {}, {}, picked));
    CHECK(picked[0] == 9 && picked[1] == 9 && picked[2] == 9);
    const std::array<std::array<uint32_t, 3>, 1> third_choice{{
        {0x00080000u, 0x00050000u, 0x00060000u},
    }};
    CHECK(priority_selection_centroid({}, {}, third_choice, picked));
    CHECK(picked[0] == 0x00080000 && picked[1] == 0x00050000 && picked[2] == 0x00060000);
    CHECK(priority_selection_centroid({}, positions, third_choice, picked));
    CHECK(picked[0] == 0x00030000 && picked[1] == 0 && picked[2] == 0x00020000);
    const std::array<std::array<uint32_t, 3>, 1> first_choice{{
        {0x00010000u, 0xfffe0000u, 0x00070000u},
    }};
    CHECK(priority_selection_centroid(first_choice, positions, third_choice, picked));
    CHECK(picked[0] == 0x00010000 && picked[1] == (-2 << 16) && picked[2] == 0x00070000);
    const std::array<uint16_t, 6> sight{{0, 0x0001, 0, 0x0004, 0, 0}};
    const std::array<uint32_t, 3> cell_10{{0x00200000u, 0, 0}};
    CHECK(viewpoint_sees_point(2, 3, sight, 0, cell_10));
    CHECK(!viewpoint_sees_point(2, 3, sight, 1, cell_10));
    const std::array<uint32_t, 3> cell_11{{0x00200000u, 0, 0x00200000u}};
    CHECK(viewpoint_sees_point(2, 3, sight, 2, cell_11));
    const std::array<uint32_t, 3> raised{{0x00200000u, 0x00400000u, 0x00200000u}};
    CHECK(viewpoint_sees_point(2, 3, sight, 0, raised));
    CHECK(!viewpoint_sees_point(2, 3, sight, 2, raised));
    const std::array<uint32_t, 3> dropped{{0x00200000u, 0xffc00000u, 0}};
    CHECK(viewpoint_sees_point(2, 3, sight, 2, dropped));
    const std::array<uint32_t, 3> outside{{0x00400000u, 0, 0}};
    CHECK(!viewpoint_sees_point(2, 3, sight, 0, outside));
    const std::array<uint32_t, 3> negative{{0xffe00000u, 0, 0}};
    CHECK(!viewpoint_sees_point(2, 3, sight, 0, negative));
    const std::array<uint8_t, 6> coverage{{0, 5, 0, 0, 0, 0x80}};
    CHECK(point_visible(0, 2, 3, {}, sight, 0, cell_10));
    CHECK(point_visible(1, 2, 3, {}, sight, 0, cell_10));
    CHECK(!point_visible(4, 2, 3, {}, sight, 1, cell_10));
    CHECK(point_visible(0, 2, 3, coverage, sight, 2, cell_11));
    CHECK(point_visible(2, 2, 3, coverage, {}, 0, cell_10));
    CHECK(point_visible(3, 2, 3, coverage, {}, 0, raised));
    const std::array<uint32_t, 3> coverage_high{{0x00200000u, 0, 0x00400000u}};
    CHECK(point_visible(6, 2, 3, coverage, {}, 0, coverage_high));
    CHECK(!point_visible(2, 2, 3, coverage, sight, 2, dropped));
    CHECK(point_visible(0, 2, 3, coverage, sight, 2, dropped));
    CHECK(!point_visible(2, 2, 3, coverage, sight, 2, cell_11));
    CHECK(!point_visible(2, 2, 3, coverage, {}, 0, outside));
    CHECK(!point_visible(2, 2, 3, {}, sight, 0, outside));
    CHECK(!point_visible(2, 2, 3, coverage, {}, 0, negative));
    Order primary_move;
    Order primary_stop;
    Order secondary_weapon;
    primary_move.kind = 26;
    primary_stop.kind = 45;
    secondary_weapon.kind = 13;
    primary_move.next = &primary_stop;
    const OrderQueue seeker{&primary_move, &secondary_weapon};
    CHECK(find_order(seeker, 45) == &primary_stop);
    CHECK(find_order(seeker, 13) == &secondary_weapon);
    CHECK(find_order(seeker, 38) == nullptr);
    TestWorld marks(1, 1);
    auto* marked = (*marks).game.players;
    marked[0].in_use = 1;
    marked[0].status = 1;
    marked[0].machine_group = 1;
    marked[1].in_use = 1;
    marked[1].status = 2;
    marked[1].machine_group = 2;
    CHECK(lowest_unused_player_mark(*marks) == 3);
    for (std::size_t i = 0; i < 10; ++i) {
        marked[i].in_use = 1;
        marked[i].status = 1;
    }
    for (int32_t mark = 1; mark <= 10; ++mark)
        marked[static_cast<std::size_t>(mark - 1)].machine_group = static_cast<uint32_t>(mark);
    CHECK(lowest_unused_player_mark(*marks) == 0);
    CHECK(player_slot_active(0, human));
    CHECK(!player_slot_active(10, human));
    human.index = 10;
    CHECK(!player_active(human));
    // Slot 1 ghost, 2-3 player1, 4 ally, 5-8 rejected, 9 defeated, 10 closer.
    TestWorld near(11, 1);
    auto& units = near.units;
    auto& ghost = units[1];
    ghost.flags = 0x10000000;
    units[2].flags = 0x10000003;
    units[2].position = {3 << 16, -1, 0};
    units[3].flags = 0x10000003;
    units[3].position = {2 << 16, 0, 0};
    auto& ally = units[4];
    ally.flags = 0x10000000;
    units[6].flags = 0x10000002;
    units[7].flags = 0x10008000;
    units[8].flags = 0x10000000;
    units[8].state_flags = 4;
    units[9].flags = 0x10000000;
    auto& closer = units[10];
    closer.flags = 0x10000001;
    closer.position = {1 << 16, 0x7fffffff, 0};
    near.player(0).status = 1;
    near.range(0, 1, 1);
    near.player(1).in_use = 1;
    near.player(1).status = 1;
    near.player(1).index = 4;
    near.range(1, 2, 2);
    near.player(2).in_use = 1;
    near.player(2).status = 2;
    near.player(2).index = 2;
    near.range(2, 4, 1);
    near.player(3).in_use = 1;
    near.player(3).status = 3;
    near.player(3).index = 3;
    near.range(3, 5, 4);
    near.player(4).in_use = 1;
    near.player(4).status = 1;
    near.player(4).index = 10;
    near.range(4, 9, 1);
    near.player(5).in_use = 1;
    near.player(5).status = 1;
    near.range(5, 10, 1);
    std::array<uint8_t, 11> relation{};
    relation[1] = 1;
    relation[2] = 1;
    relation[5] = 1;
    CHECK(nearest_candidate_unit(*near, relation, 0, 0) == &closer);
    relation[0] = 1;
    CHECK(nearest_candidate_unit(*near, relation, 0, 0) == &units[3]);
    relation[4] = 9;
    TestWorld empty(1, 1);
    CHECK(nearest_candidate_unit(*empty, relation, 0, 0) == nullptr);
    CHECK(nearest_candidate_unit(*near, relation, 0, 0) == nullptr);
    const std::array<uint8_t, 1> open{0};
    TestWorld one(3, 1);
    one.player(0).in_use = 1;
    one.player(0).status = 1;
    one.range(0, 1, 2);
    one.units[1].flags = 0x10000000;
    one.units[1].position = {0x00018000, 0, 0};
    one.units[2].flags = 0x10000000;
    one.units[2].position = {0x00010000, 0, 0};
    CHECK(nearest_candidate_unit(*one, open, 0, 0) == &one.units[2]);
    one.units[1].position = {2 << 16, 0, 0};
    one.units[2].position = {0, 0, -(2 << 16)};
    CHECK(nearest_candidate_unit(*one, open, 0, 0) == &one.units[1]);
    one.units[1].position = {0, 0, 0};
    one.units[2].position = {1 << 16, 0, 0};
    const auto edge = static_cast<int32_t>(0x80000000u);
    CHECK(nearest_candidate_unit(*one, open, edge, edge) == &one.units[1]);
    TestWorld tw(2, 2);
    auto& w = *tw;
    Fixture h;
    auto& t = tw.defs[1];
    t.max_damage = 100;
    auto& u = tw.units[1];
    u.def = oa::oa_ref_from_index(1);
    auto& p = tw.player(0);
    p.in_use = 1;
    p.status = 1;
    u.owner = oa::world_player_ref_of(&w, &p);
    auto& q = tw.orders[1];
    Order a, b, c;
    q.primary = &a;
    a.next = &b;
    b.next = &c;
    rotate_primary(q, b);
    CHECK(q.primary == &a && a.next == &c && c.next == &b && !b.next);
    remove_order(q, u, c, h);
    CHECK(a.next == &b && (c.flags & 1));
    a.preserve_flags = 4;
    clear_orders(q, u, false, h);
    CHECK(q.primary == &a && !a.next);
    clear_orders(q, u, true, h);
    CHECK(!q.primary);
    a = {};
    q.primary = &a;
    w.game.tick = 100;
    a.wake_tick = 200;
    a.wait_events = 2;
    u.events = 4;
    h.calls.clear();
    primary_orders(w, q, u, h);
    CHECK(h.calls.empty() && u.events == 4);
    a.wait_events = 0x10002;
    a.raised_events = 0x10000;
    u.events = 6;
    h.random_value = 4;
    primary_orders(w, q, u, h);
    CHECK(a.wait_events == 1 && a.wake_tick == 134 && u.events == 4);
    CHECK(h.events.back() == 0x10002);
    CHECK(
        h.calls ==
        std::vector<std::string>({"weapon0", "weapon1", "weapon2", "dispatch", "random15"})
    );
    h.calls.clear();
    w.game.tick = 134;
    primary_orders(w, q, u, h);
    CHECK(h.events.back() == 1 && a.wake_tick == 168);
    a = {};
    b = {};
    a.flags = 4;
    b.flags = 4;
    a.next = &b;
    q.secondary = &a;
    a.wait_events = 1;
    a.wake_tick = 500;
    b.wait_events = 1;
    b.wake_tick = 100;
    h.calls.clear();
    secondary_orders(w, q, u, h);
    CHECK(a.wake_tick == 500 && b.wake_tick == 168);
    h.handler = [](Unit&, Order&) { return 7; };
    w.game.tick = 500;
    secondary_orders(w, q, u, h);
    CHECK(q.secondary == &b);
    h.handler = {};
    q.primary = nullptr;
    q.secondary = nullptr;
    w.game.tick = 240;
    u.health = 25;
    u.health_percent = 33;
    u.damage_countdown = 1;
    u.capture_cooldown = 2;
    u.script = 1;
    u.flags = 0x10;
    h.calls.clear();
    update_unit(w, q, u, h);
    CHECK(
        u.health_percent == 25 && u.previous_health_percent == 33 && !u.damage_countdown &&
        u.capture_cooldown == 1 && !u.flags
    );
    CHECK(h.calls == std::vector<std::string>({"script"}));
    // The signed health * 100 is divided as an unsigned word, and the clamp comes after
    // the division.
    u.health = -1;
    update_unit(w, q, u, h);
    CHECK(u.health_percent == 100);
    u.movement = 1;
    u.flags = 0x10001;
    t.flags = 0x101000;
    t.water_line = 5;
    w.game.sea_level = 20;
    h.terrain = 30;
    h.calls.clear();
    update_height(w, u, h);
    CHECK(static_cast<uint32_t>(u.position.y) == 30u * 65536 && u.flags == 1);
    CHECK(h.calls == std::vector<std::string>({"terrain", "terrain"}));
    t.flags = 0x80000;
    u.flags = 0x10001;
    update_height(w, u, h);
    CHECK(static_cast<uint32_t>(u.position.y) == 15u * 65536);
    q.primary = &a;
    a = {};
    h.handler = [](Unit&, Order&) { return 2; };
    bool exhausted = false;
    try {
        primary_orders(w, q, u, h, 4);
    } catch (const std::runtime_error&) {
        exhausted = true;
    }
    CHECK(exhausted);
    q.primary = nullptr;
    h.handler = {};
    u.owner = 0;
    u.type_index = 1;
    u.movement = 0;
    u.script = 0;
    u.flags = 0;
    p.in_use = 1;
    p.status = 3;
    tw.range(0, 1, 1);
    w.game.periodic_flags |= 2;
    w.game.periodic_countdown = 1;
    h.calls.clear();
    update_units(w, tw.orders, h);
    CHECK(w.game.active_unit_count == 1 && w.game.periodic_countdown == 90);
    CHECK(h.calls == std::vector<std::string>({"pre", "key", "periodic1", "periodic2"}));
    PlacementGrids grids;
    grids.step_x = 1;
    grids.base = 99;
    grids.alt_base = 99;
    h.calls.clear();
    h.random_values = {4, 1, 7, 2, 9, 0, 5, 3};
    seed_placement_grids(grids, h);
    CHECK(grids.base == 3 && grids.alt_base == 6);
    CHECK(grids.step_x == 15 && grids.step_y == 12);
    CHECK(grids.phase_x == 0 && grids.phase_y == -4);
    CHECK(grids.alt_step_x == 23 && grids.alt_step_y == 14);
    CHECK(grids.alt_phase_x == -6 && grids.alt_phase_y == -4);
    CHECK(
        h.calls == std::vector<std::string>(
                       {"random10",
                        "random3",
                        "random15",
                        "random12",
                        "random20",
                        "random3",
                        "random23",
                        "random14"}
                   )
    );
    // Steps keep the low 16 bits of roll+base+8. A negative step divides by 2
    // toward zero. The phase subtract happens in 32 bits before the low-word store.
    h.calls.clear();
    h.random_cursor = 0;
    h.random_values = {0x0001fffeu, 0, 1, 0, 0xfffcu, 0, 0x10005u, 0};
    seed_placement_grids(grids, h);
    CHECK(grids.step_x == 9 && grids.step_y == 11);
    CHECK(grids.phase_x == static_cast<int16_t>(1 - 9 / 2));
    CHECK(grids.phase_y == static_cast<int16_t>(0 - 11 / 2));
    CHECK(grids.alt_step_x == 10 && grids.alt_step_y == 14);
    CHECK(grids.alt_phase_x == 0);
    CHECK(grids.alt_phase_y == static_cast<int16_t>(0 - 14 / 2));
    h.calls.clear();
    h.random_cursor = 0;
    h.random_values = {0xfff0u, 0, 1, 0, 0, 0, 0, 0};
    seed_placement_grids(grids, h);
    CHECK(grids.step_x == -5);
    CHECK(grids.phase_x == 3);
    CHECK(h.calls[2] == "random4294967291");

    // On a map whose water does damage, every 30 ticks each unit the local
    // machine simulates at or under the sea takes the map's water damage as
    // kind 11: a submarine on the floor and a ship on the water do, and a
    // hovercraft does not.
    {
        constexpr uint32_t water_damage_kind = 11;
        TestWorld sea(4, 4);
        auto& world = *sea;
        world.game.sea_level = 20;
        world.environment_enabled = 1;
        world.environment_damage = 7;
        auto& owner = sea.player(0);
        owner.in_use = 1;
        owner.status = 1;
        sea.range(0, 1, 3);
        std::array<OrderQueue, 4> queues{};
        const std::array<std::pair<uint32_t, int32_t>, 3> kinds{
            {{OA_UNIT_DEF_FLAG_UPRIGHT, 5},
             {OA_UNIT_DEF_FLAG_FLOATER, 17},
             {OA_UNIT_DEF_FLAG_CAN_HOVER, 20}}
        };
        for (std::size_t index = 0; index < kinds.size(); ++index) {
            auto& def = sea.defs[index + 1];
            def.flags = kinds[index].first;
            def.max_damage = 100;
            auto& unit = sea.units[index + 1];
            unit.type_index = static_cast<int16_t>(index + 1);
            unit.def = oa::oa_ref_from_index(static_cast<uint32_t>(index + 1));
            unit.owner = oa::oa_ref_from_index(0);
            unit.health = 100;
            unit.position.y = kinds[index].second * 65536;
        }
        Fixture host;
        for (const uint32_t tick : {59u, 60u, 61u, 90u}) {
            world.game.tick = tick;
            for (std::size_t slot = 1; slot < 4; ++slot)
                update_unit(world, queues[slot], sea.units[slot], host);
        }
        CHECK(host.damaged.size() == 4);
        for (const auto& hit : host.damaged)
            CHECK(
                (hit.unit == 1 || hit.unit == 2) && hit.amount == 7 && hit.kind == water_damage_kind
            );
    }
    std::cout << "simulation state tests passed\n";
}
