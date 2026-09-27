// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "air_fixture.hpp"

#include "oa/sim/air/attack.hpp"
#include "oa/base/game_math.hpp"
#include "oa/sim/unit_movement/movement.hpp"

using namespace air_test;

namespace {

constexpr uint32_t attack_wait = event_weapons | wait_for_goal;

// Plane in slot 1 with a 120-range first weapon, target tank in slot 2.
struct AttackFixture : Fixture {
    Unit& plane = unit(1);
    Unit& tank = unit(2);

    AttackFixture() {
        state->game.weapon_defs[0].range = 120;
        plane.weapons[0].def = oa_ref_from_index(0);
        plane.position = {world(100), world(90), world(100)};
        plane.flags = layer_air;
        layers[1] = layer_air;
        air_driver_init_local(&drivers[1], &plane);
        tank.def = oa_ref_from_index(1);
        tank.position = {world(900), world(12), world(100)};
    }
};

void test_ground_attack_run() {
    AttackFixture f;
    OrderFixture o(&f.plane);
    o.order.target = &f.tank;
    o.order.flags = order_announce;
    CHECK(air_attack_ground(&o.order, f.host, 0) == AirStep::next);
    CHECK(o.order.destination.x == f.tank.position.x);
    CHECK(f.last_speech == speech_order);
    // Approach: half way, randomly within +-45 degrees of the bearing.
    o.order.phase = 1;
    f.randoms({0x2000});
    CHECK(air_attack_ground(&o.order, f.host, 0) == AirStep::next);
    CHECK(f.weapon_resets == 1);
    CHECK(o.order.wait_events == attack_wait);
    CHECK(o.goal.arrival_radius == 0x80 && (o.goal.flags & goal_arrival_radius) != 0);
    CHECK(o.goal.point.x == world(500) && o.goal.point.z == world(100));
    // Run in: weapon 0 aimed at the unit, arrive within weapon range.
    o.order.phase = 2;
    CHECK(air_attack_ground(&o.order, f.host, 0) == AirStep::next);
    CHECK(f.aimed_unit == &f.tank && f.aims == 1);
    CHECK(o.goal.point.x == world(900) && o.goal.arrival_radius == 120);
    // Pull out three ranges beyond the target on the far side.
    f.plane.position.x = world(800);
    o.order.phase = 3;
    f.randoms({5});
    CHECK(air_attack_ground(&o.order, f.host, 0) == AirStep::next);
    CHECK(o.goal.point.x == world(900 + 360));
    CHECK(o.goal.arrival_radius == 0x85);
    CHECK(o.order.wait_events == (attack_wait | event_order_destroyed));
    // Swing a quarter turn aside by one range.
    o.order.phase = 4;
    f.plane.health = 1000;
    f.state->unit_defs[0].max_damage = 1000;
    f.plane.heading = 0;
    f.randoms({1});
    CHECK(air_attack_ground(&o.order, f.host, 0) == AirStep::next);
    CHECK(
        o.goal.point.x == f.plane.position.x - sim::unit_movement::sine_scaled(0x4000, world(120))
    );
    o.order.phase = 5;
    CHECK(air_attack_ground(&o.order, f.host, 0) == AirStep::stay && o.order.phase == 2);
}

void test_ground_attack_endings() {
    AttackFixture f;
    OrderFixture o(&f.plane);
    o.order.target = &f.tank;
    // A finished run while firing at will queues a seek at the tail.
    f.plane.flags |= 1u << OA_UNIT_FLAG_FIRE_ORDER_SHIFT;
    CHECK(air_attack_ground(&o.order, f.host, event_target_lost) == AirStep::done);
    CHECK(
        std::strcmp(f.pushed, "VTOL_SEEKATTACK") == 0 && f.pushed_append &&
        f.pushed_target == &f.tank
    );
    // Target gone with the re-seek bit: seek from the unit's position.
    o.order.target = nullptr;
    o.order.flags = order_seek_if_lost;
    f.pushed[0] = 0;
    CHECK(air_attack_ground(&o.order, f.host, 0) == AirStep::done);
    CHECK(std::strcmp(f.pushed, "VTOL_SEEKATTACK") == 0 && f.pushed_target == nullptr);
    CHECK(f.pushed_point.x == f.plane.position.x);
    // Ground target without a unit: aim at the destination.
    o.order.flags = 0;
    o.order.destination = {world(400), 0, world(400)};
    o.order.phase = 2;
    CHECK(air_attack_ground(&o.order, f.host, 0) == AirStep::next);
    CHECK(f.aimed_point.x == world(400));
    // Leash from the anchor.
    o.order.anchor_x = 0;
    o.order.anchor_z = 100;
    o.order.parameter_3 = 100;
    CHECK(air_attack_ground(&o.order, f.host, 0) == AirStep::done);
    // Badly damaged with a pad nearby: land there instead.
    o.order.parameter_3 = 0;
    Unit& pad = f.unit(4);
    pad.def = oa_ref_from_index(2);
    pad.state_flags = OA_UNIT_STATE_ACTIVE;
    pad.position = f.plane.position;
    f.state->unit_defs[0].max_damage = 1000;
    f.plane.health = 700;
    o.order.phase = 4;
    CHECK(air_attack_ground(&o.order, f.host, 0) == AirStep::restart);
    CHECK(std::strcmp(f.pushed, "VTOL_LANDING") == 0 && f.pushed_target == &pad);
    // Off the map the run restarts from the fly-over phase.
    f.outside = true;
    o.order.phase = 4;
    o.order.target = &f.tank;
    CHECK(air_attack_ground(&o.order, f.host, 0) == AirStep::next);
    CHECK(o.order.wake_tick == 130);
}

void test_air_attack() {
    AttackFixture f;
    Unit& bomber = f.unit(3);
    bomber.position = {world(100), world(120), world(400)};
    bomber.heading = 0;
    f.velocities[3] = {world(1), 0, world(-2)};
    OrderFixture o(&f.plane);
    o.order.target = &bomber;
    CHECK(air_attack_air(&o.order, f.host, 0) == AirStep::next);
    CHECK(o.order.wake_tick == 101 && o.order.parameter == 0);
    o.order.phase = 1;
    // Not lined up: patience grows, and a far target is led by its velocity.
    f.plane.heading = static_cast<uint16_t>(base::game_math::direction(0, world(300)));
    CHECK(air_attack_air(&o.order, f.host, 0) == AirStep::stay);
    CHECK(o.order.parameter == 0x2d);
    CHECK(o.goal.kind == AirGoalKind::seek);
    CHECK(o.goal.point.x == world(100 + 45) && o.goal.point.z == world(400 - 90));
    CHECK(o.order.wait_events == (event_timer | attack_wait));
    CHECK(f.aimed_unit == &bomber);
    // Lined up after a goal event: a straight pass of 30 ticks.
    f.plane.heading = static_cast<uint16_t>(base::game_math::direction(world(0), world(-300)));
    f.randoms({4});
    CHECK(air_attack_air(&o.order, f.host, event_arrived) == AirStep::stay);
    CHECK(o.goal.kind == AirGoalKind::seek && o.order.parameter == 0);
    CHECK(o.order.wake_tick == 100 + 0x3c + 4);
    // Out of patience: evade at the head of the queue and restart.
    o.order.parameter = 0x5a;
    CHECK(air_attack_air(&o.order, f.host, 0) == AirStep::restart);
    CHECK(
        std::strcmp(f.pushed, "VTOL_EVADE") == 0 && !f.pushed_append && f.pushed_target == &bomber
    );
    CHECK(o.goal.kind == AirGoalKind::none && o.order.wait_events == 0);
    CHECK(air_attack_air(&o.order, f.host, event_weapons) == AirStep::done);
}

void test_evade() {
    AttackFixture f;
    OrderFixture o(&f.plane);
    CHECK(air_evade(&o.order, f.host, 0) == AirStep::done);
    o.order.target = &f.tank;
    f.plane.heading = 0;
    f.randoms({1});
    CHECK(air_evade(&o.order, f.host, 0) == AirStep::next);
    CHECK(o.order.parameter == 1);
    CHECK(
        o.goal.point.x == f.plane.position.x - sim::unit_movement::sine_scaled(0xc000, world(120))
    );
    CHECK(o.goal.arrival_radius == 0x80 && o.order.wait_events == attack_wait);
    o.order.phase = 1;
    CHECK(air_evade(&o.order, f.host, 0) == AirStep::next);
    CHECK(
        o.goal.point.x == f.plane.position.x - sim::unit_movement::sine_scaled(0xc000, world(240))
    );
    o.order.phase = 2;
    CHECK(air_evade(&o.order, f.host, 0) == AirStep::done);
    o.order.phase = 1;
    CHECK(air_evade(&o.order, f.host, event_target_lost) == AirStep::done);
}

} // namespace

int main() {
    test_ground_attack_run();
    test_ground_attack_endings();
    test_air_attack();
    test_evade();
    return finish("air-attack");
}
