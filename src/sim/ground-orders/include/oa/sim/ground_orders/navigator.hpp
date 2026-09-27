// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/sim/ground_orders/goals.hpp"
#include "oa/sim/unit_movement/movement.hpp"
#include <array>
#include <cstdint>

namespace oa::sim::ground_orders {
// Bits of Navigation.flags.
inline constexpr uint8_t route_present_flag = 0x01;
inline constexpr uint8_t search_pending_flag = 0x02;
inline constexpr uint8_t route_sent_blocked_flag = 0x04; // movement blocked bit as last shared
inline constexpr uint8_t route_changed_flag = 0x08;      // route must be shared again
inline constexpr uint32_t search_retry_ticks = 60;
inline constexpr std::size_t navigation_capacity = 20;
inline constexpr std::size_t route_delta_capacity = 3;

using RoutePoint = std::array<int16_t, 2>; // integer world X/Z

// Local ground navigator: owns the route produced by the path search for a
// unit simulated on this machine.
struct Navigation {
    Goal* goal{};
    std::array<RoutePoint, navigation_capacity> points{};
    uint32_t count{};                  // route points held
    uint32_t last_search_tick{};       // Game.tick the last search was allowed
    uint8_t flags{route_changed_flag}; // route_present_flag and the other bits above

    /// Returns whether a path search is pending.
    bool needs_search() const noexcept { return (flags & search_pending_flag) != 0; }
};

// Mirrored ground navigator: holds the first route points the owning
// player's machine last shared. It never searches and has no tick.
struct MirroredNavigation {
    Goal* goal{};
    std::array<RoutePoint, route_delta_capacity> points{};
    int32_t count{}; // route points held
};

/// Tests whether the navigator holds a route.
///
/// @param n navigator
/// @return true when route_present_flag is set
[[nodiscard]] inline bool route_present(const Navigation& n) noexcept {
    return (n.flags & route_present_flag) != 0;
}

/// Installs a new goal on a mirrored navigator.
///
/// The replaced goal's order is flagged goal-replaced (goal_replaced_event).
///
/// @param[in,out] n mirrored navigator
/// @param goal new goal, or null
void set_mirrored_goal(MirroredNavigation& n, Goal* goal) noexcept;

/// Tests whether a mirrored navigator holds a route.
///
/// @param n mirrored navigator
/// @return true when it holds at least two points
[[nodiscard]] inline bool mirrored_route_present(const MirroredNavigation& n) noexcept {
    return 1 < n.count;
}

/// Returns the next three steering points of a mirrored route.
///
/// Missing trailing points repeat the last one.
///
/// @param n mirrored navigator
/// @return signed 16.16 points with Y zero; all zero for an empty route
[[nodiscard]] std::array<Point, 3> mirrored_steering_points(const MirroredNavigation& n) noexcept;
} // namespace oa::sim::ground_orders
