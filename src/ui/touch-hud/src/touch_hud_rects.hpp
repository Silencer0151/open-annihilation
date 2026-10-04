// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Rectangle helpers the touch HUD's sources share: containment, overlap and
// the distance from a point to a rectangle's edge. Rectangles are half open:
// a rectangle holds the pixels from x to x + width - 1.
#pragma once

#include "oa/ui/touch_hud.hpp"

#include <algorithm>
#include <stdint.h>

namespace oa::ui::touch_hud::rects {

/// Returns whether a rectangle holds no pixel.
///
/// @param rect the rectangle
/// @return whether its width or height is not positive
[[nodiscard]] inline bool empty(const Rect& rect) noexcept {
    return rect.width <= 0 || rect.height <= 0;
}

/// Returns whether a rectangle holds a point.
///
/// @param rect the rectangle
/// @param point the point
/// @return whether the point is one of the rectangle's pixels
[[nodiscard]] inline bool contains(const Rect& rect, Point point) noexcept {
    return !empty(rect) && point.x >= rect.x && point.y >= rect.y &&
           point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}

/// Returns whether two rectangles share a pixel.
///
/// @param a one rectangle
/// @param b the other
/// @return whether they overlap; rectangles that only touch do not
[[nodiscard]] inline bool intersects(const Rect& a, const Rect& b) noexcept {
    return !empty(a) && !empty(b) && a.x < b.x + b.width && b.x < a.x + a.width &&
           a.y < b.y + b.height && b.y < a.y + a.height;
}

/// Returns whether one rectangle lies wholly inside another.
///
/// @param inner the rectangle tested
/// @param outer the rectangle it must lie in
/// @return whether every pixel of inner is a pixel of outer
[[nodiscard]] inline bool inside(const Rect& inner, const Rect& outer) noexcept {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

/// Returns the squared distance from a point to a rectangle's nearest pixel.
///
/// @param rect the rectangle
/// @param point the point
/// @return the squared distance in pixels; 0 inside the rectangle
[[nodiscard]] inline int64_t distance_squared(const Rect& rect, Point point) noexcept {
    const int right = rect.x + rect.width - 1;
    const int bottom = rect.y + rect.height - 1;
    const int64_t dx =
        point.x < rect.x ? rect.x - point.x : (point.x > right ? point.x - right : 0);
    const int64_t dy =
        point.y < rect.y ? rect.y - point.y : (point.y > bottom ? point.y - bottom : 0);
    return dx * dx + dy * dy;
}

/// Returns a rectangle's centre.
///
/// @param rect the rectangle
/// @return the centre, rounded down
[[nodiscard]] inline Point centre(const Rect& rect) noexcept {
    return {rect.x + rect.width / 2, rect.y + rect.height / 2};
}

/// Returns the smallest rectangle holding two rectangles; an empty one is ignored.
///
/// @param a one rectangle
/// @param b the other
/// @return their bounding rectangle
[[nodiscard]] inline Rect united(const Rect& a, const Rect& b) noexcept {
    if (empty(a))
        return b;
    if (empty(b))
        return a;
    const int left = std::min(a.x, b.x);
    const int top = std::min(a.y, b.y);
    const int right = std::max(a.x + a.width, b.x + b.width);
    const int bottom = std::max(a.y + a.height, b.y + b.height);
    return {left, top, right - left, bottom - top};
}

/// Returns the safe area of a viewport: the canvas less its safe insets.
///
/// @param viewport the canvas
/// @return the safe rectangle in canvas pixels
[[nodiscard]] inline Rect safe_area(const Viewport& viewport) noexcept {
    return {
        viewport.safe.left,
        viewport.safe.top,
        viewport.width - viewport.safe.left - viewport.safe.right,
        viewport.height - viewport.safe.top - viewport.safe.bottom
    };
}

/// Returns where the radial's ring may go: on a tablet the battlefield inside the safe area,
/// on a phone the battlefield between the left column and the rail, under the resource strip
/// (mirrored when left-handed), so the ring never lies under a placed HUD region (the
/// minimap, the resource strip), which is drawn over the touch controls.
///
/// @param viewport the canvas
/// @return the area in canvas pixels; the safe area when the viewport is too small for it
[[nodiscard]] Rect radial_area(const Viewport& viewport) noexcept;

} // namespace oa::ui::touch_hud::rects
