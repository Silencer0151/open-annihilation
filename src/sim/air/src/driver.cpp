// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/air/driver.hpp"

#include "oa/sim/air/host.hpp"
#include "oa/sim/ground_orders/orders.hpp"
#include "oa/base/game_math.hpp"

namespace oa::sim::air {

void movement_driver_bind(AirDriver* driver, Unit* unit) noexcept {
    driver->goal = nullptr;
    driver->unit = unit;
}

void air_driver_init(AirDriver* driver, Unit* unit) noexcept {
    *driver = AirDriver{};
    movement_driver_bind(driver, unit);
    driver->position = unit->position;
    driver->heading = unit->heading;
}

void air_driver_init_local(AirDriver* driver, Unit* unit) noexcept {
    air_driver_init(driver, unit);
    driver->local = true;
    driver->flags = static_cast<uint8_t>((driver->flags & ~driver_sent_layer_mask) | driver_resend);
}

void air_driver_init_mirrored(AirDriver* driver, Unit* unit) noexcept {
    air_driver_init(driver, unit);
    driver->local = false;
}

void air_driver_set_goal(AirDriver* driver, AirGoal* goal) noexcept {
    if (driver->goal != nullptr)
        air_goal_raise(driver->goal, ground_orders::goal_replaced_event);
    driver->goal = goal;
    if (driver->local)
        driver->flags |= driver_resend;
}

bool air_driver_has_goal(const AirDriver* driver) noexcept {
    return driver->goal != nullptr;
}

void air_driver_track_goal(AirDriver* driver, const AirHost& host) noexcept {
    AirGoal* goal = driver->goal;
    if (goal == nullptr)
        return;
    const FixedVec3 previous = driver->position;
    (void)air_goal_position(goal, host, &driver->position);
    driver->velocity = FixedVec3{
        driver->position.x - previous.x,
        driver->position.y - previous.y,
        driver->position.z - previous.z,
    };
    const Unit* unit = driver->unit;
    const auto distance = static_cast<int32_t>(base::game_math::distance(
        unit->position.x - driver->position.x, unit->position.z - driver->position.z
    ));
    if (distance > driver_cruise_distance)
        driver->position.y = air_cruise_height(host, unit);
    if (distance > driver_bearing_distance ||
        (!air_goal_heading(goal, &driver->heading) && distance > driver_free_heading_distance))
        driver->heading = base::game_math::direction(
            unit->position.x - driver->position.x, unit->position.z - driver->position.z
        );
    if (!air_goal_arrived(goal, host, unit))
        return;
    air_goal_raise(goal, ground_orders::arrived_event);
    if (!air_goal_is_tracking(goal))
        air_driver_set_goal(driver, nullptr);
}

void air_driver_update(AirDriver* driver, const AirHost& host, uint8_t movement_layer) noexcept {
    if (driver->local &&
        (movement_layer & 3u) !=
            ((static_cast<uint32_t>(driver->flags) >> driver_sent_layer_shift) & 3u))
        driver->flags |= driver_resend;
    air_driver_track_goal(driver, host);
}

AirSteeringTarget air_driver_steering_target(const AirDriver* driver) noexcept {
    return AirSteeringTarget{driver->position, driver->velocity, driver->heading};
}

} // namespace oa::sim::air
