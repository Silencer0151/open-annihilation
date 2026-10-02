// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Mission meteor showers: a strike of projectiles falling around a random
// target every strike interval.

#include "oa/core/world.h"

#include <stdint.h>

namespace oa::sim::world_environment {

inline constexpr float meteor_ticks_per_second = 30.0f;
// Launch height and speed: 15 world units per tick for 90 ticks.
inline constexpr oa_fixed meteor_fall_speed = static_cast<oa_fixed>(0xfff10000u);
inline constexpr int32_t meteor_fall_ticks = 0x5a;
inline constexpr int32_t meteor_origin_weight = 0xfff;

#pragma pack(push, 1)

// Mission meteor keys (meteorweapon, meteorradius, meteordensity,
// meteorduration, meteorinterval).
struct MeteorSettings {
    char weapon[0x20]{};
    int32_t radius{}; // world units around the target
    float density{};  // hits per second
    float duration{}; // seconds per strike
    float interval{}; // seconds between strikes
};

#pragma pack(pop)
static_assert(sizeof(MeteorSettings) == 0x30);

// The match's meteor shower state.
struct MeteorState {
    uint32_t next_strike_tick{};
    char weapon_name[0x20]{};
    int32_t radius{};
    int32_t hit_interval{}; // ticks between hits
    uint32_t active{};
    uint32_t strike_end_tick{};
    int16_t origin_x{}; // cell the meteors come from
    int16_t origin_z{};
    int32_t duration{}; // ticks per strike
    oa_ref32 weapon{};  // WeaponDef
    uint32_t enabled{};
    uint32_t next_hit_tick{};
    int16_t target_x{}; // cell the strike falls around
    int16_t target_z{};
    int32_t strike_interval{}; // ticks between strikes
};

struct MeteorHost {
    void* context{};
    // rand(), 0..0x7fff.
    int32_t (*lcg_random)(void* context){};
    // WeaponDef ref for a weapon name, 0 when none.
    oa_ref32 (*find_weapon)(void* context, const char* name){};
    void (*spawn_projectile)(
        void* context, oa_ref32 weapon, const FixedVec3* position, const FixedVec3* velocity
    ){};
};

/// Applies the mission's meteor keys.
///
/// Copies the weapon name and radius and converts density, duration and
/// interval to ticks at 30 per second, truncating.
///
/// @param[out] state meteor state to configure
/// @param settings mission meteor keys
void configure_meteors(MeteorState& state, const MeteorSettings& settings) noexcept;
/// Turns meteor storms on: a strike starts each time one falls due.
///
/// @param[in,out] state meteor state
void enable_meteors(MeteorState& state) noexcept;
/// Turns meteor storms off: a strike that falls due is dropped at once.
///
/// @param[in,out] state meteor state
void disable_meteors(MeteorState& state) noexcept;
/// Stops any strike and resolves the weapon.
///
/// The first strike falls due after one hit interval.
///
/// @param[in,out] state meteor state
/// @param world world holding the weapon definitions
/// @param host weapon lookup
/// @quirk A missing or non-meteor weapon falls back to the first weapon definition.
void reset_meteors(MeteorState& state, const World& world, const MeteorHost& host) noexcept;
/// Starts a meteor strike around a random target cell.
///
/// Draws the target (z, then x) and an origin up to 15 cells away from the rand()
/// stream and schedules the strike's end and the next strike.
///
/// @param[in,out] state meteor state
/// @param world world giving the tick and map size
/// @param host rand() stream
void start_meteor_strike(MeteorState& state, const World& world, const MeteorHost& host) noexcept;
/// Runs the meteor schedule for one tick.
///
/// Starts a strike when one falls due (dropping it while storms are off) and,
/// during a strike, launches a projectile each hit interval from above the
/// origin towards a random point within the radius.
///
/// @param[in,out] state meteor state
/// @param world world giving the tick and map size
/// @param host rand() stream and projectile spawner
/// @quirk The velocity's origin * 0xfff + target product wraps in the 20-bit shift to (target - origin) cells over 90 ticks.
void step_meteors(MeteorState& state, const World& world, const MeteorHost& host) noexcept;

} // namespace oa::sim::world_environment
