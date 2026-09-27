// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/weapon_execution/projectile_pool.hpp"

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

constexpr oa_ref32 held_def = 1;

struct Fixture {
    World* w{};

    Fixture() {
        w = world_create();
        WorldCapacity cap{4, 1, 0};
        if (w == nullptr || !world_alloc_tables(w, &cap))
            std::abort();
        w->game.weapon_defs[held_def - 1].hold_time = 45;
    }

    ~Fixture() { world_destroy(w); }

    // A live record at the end of the pool, tagged by its burst tick.
    Projectile& add(uint32_t tag) {
        Projectile* record = allocate_projectile(*w);
        if (record == nullptr)
            std::abort();
        std::memset(record, 0, sizeof *record);
        record->def = held_def;
        record->burst_tick = tag;
        return *record;
    }

    oa_ref32 ref(const Projectile& shot) const {
        return static_cast<oa_ref32>(&shot - w->projectiles) + 1u;
    }
};

void allocation() {
    Fixture f;
    f.w->projectiles[0].target_unit = 7;
    f.w->projectiles[0].flags = OA_PROJECTILE_FLAG_RETIRED | 0x10;
    f.w->projectiles[0].source = 3;
    Projectile* first = allocate_projectile(*f.w);
    CHECK(first == &f.w->projectiles[0] && f.w->game.projectile_count == 1);
    CHECK(first->target_unit == 0 && first->flags == 0x10 && first->source == 3);
    f.w->game.projectile_count = OA_PROJECTILE_CAPACITY - 1;
    CHECK(allocate_projectile(*f.w) == &f.w->projectiles[OA_PROJECTILE_CAPACITY - 1]);
    CHECK(
        allocate_projectile(*f.w) == nullptr && f.w->game.projectile_count == OA_PROJECTILE_CAPACITY
    );
    f.w->game.projectile_count = -1;
    CHECK(allocate_projectile(*f.w) == nullptr);
}

/// Checks a free projectile: an allocated record, initialised with no target and no
/// firing unit (owner byte 10, burst tick = the current tick), then given its velocity.
void free_projectile() {
    Fixture f;
    f.w->game.tick = 77;
    f.w->game.projectile_count = 2;
    f.w->projectiles[2].source = 5;
    f.w->projectiles[2].target = {1, 2, 3};
    const FixedVec3 point{0x100000, 0x5460000, 0x200000};
    const FixedVec3 velocity{0x2000, static_cast<oa_fixed>(0xfff10000u), -0x1000};
    Projectile* shot = spawn_free_projectile(*f.w, held_def, point, velocity);
    CHECK(shot == &f.w->projectiles[2] && f.w->game.projectile_count == 3);
    CHECK(shot->def == held_def && shot->position.x == point.x && shot->origin.y == point.y);
    CHECK(
        shot->velocity.x == velocity.x && shot->velocity.y == velocity.y &&
        shot->velocity.z == velocity.z
    );
    CHECK(shot->owner_index == 10 && shot->source == 0 && shot->target_unit == 0);
    CHECK(shot->burst_tick == 77 && shot->created_tick == 77 && shot->burst_remaining == 0);
    CHECK(shot->target.x == 1 && shot->target.z == 3);
    f.w->game.projectile_count = OA_PROJECTILE_CAPACITY;
    CHECK(spawn_free_projectile(*f.w, held_def, point, velocity) == nullptr);
}

void retirement() {
    Fixture f;
    Projectile& shot = f.add(1);
    shot.position = {0x10000, 0x20000, 0x30000};
    retire_projectile(*f.w, shot);
    CHECK((shot.flags & OA_PROJECTILE_FLAG_RETIRED) != 0);
    CHECK(f.w->game.follow_point_ticks == 0);
    Projectile& followed = f.add(2);
    followed.position = {0x40000, 0x50000, 0x60000};
    f.w->game.follow_target = f.ref(followed);
    retire_projectile(*f.w, followed);
    CHECK(f.w->game.follow_target == 0);
    CHECK(f.w->game.follow_point.x == 0x40000 && f.w->game.follow_point.z == 0x60000);
    CHECK(f.w->game.follow_point_ticks == 45);
}

void compaction() {
    Fixture f;
    Projectile& unmoved = f.add(10);
    Projectile& gap = f.add(11);
    Projectile& mover = f.add(12);
    Projectile& followed = f.add(13);
    Projectile& orphan = f.add(14);
    unmoved.intercept_target = f.ref(followed);
    mover.intercept_target = f.ref(unmoved);
    orphan.intercept_target = f.ref(gap);
    f.w->game.follow_target = f.ref(followed);
    gap.flags = OA_PROJECTILE_FLAG_RETIRED;
    compact_projectiles(*f.w);
    Projectile* pool = f.w->projectiles;
    CHECK(f.w->game.projectile_count == 4);
    CHECK(pool[0].burst_tick == 10 && pool[1].burst_tick == 12 && pool[2].burst_tick == 13);
    CHECK(pool[3].burst_tick == 14);
    CHECK(pool[1].compact_index == 2 && pool[3].compact_index == 4);
    CHECK(f.w->game.follow_target == 3);
    // A moved interceptor follows its target; the unmoved one keeps the old slot.
    CHECK(pool[1].intercept_target == 1);
    CHECK(pool[0].intercept_target == 4);
    // A retired target leaves the reference as it was.
    CHECK(pool[3].intercept_target == 2);
}

void burst_spawners() {
    Fixture f;
    constexpr oa_ref32 dead = 2;
    Projectile& first = f.add(1);
    Projectile& second = f.add(2);
    Projectile& flying = f.add(3);
    Projectile& other = f.add(4);
    first.burst_remaining = second.burst_remaining = 2;
    first.source = second.source = flying.source = dead;
    other.burst_remaining = 1;
    other.source = 3;
    cancel_burst_spawners(*f.w, dead);
    // The second spawner slides into the first one's slot and is passed over.
    CHECK(f.w->game.projectile_count == 3);
    CHECK(f.w->projectiles[0].burst_tick == 2 && f.w->projectiles[0].burst_remaining == 2);
    CHECK(f.w->projectiles[1].burst_tick == 3 && f.w->projectiles[2].burst_tick == 4);
}
} // namespace

int main() {
    allocation();
    free_projectile();
    retirement();
    compaction();
    burst_spawners();
    if (failures != 0)
        return EXIT_FAILURE;
    std::puts("projectile pool passed");
    return EXIT_SUCCESS;
}
