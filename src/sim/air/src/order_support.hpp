// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Helpers shared by the air mission handlers.

#include "oa/sim/air/goal.hpp"
#include "oa/sim/air/host.hpp"
#include "oa/sim/air/orders.hpp"
#include "oa/sim/ground_orders/orders.hpp"

namespace oa::sim::air {

inline const UnitDef* def_of(const AirHost& host, const Unit* unit) noexcept {
    return host.world != nullptr ? world_unit_def_of(host.world, unit) : nullptr;
}

inline bool can_fly(const AirHost& host, const Unit* unit) noexcept {
    const UnitDef* def = def_of(host, unit);
    return unit->movement != 0 && def != nullptr && (def->flags & OA_UNIT_DEF_FLAG_CAN_FLY) != 0;
}

inline uint32_t draw_random(const AirHost& host, uint32_t limit) noexcept {
    return host.random(host.context, limit);
}

inline void speech(const AirHost& host, Unit* unit, uint32_t category, const char* text) noexcept {
    if (host.speech != nullptr)
        host.speech(host.context, unit, category, text);
}

inline void set_state_flags(const AirHost& host, Unit* unit, uint8_t mask, bool on) noexcept {
    if (host.set_state_flags != nullptr)
        host.set_state_flags(host.context, unit, mask, on);
    else
        unit->state_flags =
            static_cast<uint8_t>(on ? unit->state_flags | mask : unit->state_flags & ~mask);
}

inline uint8_t movement_layer(const AirHost& host, const Unit* unit) noexcept {
    return host.movement_layer != nullptr ? host.movement_layer(host.context, unit) : 0;
}

inline void snap_to_footprint(FixedVec3* point, const Unit* unit) noexcept {
    sim::ground_orders::Point snapped{point->x, point->y, point->z};
    sim::ground_orders::snap_to_footprint_centre(snapped, unit->footprint_x, unit->footprint_z);
    point->x = snapped[0];
    point->z = snapped[2];
}

inline void reset_weapons(const AirHost& host, Unit* unit, uint8_t slot) noexcept {
    if (host.reset_weapons != nullptr)
        host.reset_weapons(host.context, unit, slot);
}

inline void enable_weapons(const AirHost& host, Unit* unit, uint8_t slot) noexcept {
    if (host.enable_weapons != nullptr)
        host.enable_weapons(host.context, unit, slot);
}

inline void push_order(
    const AirHost& host,
    Unit* unit,
    bool append,
    const char* mission,
    Unit* target,
    const FixedVec3* point
) noexcept {
    if (host.push_order != nullptr)
        host.push_order(host.context, unit, append, mission, target, point, 0, 0, 0);
}

inline bool outside_map(const AirHost& host, const Unit* unit) noexcept {
    return host.outside_map != nullptr && host.outside_map(host.context, unit);
}

inline void
install_point_goal(AirOrder* order, const AirHost& host, const FixedVec3& point) noexcept {
    const AirGoal goal = air_goal_at_point(order->events, order->unit, point);
    air_order_set_goal(order, host, &goal);
}

} // namespace oa::sim::air
