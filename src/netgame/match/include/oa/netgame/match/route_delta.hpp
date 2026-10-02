// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The route head a ground unit's replication delta carries, taken from the
// owner's local navigator and applied to the other machines' mirrored one.

#include "oa/sim/ground_orders/navigator.hpp"

#include <array>
#include <cstdint>

namespace oa::netgame::match {

// Route head carried in a ground unit's replication delta.
struct RouteDelta {
    bool blocked{};  // the collision_blocked bit of Movement.flags
    uint8_t count{}; // 0..3
    std::array<sim::ground_orders::RoutePoint, sim::ground_orders::route_delta_capacity> points{};
};

/// Tells whether the route or the blocked bit changed since the last delta.
///
/// @param navigation The owner's local navigator.
/// @param movement The unit's movement state.
/// @return True when the route-changed flag is set or the blocked bit differs from the one last sent.
[[nodiscard]] bool route_needs_send(
    const sim::ground_orders::Navigation& navigation, const sim::unit_movement::Movement& movement
) noexcept;

/// Takes the route head for a replication delta and records what was sent.
///
/// @param[in,out] navigation The owner's local navigator; its sent blocked bit is updated and its
///        route-changed flag cleared.
/// @param movement The unit's movement state.
/// @return The blocked bit and up to three route points, none without a route.
[[nodiscard]] RouteDelta take_route_delta(
    sim::ground_orders::Navigation& navigation, const sim::unit_movement::Movement& movement
) noexcept;

/// Applies a received route delta to the mirrored navigator and the blocked bit.
///
/// @param[out] navigation The mirrored navigator; its route becomes the delta's points.
/// @param[in,out] movement The unit's movement state; its blocked bit is set from the delta.
/// @param delta Received route head.
void apply_route_delta(
    sim::ground_orders::MirroredNavigation& navigation,
    sim::unit_movement::Movement& movement,
    const RouteDelta& delta
) noexcept;

} // namespace oa::netgame::match
