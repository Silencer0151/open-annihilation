// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The radial order menu (touch_hud.hpp): twelve fixed wedges clockwise from
// the top around a hub, the ring kept inside the safe area.
#include "oa/ui/touch_hud.hpp"

#include "touch_hud_rects.hpp"

#include <algorithm>
#include <cmath>

namespace oa::ui::touch_hud {

namespace {

/// The ring's inner radius, in points: the hub.
constexpr float inner_radius_points = 44.0f;
/// The ring's outer radius, in points.
constexpr float outer_radius_points = 128.0f;
/// The side of a wedge's hit rectangle, in points.
constexpr float wedge_hit_points = 44.0f;
/// The angle between two wedges' centres, in degrees.
constexpr double slot_degrees = 360.0 / static_cast<double>(radial_slot_count);
/// Pi, for the wedge angles.
constexpr double pi = 3.14159265358979323846;

/// The item each slot holds, clockwise from the top; slot 3 shows blast or load.
constexpr std::array<RadialItem, radial_slot_count> slot_items{
    RadialItem::move,
    RadialItem::patrol,
    RadialItem::attack,
    RadialItem::blast,
    RadialItem::capture,
    RadialItem::stop,
    RadialItem::info,
    RadialItem::type,
    RadialItem::reclaim,
    RadialItem::repair,
    RadialItem::unload,
    RadialItem::guard,
};

/// Returns a viewport's pixels per point, never below a sliver above 0.
///
/// @param viewport the canvas
/// @return canvas pixels per point
float pixels_per_point(const Viewport& viewport) noexcept {
    return viewport.px_per_point > 0.0f ? viewport.px_per_point : 1.0f;
}

/// Returns points as whole canvas pixels.
///
/// @param viewport the canvas
/// @param points the length in points
/// @return the length in canvas pixels, rounded
int to_px(const Viewport& viewport, double points) noexcept {
    return static_cast<int>(std::lround(points * pixels_per_point(viewport)));
}

/// Returns a value kept between two limits, or their middle when they cross.
///
/// @param value the value
/// @param low the lowest value kept
/// @param high the highest value kept
/// @return the value clamped
int clamp_or_middle(int value, int low, int high) noexcept {
    if (low > high)
        return (low + high) / 2;
    return std::clamp(value, low, high);
}

/// Returns whether the selection can take a radial item.
///
/// @param item the radial item
/// @param availability what the selection can take
/// @return whether the wedge is available
bool item_available(RadialItem item, const RadialAvailability& availability) noexcept {
    if (item == RadialItem::info)
        return availability.info;
    if (item == RadialItem::type)
        return availability.type;
    const auto order = radial_order(item);
    return order.has_value() && availability.orders[static_cast<std::size_t>(*order)];
}

} // namespace

std::optional<Order> radial_order(RadialItem item) noexcept {
    switch (item) {
    case RadialItem::move:
        return Order::move;
    case RadialItem::patrol:
        return Order::patrol;
    case RadialItem::attack:
        return Order::attack;
    case RadialItem::blast:
        return Order::blast;
    case RadialItem::load:
        return Order::load;
    case RadialItem::capture:
        return Order::capture;
    case RadialItem::stop:
        return Order::stop;
    case RadialItem::info:
    case RadialItem::type:
        return std::nullopt;
    case RadialItem::reclaim:
        return Order::reclaim;
    case RadialItem::repair:
        return Order::repair;
    case RadialItem::unload:
        return Order::unload;
    case RadialItem::guard:
        return Order::guard;
    }
    return std::nullopt;
}

Radial make_radial(
    Point anchor, const RadialAvailability& availability, const Viewport& viewport
) noexcept {
    Radial radial{};
    radial.anchor = anchor;
    radial.inner_radius = to_px(viewport, inner_radius_points);
    radial.outer_radius = to_px(viewport, outer_radius_points);
    // The ring stays inside its area (the battlefield inside the safe area, clear of the
    // phone's placed regions) and so inside the safe area; the anchor stays where it was held.
    Rect area = rects::radial_area(viewport);
    if (area.width < 2 * radial.outer_radius || area.height < 2 * radial.outer_radius)
        area = rects::safe_area(viewport);
    radial.centre.x = clamp_or_middle(
        anchor.x, area.x + radial.outer_radius, area.x + area.width - radial.outer_radius
    );
    radial.centre.y = clamp_or_middle(
        anchor.y, area.y + radial.outer_radius, area.y + area.height - radial.outer_radius
    );
    // Labels sit midway across the ring; offsets are rounded in points so every size scales
    // with px_per_point exactly.
    const double label_points = (inner_radius_points + outer_radius_points) / 2.0;
    const int hit_side = to_px(viewport, wedge_hit_points);
    for (std::size_t slot = 0; slot < radial_slot_count; ++slot) {
        const double angle = static_cast<double>(slot) * slot_degrees * pi / 180.0;
        RadialWedge& wedge = radial.wedges[slot];
        wedge.item = slot_items[slot];
        if (wedge.item == RadialItem::blast && !availability.blast_slot_shows_blast)
            wedge.item = RadialItem::load;
        wedge.available = item_available(wedge.item, availability);
        wedge.default_item =
            availability.default_item.has_value() && *availability.default_item == wedge.item;
        wedge.label.x =
            radial.centre.x + to_px(viewport, std::round(label_points * std::sin(angle)));
        wedge.label.y =
            radial.centre.y - to_px(viewport, std::round(label_points * std::cos(angle)));
        wedge.hit = {
            wedge.label.x - hit_side / 2, wedge.label.y - hit_side / 2, hit_side, hit_side
        };
    }
    return radial;
}

std::optional<RadialItem>
radial_hit(const Radial& radial, Point point, const Viewport& viewport) noexcept {
    const double dx = static_cast<double>(point.x - radial.centre.x);
    const double dy = static_cast<double>(point.y - radial.centre.y);
    const double distance = std::hypot(dx, dy);
    const double reach =
        static_cast<double>(radial.outer_radius) + gadget_pick_points * pixels_per_point(viewport);
    if (distance <= static_cast<double>(radial.inner_radius) || distance > reach)
        return std::nullopt;
    // Clockwise from the top, each wedge spanning half a slot either side of its centre.
    double degrees = std::atan2(dx, -dy) * 180.0 / pi;
    if (degrees < 0.0)
        degrees += 360.0;
    const auto slot =
        static_cast<std::size_t>(std::floor((degrees + slot_degrees / 2.0) / slot_degrees)) %
        radial_slot_count;
    return radial.wedges[slot].item;
}

bool radial_hub_hit(const Radial& radial, Point point) noexcept {
    const double dx = static_cast<double>(point.x - radial.centre.x);
    const double dy = static_cast<double>(point.y - radial.centre.y);
    return std::hypot(dx, dy) <= static_cast<double>(radial.inner_radius);
}

} // namespace oa::ui::touch_hud
