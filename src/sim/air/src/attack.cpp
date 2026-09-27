// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/air/attack.hpp"

#include "order_support.hpp"

#include "oa/sim/air/flight.hpp"
#include "oa/base/game_math.hpp"
#include "oa/sim/unit_movement/movement.hpp"

namespace oa::sim::air {
namespace {

constexpr uint32_t ground_run_end_events =
    event_weapons | event_target_lost | event_order_destroyed;
constexpr uint32_t air_run_end_events = event_weapons | event_target_lost;
constexpr uint32_t attack_wait = event_weapons | wait_for_goal;
constexpr uint32_t attack_wait_timed = attack_wait | event_order_destroyed;
constexpr uint32_t repair_health_quarters = 3; // break off below 3/4 of max damage
constexpr uint32_t approach_spread = 0x4000;
constexpr int16_t approach_arrival = 0x80;
constexpr int16_t swing_arrival = 0x80;
constexpr uint32_t pull_out_arrival_spread = 0x80;
constexpr int32_t pull_out_ranges = 3;
constexpr uint32_t off_map_wait = 0x1e;
constexpr int32_t pass_ticks = 0x1e; // straight pass length in ticks of max speed
constexpr uint32_t pass_wait_minimum = 0x3c;
constexpr uint32_t pass_wait_spread = 0x1e;
constexpr int32_t chase_patience_step = 0x2d;
constexpr int32_t chase_patience_limit = 0x5a;
constexpr int16_t chase_lead_distance = 0xa0; // world units beyond which the chase leads
constexpr int32_t chase_lead_ticks = 0x2d;
constexpr uint32_t chase_wait = 0x2d;
constexpr int16_t evade_arrival = 0x80;

int32_t first_weapon_range(const AirHost& host, const Unit* unit) noexcept {
    const WeaponDef* weapon = host.weapon_def != nullptr
                                  ? host.weapon_def(host.context, unit, 0)
                                  : world_weapon_def(host.world, unit->weapons[0].def);
    return weapon != nullptr ? weapon->range : 0;
}

bool fire_at_will(const Unit* unit) noexcept {
    return (unit->flags & OA_UNIT_FLAG_FIRE_ORDER_MASK) != 0;
}

int32_t planar_distance(int32_t x, int32_t z) noexcept {
    return static_cast<int32_t>(base::game_math::distance(x, z));
}

// Point `distance` from `origin` in the direction a unit with `heading` faces.
FixedVec3 behind(const FixedVec3& origin, uint16_t heading, oa_fixed distance) noexcept {
    return FixedVec3{
        origin.x - sim::unit_movement::sine_scaled(heading, distance),
        origin.y,
        origin.z - sim::unit_movement::cosine_scaled(heading, distance),
    };
}

void install_goal_within(
    AirOrder* order, const AirHost& host, const FixedVec3& point, int16_t radius
) noexcept {
    AirGoal goal = air_goal_at_point(order->events, order->unit, point);
    air_goal_set_arrival_radius(&goal, radius);
    air_order_set_goal(order, host, &goal);
}

FixedVec3 velocity_of(const AirHost& host, const Unit* unit) noexcept {
    return host.movement_velocity != nullptr ? host.movement_velocity(host.context, unit)
                                             : FixedVec3{};
}

// Shared opening of the attack missions: end on a finished run (queueing a
// follow-up seek when firing at will) or a lost target.
bool attack_ended(
    AirOrder* order, const AirHost& host, uint32_t events, uint32_t end_events
) noexcept {
    Unit* unit = order->unit;
    if ((events & end_events) != 0) {
        if (!order->has_next && fire_at_will(unit))
            push_order(host, unit, true, "VTOL_SEEKATTACK", order->target, &order->destination);
        return true;
    }
    if (order->target == nullptr && (order->flags & order_seek_if_lost) != 0) {
        if (!order->has_next)
            push_order(host, unit, true, "VTOL_SEEKATTACK", nullptr, &unit->position);
        return true;
    }
    return false;
}

} // namespace

bool air_attack_leash_exceeded(const AirOrder* order) noexcept {
    if (order->parameter_3 == 0)
        return false;
    const Unit* unit = order->unit;
    const int32_t dx = static_cast<int16_t>(unit->position.x >> 16) - order->anchor_x;
    const int32_t dz = static_cast<int16_t>(unit->position.z >> 16) - order->anchor_z;
    return planar_distance(dx, dz) >= order->parameter_3;
}

AirStep air_attack_ground(AirOrder* order, const AirHost& host, uint32_t events) noexcept {
    Unit* unit = order->unit;
    const int32_t range = first_weapon_range(host, unit);
    if (attack_ended(order, host, events, ground_run_end_events))
        return AirStep::done;
    if (order->target != nullptr)
        order->destination = order->target->position;
    if (outside_map(host, unit)) {
        air_order_wait(order, host, off_map_wait);
        order->phase = 2;
    }
    if (air_attack_leash_exceeded(order))
        return AirStep::done;
    switch (order->phase) {
    case 0:
        if (!can_fly(host, unit))
            return AirStep::fail;
        air_order_announce(order, host, "Attacking");
        air_take_off(order, host, 0);
        return AirStep::next;
    case 1: {
        reset_weapons(host, unit, 3);
        const int32_t distance = planar_distance(
            order->destination.x - unit->position.x, order->destination.z - unit->position.z
        );
        const uint16_t bearing = base::game_math::direction(
            unit->position.x - order->destination.x, unit->position.z - order->destination.z
        );
        const int32_t half = distance / 2;
        const auto heading = static_cast<uint16_t>(
            draw_random(host, approach_spread) + bearing - approach_spread / 2
        );
        install_goal_within(order, host, behind(unit->position, heading, half), approach_arrival);
        order->wait_events = attack_wait;
        return AirStep::next;
    }
    case 2:
        enable_weapons(host, unit, 0);
        if (order->target != nullptr) {
            if (host.aim_at_unit != nullptr)
                host.aim_at_unit(host.context, unit, order->target, 0);
        } else if (host.aim_at_point != nullptr) {
            host.aim_at_point(host.context, unit, &order->destination, 0);
        }
        install_goal_within(order, host, order->destination, static_cast<int16_t>(range));
        order->wait_events = attack_wait;
        return AirStep::next;
    case 3: {
        const uint16_t away = base::game_math::direction(
            unit->position.x - order->destination.x, unit->position.z - order->destination.z
        );
        const auto distance =
            static_cast<oa_fixed>(static_cast<uint32_t>(range * pull_out_ranges) << 16);
        AirGoal goal =
            air_goal_at_point(order->events, unit, behind(order->destination, away, distance));
        air_goal_set_arrival_radius(
            &goal,
            static_cast<int16_t>(
                draw_random(host, pull_out_arrival_spread) + pull_out_arrival_spread
            )
        );
        air_order_set_goal(order, host, &goal);
        order->wait_events = attack_wait_timed;
        return AirStep::next;
    }
    case 4: {
        const UnitDef* def = def_of(host, unit);
        const uint32_t limit = def != nullptr ? (def->max_damage >> 2) * repair_health_quarters : 0;
        if (static_cast<uint32_t>(static_cast<int32_t>(unit->health)) < limit &&
            air_seek_repair_pad(order, host))
            return AirStep::restart;
        const auto heading = static_cast<uint16_t>(
            draw_random(host, 2) != 0 ? unit->heading + 0x4000 : unit->heading - 0x4000
        );
        const auto distance = static_cast<oa_fixed>(static_cast<uint32_t>(range) << 16);
        install_goal_within(order, host, behind(unit->position, heading, distance), swing_arrival);
        order->wait_events = attack_wait_timed;
        return AirStep::next;
    }
    case 5:
        order->phase = 2;
        return AirStep::stay;
    default:
        return AirStep::fail;
    }
}

AirStep air_attack_air(AirOrder* order, const AirHost& host, uint32_t events) noexcept {
    Unit* unit = order->unit;
    if (attack_ended(order, host, events, air_run_end_events))
        return AirStep::done;
    if (air_return_to_map(order, host))
        return AirStep::stay;
    if (air_attack_leash_exceeded(order))
        return AirStep::done;
    switch (order->phase) {
    case 0:
        if (!can_fly(host, unit))
            return AirStep::fail;
        air_order_announce(order, host, "Attacking");
        air_take_off(order, host, 0);
        air_order_wait(order, host, 1);
        order->parameter = 0;
        return AirStep::next;
    case 1:
        break;
    default:
        return AirStep::fail;
    }

    Unit* target = order->target;
    // A missing target finishes the order.
    if (target == nullptr)
        return AirStep::done;
    reset_weapons(host, unit, 3);
    enable_weapons(host, unit, 0);
    if (host.aim_at_unit != nullptr)
        host.aim_at_unit(host.context, unit, target, 0);
    const uint32_t goal_events = events & wait_for_goal;
    const bool behind_target = sim::unit_movement::facing_toward(
        target->position.x, target->position.z, unit->position.x, unit->position.z, unit->heading
    );
    if (goal_events != 0 && behind_target) {
        const UnitDef* def = def_of(host, unit);
        const oa_fixed speed = def != nullptr ? def->max_velocity : 0;
        const uint16_t heading = unit->heading;
        const FixedVec3 from = behind(unit->position, heading, speed * pass_ticks);
        const FixedVec3 step = behind(FixedVec3{}, heading, speed);
        const AirGoal pass = air_goal_seek(order->events, unit, from, step);
        air_order_set_goal(order, host, &pass);
        air_order_wait(order, host, draw_random(host, pass_wait_spread) + pass_wait_minimum);
        order->parameter = 0;
        return AirStep::stay;
    }
    if (goal_events == 0 && order->parameter < chase_patience_limit) {
        if (behind_target)
            order->parameter = 0;
        else
            order->parameter += chase_patience_step;
        const int32_t distance = planar_distance(
            unit->position.x - target->position.x, unit->position.z - target->position.z
        );
        if (static_cast<int16_t>(distance >> 16) > chase_lead_distance) {
            const FixedVec3 velocity = velocity_of(host, target);
            FixedVec3 from = target->position;
            from.x += velocity.x * chase_lead_ticks;
            from.z += velocity.z * chase_lead_ticks;
            const UnitDef* target_def = def_of(host, target);
            const oa_fixed half_speed = target_def != nullptr ? target_def->max_velocity / 2 : 0;
            const FixedVec3 step{
                velocity.x - sim::unit_movement::sine_scaled(target->heading, half_speed),
                velocity.y,
                velocity.z - sim::unit_movement::cosine_scaled(target->heading, half_speed),
            };
            const AirGoal lead = air_goal_seek(order->events, unit, from, step);
            air_order_set_goal(order, host, &lead);
        }
        air_order_wait(order, host, chase_wait);
        order->wait_events |= attack_wait;
        return AirStep::stay;
    }
    air_order_set_goal(order, host, nullptr);
    push_order(host, unit, false, "VTOL_EVADE", target, nullptr);
    order->parameter = 0;
    order->wait_events = 0;
    return AirStep::restart;
}

AirStep air_evade(AirOrder* order, const AirHost& host, uint32_t events) noexcept {
    Unit* unit = order->unit;
    const int32_t range = first_weapon_range(host, unit);
    if (order->target == nullptr || (events & air_run_end_events) != 0)
        return AirStep::done;
    uint32_t shift = 0;
    switch (order->phase) {
    case 0:
        if (!can_fly(host, unit))
            return AirStep::fail;
        order->parameter = static_cast<int32_t>(draw_random(host, 2));
        shift = 16;
        break;
    case 1:
        shift = 17;
        break;
    case 2:
        return AirStep::done;
    default:
        return AirStep::fail;
    }
    const auto heading = static_cast<uint16_t>(
        order->parameter != 0 ? unit->heading - 0x4000 : unit->heading + 0x4000
    );
    const auto distance = static_cast<oa_fixed>(static_cast<uint32_t>(range) << shift);
    install_goal_within(order, host, behind(unit->position, heading, distance), evade_arrival);
    order->wait_events = attack_wait;
    return AirStep::next;
}

} // namespace oa::sim::air
