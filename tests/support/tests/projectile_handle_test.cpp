// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// ProjectileHandle (oa/test/projectile_handle.hpp) stays on its projectile
// while the pool closes its gaps, created_tick moves on and a burst sibling
// copies the projectile's record, and empties once the projectile is gone.
#include "oa/test/check.hpp"
#include "oa/test/projectile_handle.hpp"

#include "oa/sim/weapon_execution/projectile_pool.hpp"

#include <cstdlib>

using namespace oa;
using oa::test::ProjectileHandle;
using namespace oa::sim::weapon_execution;

namespace {

constexpr oa_ref32 gun = 1;
constexpr oa_ref32 rocket = 2;
constexpr oa_ref32 shooter = 7;

// A world with a projectile pool and two weapons.
struct Pool {
    World* world{};

    Pool() {
        world = world_create();
        WorldCapacity capacity{4, 2, 0};
        if (world == nullptr || !world_alloc_tables(world, &capacity))
            std::abort();
    }

    ~Pool() { world_destroy(world); }

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    // Launches a projectile of a weapon from the shooter at a tick.
    Projectile& launch(oa_ref32 weapon, uint32_t tick) {
        Projectile* shot = allocate_projectile(*world);
        if (shot == nullptr)
            std::abort();
        shot->def = weapon;
        shot->source = shooter;
        shot->owner_index = 1;
        shot->created_tick = tick;
        return *shot;
    }

    // Ends a tick: the pool closes its gaps.
    void end_tick() { compact_projectiles(*world); }
};

// Compaction moves the projectile down, and created_tick moving on changes
// nothing.
void follows_through_compaction() {
    Pool pool;
    Projectile& first = pool.launch(gun, 10);
    pool.launch(rocket, 10);
    pool.end_tick();
    ProjectileHandle handle = ProjectileHandle::at(*pool.world, 1);
    OA_CHECK(!handle.empty() && handle.index() == 1);
    OA_CHECK(handle.get(*pool.world) == &pool.world->projectiles[1]);
    pool.world->projectiles[1].created_tick += 3; // a smoke trail puffs
    retire_projectile(*pool.world, first);
    pool.end_tick();
    handle.follow(*pool.world);
    OA_CHECK(handle.index() == 0);
    const Projectile* shot = handle.get(*pool.world);
    OA_CHECK(shot != nullptr && shot->def == rocket && shot->created_tick == 13);
    // A tick that moves nothing leaves the handle where it was.
    pool.end_tick();
    handle.follow(*pool.world);
    OA_CHECK(handle.index() == 0 && handle.get(*pool.world) == shot);
}

// A burst child copies its parent's weapon, source and created_tick; the
// handle stays on the projectile it was taken on.
void tells_burst_siblings_apart() {
    Pool pool;
    pool.launch(rocket, 20);
    pool.launch(gun, 20); // the parent
    pool.end_tick();
    pool.launch(gun, 20); // the child
    ProjectileHandle child = ProjectileHandle::at(*pool.world, 2);
    ProjectileHandle first = ProjectileHandle::first(*pool.world, gun, shooter);
    OA_CHECK(first.index() == 1 && child.index() == 2);
    retire_projectile(*pool.world, pool.world->projectiles[0]);
    pool.end_tick();
    child.follow(*pool.world);
    first.follow(*pool.world);
    OA_CHECK(first.index() == 0 && child.index() == 1);
    retire_projectile(*pool.world, pool.world->projectiles[0]);
    OA_CHECK(first.get(*pool.world) == nullptr && !first.empty()); // retired, not yet closed over
    pool.end_tick();
    child.follow(*pool.world);
    first.follow(*pool.world);
    OA_CHECK(first.empty() && first.get(*pool.world) == nullptr);
    OA_CHECK(child.index() == 0 && child.get(*pool.world) != nullptr);
}

// A projectile of the same weapon and source fired in the tick the handle's
// projectile goes off is not taken for it, even when it moves down into the
// slot the handle's projectile left.
void ignores_later_launches() {
    Pool pool;
    pool.launch(gun, 1);
    pool.launch(gun, 1);
    pool.end_tick();
    ProjectileHandle second = ProjectileHandle::at(*pool.world, 1);
    retire_projectile(*pool.world, pool.world->projectiles[1]);
    pool.launch(gun, 2);
    pool.end_tick();
    OA_CHECK(
        pool.world->game.projectile_count == 2 && pool.world->projectiles[1].created_tick == 2
    );
    second.follow(*pool.world);
    OA_CHECK(second.empty());
}

// Empty handles and handles on no projectile.
void empty_handles() {
    Pool pool;
    OA_CHECK(ProjectileHandle{}.empty() && ProjectileHandle{}.get(*pool.world) == nullptr);
    OA_CHECK(ProjectileHandle::at(*pool.world, 0).empty());
    OA_CHECK(ProjectileHandle::at(*pool.world, -1).empty());
    pool.launch(gun, 1);
    OA_CHECK(ProjectileHandle::first(*pool.world, rocket, shooter).empty());
    OA_CHECK(ProjectileHandle::first(*pool.world, gun, shooter + 1).empty());
    ProjectileHandle handle;
    handle.follow(*pool.world);
    OA_CHECK(handle.empty());
}

} // namespace

int main() {
    follows_through_compaction();
    tells_burst_siblings_apart();
    ignores_later_launches();
    empty_handles();
    return oa::test::check_exit_status();
}
