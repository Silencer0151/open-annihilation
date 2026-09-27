// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/weapon_execution/weapon_launch.hpp"

#include "oa/base/game_math.hpp"
#include "oa/sim/unit_movement/movement.hpp"

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

using namespace oa::sim::weapon_execution;
using oa::Game;
using oa::Unit;
using oa::UnitDef;
using oa::WeaponDef;
using oa::World;

namespace {
int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

struct ScriptedRandom {
    std::vector<uint32_t> values{};
    std::vector<uint32_t> limits{};
    size_t next{};
};

uint32_t scripted(void* context, uint32_t limit) {
    auto& random = *static_cast<ScriptedRandom*>(context);
    random.limits.push_back(limit);
    return random.values.at(random.next++);
}

constexpr int32_t units(int32_t whole) {
    return whole * 0x10000;
}

std::unique_ptr<Game> make_game(uint32_t tick, int32_t gravity) {
    auto game = std::make_unique<Game>();
    std::memset(game.get(), 0, sizeof(Game));
    game->tick = tick;
    game->gravity = gravity;
    return game;
}

std::unique_ptr<World> make_world(uint32_t tick, int32_t gravity) {
    auto world = std::make_unique<World>();
    std::memset(world.get(), 0, sizeof(World));
    world->game.tick = tick;
    world->game.gravity = gravity;
    return world;
}

WeaponDef line_weapon() {
    WeaponDef weapon{};
    weapon.flags = OA_WEAPON_FLAG_LINE_OF_SIGHT;
    weapon.weapon_velocity = units(10);
    weapon.range = 500;
    weapon.burst = 3;
    return weapon;
}

void line_projectiles_fly_at_their_target() {
    const auto weapon = line_weapon();
    const FixedVector muzzle{units(100), units(20), units(100)};
    const auto east =
        launch_line_projectile(weapon, muzzle, {units(400), units(20), units(100)}, 1000);
    check(
        east.velocity[0] > units(9) && std::abs(east.velocity[2]) < units(1), "east shot moves +x"
    );
    check(std::abs(east.velocity[1]) < 0x100, "level shot has no lift");
    check(east.distance == units(300), "ground distance recorded");
    check(east.speed == units(10), "speed is the weapon velocity");
    check(east.lifetime_tick == 1000 + 50, "auto range lifetime is range / velocity");
    check(east.burst_remaining == 3, "burst copied from the weapon");
    const auto velocity =
        oa::sim::unit_movement::aim_velocity(east.heading, east.pitch, east.speed);
    check(
        velocity[0] == east.velocity[0] && velocity[2] == east.velocity[2], "velocity from angles"
    );
    const auto south =
        launch_line_projectile(weapon, muzzle, {units(100), units(20), units(700)}, 0);
    check(
        south.velocity[2] > units(9) && std::abs(south.velocity[0]) < units(1),
        "south shot moves +z"
    );
    const auto up = launch_line_projectile(weapon, muzzle, {units(200), units(120), units(100)}, 0);
    check(
        up.velocity[1] > units(6) && up.velocity[0] > units(6), "rising shot climbs toward target"
    );
}

void launch_speed_and_expiry_rules() {
    WeaponDef weapon{};
    weapon.weapon_velocity = units(8);
    weapon.range = 400;
    weapon.weapon_timer = 90;
    check(launch_speed(weapon) == units(8), "plain weapon launches at full velocity");
    weapon.weapon_acceleration = 0x800;
    check(launch_speed(weapon) == 0, "accelerating weapon launches at rest");
    weapon.start_velocity = units(2);
    check(launch_speed(weapon) == units(2), "start velocity wins");
    check(auto_range_expiry_tick(weapon, 7) == 7 + 50, "range expiry");
    weapon.flags = OA_WEAPON_FLAG_NO_AUTO_RANGE;
    check(auto_range_expiry_tick(weapon, 7) == 7 + 90, "noautorange uses the timer");
    weapon.flags = 0;
    weapon.weapon_velocity = 0;
    check(auto_range_expiry_tick(weapon, 7) == 7 + 90, "zero velocity uses the timer");
    const auto vertical = launch_vertical_projectile(weapon, 3);
    check(
        vertical.pitch == vertical_launch_pitch && vertical.heading == 0, "vertical launch angles"
    );
    check(vertical.velocity == FixedVector{}, "vertical launch starts without velocity");
}

void ballistic_shells_use_slot_aim_and_barrel_gravity() {
    WeaponDef weapon{};
    weapon.flags = OA_WEAPON_FLAG_BALLISTIC;
    weapon.weapon_velocity = units(5);
    oa::UnitWeapon slot{};
    slot.aim_heading = 0x4000;
    slot.aim_pitch = 0x1000;
    const FixedVector muzzle{0, 0, 0};
    const FixedVector target{units(300), 0, 0};
    const auto plain = launch_ballistic_projectile(weapon, slot, 0x1fdb, muzzle, target, 40);
    check(plain.velocity[1] == oa::sim::unit_movement::sine_scaled(0x1000, units(5)), "shell lift");
    check(plain.lifetime_tick == 40, "shell without burnblow uses its (zero) timer");
    set_weapon_muzzle_offset(slot, units(5) * 3);
    check(
        weapon_muzzle_offset(slot) == static_cast<uint32_t>(units(5) * 3), "muzzle offset accessor"
    );
    const auto barrel = launch_ballistic_projectile(weapon, slot, 0x1fdb, muzzle, target, 40);
    check(barrel.velocity[1] == plain.velocity[1] - 3 * 0x1fdb, "barrel ticks lose gravity");
    check(barrel.velocity[0] == plain.velocity[0], "barrel does not change ground speed");
    weapon.flags |= OA_WEAPON_FLAG_BURN_BLOW;
    set_weapon_muzzle_offset(slot, 0);
    const auto burn = launch_ballistic_projectile(weapon, slot, 0x1fdb, muzzle, target, 40);
    const auto level = oa::sim::unit_movement::cosine_scaled(0x1000, units(5));
    check(burn.lifetime_tick == 40 + static_cast<uint32_t>(units(300) / level), "burnblow expiry");
}

// Unit records are not cleared when a unit dies, so a slot reused by a new
// unit still holds the dead unit's weapon state until it is armed.
void arming_clears_a_reused_slot() {
    constexpr uint16_t left_reload = 0x55aa;
    constexpr uint8_t left_stockpile = 0xcc;
    constexpr int16_t left_target_x = 120, left_target_z = 340;
    constexpr uint32_t left_aim_ready = 1;
    constexpr int16_t left_aim_heading = 0x2000, left_aim_pitch = -0x0400;
    constexpr uint8_t armed_registry_index = 7;
    constexpr int32_t armed_muzzle_offset = units(3) + 0x1234;
    constexpr uint8_t armed_flags = OA_UNIT_WEAPON_ENABLED | 0x04; // slot index 1 in bits 2..3

    oa::UnitWeapon slot{};
    slot.target_a = left_target_x;
    slot.target_b = left_target_z;
    slot.aim_ready = left_aim_ready;
    slot.def = oa::oa_ref_from_index(3);
    set_weapon_muzzle_offset(slot, units(9));
    slot.reload = left_reload;
    slot.aim_heading = left_aim_heading;
    slot.aim_pitch = left_aim_pitch;
    slot.stockpile = left_stockpile;
    slot.flags = OA_UNIT_WEAPON_AIMED | OA_UNIT_WEAPON_RETALIATE;

    oa::sim::combat_state::WeaponDefinition definition{};
    definition.registry_index = armed_registry_index;
    oa::sim::combat_state::WeaponSlot armed{};
    armed.definition = &definition;
    armed.muzzle_offset = armed_muzzle_offset;
    armed.stockpile = 0x33;
    armed.flags = armed_flags;
    arm_weapon_slot(slot, armed, &definition);
    check(slot.reload == 0, "arming clears the reload countdown");
    check(slot.stockpile == 0, "arming clears the stockpile");
    check(slot.flags == armed_flags, "arming takes the armed flags");
    check(slot.def == oa::oa_ref_from_index(armed_registry_index), "arming refers to the weapon");
    check(
        weapon_muzzle_offset(slot) == static_cast<uint32_t>(armed_muzzle_offset),
        "arming takes the armed muzzle offset"
    );
    check(
        slot.target_a == left_target_x && slot.target_b == left_target_z, "arming keeps the target"
    );
    check(
        slot.aim_ready == left_aim_ready && slot.aim_heading == left_aim_heading &&
            slot.aim_pitch == left_aim_pitch,
        "arming keeps the aim"
    );

    slot.reload = left_reload;
    arm_weapon_slot(slot, armed, nullptr);
    check(slot.def == 0 && slot.reload == 0, "an empty slot refers to no weapon");
}

void dropped_weapons_keep_unit_motion() {
    Unit unit{};
    unit.heading = 0x4000;
    const auto drop = launch_dropped_projectile(unit, units(3));
    check(drop.velocity[1] == 0 && drop.speed == 0, "bomb has no lift or speed");
    check(drop.heading == 0x4000, "bomb keeps unit heading");
    check(
        drop.velocity[0] == -oa::sim::unit_movement::sine_scaled(0x4000, units(3)),
        "bomb x velocity"
    );
}

void accuracy_spread_uses_two_draws() {
    Unit unit{};
    unit.heading = 0x1000;
    unit.health = 50;
    UnitDef type{};
    type.max_damage = 100;
    WeaponDef weapon{};
    weapon.accuracy = 0x100;
    ScriptedRandom random{{100, 0}, {}, 0};
    const auto aim = apply_accuracy_spread(unit, type, weapon, {0x100, 0x200}, scripted, &random);
    const uint32_t spread = 0x100 - (50 << 11) / 100 + 0x800;
    check(random.limits.size() == 2 && random.limits[0] == spread, "spread draws");
    const auto half = static_cast<int16_t>(spread >> 1);
    check(
        aim.heading == static_cast<int16_t>(0x1100 + 100 - half),
        "heading made absolute and jittered"
    );
    check(aim.pitch == static_cast<int16_t>(0x200 - half), "pitch jittered");
    unit.health = 100;
    unit.heading = 7;
    weapon.accuracy = 0;
    ScriptedRandom none{};
    const auto exact = apply_accuracy_spread(unit, type, weapon, {5, 6}, scripted, &none);
    check(
        none.limits.empty() && exact.heading == 12 && exact.pitch == 6, "zero spread draws nothing"
    );
}

void rock_and_lead() {
    Unit shooter{};
    shooter.heading = 0x2000;
    const auto rock = rock_unit_arguments(shooter, 0x2000);
    check(rock[0] == -800 && rock[1] == 0, "forward shot rocks straight back");
    WeaponDef weapon{};
    weapon.weapon_velocity = units(10);
    shooter.veteran_level = 6;
    check(lead_applies(weapon, shooter, true), "veteran leads");
    check(!lead_applies(weapon, shooter, false), "static targets are not led");
    shooter.veteran_level = 5;
    check(!lead_applies(weapon, shooter, true), "level five does not lead");
    shooter.veteran_level = 9;
    weapon.flags = OA_WEAPON_FLAG_CRUISE;
    check(!lead_applies(weapon, shooter, true), "cruise weapons do not lead");
    const auto offset =
        veteran_lead_offset(weapon, shooter, {units(100), 0, 0}, {units(1), 0, units(-2)});
    check(
        offset[0] == 0x7fff8 && offset[1] == 0 && offset[2] == -2 * 0x7fff8, "lead is 0.8 of travel"
    );
}

void burst_children() {
    WeaponDef weapon{};
    weapon.weapon_velocity = units(8);
    weapon.random_decay = 30;
    weapon.spray_angle = 0x1000;
    ScriptedRandom random{{10, 0x800}, {}, 0};
    const FixedVector velocity{units(1), units(2), units(3)};
    const auto child =
        burst_child(weapon, units(64), units(8), 0x1000, 0, velocity, 500, scripted, &random);
    check(
        child.lifetime_tick == 500 + (units(64) + 0x100000) / units(8) + 10 - 15,
        "decayed child expiry"
    );
    check(child.sprayed && child.parent_velocity[1] == units(2), "spray keeps vertical velocity");
    const auto heading = static_cast<uint16_t>(0x800 + 0x1000 - 0x800);
    check(
        child.parent_velocity[0] == -oa::sim::unit_movement::sine_scaled(heading, units(8)),
        "spray x"
    );
    WeaponDef timed_weapon{};
    timed_weapon.weapon_timer = 45;
    ScriptedRandom quiet{};
    const auto timed = burst_child(timed_weapon, 0, 0, 0, 0, velocity, 500, scripted, &quiet);
    check(
        timed.lifetime_tick == 545 && !timed.sprayed && timed.parent_velocity == velocity,
        "timer child"
    );
}

void flight_modes() {
    WeaponDef weapon{};
    const auto mode = [&](uint32_t flags) {
        weapon.flags = flags;
        return flight_mode(weapon);
    };
    check(
        mode(OA_WEAPON_FLAG_SELF_PROP | OA_WEAPON_FLAG_LINE_OF_SIGHT) == FlightMode::self_propelled,
        "selfprop"
    );
    check(
        mode(OA_WEAPON_FLAG_LINE_OF_SIGHT | OA_WEAPON_FLAG_BALLISTIC) == FlightMode::line, "line"
    );
    check(mode(OA_WEAPON_FLAG_BALLISTIC) == FlightMode::ballistic, "ballistic");
    check(mode(OA_WEAPON_FLAG_DROPPED) == FlightMode::dropped, "dropped");
    check(mode(OA_WEAPON_FLAG_METEOR) == FlightMode::meteor, "meteor");
    check(mode(OA_WEAPON_FLAG_VLAUNCH) == FlightMode::inert, "vlaunch alone is inert");
}

void guided_projectiles_turn_at_their_rate() {
    WeaponDef weapon{};
    weapon.turn_rate = 0x100;
    uint16_t heading = 0;
    uint16_t pitch = 0x100;
    check(steer_projectile(heading, pitch, {0x1000, 0x180, 0}, weapon), "steer");
    check(heading == 0x100 && pitch == 0x180, "limited heading turn, pitch reaches goal");
    heading = 0;
    check(
        steer_projectile(heading, pitch, {0xf000, 0x180, 0}, weapon) && heading == 0xff00,
        "turns the short way"
    );
    weapon.flags = OA_WEAPON_FLAG_BURN_BLOW;
    heading = 0;
    check(
        !steer_projectile(heading, pitch, {0x7000, 0x180, 0}, weapon) && heading == 0,
        "burnblow gives up"
    );
}

void damage_rules() {
    auto game = make_game(0, 0);
    Unit source{};
    check(projectile_damage(100, 1.0F, nullptr, *game) == 100, "no source, no bonus");
    source.veteran_level = 12;
    check(projectile_damage(100, 1.0F, &source, *game) == 112, "two veteran steps");
    source.veteran_level = 60;
    check(projectile_damage(100, 1.0F, &source, *game) == 130, "veteran bonus capped");
    game->console_flags = damage_cheat_double;
    source.veteran_level = 0;
    check(projectile_damage(100, 0.5F, &source, *game) == 100, "double");
    game->console_flags = damage_cheat_halve;
    check(projectile_damage(-7, 1.0F, nullptr, *game) == -3, "halve truncates to zero");
    WeaponDef weapon{};
    weapon.edge_effectiveness = 0.25F;
    check(area_damage_scale(weapon, 0, 40) == 1.0F, "centre takes full damage");
    check(area_damage_scale(weapon, 40, 40) == 0.25F, "rim takes edge damage");
    weapon.edge_effectiveness = 0.0F;
    check(area_damage_scale(weapon, 20, 40) == 0.25F, "half radius falls off quadratically");
    Unit unit{};
    check(
        impact_direction({units(5), 0, 0}, unit) == oa::base::game_math::direction(units(5), 0),
        "direction"
    );
}

void shot_plans_follow_the_installed_constructor() {
    auto world = make_world(10, 0x1fdb);
    auto weapon = line_weapon();
    weapon.range = 300;
    weapon.burst = 0;
    Unit unit{};
    unit.health = 100;
    UnitDef type{};
    type.max_damage = 100;
    const FixedVector muzzle{0, 0, 0};
    const FixedVector target{units(200), 0, 0};
    unit.heading = bearing_toward(muzzle, target).heading;
    ScriptedRandom quiet{};
    auto plan =
        plan_weapon_shot(weapon, unit, type, 0, muzzle, target, 0, *world, scripted, &quiet);
    check(
        plan.fired && plan.fire_script && plan.launch.velocity[0] > units(9),
        "fixed gun facing target fires"
    );
    check(plan.slot_aim.heading == static_cast<int16_t>(unit.heading), "slot aims along bearing");
    unit.heading = static_cast<uint16_t>(unit.heading + 0x4000);
    plan = plan_weapon_shot(weapon, unit, type, 0, muzzle, target, 0, *world, scripted, &quiet);
    check(!plan.fired, "fixed gun facing away holds fire");
    weapon.tolerance = 0x5000;
    plan = plan_weapon_shot(weapon, unit, type, 0, muzzle, target, 0, *world, scripted, &quiet);
    check(plan.fired, "wide tolerance fires");

    weapon.flags = OA_WEAPON_FLAG_VLAUNCH;
    unit.weapons[0].aim_heading = 0x123;
    unit.weapons[0].aim_pitch = 0x456;
    plan = plan_weapon_shot(weapon, unit, type, 0, muzzle, target, 0, *world, scripted, &quiet);
    check(!plan.fired && !plan.spends_aim, "vertical launch waits for its Aim script");
    check(
        plan.slot_aim.heading == 0x123 && plan.slot_aim.pitch == 0x456,
        "waiting launcher keeps its aim"
    );
    record_aim_result(unit.weapons[0], 0);
    check(unit.weapons[0].aim_ready == 0, "an Aim script returning zero leaves the slot waiting");
    record_aim_result(unit.weapons[0], -1);
    check(unit.weapons[0].aim_ready == 1, "any nonzero return readies the slot");
    plan = plan_weapon_shot(weapon, unit, type, 0, muzzle, target, 0, *world, scripted, &quiet);
    check(
        plan.fired && plan.spends_aim && plan.launch.pitch == vertical_launch_pitch,
        "vertical launch"
    );
    check(
        plan.slot_aim.heading == static_cast<int16_t>(bearing_toward(muzzle, target).heading),
        "ready launcher aims along the bearing"
    );
    weapon.flags |= OA_WEAPON_FLAG_INTERCEPTOR;
    check(
        !plan_weapon_shot(weapon, unit, type, 0, muzzle, target, 0, *world, scripted, &quiet).fired,
        "an interceptor with no projectile to intercept holds fire"
    );

    weapon.flags = OA_WEAPON_FLAG_TURRET | OA_WEAPON_FLAG_BALLISTIC;
    unit.weapons[1].aim_heading = 0x100;
    unit.weapons[1].aim_pitch = 0x1000;
    unit.heading = 0x2000;
    plan = plan_weapon_shot(weapon, unit, type, 1, muzzle, target, 0, *world, scripted, &quiet);
    check(
        plan.fired && plan.spends_aim && plan.slot_aim.heading == 0x2100 &&
            plan.launch.heading == 0x2100,
        "turret shell"
    );
    check(plan.launch.pitch == 0x1000, "turret shell keeps slot pitch");
    weapon.flags = OA_WEAPON_FLAG_TURRET;
    check(
        !plan_weapon_shot(weapon, unit, type, 1, muzzle, target, 0, *world, scripted, &quiet).fired,
        "turret without a route fires nothing"
    );

    weapon.flags = OA_WEAPON_FLAG_DROPPED;
    plan =
        plan_weapon_shot(weapon, unit, type, 0, muzzle, target, units(2), *world, scripted, &quiet);
    check(
        plan.fired && !plan.fire_script && !plan.spends_aim && plan.launch.speed == 0,
        "bomb drop runs no script"
    );
    check(quiet.limits.empty(), "no random draws without spread");
}

/// Checks that the turret route tests line-of-sight or selfprop (line route)
/// before ballistic (ballistic route along the slot's aim).
void turret_projectiles_take_the_flag_route() {
    const FixedVector muzzle{0, 0, 0};
    const FixedVector target{units(200), 0, units(50)};
    WeaponDef weapon = line_weapon();
    oa::UnitWeapon aimed{};
    aimed.aim_heading = 0x100;
    aimed.aim_pitch = 0x1000;
    const auto line = launch_line_projectile(weapon, muzzle, target, 10);
    for (const uint32_t flags :
         {OA_WEAPON_FLAG_LINE_OF_SIGHT | OA_WEAPON_FLAG_BALLISTIC,
          OA_WEAPON_FLAG_SELF_PROP | OA_WEAPON_FLAG_BALLISTIC}) {
        weapon.flags = flags;
        ProjectileLaunch launch{};
        check(
            launch_turret_projectile(weapon, aimed, 0x1fdb, muzzle, target, 10, &launch),
            "line route fires"
        );
        check(
            launch.heading == line.heading && launch.pitch == line.pitch &&
                launch.velocity == line.velocity,
            "line route flies at the target"
        );
    }
    weapon.flags = OA_WEAPON_FLAG_BALLISTIC;
    ProjectileLaunch shell{};
    check(
        launch_turret_projectile(weapon, aimed, 0x1fdb, muzzle, target, 10, &shell),
        "ballistic route fires"
    );
    check(shell.heading == 0x100 && shell.pitch == 0x1000, "ballistic route keeps the slot aim");
    weapon.flags = OA_WEAPON_FLAG_TURRET;
    ProjectileLaunch none{};
    none.speed = 7;
    check(
        !launch_turret_projectile(weapon, aimed, 0x1fdb, muzzle, target, 10, &none) &&
            none.speed == 7,
        "no route launches nothing"
    );
}

// The line constructor compares the bearing with the unit's heading and pitch
// through the slew test: a zero tolerance allows 150 while stopped and 2000 while moving.
void line_and_dropped_constructors() {
    WeaponDef weapon = line_weapon();
    const FixedVector muzzle{0, 0, 0};
    const FixedVector target{units(200), 0, 0};
    const auto bearing = bearing_toward(muzzle, target);
    Unit unit{};
    unit.heading = static_cast<uint16_t>(bearing.heading + 1000);
    auto plan = plan_line_shot(weapon, unit, muzzle, target, 10);
    check(
        !plan.fired && plan.slot_aim.heading == static_cast<int16_t>(bearing.heading),
        "stopped gun 1000 off its bearing holds fire but aims"
    );
    unit.flags = 1u << 2; // moving
    plan = plan_line_shot(weapon, unit, muzzle, target, 10);
    check(plan.fired && plan.fire_script && !plan.spends_aim, "moving gun within 2000 fires");
    unit.heading = static_cast<uint16_t>(bearing.heading + 2001);
    check(
        !plan_line_shot(weapon, unit, muzzle, target, 10).fired, "moving gun 2001 off holds fire"
    );
    unit.flags = 0;
    unit.heading = static_cast<uint16_t>(bearing.heading + 150);
    check(plan_line_shot(weapon, unit, muzzle, target, 10).fired, "stopped gun within 150 fires");

    unit.weapons[2].aim_heading = 0x321;
    unit.weapons[2].aim_pitch = -0x20;
    const auto drop = plan_dropped_shot(unit, 2, units(3));
    const auto bomb = launch_dropped_projectile(unit, units(3));
    check(
        drop.fired && !drop.fire_script && !drop.spends_aim, "bomb always drops without a script"
    );
    check(
        drop.launch.heading == bomb.heading && drop.launch.velocity == bomb.velocity, "bomb launch"
    );
    check(
        drop.slot_aim.heading == 0x321 && drop.slot_aim.pitch == -0x20, "bomb keeps the slot aim"
    );
}
} // namespace

