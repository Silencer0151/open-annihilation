// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/weapon_execution.hpp"

#include "oa/sim/combat_state.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <stdexcept>

namespace oa::sim::weapon_execution {

FireMode select_fire_mode(uint32_t weapon_flags) noexcept {
    if ((weapon_flags & sim::combat_state::weapon_turret_flag) != 0)
        return FireMode::turret;
    if ((weapon_flags & sim::combat_state::weapon_vlaunch_flag) != 0)
        return FireMode::vertical_launch;
    if ((weapon_flags & sim::combat_state::weapon_line_of_sight_flag) == 0 &&
        (weapon_flags & sim::combat_state::weapon_selfprop_flag) == 0) {
        if ((weapon_flags & sim::combat_state::weapon_dropped_flag) != 0)
            return FireMode::dropped;
        return FireMode::none;
    }
    return FireMode::line;
}

ProjectileRoute projectile_route(uint32_t weapon_flags) noexcept {
    // Line of sight or selfprop outranks ballistic; neither bit set leaves the
    // zero route.
    if ((weapon_flags & sim::combat_state::weapon_line_of_sight_flag) != 0 ||
        (weapon_flags & sim::combat_state::weapon_selfprop_flag) != 0)
        return ProjectileRoute::line;
    if ((weapon_flags & sim::combat_state::weapon_ballistic_flag) != 0)
        return ProjectileRoute::ballistic;
    return ProjectileRoute::none;
}

bool turret_within_tolerance(const TurretSlewInput& input) noexcept {
    // Fallback limits, then the 16-bit truncated angular differences against the
    // definition's turret slew limits.
    uint32_t yaw = input.yaw_limit, pitch = input.pitch_limit;
    if (yaw == 0)
        yaw = pitch = input.moving ? 2000u : 150u;
    else if (pitch == 0)
        pitch = yaw;
    const auto yaw_difference =
        std::abs(static_cast<int32_t>(static_cast<int16_t>(input.current_yaw - input.desired_yaw)));
    const auto pitch_difference = std::abs(
        static_cast<int32_t>(static_cast<int16_t>(input.current_pitch - input.desired_pitch))
    );
    return yaw_difference <= static_cast<int32_t>(yaw) &&
           pitch_difference <= static_cast<int32_t>(pitch);
}

uint16_t reload_ticks_after_shot(
    const uint16_t base,
    const uint16_t veteran_level,
    const int16_t health,
    const uint32_t maximum_health
) {
    if (!maximum_health)
        throw std::invalid_argument("weapon reload experience divisor is zero");
    const auto capped_level = std::min<uint32_t>(veteran_level / 5u, 5u);
    const auto veteran_percent = 100 - 6 * static_cast<int32_t>(capped_level);
    const auto experience_product = static_cast<uint32_t>(static_cast<int32_t>(health)) * 20u;
    const auto experience_percent = 120u - experience_product / maximum_health;
    auto ticks = std::bit_cast<int32_t>(
                     static_cast<uint32_t>(base) * static_cast<uint32_t>(veteran_percent)
                 ) /
                 100;
    ticks = std::bit_cast<int32_t>(static_cast<uint32_t>(ticks) * experience_percent) / 100;
    return static_cast<uint16_t>(ticks);
}

BurstFire burst_fire_step(
    const uint16_t burst_remaining,
    const uint16_t burst_rate,
    const uint32_t anchor_tick,
    const uint32_t current_tick
) noexcept {
    BurstFire result;
    result.remaining = burst_remaining;
    result.anchor = anchor_tick;
    if (burst_remaining == 0)
        return result;
    const auto rate = static_cast<uint32_t>(burst_rate);
    const auto due = anchor_tick + rate;
    if (current_tick < due) {
        result.next_due = due;
        result.has_next = true;
        return result;
    }
    result.shots = 1;
    result.remaining = static_cast<uint16_t>(burst_remaining - 1u);
    result.anchor = due;
    result.next_due = due + rate;
    result.has_next = result.remaining != 0;
    return result;
}

TickResult tick_weapons(UnitState& unit, Host& host) {
    TickResult result;
    for (uint8_t index = 0; index < weapon_slot_count; ++index) {
        auto& slot = unit.slots[index];
        if (!(slot.flags & enabled_flag)) {
            result.slots[index] = SlotResult::disabled;
            continue;
        }
        // Reached through the record: the packed reload field cannot be bound to a reference.
        UnitWeapon& record = *slot.record;
        if (record.reload)
            --record.reload;

        Target target;
        if (!host.resolve_target(index, target)) {
            slot.flags = static_cast<uint8_t>(slot.flags & ~aimed_flag);
            result.slots[index] = SlotResult::target_lost;
            continue;
        }
        const auto* definition = slot.definition;
        if (!definition || !definition->projectile_constructor_present) {
            result.slots[index] = SlotResult::waiting;
            continue;
        }

        if (definition->flags & turret_flag) {
            if (!(slot.flags & aimed_flag)) {
                if (host.begin_turret_aim(index, target))
                    slot.flags = static_cast<uint8_t>(slot.flags | aimed_flag);
            }
        } else if (
            (definition->flags & vlaunch_flag) &&
            !((definition->flags & stockpile_flag) && slot.stockpile_count == 0) &&
            !(slot.flags & aimed_flag)
        ) {
            host.begin_vlaunch_aim(index);
            slot.flags = static_cast<uint8_t>(slot.flags | aimed_flag);
        }

        if (record.reload) {
            result.slots[index] = SlotResult::waiting;
            continue;
        }
        const auto source = host.source_position();
        if (!host.can_reach(index, source, target)) {
            unit.shot_event_bits =
                static_cast<uint16_t>(unit.shot_event_bits | target_unreachable_event);
            result.slots[index] = SlotResult::out_of_range;
            continue;
        }
        const bool stockpiled = (definition->flags & stockpile_flag) != 0;
        if (stockpiled) {
            if (!slot.stockpile_count) {
                result.slots[index] = SlotResult::waiting;
                continue;
            }
        } else if (!host.can_pay_shot_cost(
                       definition->energy_per_shot, definition->metal_per_shot
                   )) {
            result.slots[index] = SlotResult::insufficient_resources;
            continue;
        }
        const auto mode = select_fire_mode(definition->flags);
        if (mode == FireMode::turret) {
            // The turret fires only once the Aim script it started has returned
            // nonzero, and only while the target stays within tolerance. Either
            // miss drops the aim; an unsolvable one also raises the event.
            if (!(slot.flags & aimed_flag) || !host.aim_ready(index)) {
                result.slots[index] = SlotResult::waiting;
                continue;
            }
            const auto aim = host.turret_aim(index, target);
            if (aim != TurretAim::on_target) {
                slot.flags = static_cast<uint8_t>(slot.flags & ~aimed_flag);
                if (aim == TurretAim::unsolved)
                    unit.shot_event_bits =
                        static_cast<uint16_t>(unit.shot_event_bits | target_unreachable_event);
                result.slots[index] = SlotResult::waiting;
                continue;
            }
        }
        if (!host.fire_projectile(index, target)) {
            result.slots[index] = SlotResult::projectile_rejected;
            continue;
        }
        // A turret or vertical-launch shot spends the aim: the slot runs its
        // Aim script again before the next one.
        if (mode == FireMode::turret || mode == FireMode::vertical_launch)
            slot.flags = static_cast<uint8_t>(slot.flags & ~aimed_flag);

        if (stockpiled) {
            --slot.stockpile_count;
            host.stockpile_consumed(index);
        } else {
            record.reload = reload_ticks_after_shot(
                definition->base_reload_ticks, unit.veteran_level, unit.health, unit.maximum_health
            );
        }
        unit.shot_event_bits = static_cast<uint16_t>(
            unit.shot_event_bits |
            ((definition->flags & commandfire_flag) ? commandfire_shot_event : shot_fired_event)
        );
        if (!stockpiled)
            host.pay_shot_cost(definition->energy_per_shot, definition->metal_per_shot);
        result.slots[index] = SlotResult::fired;
    }
    return result;
}

} // namespace oa::sim::weapon_execution
