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

} // namespace oa::sim::air
