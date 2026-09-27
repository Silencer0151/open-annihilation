// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/air/goal.hpp"

#include "oa/sim/air/flight.hpp"
#include "oa/sim/air/host.hpp"
#include "oa/base/game_math.hpp"
#include "oa/sim/unit_movement/movement.hpp"

#include <cstdlib>

namespace oa::sim::air {
namespace {

constexpr double fixed_to_world = 1.0 / 65536.0;

const UnitDef* def_of(const AirHost& host, const Unit* unit) noexcept {
    return host.world != nullptr && unit != nullptr ? world_unit_def_of(host.world, unit) : nullptr;
}

uint8_t sea_level(const AirHost& host) noexcept {
    return host.world != nullptr ? host.world->game.sea_level : 0;
}

bool outside_map(const AirHost& host, const Unit* unit) noexcept {
    return host.outside_map != nullptr && host.outside_map(host.context, unit);
}

uint16_t bearing_between(const FixedVec3& from, const FixedVec3& to) noexcept {
    return base::game_math::direction(from.x - to.x, from.z - to.z);
}

FixedVec3 goal_start(Unit* unit) noexcept {
    return unit != nullptr ? unit->position : FixedVec3{};
}

bool target_position(AirGoal* goal, const AirHost& host, FixedVec3* out) noexcept {
    const uint16_t flags = goal->flags;
    if ((flags & goal_track_unit) == 0 || (flags & goal_face_target) != 0) {
        if ((flags & goal_fixed_altitude) == 0)
            goal->point.y = air_cruise_height(host, goal->unit);
    } else {
        Unit* target = goal->target;
        if (target == nullptr || outside_map(host, target))
            return false;
        goal->point = host.query_point != nullptr
                          ? host.query_point(host.context, target, goal->query_point)
                          : target->position;
        if ((flags & goal_stand_off) != 0) {
            uint16_t heading = target->heading;
            if ((flags & goal_fixed_bearing) != 0)
                heading = static_cast<uint16_t>(heading + goal->bearing);
            goal->point.x += sim::unit_movement::sine_scaled(heading, goal->stand_off);
            goal->point.z += sim::unit_movement::cosine_scaled(heading, goal->stand_off);
        }
        goal->point.y += static_cast<int32_t>(goal->altitude) * 0x10000;
    }
    if (goal->point.y > goal_altitude_limit)
        goal->point.y = goal_altitude_limit;
    *out = goal->point;
    return true;
}

// The step turns at most an eighth of the unit's turn rate per tick toward
// the goal heading; the vertical step is dropped once it turns.
void seek_advance(AirGoal* goal, const AirHost& host, FixedVec3* out) noexcept {
    *out = goal->point;
    goal->point.x += goal->step.x;
    goal->point.z += goal->step.z;
    const uint16_t step_heading = bearing_between(FixedVec3{}, goal->step);
    if ((goal->flags & seek_turn_to_heading) == 0 || step_heading == goal->heading)
        return;
    const UnitDef* def = def_of(host, goal->unit);
    const int limit = def != nullptr ? static_cast<uint16_t>(def->turn_rate) >> 3 : 0;
    auto turn = static_cast<int16_t>(goal->heading - step_heading);
    if (turn >= limit)
        turn = static_cast<int16_t>(limit);
    else if (turn <= -limit)
        turn = static_cast<int16_t>(-limit);
    int32_t x = goal->step.x;
    int32_t z = goal->step.z;
    rotate_xz(static_cast<int16_t>(-turn), &x, &z);
    goal->step.y = 0;
    goal->step.x = x;
    goal->step.z = z;
}

} // namespace

oa_fixed air_cruise_height(const AirHost& host, const Unit* unit) noexcept {
    const UnitDef* def = def_of(host, unit);
    if (def == nullptr)
        return 0;
    uint32_t base = 0;
    if ((def->flags & OA_UNIT_DEF_FLAG_CRUISE_FROM_SEA_LEVEL) != 0)
        base = sea_level(host);
    else if (host.bucket_height != nullptr)
        base = host.bucket_height(host.context, unit);
    const auto cruise = static_cast<uint32_t>(static_cast<int32_t>(def->cruise_alt));
    return static_cast<oa_fixed>((base + cruise) << 16);
}

int32_t air_surface_height(const AirHost& host, oa_fixed x, oa_fixed z) noexcept {
    const int32_t sea = sea_level(host);
    const int32_t terrain =
        host.terrain_height != nullptr ? host.terrain_height(host.context, x, z) : -1;
    return sea < terrain ? terrain : sea;
}

void air_goal_raise(const AirGoal* goal, uint32_t events) noexcept {
    if (goal->order_events != nullptr)
        *goal->order_events |= events;
}

AirGoal air_goal_at_point(uint32_t* order_events, Unit* unit, const FixedVec3& point) noexcept {
    AirGoal goal{};
    goal.kind = AirGoalKind::target;
    goal.order_events = order_events;
    goal.flags = goal_terrain_altitude;
    goal.query_point = -1;
    goal.unit = unit;
    goal.point = point;
    return goal;
}

AirGoal air_goal_follow_unit(
    const AirHost& host, uint32_t* order_events, Unit* unit, Unit* target
) noexcept {
    AirGoal goal{};
    goal.kind = AirGoalKind::target;
    goal.order_events = order_events;
    goal.target = target;
    goal.unit = unit;
    goal.point = goal_start(unit);
    goal.query_point = -1;
    const UnitDef* target_def = def_of(host, target);
    if (target_def == nullptr || (target_def->flags & OA_UNIT_DEF_FLAG_CAN_FLY) == 0) {
        goal.flags = goal_track_unit;
        return goal;
    }
    goal.flags = goal_track_unit | goal_stand_off | goal_match_heading;
    const WeaponDef* weapon = host.world != nullptr && unit != nullptr
                                  ? world_weapon_def(host.world, unit->weapons[0].def)
                                  : nullptr;
    const int32_t range = weapon != nullptr ? weapon->range : 0;
    goal.stand_off = range == 0 ? goal_default_stand_off
                                : static_cast<oa_fixed>(static_cast<uint32_t>(range) << 16);
    return goal;
}

AirGoal
air_goal_over_unit(uint32_t* order_events, Unit* unit, Unit* target, int16_t query_point) noexcept {
    AirGoal goal{};
    goal.kind = AirGoalKind::target;
    goal.order_events = order_events;
    goal.query_point = query_point;
    goal.flags = goal_track_unit | goal_match_heading;
    goal.unit = unit;
    goal.point = goal_start(unit);
    goal.target = target;
    return goal;
}

AirGoal air_goal_facing_unit(
    uint32_t* order_events, Unit* unit, Unit* target, const FixedVec3& point
) noexcept {
    AirGoal goal{};
    goal.kind = AirGoalKind::target;
    goal.order_events = order_events;
    goal.target = target;
    goal.point = point;
    goal.query_point = -1;
    goal.unit = unit;
    goal.flags = goal_face_target | goal_terrain_altitude | goal_stand_off | goal_track_unit;
    return goal;
}

AirGoal air_goal_seek(
    uint32_t* order_events, Unit* unit, const FixedVec3& from, const FixedVec3& step
) noexcept {
    AirGoal goal{};
    goal.kind = AirGoalKind::seek;
    goal.order_events = order_events;
    goal.unit = unit;
    goal.point = from;
    goal.step = step;
    return goal;
}

bool air_goal_position(AirGoal* goal, const AirHost& host, FixedVec3* out) noexcept {
    switch (goal->kind) {
    case AirGoalKind::target:
        return target_position(goal, host, out);
    case AirGoalKind::seek:
        seek_advance(goal, host, out);
        return true;
    case AirGoalKind::none:
        break;
    }
    return false;
}

bool air_goal_heading(const AirGoal* goal, uint16_t* out) noexcept {
    if (goal->kind == AirGoalKind::seek) {
        *out = goal->unit != nullptr ? bearing_between(goal->unit->position, goal->point) : 0;
        return true;
    }
    if (goal->kind != AirGoalKind::target)
        return false;
    const uint16_t flags = goal->flags;
    if ((flags & (goal_match_heading | goal_face_target)) != 0 && goal->target != nullptr) {
        if ((flags & goal_stand_off) != 0) {
            *out = goal->unit != nullptr
                       ? bearing_between(goal->unit->position, goal->target->position)
                       : 0;
            return true;
        }
        if ((flags & goal_fixed_bearing) == 0) {
            *out = goal->target->heading;
            return true;
        }
    } else if ((flags & goal_fixed_bearing) == 0) {
        return false;
    }
    *out = goal->bearing;
    return true;
}

bool air_goal_arrived(AirGoal* goal, const AirHost& host, const Unit* unit) noexcept {
    if (goal->kind == AirGoalKind::seek) {
        const double distance = planar_length(
            static_cast<double>(unit->position.x - goal->point.x),
            static_cast<double>(unit->position.z - goal->point.z)
        );
        if (distance * fixed_to_world < seek_arrival_distance)
            return true;
        return (goal->flags & seek_turn_to_heading) != 0 &&
               bearing_between(FixedVec3{}, goal->step) == goal->heading;
    }
    if (goal->kind != AirGoalKind::target)
        return false;
    FixedVec3 point{};
    (void)air_goal_position(goal, host, &point);
    const double distance = planar_length(
                                static_cast<double>(unit->position.x - point.x),
                                static_cast<double>(unit->position.z - point.z)
                            ) *
                            fixed_to_world;
    const uint16_t flags = goal->flags;
    if ((flags & goal_arrival_radius) != 0)
        return distance < static_cast<double>(goal->arrival_radius);
    if (distance > goal_arrival_distance)
        return false;
    if ((flags & goal_track_unit) != 0 && goal->target == nullptr)
        return false;
    // Without a link there is no heading to match.
    if ((flags & goal_match_heading) != 0 &&
        (goal->target == nullptr || unit->heading != goal->target->heading))
        return false;
    if ((flags & goal_fixed_altitude) == 0)
        return true;
    return std::abs(unit->position.y - point.y) < 0x10001;
}

bool air_goal_is_tracking(const AirGoal* goal) noexcept {
    return goal->kind == AirGoalKind::target && (goal->flags & goal_track_unit) != 0 &&
           goal->target != nullptr;
}

void air_goal_set_altitude(AirGoal* goal, const AirHost& host, int16_t altitude) noexcept {
    goal->flags |= goal_fixed_altitude;
    goal->altitude = altitude;
    if ((goal->flags & goal_terrain_altitude) == 0)
        return;
    const int32_t surface = air_surface_height(host, goal->point.x, goal->point.z);
    goal->point.y = static_cast<oa_fixed>(static_cast<uint32_t>(surface + altitude) << 16);
    if (goal->point.y > goal_altitude_limit)
        goal->point.y = goal_altitude_limit;
}

void air_goal_set_bearing(AirGoal* goal, uint16_t bearing) noexcept {
    goal->flags |= goal_fixed_bearing;
    goal->bearing = bearing;
}

void air_goal_set_arrival_radius(AirGoal* goal, int16_t radius) noexcept {
    goal->flags |= goal_arrival_radius;
    goal->arrival_radius = radius;
}

void air_goal_unlink(AirGoal* goal, const Unit* unit) noexcept {
    if (goal->target == unit)
        goal->target = nullptr;
}

} // namespace oa::sim::air
