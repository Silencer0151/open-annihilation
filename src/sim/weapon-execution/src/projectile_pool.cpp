// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/weapon_execution/projectile_pool.hpp"

#include "oa/core/weapon_def.h"
#include "oa/sim/weapon_execution/interceptor.hpp"

namespace oa::sim::weapon_execution {

namespace {

int32_t live_count(const World& world) noexcept {
    const int32_t count = world.game.projectile_count;
    if (world.projectiles == nullptr || count < 0)
        return 0;
    return count > OA_PROJECTILE_CAPACITY ? OA_PROJECTILE_CAPACITY : count;
}

oa_ref32 pool_ref(const World& world, const Projectile& shot) noexcept {
    if (world.projectiles == nullptr || &shot < world.projectiles ||
        &shot >= world.projectiles + OA_PROJECTILE_CAPACITY)
        return 0;
    return static_cast<oa_ref32>(&shot - world.projectiles) + 1u;
}

} // namespace

Projectile* allocate_projectile(World& world) noexcept {
    const int32_t count = world.game.projectile_count;
    if (world.projectiles == nullptr || count < 0 || count >= OA_PROJECTILE_CAPACITY)
        return nullptr;
    Projectile& record = world.projectiles[count];
    world.game.projectile_count = count + 1;
    record.target_unit = 0;
    record.flags = static_cast<uint16_t>(record.flags & ~OA_PROJECTILE_FLAG_RETIRED);
    return &record;
}

Projectile* spawn_free_projectile(
    World& world, oa_ref32 weapon_def, const FixedVec3& position, const FixedVec3& velocity
) noexcept {
    Projectile* shot = allocate_projectile(world);
    if (shot == nullptr)
        return nullptr;
    init_projectile_record(
        world, *shot, weapon_def, position, nullptr, world.game.tick, nullptr, 0
    );
    shot->velocity = velocity;
    return shot;
}

void retire_projectile(World& world, Projectile& shot) noexcept {
    const oa_ref32 ref = pool_ref(world, shot);
    if (ref != 0 && world.game.follow_target == ref) {
        world.game.follow_point = shot.position;
        const WeaponDef* def = world_weapon_def(&world, shot.def);
        world.game.follow_point_ticks = def != nullptr ? def->hold_time : int16_t{};
        world.game.follow_target = 0;
    }
    shot.flags = static_cast<uint16_t>(shot.flags | OA_PROJECTILE_FLAG_RETIRED);
}

void compact_projectiles(World& world) noexcept {
    const int32_t count = live_count(world);
    Projectile* pool = world.projectiles;
    // A moved record that intercepts: where it now sits, and its target's index before the move.
    int16_t moved_to[OA_PROJECTILE_CAPACITY];
    int16_t target_was[OA_PROJECTILE_CAPACITY];
    int32_t pairs = 0;
    int32_t destination = -1;
    int32_t live = count;
    for (int32_t index = 0; index < count; ++index) {
        Projectile& shot = pool[index];
        shot.compact_index = static_cast<int16_t>(index);
        if ((shot.flags & OA_PROJECTILE_FLAG_RETIRED) != 0) {
            if (destination < 0)
                destination = index;
            --live;
            continue;
        }
        if (destination < 0)
            continue;
        if (world.game.follow_target == oa_ref_from_index(static_cast<uint32_t>(index)))
            world.game.follow_target = oa_ref_from_index(static_cast<uint32_t>(destination));
        if (shot.intercept_target != 0) {
            moved_to[pairs] = static_cast<int16_t>(destination);
            target_was[pairs] = static_cast<int16_t>(shot.intercept_target - 1u);
            ++pairs;
        }
        pool[destination] = shot;
        ++destination;
    }
    int32_t patched = 0;
    for (int32_t index = 0; index < live && patched < pairs; ++index) {
        for (int32_t pair = 0; pair < pairs && patched < pairs; ++pair) {
            if (pool[index].compact_index != target_was[pair])
                continue;
            pool[moved_to[pair]].intercept_target = oa_ref_from_index(static_cast<uint32_t>(index));
            ++patched;
        }
    }
    if (pool != nullptr)
        world.game.projectile_count = live;
}

void cancel_burst_spawners(World& world, oa_ref32 source) noexcept {
    for (int32_t index = 0; index < live_count(world); ++index) {
        Projectile& shot = world.projectiles[index];
        if (shot.burst_remaining == 0 || shot.source != source)
            continue;
        retire_projectile(world, shot);
        compact_projectiles(world);
    }
}

} // namespace oa::sim::weapon_execution
