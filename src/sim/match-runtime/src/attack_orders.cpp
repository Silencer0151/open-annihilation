// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/match_runtime/attack_orders.hpp"
#include "oa/base/game_math.hpp"
#include "oa/sim/unit_movement/movement.hpp"
#include <bit>
#include <cstdint>

namespace oa::sim::match_runtime {
namespace {
int32_t signed_bits(uint32_t n) {
    return std::bit_cast<int32_t>(n);
}

int16_t high_word(uint32_t n) {
    return std::bit_cast<int16_t>(static_cast<uint16_t>(n >> 16));
}

void engage(AttackOrderState& state, AttackOrderHost& host) {
    host.enable_weapon(0);
    host.enable_weapon(2);
    host.assign_target(*state.target, state.weapon_slot);
}
} // namespace

uint32_t air_to_ground(
    sim::simulation_state::Unit& unit,
    sim::simulation_state::Order& order,
    AttackOrderState& state,
    uint32_t events,
    uint32_t tick,
    AttackOrderHost& host
) {
    if ((events & 0x1000au) != 0 || !state.target)
        return 5;
    if (state.leash) {
        const auto distance = base::game_math::distance(
            static_cast<int32_t>(high_word(unit.position[0])) - state.origin[0],
            static_cast<int32_t>(high_word(unit.position[2])) - state.origin[1]
        );
        if (state.leash <= signed_bits(distance))
            return 5;
    }
    const auto slot = static_cast<uint8_t>(state.weapon_slot);
    if (order.phase == 0) {
        if (!unit.object_present || !(unit.type->flags & 0x800u))
            return 7;
        host.announce();
        if (!state.weapon_slot)
            state.weapon_slot = host.selected_weapon();
        return 1;
    }
    if (order.phase == 1) {
        host.reset_weapons();
        const auto dx = signed_bits(state.target->position[0] - unit.position[0]);
        const auto dz = signed_bits(state.target->position[2] - unit.position[2]);
        const auto range = host.range(slot);
        auto heading = base::game_math::direction(dx, dz);
        heading = static_cast<uint16_t>(heading + host.random(0x4000) - 0x2000);
        const auto half = signed_bits(base::game_math::distance(dx, dz) / 2);
        const auto magnitude = signed_bits(static_cast<uint32_t>(half));
        AttackPoint point = unit.position;
        point[0] -= static_cast<uint32_t>(sim::unit_movement::sine_scaled(heading, magnitude));
        point[2] -= static_cast<uint32_t>(sim::unit_movement::cosine_scaled(heading, magnitude));
        host.circle_goal(point, range);
        if (host.can_reach(*state.target, slot))
            engage(state, host);
        order.wait_events = 0xe0;
        order.wait_events |= 1;
        order.wake_tick = tick + 30u;
        return 1;
    }
    if (order.phase == 2) {
        if (events & 0xe0u) {
            order.phase = 1;
            return 4;
        }
        if (host.can_reach(*state.target, slot))
            engage(state, host);
        else
            host.reset_weapons();
        order.wait_events = 0xe0;
        order.wait_events |= 1;
        order.wake_tick = tick + 30u;
        return 2;
    }
    return 7;
}
} // namespace oa::sim::match_runtime
