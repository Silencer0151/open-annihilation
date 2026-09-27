// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/core/projectile.h"
#include "oa/core/world.h"

#include <cstdint>

// The game state's fixed pool of projectile records: allocation at the live
// count, retirement, cancelling a dead unit's burst spawners, and the
// end-of-tick compaction.
namespace oa::sim::weapon_execution {

/// Allocates the projectile record at the live count, which grows by one.
///
/// Only the target unit and the retired bit are cleared.
///
/// @param[in,out] world projectile pool
/// @return the record, or null when the pool is full
[[nodiscard]] Projectile* allocate_projectile(World& world) noexcept;

/// Spawns a projectile no unit fired (the meteor storm's), owned by no player.
///
/// Sharing it with the other players is the caller's.
///
/// @param[in,out] world projectile pool
/// @param weapon_def weapon definition reference
/// @param position start point, 16.16 world coordinates
/// @param velocity 16.16 per tick
/// @return the record, or null when the pool is full
Projectile* spawn_free_projectile(
    World& world, oa_ref32 weapon_def, const FixedVec3& position, const FixedVec3& velocity
) noexcept;

/// Marks a projectile record retired.
///
/// When the camera follows it, the camera keeps its position for the weapon's hold time
/// and stops following.
///
/// @param[in,out] world camera follow state
/// @param[in,out] projectile record to retire
void retire_projectile(World& world, Projectile& projectile) noexcept;

/// Moves the live records down over the retired ones, in order, and shrinks the live count.
///
/// The camera's follow target moves with its record. A moved record's intercept_target
/// is rewritten to where its target now sits.
///
/// @param[in,out] world projectile pool and camera follow state
/// @quirk A record before the first retired one keeps its reference even when its target
///        moved, and a reference to a retired record is left as it was.
void compact_projectiles(World& world) noexcept;

/// Retires and compacts away every burst spawner a unit fired.
///
/// @param[in,out] world projectile pool
/// @param source unit whose spawners are cancelled
/// @quirk The scan continues at the next index after each compaction, so the record that
///        slid into a cancelled spawner's place is not examined.
void cancel_burst_spawners(World& world, oa_ref32 source) noexcept;

} // namespace oa::sim::weapon_execution
