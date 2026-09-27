// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/world_environment/meteor.hpp"

#include "oa/sim/unit_movement/movement.hpp"

#include <cstring>

namespace oa::sim::world_environment {
namespace {

constexpr int64_t lcg_rand_range = 0x8000;
constexpr int32_t cell_shift = 20;
constexpr int32_t origin_spread_x = 30;
constexpr int32_t origin_spread_z = 10;
constexpr int32_t origin_offset = 15;
constexpr int32_t full_turn = 0x10000;

int32_t lcg(const MeteorHost& host) noexcept {
    return host.lcg_random != nullptr ? host.lcg_random(host.context) : 0;
}

// rand() * range / 32768 with a 64-bit product, truncated toward zero.
int32_t scaled_rand(const MeteorHost& host, int64_t range) noexcept {
    return static_cast<int32_t>(static_cast<int64_t>(lcg(host)) * range / lcg_rand_range);
}

// Seconds to ticks at 30 per second, truncated toward zero as in 3.1c.
int32_t ticks(float seconds) noexcept {
    return static_cast<int32_t>(
        static_cast<int64_t>(static_cast<long double>(seconds) * meteor_ticks_per_second)
    );
}

int32_t shifted_cell(int32_t cell) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(cell) << cell_shift);
}

} // namespace

void configure_meteors(MeteorState& state, const MeteorSettings& settings) noexcept {
    std::memcpy(state.weapon_name, settings.weapon, sizeof(state.weapon_name));
    state.weapon_name[sizeof(state.weapon_name) - 1] = '\0';
    state.radius = settings.radius;
    state.hit_interval = static_cast<int32_t>(
        static_cast<int64_t>(static_cast<long double>(meteor_ticks_per_second) / settings.density)
    );
    state.duration = ticks(settings.duration);
    state.strike_interval = ticks(settings.interval);
}

void enable_meteors(MeteorState& state) noexcept {
    state.enabled = 1;
}

void disable_meteors(MeteorState& state) noexcept {
    state.enabled = 0;
}

void reset_meteors(MeteorState& state, const World& world, const MeteorHost& host) noexcept {
    state.active = 0;
    state.next_strike_tick = static_cast<uint32_t>(state.hit_interval);
    state.weapon =
        host.find_weapon != nullptr ? host.find_weapon(host.context, state.weapon_name) : 0;
    const auto* weapon = world_weapon_def(&world, state.weapon);
    if (weapon == nullptr || (weapon->flags & OA_WEAPON_FLAG_METEOR) == 0)
        state.weapon = oa_ref_from_index(0);
}

void start_meteor_strike(MeteorState& state, const World& world, const MeteorHost& host) noexcept {
    const auto tick = world.game.tick;
    state.active = 1;
    state.strike_end_tick = static_cast<uint32_t>(state.duration) + tick;
    state.next_strike_tick = static_cast<uint32_t>(state.strike_interval) + state.strike_end_tick;
    state.next_hit_tick = tick;
    state.target_z = static_cast<int16_t>(scaled_rand(host, world.game.map_height));
    state.target_x = static_cast<int16_t>(scaled_rand(host, world.game.map_width));
    const auto dz = scaled_rand(host, origin_spread_z) - origin_offset;
    const auto dx = scaled_rand(host, origin_spread_x) - origin_offset;
    state.origin_x = static_cast<int16_t>(state.target_x + dx);
    state.origin_z = static_cast<int16_t>(state.target_z + dz);
}

void step_meteors(MeteorState& state, const World& world, const MeteorHost& host) noexcept {
    const auto tick = world.game.tick;
    if (state.next_strike_tick <= tick) {
        start_meteor_strike(state, world, host);
        if (state.enabled == 0)
            state.active = 0;
    }
    if (state.active == 0)
        return;
    if (state.next_hit_tick <= tick) {
        state.next_hit_tick = static_cast<uint32_t>(state.hit_interval) + tick;
        FixedVec3 velocity{};
        velocity.x = shifted_cell(state.origin_x * meteor_origin_weight + state.target_x) /
                     meteor_fall_ticks;
        velocity.y = meteor_fall_speed;
        velocity.z = shifted_cell(state.origin_z * meteor_origin_weight + state.target_z) /
                     meteor_fall_ticks;
        const auto distance =
            static_cast<oa_fixed>(static_cast<uint32_t>(scaled_rand(host, state.radius)) << 16);
        const auto angle = static_cast<uint16_t>(scaled_rand(host, full_turn));
        FixedVec3 position{};
        position.x = static_cast<oa_fixed>(
            static_cast<uint32_t>(shifted_cell(state.origin_x)) -
            static_cast<uint32_t>(sim::unit_movement::sine_scaled(angle, distance))
        );
        position.y = -meteor_fall_speed * meteor_fall_ticks;
        position.z = static_cast<oa_fixed>(
            static_cast<uint32_t>(shifted_cell(state.origin_z)) -
            static_cast<uint32_t>(sim::unit_movement::cosine_scaled(angle, distance))
        );
        if (host.spawn_projectile != nullptr)
            host.spawn_projectile(host.context, state.weapon, &position, &velocity);
    }
    if (state.strike_end_tick <= tick)
        state.active = 0;
}

} // namespace oa::sim::world_environment
