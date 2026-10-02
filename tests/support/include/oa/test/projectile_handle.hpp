// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A test's handle on one projectile of a world's pool, which stays on that
// projectile from tick to tick. Link oa-test-match, or oa-test-support and
// oa-core-types.
#pragma once

#include "oa/core/projectile.h"
#include "oa/core/world.h"

#include <cstdint>

namespace oa::test {

/// Follows one projectile of a world's pool from tick to tick.
///
/// No field of the record names a projectile for its whole flight: the end
/// of every tick closes the pool's gaps, moving the later projectiles to
/// lower indices; Projectile.created_tick moves on after launch, by the
/// weapon's smoke delay each time its smoke trail puffs; a cruise missile
/// rewrites its origin; and a burst's children copy their parent's weapon,
/// source, origin and created_tick. The handle keeps the projectile's index
/// and what never changes in flight (its weapon, source unit and owner),
/// and follow() finds it again where the closing moved it, by the index
/// each record notes there (Projectile.compact_index).
class ProjectileHandle {
  public:

    /// Makes an empty handle, which names no projectile.
    ProjectileHandle() = default;

    /// Takes a handle on the projectile at a pool index.
    ///
    /// @param world the pool
    /// @param index the projectile's index now
    /// @return the handle; empty when the index is not below the live count
    [[nodiscard]] static ProjectileHandle at(const World& world, int32_t index) noexcept {
        ProjectileHandle handle;
        if (index < 0 || index >= world.game.projectile_count)
            return handle;
        const Projectile& shot = world.projectiles[index];
        handle.index_ = index;
        handle.weapon_ = shot.def;
        handle.source_ = shot.source;
        handle.owner_ = shot.owner_index;
        return handle;
    }

    /// Takes a handle on the first projectile in the pool, retired ones left
    /// out, of a weapon that a unit fired.
    ///
    /// @param world the pool
    /// @param weapon the projectile's weapon (Projectile.def)
    /// @param source the unit that fired it (Projectile.source)
    /// @return the handle; empty when there is no such projectile
    [[nodiscard]] static ProjectileHandle
    first(const World& world, oa_ref32 weapon, oa_ref32 source) noexcept {
        for (int32_t index = 0; index < world.game.projectile_count; ++index) {
            const Projectile& shot = world.projectiles[index];
            if (shot.def == weapon && shot.source == source &&
                (shot.flags & OA_PROJECTILE_FLAG_RETIRED) == 0)
                return at(world, index);
        }
        return {};
    }

    /// Finds the projectile again after one tick: the record whose
    /// compact_index is the handle's index and whose weapon, source and
    /// owner are the projectile's. Follow after every tick, since each tick
    /// closes the pool's gaps once; a projectile that left the pool, or one
    /// lost to a tick that was not followed, empties the handle.
    ///
    /// Units fire before the tick closes the pool's gaps, so their new
    /// projectiles carry their own compact_index. A record placed after
    /// that (the meteor storm's, or a shot applied between ticks) keeps the
    /// compact_index of the slot it took, and when it takes the very slot
    /// the handle's projectile left, with the same weapon, source and owner,
    /// it is taken for that projectile.
    ///
    /// @param world the pool
    void follow(const World& world) noexcept {
        if (index_ < 0)
            return;
        const int32_t was = index_;
        index_ = -1;
        // Closing the gaps moves a record only down.
        for (int32_t index = 0; index <= was && index < world.game.projectile_count; ++index) {
            const Projectile& shot = world.projectiles[index];
            if (shot.compact_index == was && launched_as(shot)) {
                index_ = index;
                return;
            }
        }
    }

    /// Returns the projectile.
    ///
    /// @param world the pool
    /// @return the record; null when the handle is empty or the projectile
    ///     is retired
    [[nodiscard]] const Projectile* get(const World& world) const noexcept {
        if (index_ < 0 || index_ >= world.game.projectile_count)
            return nullptr;
        const Projectile& shot = world.projectiles[index_];
        if (!launched_as(shot) || (shot.flags & OA_PROJECTILE_FLAG_RETIRED) != 0)
            return nullptr;
        return &shot;
    }

    /// Returns the projectile, to change it.
    ///
    /// @param world the pool
    /// @return the record; null when the handle is empty or the projectile
    ///     is retired
    [[nodiscard]] Projectile* get(World& world) const noexcept {
        return const_cast<Projectile*>(get(static_cast<const World&>(world)));
    }

    /// Tells whether the handle names no projectile.
    ///
    /// @return true for an empty handle, or one whose projectile left the
    ///     pool
    [[nodiscard]] bool empty() const noexcept { return index_ < 0; }

    /// Returns the projectile's index in the pool.
    ///
    /// @return the index; -1 for an empty handle
    [[nodiscard]] int32_t index() const noexcept { return index_; }

  private:

    /// Tells whether a record holds what the projectile's launch fixed.
    ///
    /// @param shot the record
    /// @return true when its weapon, source and owner are the projectile's
    [[nodiscard]] bool launched_as(const Projectile& shot) const noexcept {
        return shot.def == weapon_ && shot.source == source_ && shot.owner_index == owner_;
    }

    int32_t index_ = -1;
    oa_ref32 weapon_{};
    oa_ref32 source_{};
    uint8_t owner_{};
};

} // namespace oa::test
