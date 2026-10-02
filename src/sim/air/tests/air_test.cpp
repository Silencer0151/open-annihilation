// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "air_fixture.hpp"

#include "oa/sim/air/flight.hpp"
#include "oa/sim/air/goal.hpp"
#include "oa/base/game_math.hpp"
#include "oa/sim/ground_orders/orders.hpp"

#include <cmath>
#include <cstdlib>

using namespace air_test;

namespace {

void test_point_goal() {
    Fixture f;
    Unit& plane = f.unit(1);
    plane.position = {world(100), world(30), world(100)};
    uint32_t events = 0;
    AirGoal goal = air_goal_at_point(&events, &plane, {world(300), 0, world(100)});
    CHECK(goal.flags == goal_terrain_altitude);
    FixedVec3 point{};
    CHECK(air_goal_position(&goal, f.host, &point));
    // No explicit altitude: cruise over the bucket height (20 + 80).
    CHECK(point.y == world(100));
    uint16_t heading = 0;
    CHECK(!air_goal_heading(&goal, &heading));
    CHECK(!air_goal_arrived(&goal, f.host, &plane));
    plane.position.x = world(300);
    CHECK(air_goal_arrived(&goal, f.host, &plane));
    // Terrain-relative altitude: max(sea 5, terrain 10) + 40, and arrival
    // then also needs the height within one unit.
    air_goal_set_altitude(&goal, f.host, 40);
    CHECK(goal.point.y == world(50));
    CHECK(!air_goal_arrived(&goal, f.host, &plane));
    plane.position.y = world(50) + 0x8000;
    CHECK(air_goal_arrived(&goal, f.host, &plane));
    f.terrain = 2;
    air_goal_set_altitude(&goal, f.host, 1000);
    CHECK(goal.point.y == goal_altitude_limit);
    air_goal_set_arrival_radius(&goal, 10);
    plane.position.x = world(309);
    CHECK(air_goal_arrived(&goal, f.host, &plane));
    plane.position.x = world(311);
    CHECK(!air_goal_arrived(&goal, f.host, &plane));
    air_goal_set_bearing(&goal, 0x4000);
    CHECK(air_goal_heading(&goal, &heading) && heading == 0x4000);
    CHECK(!air_goal_is_tracking(&goal));
}

void test_follow_goal() {
    Fixture f;
    Unit& plane = f.unit(1);
    Unit& tank = f.unit(2);
    Unit& fighter = f.unit(3);
    tank.def = oa_ref_from_index(1);
    tank.position = {world(500), world(12), world(600)};
    tank.heading = 0x2000;
    uint32_t events = 0;
    AirGoal goal = air_goal_follow_unit(f.host, &events, &plane, &tank);
    CHECK(goal.flags == goal_track_unit);
    FixedVec3 point{};
    CHECK(air_goal_position(&goal, f.host, &point));
    CHECK(point.x == tank.position.x && point.y == tank.position.y && point.z == tank.position.z);
    CHECK(air_goal_is_tracking(&goal));
    f.outside = true;
    CHECK(!air_goal_position(&goal, f.host, &point));
    f.outside = false;
    // Flying target: shadowed at the stand-off (default 100) along its heading.
    fighter.position = {world(1000), world(90), world(1000)};
    fighter.heading = 0;
    AirGoal chase = air_goal_follow_unit(f.host, &events, &plane, &fighter);
    CHECK(chase.flags == (goal_track_unit | goal_stand_off | goal_match_heading));
    CHECK(chase.stand_off == goal_default_stand_off);
    CHECK(air_goal_position(&chase, f.host, &point));
    CHECK(
        point.x == fighter.position.x + sim::unit_movement::sine_scaled(0, goal_default_stand_off)
    );
    CHECK(
        point.z == fighter.position.z + sim::unit_movement::cosine_scaled(0, goal_default_stand_off)
    );
    uint16_t heading = 0;
    plane.position = {world(0), world(90), world(0)};
    CHECK(air_goal_heading(&chase, &heading));
    CHECK(
        heading == base::game_math::direction(
                       plane.position.x - fighter.position.x, plane.position.z - fighter.position.z
                   )
    );
    air_goal_unlink(&chase, &fighter);
    CHECK(!air_goal_is_tracking(&chase));
    CHECK(!air_goal_heading(&chase, &heading));
}

void test_seek_goal() {
    Fixture f;
    Unit& plane = f.unit(1);
    uint32_t events = 0;
    AirGoal seek =
        air_goal_seek(&events, &plane, {world(0), world(50), world(0)}, {world(4), world(1), 0});
    seek.flags = seek_turn_to_heading;
    seek.heading = base::game_math::direction(0, world(4));
    FixedVec3 point{};
    CHECK(air_goal_position(&seek, f.host, &point));
    CHECK(point.x == 0 && point.y == world(50));
    // The point moves by the step before it turns (Y never moves).
    CHECK(seek.point.x == world(4) && seek.point.y == world(50));
    // Turned by at most turn_rate / 8 (100 angle units) toward the heading.
    CHECK(seek.step.y == 0);
    const uint16_t after = base::game_math::direction(-seek.step.x, -seek.step.z);
    const uint16_t before = base::game_math::direction(-world(4), 0);
    CHECK(
        static_cast<int16_t>(after - before) == -100 ||
        static_cast<int16_t>(before - after) == -100 || static_cast<int16_t>(after - before) == 100
    );
    plane.position = {world(200), 0, world(0)};
    CHECK(!air_goal_arrived(&seek, f.host, &plane));
    plane.position = {world(40), 0, world(0)};
    CHECK(air_goal_arrived(&seek, f.host, &plane));
    CHECK(!air_goal_is_tracking(&seek));
}

void test_rotate() {
    int32_t x = 1000;
    int32_t z = 0;
    rotate_xz(0x4000, &x, &z);
    CHECK(x == 0 && z == 1000);
    rotate_xz(0, &x, &z);
    CHECK(x == 0 && z == 1000);
    x = 1000;
    z = 0;
    rotate_xz(-0x4000, &x, &z);
    CHECK(x == 0 && z == -1000);
}

// air_attitude keeps 0xf333/0x10000 of the filtered velocity change, adds this
// tick's change, turns X/Z into the heading frame with the rounding rotation
// of rotate_xz, and takes bank and pitch from the negated X against
// gravity * 0x10000 / 0xccd.
void test_attitude() {
    constexpr uint16_t heading = 0x1234;
    constexpr oa_fixed gravity = 0x1fdb;
    constexpr double binary_angle = 9.587379924285e-05; // radians per heading unit
    sim::unit_movement::Unit unit{};
    unit.heading = heading;
    std::array<sim::unit_movement::Fixed, 3> filtered{0x23456, 0x100, -0x12345};
    int16_t bank = 0;
    air_attitude(unit, filtered, {0x1000, 0, -0x800}, 0x10000, 0x8000, gravity, &bank);
    const auto kept = [](int64_t value) { return static_cast<int32_t>((value * 0xf333) >> 16); };
    CHECK(filtered[0] == kept(0x23456) + 0x1000);
    CHECK(filtered[1] == kept(0x100));
    CHECK(filtered[2] == kept(-0x12345) - 0x800);
    const double angle = static_cast<int16_t>(heading) * binary_angle;
    const auto turned = static_cast<int32_t>(
        std::nearbyint(std::cos(angle) * filtered[0] - filtered[2] * std::sin(angle))
    );
    const int32_t vertical = static_cast<int32_t>((int64_t{gravity} << 16) / 0xccd);
    CHECK(bank == static_cast<int16_t>(base::game_math::direction(-turned, vertical)));
    const auto tilt = static_cast<int32_t>((int64_t{0x8000} * -turned) >> 16);
    CHECK(unit.pitch == static_cast<int16_t>(base::game_math::direction(tilt, vertical)));
    // Without a velocity change the bank eases back toward level by the decay
    // alone and never swings past it.
    int16_t previous = bank;
    for (int tick = 0; tick < 120; ++tick) {
        air_attitude(unit, filtered, {}, 0x10000, 0x8000, gravity, &bank);
        CHECK(std::abs(bank) <= std::abs(previous) && (bank == 0 || (bank < 0) == (previous < 0)));
        previous = bank;
    }
    CHECK(std::abs(bank) < 0x40);
}

void test_driver() {
    Fixture f;
    Unit& plane = f.unit(1);
    plane.position = {world(100), world(40), world(100)};
    plane.heading = 0x1234;
    AirDriver& driver = f.drivers[1];
    uint32_t stale_events = 0;
    AirGoal stale = air_goal_at_point(&stale_events, &plane, {world(1), 0, world(1)});
    driver.goal = &stale;
    // movement_driver_bind clears the goal and stores the unit.
    movement_driver_bind(&driver, &plane);
    CHECK(driver.goal == nullptr && driver.unit == &plane);
    air_driver_init_local(&driver, &plane);
    CHECK(driver.position.x == plane.position.x && driver.heading == 0x1234);
    CHECK((driver.flags & driver_resend) != 0);
    driver.flags = 0;
    uint32_t events = 0;
    AirGoal goal = air_goal_at_point(&events, &plane, {world(500), 0, world(100)});
    air_driver_set_goal(&driver, &goal);
    CHECK(air_driver_has_goal(&driver) && (driver.flags & driver_resend) != 0);
    // Far away (400 > 160): cruise altitude over the bucket, heading to the point.
    air_driver_update(&driver, f.host, layer_air);
    CHECK(driver.position.x == world(500));
    CHECK(driver.position.y == world(100));
    CHECK(driver.velocity.x == world(400));
    CHECK(driver.heading == base::game_math::direction(world(100) - world(500), 0));
    // Arrival raises 0x20 and drops the non-tracking goal (raising 0x80).
    plane.position.x = world(500);
    air_driver_update(&driver, f.host, layer_air);
    CHECK(
        (events & sim::ground_orders::arrived_event) != 0 &&
        (events & sim::ground_orders::goal_replaced_event) != 0
    );
    CHECK(!air_driver_has_goal(&driver));
    const AirSteeringTarget target = air_driver_steering_target(&driver);
    CHECK(target.position.x == world(500) && target.velocity.x == 0);
    // Layer change against the last sent layer re-arms the delta.
    driver.flags = static_cast<uint8_t>(layer_air << driver_sent_layer_shift);
    air_driver_update(&driver, f.host, layer_air);
    CHECK((driver.flags & driver_resend) == 0);
    air_driver_update(&driver, f.host, layer_ground);
    CHECK((driver.flags & driver_resend) != 0);
    AirDriver mirrored{};
    air_driver_init_mirrored(&mirrored, &plane);
    air_driver_set_goal(&mirrored, &goal);
    CHECK((mirrored.flags & driver_resend) == 0);
}

void test_flight_step() {
    sim::unit_movement::Unit unit{};
    unit.position = {world(100), world(50), world(100)};
    unit.type.maximum_turn = 0x200;
    sim::unit_movement::Movement movement{};
    movement.flags = layer_ground;
    movement.velocity = {5, 6, 7};
    movement.speed = 9;
    std::array<sim::unit_movement::Fixed, 3> attitude{};
    AirSteeringTarget target{{world(200), world(80), world(100)}, {}, 0x4000};
    UnitDef def{};
    def.acceleration = 0x2000;
    def.max_velocity = 0x80000;
    def.brake_rate = 0x60000;
    def.bank_scale = 0x10000;
    def.pitch_scale = 0x10000;
    constexpr oa_fixed gravity = 0x1fdb;
    bool outside = false;
    int16_t bank = 0;
    air_flight_step(unit, movement, attitude, target, def, gravity, outside, &bank);
    CHECK(movement.velocity[0] == 0 && movement.velocity[1] == 0 && movement.velocity[2] == 0);
    CHECK(movement.speed == 0);
    movement.flags = layer_air;
    for (int tick = 0; tick < 400; ++tick) {
        air_flight_step(unit, movement, attitude, target, def, gravity, outside, &bank);
        for (int axis = 0; axis < 3; ++axis)
            unit.position[static_cast<std::size_t>(axis)] +=
                movement.velocity[static_cast<std::size_t>(axis)];
    }
    // Converges on the target point and heading.
    CHECK(std::abs(unit.position[0] - world(200)) < world(4));
    CHECK(std::abs(unit.position[1] - world(80)) < world(2));
    CHECK(std::abs(unit.position[2] - world(100)) < world(4));
    CHECK(unit.heading == 0x4000);
    // Horizontal speed never settles above the brake rate for long.
    CHECK(planar_length(movement.velocity[0], movement.velocity[2]) <= 0x60000 + 0x10000);
    // Climb is capped at one unit per tick while slow.
    unit.position[1] = world(0);
    movement.velocity = {};
    movement.speed = 0;
    air_flight_step(unit, movement, attitude, target, def, gravity, outside, &bank);
    CHECK(movement.velocity[1] == 0x10000);
    outside = true;
    movement.velocity[1] = 77;
    air_flight_step(unit, movement, attitude, target, def, gravity, outside, &bank);
    // Off the map only the damping (acceleration / max velocity) applies.
    CHECK(movement.velocity[1] == 75);
}

// Kinds the target and seek goals report; the air driver's delta capture
// sends only these two.
static_assert(static_cast<int>(AirGoalKind::target) == 2);
static_assert(static_cast<int>(AirGoalKind::seek) == 3);

} // namespace

int main() {
    test_point_goal();
    test_follow_goal();
    test_seek_goal();
    test_rotate();
    test_attitude();
    test_driver();
    test_flight_step();
    return finish("air");
}
