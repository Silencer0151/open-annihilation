// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/core/projectile.h"
#include "oa/core/world.h"
#include "oa/sim/weapon_execution/weapon_launch.hpp"

#include <cstdint>

// Interceptor weapons (TDF interceptor=1 on a vlaunch weapon), the projectile
// records they launch, and the aim point guided shots steer toward, over the
// canonical World tables.
namespace oa::sim::weapon_execution {

inline constexpr uint8_t neutral_owner_index = 10; // owner byte of a shot no unit fired
inline constexpr uint32_t fire_decloak_ticks = 600;
inline constexpr int32_t cruise_descent_distance = 0x400;
inline constexpr int32_t cruise_altitude = 700;

/// Finds the first live projectile a stockpiled interceptor slot may shoot down.
///
/// It must be fired by another player, of a targetable weapon, aimed within the weapon's
/// coverage square of the unit (measured on the shot's aim point, not its position),
/// and not already named by another shot's intercept_target.
///
/// @param world projectile pool
/// @param unit interceptor unit
/// @param slot weapon slot 0..2
/// @return the projectile, or 0 when none
[[nodiscard]]
oa_ref32 find_interceptor_target(const World& world, const Unit& unit, uint8_t slot) noexcept;

/// Finds the shot an interceptor launched by another player's simulation homes on.
///
/// The first record below the live count that a player other than the local one fired,
/// aimed at exactly `target`, by the unit whose id is `fired_by`; a record no unit
/// fired is passed over.
///
/// @param world projectile pool
/// @param target the interceptor's aim point, 16.16 world coordinates
/// @param fired_by unit id that fired the intercepted shot
/// @return the projectile, or 0 when none
[[nodiscard]]
oa_ref32 find_intercept_target_fired_by(
    const World& world, const FixedVec3& target, uint16_t fired_by
) noexcept;

struct VerticalLaunchShot {
    bool fired{};
    ProjectileLaunch launch{};
    AimAngles slot_aim{}; // written back to the weapon slot
    // The projectile the launched interceptor homes on, for its
    // Projectile.intercept_target; 0 for none.
    oa_ref32 intercept_target{};
};

/// Plans a shot of the vertical-launch constructor.
///
/// Nothing launches, and the slot aim stays, until the slot's Aim script has returned
/// nonzero (aim_ready). Then the slot aims along the muzzle-to-target bearing, and an
/// interceptor weapon needs a projectile to intercept or fires nothing. The shot
/// leaves straight up with no velocity and carries the intercepted projectile for
/// guidance. Clearing the slot's aim bit and aim_ready after the launch, and sharing
/// the launch with the other players, stay with the caller.
///
/// @param world projectile pool, for the interceptor search
/// @param weapon weapon definition
/// @param unit firing unit
/// @param slot weapon slot 0..2
/// @param muzzle launch point, 16.16 world coordinates
/// @param target aim point, 16.16 world coordinates
/// @param current_tick current game tick
/// @return whether it fired, the launch, the slot aim and the intercepted projectile
[[nodiscard]]
VerticalLaunchShot plan_vertical_launch_shot(
    const World& world,
    const WeaponDef& weapon,
    const Unit& unit,
    uint8_t slot,
    const FixedVector& muzzle,
    const FixedVector& target,
    uint32_t current_tick
) noexcept;

/// Fills a freshly allocated projectile record.
///
/// Sets position and origin at the muzzle, the aim point when given, both ticks, a
/// cleared burst count, phase bits and target links, and the owner. A firing unit holds
/// its cloak off for 600 ticks; when it is the followed building the camera follows the
/// shot. The start sound stays with the caller.
///
/// @param[in,out] world game state (camera follow)
/// @param[out] record projectile record to fill
/// @param weapon_def weapon definition reference
/// @param muzzle launch point, 16.16 world coordinates
/// @param target aim point, or null for none
/// @param burst_tick tick the burst spawner starts from
/// @param source firing unit, or null for a shot no unit fired
/// @param query_piece the slot's QueryWeapon piece
void init_projectile_record(
    World& world,
    Projectile& record,
    oa_ref32 weapon_def,
    const FixedVec3& muzzle,
    const FixedVec3* target,
    uint32_t burst_tick,
    Unit* source,
    uint16_t query_piece
) noexcept;

using SurfaceHeight = int32_t (*)(void* context, int32_t x, int32_t z); // terrain surface height

/// Returns the whole units from one 16.16 point to another.
///
/// @param from start point, 16.16 world coordinates
/// @param to end point, 16.16 world coordinates
/// @return the high word of the truncated length of their wrapping difference, as a
///         signed word
[[nodiscard]] int32_t point_distance(const FixedVec3& from, const FixedVec3& to) noexcept;

/// Returns the point a guided projectile steers toward.
///
/// The intercepted projectile's position, else the target unit's position while it is
/// live, else the stored aim point. A cruise weapon farther than 0x400 units from its
/// aim point flies at 700 over it; nearer, it descends to the surface height there.
///
/// @param world projectile pool and units
/// @param[in,out] projectile guided projectile; both cruise branches rewrite its origin
/// @param surface_height terrain surface height query
/// @param context passed to `surface_height`
/// @return the aim point, 16.16 world coordinates
[[nodiscard]]
FixedVec3 projectile_aim_point(
    const World& world, Projectile& projectile, SurfaceHeight surface_height, void* context
) noexcept;

} // namespace oa::sim::weapon_execution
