// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <array>
#include <cstdint>

namespace oa::sim::ballistics {

// Bits of WeaponDef.flags the reach tests read.
inline constexpr uint32_t line_of_sight_weapon_flag = 0x00000001U;
inline constexpr uint32_t ballistic_weapon_flag = 0x00000002U;
inline constexpr uint32_t water_weapon_flag = 0x00010000U;
inline constexpr uint32_t to_air_weapon_flag = 0x00020000U;
// The occupancy field of Unit.flags (OA_UNIT_FLAG_OCCUPANCY_MASK), and the value a
// to-air weapon's target must hold there.
inline constexpr uint32_t target_air_state_mask = 0x00000003U;
inline constexpr uint32_t target_air_state = 0x00000002U;
// Bits of UnitDef.flags: FBI floater and canhover.
inline constexpr uint32_t target_type_floats_flag = 0x00080000U;
inline constexpr uint32_t target_type_hover_flag = 0x00001000U;

struct BallisticParameters {
    int32_t projectile_velocity{};        // WeaponDef.weapon_velocity
    float minimum_barrel_angle_radians{}; // WeaponDef.min_barrel_angle
    int32_t simulation_gravity{};         // Game.gravity
};

struct WeaponReachParameters {
    uint32_t weapon_flags{};     // WeaponDef.flags
    int32_t range_world_units{}; // WeaponDef.range
    BallisticParameters ballistic{};
};

struct ReachUnitGeometry {
    std::array<uint32_t, 3> position{}; // Unit.position, signed 16.16 bit patterns
    int16_t model_maximum_y{};          // UnitDef.model_height's whole world units
    uint32_t unit_flags{};              // Unit.flags
    uint32_t type_flags{};              // UnitDef.flags
};

// Game.gravity when neither the campaign nor the map sets one (112 units/s^2).
inline constexpr int32_t default_simulation_gravity = 0x1fdb;

/// Returns Game.gravity for a new match.
///
/// A setting g becomes truncate(g * 65536 / 900).
///
/// @param campaign_gravity_applies true when the campaign sets the gravity
/// @param campaign_gravity campaign gravity setting; used when it applies and is not negative
/// @param map_gravity the map's OTA gravity setting; used when nonzero
/// @return gravity in 16.16 world units per tick squared, or default_simulation_gravity
[[nodiscard]] int32_t simulation_gravity(
    bool campaign_gravity_applies, int32_t campaign_gravity, int32_t map_gravity
) noexcept;

// Pitch the launch solver reports when no angle reaches the target.
inline constexpr int16_t invalid_launch_pitch = static_cast<int16_t>(0x8000);

/// Solves the ballistic launch pitch that reaches a target.
///
/// The first root of the trajectory equation is taken when its angle lies
/// above the minimum barrel angle and at most a quarter of pi, else the second
/// root under the same bounds. Each intermediate is rounded to binary64 as in
/// 3.1c; sqrt, hypot and acos come from the host library.
///
/// @param parameters projectile velocity, minimum barrel angle and gravity
/// @param dx wrapped AimFrom-minus-target x delta, 16.16 world units
/// @param dy wrapped AimFrom-minus-target y delta, 16.16 world units
/// @param dz wrapped AimFrom-minus-target z delta, 16.16 world units
/// @return binary-angle pitch (65536 per turn), or invalid_launch_pitch
/// @quirk A NaN angle or minimum passes the first root's bounds test but fails
///        the second's.
[[nodiscard]] int16_t
launch_pitch(const BallisticParameters& parameters, int32_t dx, int32_t dy, int32_t dz) noexcept;

/// Tests whether a ballistic weapon has a launch pitch from source to target.
///
/// @param parameters projectile velocity, minimum barrel angle and gravity
/// @param source launch position, signed 16.16 bit patterns
/// @param target target position, signed 16.16 bit patterns
/// @return true when launch_pitch is not invalid_launch_pitch
[[nodiscard]] bool ballistic_feasible(
    const BallisticParameters& parameters,
    const std::array<uint32_t, 3>& source,
    const std::array<uint32_t, 3>& target
) noexcept;

/// Tests whether a weapon on one unit can reach another unit.
///
/// A non-water weapon needs both models' tops above sea level, a to-air weapon
/// an airborne target and a ballistic weapon a launch pitch. A water weapon
/// rejects a target above sea level unless its type floats, and a
/// hovering-type target whose half-height point is above sea level. The
/// horizontal range is tested last.
///
/// @param weapon weapon flags, range and ballistic parameters
/// @param source firing unit
/// @param target target unit
/// @param sea_level sea level in integral world units
/// @return true when every test passes
[[nodiscard]] bool weapon_can_reach(
    const WeaponReachParameters& weapon,
    const ReachUnitGeometry& source,
    const ReachUnitGeometry& target,
    uint8_t sea_level
) noexcept;

/// Tests whether a weapon on a unit can fire at a point.
///
/// Horizontal range is tested first. A non-water weapon also needs the source
/// model top above sea level, and a ballistic weapon a launch pitch. Target
/// height, air state and water-target type flags are not read.
///
/// @param weapon weapon flags, range and ballistic parameters
/// @param source firing unit
/// @param target target position, signed 16.16 bit patterns
/// @param sea_level sea level in integral world units
/// @return true when every test passes
[[nodiscard]] bool fire_can_reach(
    const WeaponReachParameters& weapon,
    const ReachUnitGeometry& source,
    const std::array<uint32_t, 3>& target,
    uint8_t sea_level
) noexcept;

// Turret aim input. The target and aim_from positions become wrapped 16.16
// deltas before the heading and pitch are computed.
struct TurretAimGeometry {
    std::array<int32_t, 3> aim_from{}; // AimFrom piece world position
    std::array<int32_t, 3> target{};   // target world position
    int16_t unit_yaw{};                // Unit.heading
};

struct TurretAimResult {
    int16_t heading{}; // base::game_math::direction(dx, dz) - unit_yaw, low 16 bits
    int16_t pitch{};   // launch pitch
    bool feasible{};   // pitch != invalid_launch_pitch
};

/// Aims a ballistic turret at a target.
///
/// @param parameters projectile velocity, minimum barrel angle and gravity
/// @param geometry AimFrom position, target position and unit yaw
/// @return heading relative to the unit, launch pitch, and whether a pitch was found
[[nodiscard]] TurretAimResult turret_aim_ballistic(
    const BallisticParameters& parameters, const TurretAimGeometry& geometry
) noexcept;

/// Aims a line-of-sight turret at a target.
///
/// The pitch is the direction of the wrapped dy high word over the high word
/// of the horizontal range.
///
/// @param geometry AimFrom position, target position and unit yaw
/// @return heading relative to the unit and elevation pitch; always feasible
[[nodiscard]] TurretAimResult turret_aim_line_of_sight(const TurretAimGeometry& geometry) noexcept;

/// Aims a turret with the solver its weapon flags select.
///
/// The ballistic flag is tested before the line-of-sight flag.
///
/// @param weapon_flags weapon definition flags
/// @param parameters projectile velocity, minimum barrel angle and gravity
/// @param geometry AimFrom position, target position and unit yaw
/// @return the solver's result, or an infeasible zero result for a weapon with neither flag
[[nodiscard]] TurretAimResult turret_aim(
    uint32_t weapon_flags, const BallisticParameters& parameters, const TurretAimGeometry& geometry
) noexcept;

} // namespace oa::sim::ballistics
