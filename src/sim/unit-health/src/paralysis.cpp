// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_health/paralysis.hpp"

#include "oa/core/player.h"
#include "oa/core/unit_def.h"

namespace oa::sim::unit_health {

namespace {

constexpr uint8_t all_weapon_slots = 3;

// Without a hook only the bits change: no script calls or replication.
void set_state_flags(const ParalysisHooks& hooks, oa::Unit& unit, uint8_t mask, bool on) {
    if (hooks.set_state_flags != nullptr)
        hooks.set_state_flags(hooks.context, unit, mask, on);
    else
        unit.state_flags =
            static_cast<uint8_t>(on ? unit.state_flags | mask : unit.state_flags & ~mask);
}

} // namespace

ParalyzeAction paralyze_action(
    const oa::World& world, const oa::Unit& target, bool head_order_is_paralyze
) noexcept {
    if (!unit_is_live_target(target.flags))
        return ParalyzeAction::none;
    const Player* owner = world_unit_owner(&world, &target);
    if (owner == nullptr || owner->in_use == 0)
        return ParalyzeAction::none;
    if (owner->status != OA_PLAYER_STATUS_LOCAL && owner->status != OA_PLAYER_STATUS_COMPUTER)
        return ParalyzeAction::none;
    const UnitDef* def = world_unit_def_of(&world, &target);
    if (def == nullptr || (def->flags & OA_UNIT_DEF_FLAG_IMMUNE_TO_PARALYZER) != 0)
        return ParalyzeAction::none;
    return head_order_is_paralyze ? ParalyzeAction::extend : ParalyzeAction::insert;
}

bool clear_weapon_target(oa::Unit& unit, uint8_t slot, const ParalysisHooks& hooks) {
    if (slot >= OA_UNIT_WEAPON_COUNT)
        return false;
    UnitWeapon& weapon = unit.weapons[slot];
    if (weapon.target_a == 0 && weapon.target_b == OA_UNIT_TARGET_IS_UNIT)
        return false;
    weapon.target_a = 0;
    weapon.target_b = OA_UNIT_TARGET_IS_UNIT;
    if (hooks.target_cleared != nullptr)
        hooks.target_cleared(hooks.context, unit, slot);
    return true;
}

uint8_t clear_tracked_weapon_targets(oa::Unit& unit, uint8_t slot, const ParalysisHooks& hooks) {
    uint8_t cleared = 0;
    if (slot == all_weapon_slots) {
        cleared = clear_tracked_weapon_targets(unit, 0, hooks);
        cleared = static_cast<uint8_t>(cleared | clear_tracked_weapon_targets(unit, 1, hooks));
        slot = 2;
    }
    if (slot >= OA_UNIT_WEAPON_COUNT)
        return cleared;
    UnitWeapon& weapon = unit.weapons[slot];
    if ((weapon.flags & OA_UNIT_WEAPON_ENABLED) != 0 &&
        (weapon.flags & OA_UNIT_WEAPON_RETALIATE) != 0) {
        weapon.flags = static_cast<uint8_t>(weapon.flags & ~OA_UNIT_WEAPON_RETALIATE);
        if (clear_weapon_target(unit, slot, hooks))
            cleared = static_cast<uint8_t>(cleared | (1u << slot));
    }
    return cleared;
}

ParalyzedStep
paralyzed_mission_step(oa::Unit& unit, int32_t& duration, const ParalysisHooks& hooks) {
    ParalyzedStep step;
    if (duration == 0) {
        set_state_flags(hooks, unit, paralyzed_state_flag, false);
        step.result = mission_result_finished;
        return step;
    }
    if (duration > paralysis_wait_limit)
        duration = paralysis_wait_limit;
    (void)clear_tracked_weapon_targets(unit, all_weapon_slots, hooks);
    for (uint8_t slot = 0; slot < OA_UNIT_WEAPON_COUNT; ++slot)
        (void)clear_weapon_target(unit, slot, hooks);
    if (hooks.release_order_goal != nullptr)
        hooks.release_order_goal(hooks.context);
    step.wait_ticks = duration;
    duration = 0;
    set_state_flags(hooks, unit, paralyzed_state_flag, true);
    step.result = mission_result_waiting;
    return step;
}

} // namespace oa::sim::unit_health
