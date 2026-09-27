// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/weapon_execution/interceptor.hpp"

#include "oa/core/player.h"
#include "oa/core/unit_def.h"
#include "oa/core/weapon_def.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace oa;
using namespace oa::sim::weapon_execution;

namespace {
int failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

constexpr int32_t fx(int32_t whole) {
    return static_cast<int32_t>(static_cast<uint32_t>(whole) << 16);
}

constexpr oa_ref32 interceptor_def = 1;
constexpr oa_ref32 nuke_def = 2;
constexpr oa_ref32 plain_def = 3;
constexpr oa_ref32 cruise_def = 4;

// Unit 1 (player 0) with a stockpiled interceptor of coverage 100 at (1000, 0, 1000).
World* make_world() {
    World* w = world_create();
    WorldCapacity cap{8, 4, 0};
    if (w == nullptr || !world_alloc_tables(w, &cap))
        std::abort();
    WeaponDef& interceptor = w->game.weapon_defs[interceptor_def - 1];
    interceptor.flags = OA_WEAPON_FLAG_VLAUNCH | OA_WEAPON_FLAG_INTERCEPTOR;
    interceptor.coverage = 100;
    interceptor.weapon_velocity = fx(5);
    interceptor.burst = 1;
    w->game.weapon_defs[nuke_def - 1].flags = OA_WEAPON_FLAG_TARGETABLE | OA_WEAPON_FLAG_BALLISTIC;
    w->game.weapon_defs[plain_def - 1].flags = OA_WEAPON_FLAG_BALLISTIC;
    w->game.weapon_defs[cruise_def - 1].flags = OA_WEAPON_FLAG_CRUISE | OA_WEAPON_FLAG_SELF_PROP;
    Unit& unit = w->units[1];
    unit.owner_index = 0;
    unit.owner = 1;
    unit.def = 2;
    unit.type_index = 1;
    unit.id = 1;
    unit.position = {fx(1000), 0, fx(1000)};
    unit.flags = OA_UNIT_FLAG_LIVE;
    unit.weapons[0].def = interceptor_def;
    unit.weapons[0].stockpile = 1;
    w->game.tick = 500;
    return w;
}

Projectile& add_shot(World& w, uint8_t owner, oa_ref32 def, int32_t target_x, int32_t target_z) {
    Projectile& shot = w.projectiles[w.game.projectile_count++];
    shot.owner_index = owner;
    shot.def = def;
    shot.target = {fx(target_x), 0, fx(target_z)};
    shot.position = {fx(target_x), fx(300), fx(target_z)};
    return shot;
}

void target_search() {
    World* w = make_world();
    Unit& unit = w->units[1];
    add_shot(*w, 1, nuke_def, 1050, 1000);  // inside: dx 50
    add_shot(*w, 0, nuke_def, 1050, 1000);  // own player
    add_shot(*w, 1, plain_def, 1050, 1000); // not targetable
    add_shot(*w, 1, nuke_def, 1000, 1200);  // dz 200 > coverage
    CHECK(find_interceptor_target(*w, unit, 0) == 1);
    unit.weapons[0].stockpile = 0;
    CHECK(find_interceptor_target(*w, unit, 0) == 0);
    unit.weapons[0].stockpile = 2;
    CHECK(find_interceptor_target(*w, unit, 1) == 0);

    // Another shot already intercepting the first passes it over.
    w->projectiles[2].intercept_target = 1;
    CHECK(find_interceptor_target(*w, unit, 0) == 0);
    // Exactly coverage away on an axis is still inside; one more is outside.
    w->projectiles[3].target = {fx(900), 0, fx(1100)};
    CHECK(find_interceptor_target(*w, unit, 0) == 4);
    w->projectiles[3].target = {fx(899), 0, fx(1100)};
    CHECK(find_interceptor_target(*w, unit, 0) == 0);
    w->projectiles[3].target = {fx(1100), 0, fx(1101)};
    CHECK(find_interceptor_target(*w, unit, 0) == 0);
    // The scan is in table order, not by distance.
    w->projectiles[2].intercept_target = 0;
    w->projectiles[3].target = {fx(1001), 0, fx(1000)};
    CHECK(find_interceptor_target(*w, unit, 0) == 1);
    world_destroy(w);
}

// An interceptor launched by another player's simulation finds the
// shot by its exact aim point and the id of the unit that fired it, among
// the other players' shots.
void intercept_target_fired_by() {
    World* w = make_world();
    w->game.local_player_index = 0;
    Unit& enemy = w->units[2];
    enemy.id = 2;
    enemy.owner_index = 1;
    Projectile& own = add_shot(*w, 0, nuke_def, 1050, 1000);
    own.source = 3; // unit 2, but the local player's shot
    Projectile& unfired = add_shot(*w, 1, nuke_def, 1050, 1000);
    unfired.source = 0;
    Projectile& other = add_shot(*w, 1, nuke_def, 1050, 1000);
    other.source = 2; // unit 1
    Projectile& match = add_shot(*w, 1, nuke_def, 1050, 1000);
    match.source = 3;
    const FixedVec3 aim{fx(1050), 0, fx(1000)};
    CHECK(find_intercept_target_fired_by(*w, aim, 2) == 4);
    CHECK(find_intercept_target_fired_by(*w, aim, 1) == 3);
    CHECK(find_intercept_target_fired_by(*w, {fx(1050), 1, fx(1000)}, 2) == 0);
    w->game.projectile_count = 3;
    CHECK(find_intercept_target_fired_by(*w, aim, 2) == 0);
    world_destroy(w);
}

void vertical_launch() {
    World* w = make_world();
    Unit& unit = w->units[1];
    const WeaponDef& interceptor = w->game.weapon_defs[interceptor_def - 1];
    const FixedVector muzzle{fx(1000), fx(10), fx(1000)};
    const FixedVector target{fx(1050), 0, fx(1000)};
    // Until the slot's Aim script returns nonzero (aim_ready), the constructor neither aims
    // nor looks for a missile, even with one in coverage.
    add_shot(*w, 1, nuke_def, 1050, 1000);
    unit.weapons[0].aim_heading = 0x0777;
    unit.weapons[0].aim_pitch = -0x0100;
    auto shot = plan_vertical_launch_shot(*w, interceptor, unit, 0, muzzle, target, 500);
    CHECK(!shot.fired && shot.intercept_target == 0);
    CHECK(shot.slot_aim.heading == 0x0777 && shot.slot_aim.pitch == -0x0100);
    w->game.projectile_count = 0;
    record_aim_result(unit.weapons[0], 1);
    CHECK(unit.weapons[0].aim_ready == 1);
    shot = plan_vertical_launch_shot(*w, interceptor, unit, 0, muzzle, target, 500);
    CHECK(!shot.fired && shot.intercept_target == 0);
    const auto bearing = bearing_toward(muzzle, target);
    CHECK(shot.slot_aim.heading == static_cast<int16_t>(bearing.heading));
    CHECK(shot.slot_aim.pitch == static_cast<int16_t>(bearing.pitch));

    add_shot(*w, 1, nuke_def, 1050, 1000);
    shot = plan_vertical_launch_shot(*w, interceptor, unit, 0, muzzle, target, 500);
    CHECK(shot.fired && shot.intercept_target == 1);
    CHECK(shot.launch.pitch == vertical_launch_pitch && shot.launch.heading == 0);
    CHECK(shot.launch.speed == fx(5) && shot.launch.burst_remaining == 1);
    CHECK(
        shot.launch.velocity[0] == 0 && shot.launch.velocity[1] == 0 && shot.launch.velocity[2] == 0
    );

    WeaponDef plain_vlaunch = interceptor;
    plain_vlaunch.flags = OA_WEAPON_FLAG_VLAUNCH;
    w->game.projectile_count = 0;
    shot = plan_vertical_launch_shot(*w, plain_vlaunch, unit, 0, muzzle, target, 500);
    CHECK(shot.fired && shot.intercept_target == 0);
    world_destroy(w);
}

void record_init() {
    World* w = make_world();
    Unit& source = w->units[1];
    source.flags |= OA_UNIT_FLAG_BUILDING;
    w->game.follow_unit = 2; // ref of slot 1
    Projectile& record = w->projectiles[3];
    std::memset(&record, 0xff, sizeof record);
    record.flags = 0x0031;
    const FixedVec3 muzzle{fx(1000), fx(10), fx(1000)};
    const FixedVec3 target{fx(1050), 0, fx(1000)};
    init_projectile_record(*w, record, interceptor_def, muzzle, &target, 480, &source, 7);
    CHECK(record.def == interceptor_def);
    CHECK(
        record.position.x == fx(1000) && record.position.y == fx(10) &&
        record.position.z == fx(1000)
    );
    CHECK(record.origin.y == fx(10) && record.target.x == fx(1050) && record.target.z == fx(1000));
    CHECK(record.flags == 0);
    CHECK(record.burst_tick == 480 && record.created_tick == 500 && record.burst_remaining == 0);
    CHECK(record.target_unit == 0 && record.intercept_target == 0);
    CHECK(record.source == 2 && record.owner_index == 0 && record.query_piece == 7);
    CHECK(source.decloak_until_tick == 1100);
    CHECK(w->game.follow_target == 4);

    w->game.follow_target = 0;
    source.flags = OA_UNIT_FLAG_LIVE;
    init_projectile_record(*w, record, interceptor_def, muzzle, &target, 480, &source, 7);
    CHECK(w->game.follow_target == 0);

    record.target = {1, 2, 3};
    init_projectile_record(*w, record, plain_def, muzzle, nullptr, 480, nullptr, 9);
    CHECK(record.owner_index == neutral_owner_index && record.source == 0);
    CHECK(record.target.x == 1 && record.target.y == 2 && record.target.z == 3);
    CHECK(record.query_piece == 7);
    world_destroy(w);
}

int32_t flat_height(void* context, int32_t, int32_t) {
    return *static_cast<int32_t*>(context);
}

void aim_point() {
    World* w = make_world();
    add_shot(*w, 1, nuke_def, 1050, 1000);
    Projectile& nuke = w->projectiles[0];
    Projectile& shot = add_shot(*w, 0, plain_def, 1200, 1300);
    int32_t height = 44;

    shot.intercept_target = 1;
    auto aim = projectile_aim_point(*w, shot, flat_height, &height);
    CHECK(aim.x == nuke.position.x && aim.y == nuke.position.y && aim.z == nuke.position.z);

    shot.intercept_target = 0;
    shot.target_unit = 2;
    aim = projectile_aim_point(*w, shot, flat_height, &height);
    CHECK(aim.x == fx(1000) && aim.z == fx(1000));
    w->units[1].flags = 0;
    aim = projectile_aim_point(*w, shot, flat_height, &height);
    CHECK(aim.x == fx(1200) && aim.z == fx(1300));

    shot.def = cruise_def;
    shot.target = {fx(1200), 0x1234, fx(1300)};
    shot.position = {fx(1200), fx(100), fx(1300 + 2000)};
    aim = projectile_aim_point(*w, shot, flat_height, &height);
    CHECK(aim.x == fx(1200) && aim.z == fx(1300));
    CHECK(aim.y == fx(700) + 0x1234);
    CHECK(shot.origin.y == aim.y);

    shot.position = {fx(1200), fx(100), fx(1300 + 100)};
    aim = projectile_aim_point(*w, shot, flat_height, &height);
    CHECK(aim.y == fx(44) && shot.origin.y == fx(44) && shot.origin.x == fx(1200));
    world_destroy(w);
}

// Whole units of the difference, the fraction dropped.
void distances() {
    CHECK(point_distance({fx(3), fx(4), 0}, {0, 0, 0}) == 5);
    CHECK(point_distance({0, 0, 0}, {fx(3), 0, fx(4)}) == 5);
    CHECK(point_distance({fx(10), fx(10), fx(10)}, {fx(10), fx(10), fx(10)}) == 0);
    CHECK(point_distance({fx(1) - 1, 0, 0}, {0, 0, 0}) == 0);
    CHECK(point_distance({fx(-300), 0, fx(400)}, {0, 0, 0}) == 500);
}
} // namespace

int main() {
    distances();
    target_search();
    intercept_target_fired_by();
    vertical_launch();
    record_init();
    aim_point();
    if (failures != 0) {
        std::fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    std::puts("interceptor tests passed");
    return 0;
}
