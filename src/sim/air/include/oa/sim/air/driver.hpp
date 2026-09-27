// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Air movement driver: the object a flying unit's movement record points at
// (one kind on the owning machine and one for a mirrored copy).
// Each tick it asks its goal for a point and heading and keeps them, plus the
// point's displacement since the last tick, for the flight step to steer
// toward.

#include "oa/sim/air/goal.hpp"
#include "oa/core/world.h"

#include <cstdint>

namespace oa::sim::air {

struct AirHost;

// Bits of AirDriver.flags.
inline constexpr uint8_t driver_resend = 0x01;        // goal or layer changed since sent
inline constexpr uint8_t driver_sent_layer_shift = 1; // bits 1..2: layer last sent
inline constexpr uint8_t driver_sent_layer_mask = 0x06;

// Horizontal distances (16.16) at which the driver overrides its goal.
inline constexpr int32_t driver_cruise_distance = 0xa00000;   // beyond: cruise altitude
inline constexpr int32_t driver_bearing_distance = 0x1400000; // beyond: face the point
inline constexpr int32_t driver_free_heading_distance = 0x100000;

// Movement layers: the occupancy bits of the movement record's flags
// (Movement.flags & occupancy_mask).
inline constexpr uint8_t layer_ground = 1;
inline constexpr uint8_t layer_air = 2;

struct AirDriver {
    AirGoal* goal{};
    Unit* unit{};
    FixedVec3 position{}; // point to steer toward
    FixedVec3 velocity{}; // its displacement over the last tick
    uint16_t heading{};   // heading to hold
    uint8_t flags{};      // driver_* bits
    bool local{};         // owning-machine driver vs mirrored copy
};

// What the flight step steers toward.
struct AirSteeringTarget {
    FixedVec3 position{};
    FixedVec3 velocity{};
    uint16_t heading{};
};

/// Sets up the part every movement driver shares: no goal, bound to its unit.
///
/// @param[out] driver driver to set up
/// @param unit the flying unit
void movement_driver_bind(AirDriver* driver, Unit* unit) noexcept;

/// Sets up a driver at the unit's position and heading with no goal.
///
/// @param[out] driver driver to set up
/// @param unit the flying unit
void air_driver_init(AirDriver* driver, Unit* unit) noexcept;

/// Sets up an owning-machine driver: as air_driver_init, then due to be sent.
///
/// @param[out] driver driver to set up
/// @param unit the flying unit
void air_driver_init_local(AirDriver* driver, Unit* unit) noexcept;

/// Sets up a mirrored copy of another player's driver.
///
/// @param[out] driver driver to set up
/// @param unit the flying unit
void air_driver_init_mirrored(AirDriver* driver, Unit* unit) noexcept;

/// Replaces the driver's goal.
///
/// The replaced goal's order is told with event_goal_replaced; an owning-machine driver
/// also becomes due to be sent.
///
/// @param[in,out] driver the driver
/// @param goal new goal, or null to clear it
void air_driver_set_goal(AirDriver* driver, AirGoal* goal) noexcept;

/// Tests whether a goal is installed.
///
/// @param driver the driver
/// @return true with a goal
[[nodiscard]] bool air_driver_has_goal(const AirDriver* driver) noexcept;

/// Follows the goal for one tick.
///
/// Refreshes the point, its displacement and the heading: beyond 10 world units
/// the point takes cruise altitude, and beyond 20 (or 1 when the goal leaves the
/// heading free) the unit faces the point. Arrival raises event_arrived and drops a
/// goal that is not tracking a unit.
///
/// @param[in,out] driver the driver
/// @param host world queries for the goal
void air_driver_track_goal(AirDriver* driver, const AirHost& host) noexcept;

/// Runs the driver's per-tick update.
///
/// An owning-machine driver first marks itself due to be sent when the movement
/// layer differs from the one last sent, then the goal is tracked.
///
/// @param[in,out] driver the driver
/// @param host world queries for the goal
/// @param movement_layer the unit's movement layer (layer_ground or layer_air)
void air_driver_update(AirDriver* driver, const AirHost& host, uint8_t movement_layer) noexcept;

/// Returns what the flight step steers toward.
///
/// @param driver the driver
/// @return the point, its displacement over the last tick and the heading to hold
[[nodiscard]] AirSteeringTarget air_driver_steering_target(const AirDriver* driver) noexcept;

} // namespace oa::sim::air