/// Checks an area blast's visit lists and reach.
///
/// The blast keeps 20 unit pointers and 64 plots; both lists report a repeat
/// as 0 and a new entry as 1 even when the list is full. The box reach
/// measures from the box edges and keeps the sign-extended high word of the
/// 16.16 length.
void area_blast_visits_and_reach() {
    AreaBlastVisits visits{};
    Unit units_seen[area_blast_unit_capacity + 1]{};
    check(
        note_blast_unit(visits, units_seen[0]) && !note_blast_unit(visits, units_seen[0]),
        "a unit is damaged once"
    );
    for (int32_t i = 1; i <= area_blast_unit_capacity; ++i)
        check(note_blast_unit(visits, units_seen[i]), "new units are damaged");
    check(visits.unit_count == area_blast_unit_capacity, "20 units remembered");
    check(note_blast_unit(visits, units_seen[area_blast_unit_capacity]), "past 20 a unit repeats");
    for (uint32_t plot = 0; plot < 70; ++plot)
        check(note_blast_feature(visits, plot), "new feature origins");
    check(
        visits.feature_count == area_blast_feature_capacity && !note_blast_feature(visits, 5) &&
            note_blast_feature(visits, 69),
        "64 feature origins remembered"
    );
    const FixedVector low{units(10), units(0), units(10)};
    const FixedVector high{units(20), units(8), units(20)};
    check(blast_reach_to_box({units(15), units(4), units(15)}, low, high) == 0, "inside the box");
    check(
        blast_reach_to_box({units(23), units(4), units(24)}, low, high) == 5,
        "3-4-5 outside the corner"
    );
    check(
        blast_reach_to_box({units(5), units(4), units(15)}, low, high) == 5, "below the low edge"
    );
    constexpr int32_t far_gap = 0x70000000;
    const auto far_length =
        std::bit_cast<uint32_t>(oa::base::game_math::truncated_length(far_gap, 0, far_gap));
    const auto far =
        blast_reach_to_box({units(20) + far_gap, units(4), units(20) + far_gap}, low, high);
    check(
        far < 0 && far == static_cast<int16_t>(far_length >> 16), "the high word is sign-extended"
    );
}

int main() {
    area_blast_visits_and_reach();
    line_projectiles_fly_at_their_target();
    launch_speed_and_expiry_rules();
    ballistic_shells_use_slot_aim_and_barrel_gravity();
    arming_clears_a_reused_slot();
    dropped_weapons_keep_unit_motion();
    accuracy_spread_uses_two_draws();
    rock_and_lead();
    burst_children();
    flight_modes();
    guided_projectiles_turn_at_their_rate();
    damage_rules();
    shot_plans_follow_the_installed_constructor();
    turret_projectiles_take_the_flag_route();
    line_and_dropped_constructors();
    if (failures != 0) {
        std::fprintf(stderr, "%d weapon launch checks failed\n", failures);
        return 1;
    }
    std::puts("weapon launch: all checks passed");
    return 0;
}
