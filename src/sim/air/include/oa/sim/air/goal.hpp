// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Aircraft goal objects. An air order hands its unit's air driver one goal;
// every tick the driver asks the goal for a point to fly to, a heading to
// hold, and whether the unit has arrived. Two goal classes exist: the target
// goal, which holds a point or follows a linked unit, and the seek goal,
// which slides a point along a fixed step and turns that step toward a
// heading.

#include "oa/core/world.h"

#include <cstdint>

namespace oa::sim::air {

struct AirHost;

// Kind a goal reports (the ground goals are kinds 4 to 6).
enum class AirGoalKind : uint8_t {
    none = 0,
    /// Target goal: a point, or a point following a linked unit.
    target = 2,
    /// Seek goal: a point sliding along a fixed step.
    seek = 3,
};

// Bits of AirGoal.flags for target goals.
inline constexpr uint16_t goal_track_unit = 0x0001;       // point follows the linked unit
inline constexpr uint16_t goal_stand_off = 0x0002;        // offset along the linked unit's heading
inline constexpr uint16_t goal_match_heading = 0x0004;    // arrival needs the linked unit's heading
inline constexpr uint16_t goal_fixed_altitude = 0x0008;   // altitude set explicitly
inline constexpr uint16_t goal_arrival_radius = 0x0010;   // arrival inside arrival_radius
inline constexpr uint16_t goal_terrain_altitude = 0x0020; // altitude is above the surface
inline constexpr uint16_t goal_fixed_bearing = 0x0040;    // heading from AirGoal.bearing
inline constexpr uint16_t goal_face_target = 0x0080;      // heading toward the linked unit

// Bit of AirGoal.flags for seek goals.
inline constexpr uint16_t seek_turn_to_heading = 0x0001;

// Altitude ceiling of every target goal point, 16.16.
inline constexpr oa_fixed goal_altitude_limit = 0x1ff0000;
// Stand-off used when the unit's first weapon has no range.
inline constexpr oa_fixed goal_default_stand_off = 0x640000;
// Arrival distance of point goals and seek goals, world units.
inline constexpr double goal_arrival_distance = 0.5;
inline constexpr double seek_arrival_distance = 48.0;

// One goal object. Every goal uses kind and order_events; target goals use
// every other field except step and heading, and seek goals use unit, point,
// step, heading and flags.
struct AirGoal {
    AirGoalKind kind{};
    uint32_t* order_events{}; // owning order's raised event bits (AirOrder.events)
    uint16_t flags{};         // goal_* bits, or seek_* bits for a seek goal
    int16_t arrival_radius{}; // world units
    int16_t altitude{};       // world units
    uint16_t bearing{};       // goal_fixed_bearing: heading held, or stand-off turn from the link
    int16_t query_point{-1};  // query point of the linked unit; -1 its origin
    Unit* unit{};             // the unit flying to this goal
    Unit* target{};           // unit link; cleared when that unit goes away
    FixedVec3 point{};
    oa_fixed stand_off{}; // distance kept from the linked unit, signed 16.16
    FixedVec3 step{};     // seek goals: added to point every tick
    uint16_t heading{};   // seek goals: heading the step turns toward
};

/// Returns the unit's cruise altitude over the ground under it.
///
/// Defs flagged OA_UNIT_DEF_FLAG_CRUISE_FROM_SEA_LEVEL measure it from sea level instead.
///
/// @param host world and bucket height queries
/// @param unit the flying unit
/// @return (base + cruise_alt) << 16, or 0 without a def
[[nodiscard]] oa_fixed air_cruise_height(const AirHost& host, const Unit* unit) noexcept;

/// Returns the surface height at X/Z: the terrain, or sea level where the terrain is lower.
///
/// @param host world and terrain height queries
/// @param x signed 16.16 world X
/// @param z signed 16.16 world Z
/// @return the height in whole world units
[[nodiscard]] int32_t air_surface_height(const AirHost& host, oa_fixed x, oa_fixed z) noexcept;

/// Raises event bits on the goal's order.
///
/// @param goal the goal
/// @param events event bits to OR into the order's raised events
void air_goal_raise(const AirGoal* goal, uint32_t events) noexcept;

/// Builds a target goal on a fixed point, altitude relative to the surface there.
///
/// @param order_events the owning order's raised event bits
/// @param unit the flying unit
/// @param point signed 16.16 point
/// @return the goal
[[nodiscard]] AirGoal
air_goal_at_point(uint32_t* order_events, Unit* unit, const FixedVec3& point) noexcept;

/// Builds a target goal following another unit.
///
/// A flying target is shadowed from the stand-off distance of the unit's first
/// weapon range (default 100) along the target's heading, matching it.
///
/// @param host world queries for the defs and weapon
/// @param order_events the owning order's raised event bits
/// @param unit the flying unit
/// @param target unit to follow
/// @return the goal
[[nodiscard]] AirGoal air_goal_follow_unit(
    const AirHost& host, uint32_t* order_events, Unit* unit, Unit* target
) noexcept;

/// Builds a target goal over one query point of a unit, holding that unit's heading.
///
/// @param order_events the owning order's raised event bits
/// @param unit the flying unit
/// @param target unit to hover over
/// @param query_point query point of the target; -1 its origin
/// @return the goal
[[nodiscard]] AirGoal
air_goal_over_unit(uint32_t* order_events, Unit* unit, Unit* target, int16_t query_point) noexcept;

/// Builds a target goal on a fixed point that keeps the unit facing a target.
///
/// @param order_events the owning order's raised event bits
/// @param unit the flying unit
/// @param target unit to face
/// @param point signed 16.16 point
/// @return the goal
[[nodiscard]] AirGoal air_goal_facing_unit(
    uint32_t* order_events, Unit* unit, Unit* target, const FixedVec3& point
) noexcept;

/// Builds a seek goal starting at a point and moving by a step each tick; heading unset.
///
/// @param order_events the owning order's raised event bits
/// @param unit the flying unit
/// @param from signed 16.16 start point
/// @param step signed 16.16 displacement per tick
/// @return the goal
[[nodiscard]] AirGoal air_goal_seek(
    uint32_t* order_events, Unit* unit, const FixedVec3& from, const FixedVec3& step
) noexcept;

/// Returns the point to fly to this tick.
///
/// Target goals refresh their stored point from the linked unit or the cruise
/// altitude, capped at goal_altitude_limit; seek goals return their point and
/// advance it by the step, turning the step toward their heading by at most an
/// eighth of the unit's turn rate.
///
/// @param[in,out] goal the goal
/// @param host world queries
/// @param[out] out the point
/// @return false when the goal has none (no kind, or a lost or off-map linked unit)
[[nodiscard]] bool air_goal_position(AirGoal* goal, const AirHost& host, FixedVec3* out) noexcept;

/// Returns the heading to hold.
///
/// @param goal the goal
/// @param[out] out the heading, 65536 per turn
/// @return false when the goal leaves the heading free
[[nodiscard]] bool air_goal_heading(const AirGoal* goal, uint16_t* out) noexcept;

/// Tests whether the unit flying the goal has arrived.
///
/// Seek goals arrive within 48 world units or once the step faces their
/// heading. Target goals arrive within their arrival radius, or within half a
/// world unit with the linked unit present, its heading matched when required
/// and the fixed altitude reached within one world unit. A goal that must
/// match the linked unit's heading does not arrive without a linked unit.
///
/// @param[in,out] goal the goal; a target goal's point is refreshed
/// @param host world queries
/// @param unit the flying unit
/// @return true on arrival
[[nodiscard]] bool air_goal_arrived(AirGoal* goal, const AirHost& host, const Unit* unit) noexcept;

/// Tests whether the goal keeps following a live unit after arrival.
///
/// @param goal the goal
/// @return true for a target goal tracking a linked unit
[[nodiscard]] bool air_goal_is_tracking(const AirGoal* goal) noexcept;

/// Fixes the goal altitude; above the surface for terrain-relative goals.
///
/// @param[in,out] goal the goal
/// @param host terrain queries
/// @param altitude altitude in world units
void air_goal_set_altitude(AirGoal* goal, const AirHost& host, int16_t altitude) noexcept;

/// Makes the goal hold a fixed heading.
///
/// @param[in,out] goal the goal
/// @param bearing heading to hold, 65536 per turn
void air_goal_set_bearing(AirGoal* goal, uint16_t bearing) noexcept;

/// Lets the goal be reached anywhere within a radius.
///
/// @param[in,out] goal the goal
/// @param radius arrival radius in world units
void air_goal_set_arrival_radius(AirGoal* goal, int16_t radius) noexcept;

/// Drops the goal's link to a unit that is going away.
///
/// @param[in,out] goal the goal
/// @param unit the departing unit; other links are kept
void air_goal_unlink(AirGoal* goal, const Unit* unit) noexcept;

} // namespace oa::sim::air
