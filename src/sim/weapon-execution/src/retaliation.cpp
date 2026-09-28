// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/weapon_execution/retaliation.hpp"

#include "oa/sim/ballistics.hpp"
#include "oa/core/player.h"
#include "oa/core/unit_def.h"
#include "oa/core/weapon_def.h"

namespace oa::sim::weapon_execution {

namespace {

constexpr uint8_t weapon_damage_kind = 1;

sim::ballistics::ReachUnitGeometry reach_geometry(const Unit& unit, const UnitDef& def) noexcept {
    return {
        {static_cast<uint32_t>(unit.position.x),
         static_cast<uint32_t>(unit.position.y),
         static_cast<uint32_t>(unit.position.z)},
        static_cast<int16_t>(static_cast<uint32_t>(def.model_height) >> 16),
        unit.flags,
        def.flags
    };
}

bool in_category(const RetaliationHooks& hooks, oa_ref32 mask, uint16_t type_index) {
    return hooks.category_contains != nullptr &&
           hooks.category_contains(hooks.context, mask, type_index);
}

/// Returns the flag word of the unit's head order.
///
/// @param hooks head_order_flags hook
/// @param unit unit whose head order is read
/// @param[out] present set to whether the unit has a head order; may be null
/// @return the flags, or 0 without a head order
uint32_t head_flags(const RetaliationHooks& hooks, const Unit& unit, bool* present) {
    uint32_t flags = 0;
    const bool found =
        hooks.head_order_flags != nullptr && hooks.head_order_flags(hooks.context, unit, &flags);
    if (present != nullptr)
        *present = found;
    return found ? flags : 0;
}

} // namespace

Unit* slot_target_unit(World& world, const Unit& unit, uint8_t slot) noexcept {
    if (slot >= OA_UNIT_WEAPON_COUNT)
        return nullptr;
    const UnitWeapon& weapon = unit.weapons[slot];
    if (weapon.target_b != OA_UNIT_TARGET_IS_UNIT || weapon.target_a == 0)
        return nullptr;
    const int16_t index = weapon.target_a;
    if (index < 0 || static_cast<uint32_t>(index) >= world.unit_slot_count)
        return nullptr;
    return world_unit_at(&world, static_cast<uint32_t>(index));
}

void aim_slot_at_unit(Unit& shooter, const Unit& target, uint8_t slot) noexcept {
    if (slot >= OA_UNIT_WEAPON_COUNT)
        return;
    UnitWeapon& weapon = shooter.weapons[slot];
    weapon.target_a = static_cast<int16_t>(target.id);
    weapon.target_b = OA_UNIT_TARGET_IS_UNIT;
    shooter.events = static_cast<uint16_t>(shooter.events & retarget_event_mask);
}

void aim_slot_at_point(Unit& shooter, const FixedVec3& point, uint8_t slot) noexcept {
    if (slot >= OA_UNIT_WEAPON_COUNT)
        return;
    UnitWeapon& weapon = shooter.weapons[slot];
    weapon.target_a = static_cast<int16_t>(static_cast<uint32_t>(point.x) >> 16);
    weapon.target_b = static_cast<int16_t>(static_cast<uint32_t>(point.z) >> 16);
    if (weapon.target_b == OA_UNIT_TARGET_IS_UNIT)
        weapon.target_b = OA_UNIT_TARGET_IS_UNIT + 1;
    shooter.events = static_cast<uint16_t>(shooter.events & retarget_event_mask);
}

bool slot_reaches_unit(
    const World& world, const Unit& shooter, const Unit& target, uint8_t slot
) noexcept {
    if (slot >= OA_UNIT_WEAPON_COUNT)
        return false;
    const WeaponDef* weapon = world_weapon_def(&world, shooter.weapons[slot].def);
    const UnitDef* shooter_def = world_unit_def_of(&world, &shooter);
    const UnitDef* target_def = world_unit_def_of(&world, &target);
    if (weapon == nullptr || shooter_def == nullptr || target_def == nullptr)
        return false;
    const sim::ballistics::WeaponReachParameters reach{
        weapon->flags,
        weapon->range,
        {weapon->weapon_velocity, weapon->min_barrel_angle, world.game.gravity}
    };
    return sim::ballistics::weapon_can_reach(
        reach,
        reach_geometry(shooter, *shooter_def),
        reach_geometry(target, *target_def),
        world.game.sea_level
    );
}

bool slot_reaches_point(
    const World& world, const Unit& shooter, const FixedVec3& target, uint8_t slot
) noexcept {
    if (slot >= OA_UNIT_WEAPON_COUNT)
        return false;
    const WeaponDef* weapon = world_weapon_def(&world, shooter.weapons[slot].def);
    const UnitDef* shooter_def = world_unit_def_of(&world, &shooter);
    if (weapon == nullptr || shooter_def == nullptr)
        return false;
    const sim::ballistics::WeaponReachParameters reach{
        weapon->flags,
        weapon->range,
        {weapon->weapon_velocity, weapon->min_barrel_angle, world.game.gravity}
    };
    return sim::ballistics::fire_can_reach(
        reach,
        reach_geometry(shooter, *shooter_def),
        {static_cast<uint32_t>(target.x),
         static_cast<uint32_t>(target.y),
         static_cast<uint32_t>(target.z)},
        world.game.sea_level
    );
}

RetaliationResult
retaliate(World& world, Unit& victim, Unit* attacker, const RetaliationHooks& hooks) {
    RetaliationResult result;
    if (attacker != nullptr && attacker->type_index == 0)
        attacker = nullptr;
    const UnitDef* def = world_unit_def_of(&world, &victim);
    const Player* owner = world_unit_owner(&world, &victim);
    const bool owner_present = owner != nullptr && owner->in_use != 0;
    if (def != nullptr && (def->abilities & OA_UNIT_DEF_ABILITY_CAN_CAPTURE) != 0 &&
        owner_present && owner->status == OA_PLAYER_STATUS_COMPUTER && hooks.random != nullptr) {
        result.computer_alert = true;
        result.computer_alert_tick = hooks.random(hooks.context, computer_alert_random_ticks) +
                                     computer_alert_base_ticks + world.game.tick;
        if (hooks.computer_alert != nullptr)
            hooks.computer_alert(hooks.context, victim, result.computer_alert_tick);
    }
    if (attacker != nullptr && def != nullptr) {
        const Player* attacker_owner = world_unit_owner(&world, attacker);
        const bool local = owner_present && (owner->status == OA_PLAYER_STATUS_LOCAL ||
                                             owner->status == OA_PLAYER_STATUS_COMPUTER);
        const bool armed =
            (def->flags & (OA_UNIT_DEF_FLAG_HAS_WEAPONS | OA_UNIT_DEF_FLAG_KAMIKAZE)) != 0;
        const bool finished = victim.build_remaining == 0.0F;
        const bool allied = attacker_owner != nullptr && owner != nullptr &&
                            attacker_owner->index < sizeof owner->alliance &&
                            owner->alliance[attacker_owner->index] != 0;
        if (local && armed && finished && !allied) {
            bool has_head = false;
            const uint32_t head = head_flags(hooks, victim, &has_head);
            const bool head_yields = !has_head || (head & order_standby_flag) != 0;
            if (head_yields && !in_category(hooks, def->no_chase_category, attacker->type_index) &&
                !in_category(hooks, def->primary_bad_target_category, attacker->type_index) &&
                slot_reaches_unit(world, victim, *attacker, 0) && hooks.queue_attack != nullptr &&
                hooks.queue_attack(hooks.context, victim, *attacker))
                result.chased = true;
            if (!result.chased && (victim.flags & OA_UNIT_FLAG_FIRE_ORDER_MASK) != 0) {
                const oa_ref32 bad_targets[OA_UNIT_WEAPON_COUNT] = {
                    def->primary_bad_target_category,
                    def->secondary_bad_target_category,
                    def->special_bad_target_category
                };
                for (uint8_t slot = 0; slot < OA_UNIT_WEAPON_COUNT; ++slot) {
                    const UnitWeapon& weapon = victim.weapons[slot];
                    if ((weapon.flags & OA_UNIT_WEAPON_ENABLED) == 0 ||
                        (weapon.flags & OA_UNIT_WEAPON_RETALIATE) == 0)
                        continue;
                    if (!slot_reaches_unit(world, victim, *attacker, slot))
                        continue;
                    const WeaponDef* weapon_def = world_weapon_def(&world, weapon.def);
                    if (weapon_def != nullptr &&
                        (weapon_def->flags & OA_WEAPON_FLAG_COMMAND_FIRE) != 0)
                        continue;
                    if (const Unit* current = slot_target_unit(world, victim, slot);
                        current != nullptr) {
                        if (slot_reaches_unit(world, victim, *current, slot) &&
                            !in_category(hooks, bad_targets[slot], current->type_index))
                            continue;
                    }
                    aim_slot_at_unit(victim, *attacker, slot);
                    result.retargeted_slots =
                        static_cast<uint8_t>(result.retargeted_slots | (1u << slot));
                }
            }
        }
    }
    if ((head_flags(hooks, victim, nullptr) & order_mutes_attack_notice_flag) == 0 &&
        (victim.last_attacker_owner != victim.owner_index ||
         victim.damage_kind == weapon_damage_kind))
        result.attack_notice = true;
    return result;
}

} // namespace oa::sim::weapon_execution
