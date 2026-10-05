// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The radial order menu and the pad's build ring (touch_hud.hpp): twelve
// fixed wedges, or eight, clockwise from the top around a hub, the ring kept
// inside the safe area.
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
/// The angle between two build ring wedges' centres, in degrees.
constexpr double build_slot_degrees = 360.0 / static_cast<double>(build_ring_slot_count);
/// The side of a build ring wedge's picture and hit rectangle, in points: the 3.1c build
/// picture fits inside it, and neighbouring wedges' squares stay apart.
constexpr float build_picture_points = 56.0f;
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

/// Returns the centre a ring of an outer radius takes for an anchor: the anchor moved inside
/// the radial's area, or inside the safe area when that area is too small for the ring.
///
/// @param anchor the point it opens at, canvas pixels
/// @param outer_radius the ring's outer radius, canvas pixels
/// @param viewport the canvas
/// @return the centre, canvas pixels
Point ring_centre(Point anchor, int outer_radius, const Viewport& viewport) noexcept {
    Rect area = rects::radial_area(viewport);
    if (area.width < 2 * outer_radius || area.height < 2 * outer_radius)
        area = rects::safe_area(viewport);
    return {
        clamp_or_middle(anchor.x, area.x + outer_radius, area.x + area.width - outer_radius),
        clamp_or_middle(anchor.y, area.y + outer_radius, area.y + area.height - outer_radius)
    };
}

/// Returns the slot a point's direction from a ring's centre falls in, clockwise from the top,
/// each slot spanning half its angle either side of its centre.
///
/// @param dx canvas pixels right of the centre
/// @param dy canvas pixels below the centre
/// @param slots the ring's slots
/// @return the slot, 0 to slots − 1
std::size_t slot_of(double dx, double dy, std::size_t slots) noexcept {
    const double step = 360.0 / static_cast<double>(slots);
    double degrees = std::atan2(dx, -dy) * 180.0 / pi;
    if (degrees < 0.0)
        degrees += 360.0;
    return static_cast<std::size_t>(std::floor((degrees + step / 2.0) / step)) % slots;
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
    radial.centre = ring_centre(anchor, radial.outer_radius, viewport);
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
    return radial.wedges[slot_of(dx, dy, radial_slot_count)].item;
}

BuildRing
make_build_ring(Point anchor, const BuildRingContent& content, const Viewport& viewport) noexcept {
    BuildRing ring{};
    ring.anchor = anchor;
    ring.inner_radius = to_px(viewport, inner_radius_points);
    ring.outer_radius = to_px(viewport, outer_radius_points);
    ring.centre = ring_centre(anchor, ring.outer_radius, viewport);
    ring.standing_orders = content.standing_orders;
    // Pictures sit midway across the ring, their offsets rounded in points as the radial's are.
    const double picture_radius = (inner_radius_points + outer_radius_points) / 2.0;
    const int side = to_px(viewport, build_picture_points);
    for (std::size_t slot = 0; slot < build_ring_slot_count; ++slot) {
        const double angle = static_cast<double>(slot) * build_slot_degrees * pi / 180.0;
        BuildWedge& wedge = ring.wedges[slot];
        wedge.kind = content.kinds[slot];
        wedge.gadget = content.gadgets[slot];
        wedge.queued = content.queued[slot];
        wedge.available = content.available[slot] && wedge.kind != BuildWedgeKind::empty;
        const Point middle{
            ring.centre.x + to_px(viewport, std::round(picture_radius * std::sin(angle))),
            ring.centre.y - to_px(viewport, std::round(picture_radius * std::cos(angle)))
        };
        wedge.picture = {middle.x - side / 2, middle.y - side / 2, side, side};
        wedge.hit = wedge.picture;
    }
    return ring;
}

std::optional<uint8_t>
build_ring_hit(const BuildRing& ring, Point point, const Viewport& viewport) noexcept {
    const double dx = static_cast<double>(point.x - ring.centre.x);
    const double dy = static_cast<double>(point.y - ring.centre.y);
    const double distance = std::hypot(dx, dy);
    const double reach =
        static_cast<double>(ring.outer_radius) + gadget_pick_points * pixels_per_point(viewport);
    if (ring.outer_radius <= 0 || distance <= static_cast<double>(ring.inner_radius) ||
        distance > reach)
        return std::nullopt;
    const std::size_t slot = slot_of(dx, dy, build_ring_slot_count);
    if (ring.wedges[slot].kind == BuildWedgeKind::empty)
        return std::nullopt;
    return static_cast<uint8_t>(slot);
}

bool radial_hub_hit(const Radial& radial, Point point) noexcept {
    const double dx = static_cast<double>(point.x - radial.centre.x);
    const double dy = static_cast<double>(point.y - radial.centre.y);
    return std::hypot(dx, dy) <= static_cast<double>(radial.inner_radius);
}

} // namespace oa::ui::touch_hud
