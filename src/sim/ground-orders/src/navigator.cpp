// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/ground_orders/navigator.hpp"
#include <algorithm>

namespace oa::sim::ground_orders {
namespace {
constexpr Fixed one_world_unit = 0x10000;
} // namespace

void set_mirrored_goal(MirroredNavigation& n, Goal* goal) noexcept {
    if (n.goal && n.goal->order)
        n.goal->order->raised_events |= goal_replaced_event;
    n.goal = goal;
}

std::array<Point, 3> mirrored_steering_points(const MirroredNavigation& n) noexcept {
    std::array<Point, 3> out{};
    if (n.count <= 0)
        return out;
    const auto last = std::min<int32_t>(n.count, route_delta_capacity) - 1;
    for (int32_t i = 0; i < 3; ++i) {
        const auto& p = n.points[static_cast<std::size_t>(std::min(i, last))];
        out[static_cast<std::size_t>(i)] = {
            Fixed{p[0]} * one_world_unit, 0, Fixed{p[1]} * one_world_unit
        };
    }
    return out;
}
} // namespace oa::sim::ground_orders
