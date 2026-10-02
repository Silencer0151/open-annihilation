// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/world_environment/meteor.hpp"
#include "oa/base/text.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

using namespace oa;
using namespace oa::sim::world_environment;

int failures = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

struct Script {
    std::vector<int32_t> rolls;
    size_t next{};
    oa_ref32 found{};
    std::vector<FixedVec3> positions;
    std::vector<FixedVec3> velocities;
    oa_ref32 fired{};
};

MeteorHost make_host(Script& script) {
    MeteorHost host{};
    host.context = &script;
    host.lcg_random = [](void* context) -> int32_t {
        auto* s = static_cast<Script*>(context);
        return s->next < s->rolls.size() ? s->rolls[s->next++] : 0;
    };
    host.find_weapon = [](void* context, const char*) {
        return static_cast<Script*>(context)->found;
    };
    host.spawn_projectile =
        [](void* context, oa_ref32 weapon, const FixedVec3* position, const FixedVec3* velocity) {
            auto* s = static_cast<Script*>(context);
            s->fired = weapon;
            s->positions.push_back(*position);
            s->velocities.push_back(*velocity);
        };
    return host;
}

void test_configure_and_reset() {
    MeteorSettings settings{};
    oa::base::text::copy_terminated(settings.weapon, "METEOR");
    settings.radius = 200;
    settings.density = 4.0f;
    settings.duration = 10.0f;
    settings.interval = 60.5f;
    MeteorState state{};
    configure_meteors(state, settings);
    require(
        state.hit_interval == 7 && state.duration == 300 && state.strike_interval == 1815 &&
            state.radius == 200,
        "settings converted to ticks by truncation"
    );

    auto* world = world_create();
    Script script;
    const auto host = make_host(script);
    script.found = 3;
    reset_meteors(state, *world, host);
    require(
        state.weapon == 1 && state.next_strike_tick == 7 && state.active == 0,
        "non-meteor weapon falls back"
    );
    world->game.weapon_defs[2].flags = OA_WEAPON_FLAG_METEOR;
    reset_meteors(state, *world, host);
    require(state.weapon == 3, "meteor weapon resolved");
    world_destroy(world);
}

void test_strike() {
    auto* world = world_create();
    world->game.map_width = 64;
    world->game.map_height = 32;
    world->game.tick = 100;
    MeteorState state{};
    state.enabled = 1;
    state.hit_interval = 5;
    state.duration = 12;
    state.strike_interval = 50;
    state.radius = 0;
    state.weapon = 9;
    state.next_strike_tick = 100;
    Script script;
    // Strike: z, x, dz, dx; first hit: distance, angle.
    script.rolls = {0x4000, 0x4000, 0x4000, 0x4000, 0, 0};
    const auto host = make_host(script);
    step_meteors(state, *world, host);
    require(state.target_z == 16 && state.target_x == 32, "target from map size");
    require(
        state.origin_z == 16 + 5 - 15 && state.origin_x == 32 + 15 - 15,
        "origin offset north of target"
    );
    require(state.strike_end_tick == 112 && state.next_strike_tick == 162, "strike schedule");
    require(script.positions.size() == 1 && script.fired == 9, "first hit fires at once");
    require(state.next_hit_tick == 105, "next hit after the hit interval");
    const auto& velocity = script.velocities[0];
    require(
        velocity.x == 0 && velocity.z == (10 << 20) / 90 && velocity.y == meteor_fall_speed,
        "velocity carries the meteor from origin to target in 90 ticks"
    );
    const auto& position = script.positions[0];
    require(
        position.x == (32 << 20) && position.z == (6 << 20) && position.y == 1350 << 16,
        "launch point above the origin"
    );
    world->game.tick = 104;
    step_meteors(state, *world, host);
    require(script.positions.size() == 1, "waits for the hit interval");
    world->game.tick = 112;
    step_meteors(state, *world, host);
    require(script.positions.size() == 2 && state.active == 0, "strike ends at its end tick");

    MeteorState disabled{};
    disabled.next_strike_tick = 0;
    step_meteors(disabled, *world, host);
    require(
        disabled.active == 0 && script.positions.size() == 2, "disabled meteors only reschedule"
    );
    world_destroy(world);
}

// enable_meteors stores 1 and disable_meteors stores 0 in the storm switch
// and touch nothing else.
void test_storm_switch() {
    MeteorState state{};
    state.active = 1;
    state.next_strike_tick = 40;
    enable_meteors(state);
    require(state.enabled == 1 && state.active == 1 && state.next_strike_tick == 40, "storms on");
    enable_meteors(state);
    require(state.enabled == 1, "storms stay on");
    disable_meteors(state);
    require(state.enabled == 0 && state.active == 1 && state.next_strike_tick == 40, "storms off");
}

} // namespace

int main() {
    test_configure_and_reset();
    test_storm_switch();
    test_strike();
    if (failures != 0)
        return EXIT_FAILURE;
    std::puts("meteors: ok");
    return EXIT_SUCCESS;
}
