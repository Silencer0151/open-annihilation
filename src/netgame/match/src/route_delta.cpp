// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/match/route_delta.hpp"

#include <algorithm>

namespace oa::netgame::match {
namespace {
constexpr uint8_t blocked_bit = sim::unit_movement::collision_blocked;
} // namespace

bool route_needs_send(
    const sim::ground_orders::Navigation& n, const sim::unit_movement::Movement& m
) noexcept {
    return (n.flags & sim::ground_orders::route_changed_flag) != 0 ||
           ((m.flags ^ n.flags) & blocked_bit) != 0;
}

RouteDelta take_route_delta(
    sim::ground_orders::Navigation& n, const sim::unit_movement::Movement& m
) noexcept {
    RouteDelta delta;
    delta.blocked = (m.flags & blocked_bit) != 0;
    if (sim::ground_orders::route_present(n))
        delta.count = static_cast<uint8_t>(std::min<uint32_t>(
            n.count, static_cast<uint32_t>(sim::ground_orders::route_delta_capacity)
        ));
    std::copy_n(n.points.begin(), delta.count, delta.points.begin());
    n.flags = static_cast<uint8_t>(
        ((n.flags & ~blocked_bit) | (m.flags & blocked_bit)) &
        ~sim::ground_orders::route_changed_flag
    );
    return delta;
}

void apply_route_delta(
    sim::ground_orders::MirroredNavigation& n,
    sim::unit_movement::Movement& m,
    const RouteDelta& delta
) noexcept {
    m.flags = static_cast<uint8_t>((m.flags & ~blocked_bit) | (delta.blocked ? blocked_bit : 0));
    n.count = std::min<int32_t>(delta.count, sim::ground_orders::route_delta_capacity);
    std::copy_n(delta.points.begin(), n.count, n.points.begin());
}

} // namespace oa::netgame::match
