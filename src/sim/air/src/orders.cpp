// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/air/orders.hpp"

#include "order_support.hpp"

#include "oa/sim/air/driver.hpp"
#include "oa/base/game_math.hpp"
#include "oa/sim/unit_movement/movement.hpp"
#include "oa/sim/ground_orders/orders.hpp"

namespace oa::sim::air {
namespace {

// Loiter circle of an idle aircraft around its anchor, world units.
constexpr uint32_t loiter_radius_spread = 0x20;
constexpr uint32_t loiter_radius_minimum = 8;
constexpr uint32_t loiter_ticks_minimum = 0x1e;
constexpr uint32_t loiter_ticks_spread = 0xf;
constexpr uint32_t grounded_ticks_spread = 0x1e;
// Landing search: box side and half-offset grow per failed probe.
constexpr uint32_t landing_search_span = 0x81;
constexpr uint32_t landing_search_half = 0x40;
constexpr uint32_t landing_search_span_step = 0x20;
constexpr uint32_t landing_search_half_step = 0x10;
constexpr uint32_t landing_search_span_limit = 0x200;
constexpr oa_fixed landing_circle_radius = 0xa00000;
constexpr int32_t landing_circle_turn = -0x5555; // a third of a turn back
constexpr int16_t landing_circle_arrival = 0x40;
constexpr oa_fixed return_to_map_step = 0x3200000;
constexpr int16_t return_to_map_arrival = 0x80;
constexpr int32_t repair_pad_search_radius = 0xf00;
constexpr uint32_t repair_pad_capacity = 256;

/// Tests whether a unit is an active repair pad: an air base builder, activated.
///
/// @param host def lookup
/// @param unit unit to test; an empty slot is not a pad
/// @return true for an active repair pad
bool is_active_repair_pad(const AirHost& host, const Unit* unit) noexcept {
    const UnitDef* def = unit->type_index != 0 ? def_of(host, unit) : nullptr;
    return def != nullptr && (def->flags & OA_UNIT_DEF_FLAG_BUILDER) != 0 &&
           (def->flags & OA_UNIT_DEF_FLAG_IS_AIRBASE) != 0 &&
           (unit->state_flags & OA_UNIT_STATE_ACTIVE) != 0;
}

// High words of the squared X and Z offsets, as the pad search compares them.
int32_t squared_distance_high(const FixedVec3& a, const FixedVec3& b) noexcept {
    const auto dx = static_cast<int64_t>(a.x - b.x);
    const auto dz = static_cast<int64_t>(a.z - b.z);
    return static_cast<int32_t>((dz * dz) >> 32) + static_cast<int32_t>((dx * dx) >> 32);
}

uint32_t
repair_pads_near(const AirHost& host, const Unit* unit, Unit** out, uint32_t capacity) noexcept {
    const auto player = unit->owner_index;
    if (host.repair_pads_near != nullptr)
        return host.repair_pads_near(
            host.context, player, &unit->position, repair_pad_search_radius, out, capacity
        );
    Player* owner = world_player(host.world, player);
    if (owner == nullptr)
        return 0;
    uint32_t range = 0;
    Unit* first = world_player_units(host.world, owner, &range);
    uint32_t count = 0;
    for (uint32_t i = 0; i < range && count < capacity; ++i) {
        Unit* candidate = &first[i];
        if (is_active_repair_pad(host, candidate) &&
            squared_distance_high(unit->position, candidate->position) <=
                repair_pad_search_radius * repair_pad_search_radius)
            out[count++] = candidate;
    }
    return count;
}

} // namespace

void air_order_wait(AirOrder* order, const AirHost& host, uint32_t ticks) noexcept {
    order->wait_events |= event_timer;
    const uint32_t now = host.tick != nullptr ? host.tick(host.context) : host.world->game.tick;
    order->wake_tick = now + ticks;
}

void air_order_announce(AirOrder* order, const AirHost& host, const char* text) noexcept {
    if ((order->flags & order_announce) == 0)
        return;
    order->flags &= ~order_announce;
    speech(host, order->unit, speech_order, text);
}

void air_order_set_goal(AirOrder* order, const AirHost& host, const AirGoal* goal) noexcept {
    if (order->unit->movement == 0)
        return;
    AirDriver* driver = host.driver != nullptr ? host.driver(host.context, order->unit) : nullptr;
    if (order->goal->kind != AirGoalKind::none) {
        if (driver != nullptr)
            air_driver_set_goal(driver, nullptr);
        *order->goal = AirGoal{};
    }
    if (goal == nullptr)
        return;
    if (order->events != nullptr)
        *order->events &= ~event_goal_mask;
    *order->goal = *goal;
    if (driver != nullptr)
        air_driver_set_goal(driver, order->goal);
}

void air_take_off(AirOrder* order, const AirHost& host, uint32_t extra_events) noexcept {
    Unit* unit = order->unit;
    if (host.enable_weapons != nullptr)
        host.enable_weapons(host.context, unit, 3);
    if (unit->attach_parent != 0 && host.set_carry_link != nullptr)
        host.set_carry_link(host.context, unit, nullptr, -1, 2);
    set_state_flags(host, unit, OA_UNIT_STATE_ACTIVE, true);
    if (movement_layer(host, unit) != layer_ground)
        return;
    host.set_movement_layer(host.context, unit, layer_air);
    AirGoal goal = air_goal_at_point(order->events, unit, unit->position);
    const UnitDef* def = def_of(host, unit);
    air_goal_set_altitude(
        &goal, host, static_cast<int16_t>(def != nullptr ? def->cruise_alt / 2 : 0)
    );
    air_order_set_goal(order, host, &goal);
    order->wait_events |= extra_events | wait_for_goal;
}

bool air_return_to_map(AirOrder* order, const AirHost& host) noexcept {
    Unit* unit = order->unit;
    if (host.outside_map == nullptr || !host.outside_map(host.context, unit))
        return false;
    const Game& game = host.world->game;
    const oa_fixed centre_x =
        static_cast<oa_fixed>(static_cast<uint32_t>(game.map_pixel_width / 2) << 16);
    const oa_fixed centre_z =
        static_cast<oa_fixed>(static_cast<uint32_t>(game.map_pixel_height / 2) << 16);
    const uint16_t heading =
        base::game_math::direction(unit->position.x - centre_x, unit->position.z - centre_z);
    const FixedVec3 point{
        unit->position.x - sim::unit_movement::sine_scaled(heading, return_to_map_step),
        unit->position.y,
        unit->position.z - sim::unit_movement::cosine_scaled(heading, return_to_map_step),
    };
    AirGoal goal = air_goal_at_point(order->events, unit, point);
    air_goal_set_arrival_radius(&goal, return_to_map_arrival);
    order->wait_events |= wait_for_goal;
    air_order_set_goal(order, host, &goal);
    return true;
}

bool air_seek_repair_pad(AirOrder* order, const AirHost& host) noexcept {
    Unit* pads[repair_pad_capacity];
    const uint32_t count = repair_pads_near(host, order->unit, pads, repair_pad_capacity);
    if (count == 0)
        return false;
    air_order_set_goal(order, host, nullptr);
    Unit* pad = pads[draw_random(host, count)];
    if (host.push_order != nullptr)
        host.push_order(host.context, order->unit, false, "VTOL_LANDING", pad, nullptr, 0, 0, 0);
    order->wait_events = 0;
    return true;
}

AirStep air_move(AirOrder* order, const AirHost& host, uint32_t) noexcept {
    Unit* unit = order->unit;
    switch (order->phase) {
    case 0:
        if (!can_fly(host, unit))
            return AirStep::fail;
        air_take_off(order, host, 0);
        return AirStep::next;
    case 1: {
        air_order_announce(order, host, nullptr);
        if (host.reset_weapons != nullptr)
            host.reset_weapons(host.context, unit, 3);
        snap_to_footprint(&order->destination, unit);
        install_point_goal(order, host, order->destination);
        order->wait_events = wait_for_goal;
        return AirStep::next;
    }
    case 2:
        if (!order->has_next)
            speech(host, unit, speech_order_done, nullptr);
        return AirStep::done;
    default:
        return AirStep::fail;
    }
}

AirStep air_standby(AirOrder* order, const AirHost& host, uint32_t) noexcept {
    Unit* unit = order->unit;
    switch (order->phase) {
    case 0:
        if (!can_fly(host, unit))
            return AirStep::fail;
        if (host.enable_weapons != nullptr)
            host.enable_weapons(host.context, unit, 3);
        order->wait_events |= event_weapons;
        air_order_wait(order, host, 1);
        order->anchor_x = static_cast<int16_t>(unit->position.x >> 16);
        order->anchor_z = static_cast<int16_t>(unit->position.z >> 16);
        return AirStep::next;
    case 1: {
        Unit* target =
            host.automatic_target != nullptr ? host.automatic_target(host.context, unit) : nullptr;
        if (target != nullptr && host.issue_attack != nullptr &&
            host.issue_attack(host.context, unit, target)) {
            order->phase = 0;
            order->wait_events = 0;
            return AirStep::retry;
        }
        return AirStep::next;
    }
    case 2: {
        const UnitDef* def = def_of(host, unit);
        const bool airborne = (unit->flags & OA_UNIT_FLAG_OCCUPANCY_MASK) == layer_air;
        uint32_t spread = grounded_ticks_spread;
        if (def == nullptr || (def->flags & OA_UNIT_DEF_FLAG_CAN_FLY) == 0 || !airborne) {
            order->wait_events |= event_weapons;
        } else if (unit->attach_first_child == 0) {
            if (host.push_order != nullptr)
                host.push_order(
                    host.context,
                    unit,
                    false,
                    "VTOL_LANDIFCAN",
                    nullptr,
                    &order->destination,
                    0,
                    0,
                    0
                );
            return AirStep::done;
        } else {
            const auto heading = static_cast<uint16_t>(draw_random(host, 0x10000));
            const auto radius = static_cast<oa_fixed>(
                (draw_random(host, loiter_radius_spread) + loiter_radius_minimum) << 16
            );
            const FixedVec3 point{
                static_cast<oa_fixed>(order->anchor_x) * 0x10000 -
                    sim::unit_movement::sine_scaled(heading, radius),
                0,
                static_cast<oa_fixed>(order->anchor_z) * 0x10000 -
                    sim::unit_movement::cosine_scaled(heading, radius),
            };
            AirGoal goal = air_goal_at_point(order->events, unit, point);
            air_goal_set_altitude(&goal, host, def->cruise_alt);
            air_order_set_goal(order, host, &goal);
            spread = loiter_ticks_spread;
        }
        air_order_wait(order, host, draw_random(host, spread) + loiter_ticks_minimum);
        order->phase = 1;
        return AirStep::stay;
    }
    default:
        return AirStep::fail;
    }
}

AirStep air_land_if_can(AirOrder* order, const AirHost& host, uint32_t events) noexcept {
    if (order->has_next || (events & event_path_failed) != 0)
        return AirStep::done;
    if (air_return_to_map(order, host))
        return AirStep::stay;
    Unit* unit = order->unit;
    switch (order->phase) {
    case 0: {
        if (!can_fly(host, unit))
            return AirStep::fail;
        FixedVec3& destination = order->destination;
        if (destination.x == 0 && destination.z == 0 && destination.y == 0)
            destination = unit->position;
        const uint32_t heading = draw_random(host, 0x10000);
        order->parameter = static_cast<int32_t>(heading);
        order->parameter_2 = static_cast<int32_t>(heading & 1);
        air_take_off(order, host, 0);
        return AirStep::next;
    }
    case 1: {
        const auto can_land = [&](const FixedVec3& point) {
            return host.can_land_at != nullptr && host.can_land_at(host.context, unit, &point);
        };
        if (can_land(unit->position)) {
            if (host.script_start != nullptr)
                host.script_start(host.context, unit, "EndTransport", 0, true);
            AirGoal goal = air_goal_at_point(order->events, unit, unit->position);
            const int32_t sea = host.world->game.sea_level;
            int32_t altitude = 0;
            if (air_surface_height(host, unit->position.x, unit->position.z) <= sea) {
                const int32_t terrain =
                    host.terrain_height != nullptr
                        ? host.terrain_height(host.context, unit->position.x, unit->position.z)
                        : -1;
                altitude = terrain - sea;
            }
            air_goal_set_altitude(&goal, host, static_cast<int16_t>(altitude));
            air_order_set_goal(order, host, &goal);
            order->wait_events = wait_for_goal;
            set_state_flags(host, unit, OA_UNIT_STATE_ACTIVE, false);
            return AirStep::next;
        }
        uint32_t span = landing_search_span;
        uint32_t half = landing_search_half;
        for (;;) {
            FixedVec3 probe = unit->position;
            probe.x += static_cast<oa_fixed>((draw_random(host, span) - half) << 16);
            probe.z += static_cast<oa_fixed>((draw_random(host, span) - half) << 16);
            snap_to_footprint(&probe, unit);
            if (can_land(probe)) {
                install_point_goal(order, host, probe);
                order->wait_events = wait_for_goal;
                return AirStep::stay;
            }
            span += landing_search_span_step;
            half += landing_search_half_step;
            if (span > landing_search_span_limit)
                break;
        }
        if ((events & wait_for_goal) != 0)
            order->parameter += landing_circle_turn;
        const auto heading = static_cast<uint16_t>(order->parameter);
        const FixedVec3 point{
            order->destination.x - sim::unit_movement::sine_scaled(heading, landing_circle_radius),
            order->destination.y,
            order->destination.z -
                sim::unit_movement::cosine_scaled(heading, landing_circle_radius),
        };
        AirGoal goal = air_goal_at_point(order->events, unit, point);
        air_goal_set_arrival_radius(&goal, landing_circle_arrival);
        air_order_set_goal(order, host, &goal);
        order->wait_events |= wait_for_goal;
        return AirStep::stay;
    }
    case 2:
        if ((events & event_arrived) == 0)
            return AirStep::finished;
        host.set_movement_layer(host.context, unit, layer_ground);
        return AirStep::done;
    default:
        return AirStep::fail;
    }
}

} // namespace oa::sim::air
