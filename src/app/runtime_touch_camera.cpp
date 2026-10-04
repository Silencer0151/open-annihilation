// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The touch controls' camera: the map dragged under a finger, the pinch
// zoom applied at once about the fingers, inertia and auto-scroll
// (docs/touch-controls.md).
#include "oa/app/runtime.hpp"
#include "touch_state.hpp"
#include "oa/app/frame_pacing.hpp"
#include "oa/present/world_renderer/world_camera.hpp"
#include "oa/ui/touch_hud.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace oa::app {
namespace {

// The range of the map pixels a zoom's exact anchor lies past its whole map
// pixel, as the wheel's zoom keeps it (runtime_camera.cpp).
constexpr double kLeastAnchorFraction = -0.5;
constexpr double kMostAnchorFraction = 1.5;

/// The time constant a pan's inertia decays with, seconds.
constexpr double kInertiaDecaySeconds = 0.25;
/// The speed under which a pan's inertia stops, points a second.
constexpr float kInertiaStopPointsPerSecond = 20.0F;

} // namespace

void Runtime::pan_match_camera_by(float dx, float dy) {
    if (screen_ != Screen::match || !selected_tnt_ || director_mode())
        return;
    auto& dispatch = touch_state().dispatch;
    const auto zoom = static_cast<double>(match_zoom() <= 0.0F ? 1.0F : match_zoom());
    dispatch.pan_carry_x += static_cast<double>(dx) / zoom;
    dispatch.pan_carry_y += static_cast<double>(dy) / zoom;
    const auto move_x = static_cast<int32_t>(std::trunc(dispatch.pan_carry_x));
    const auto move_z = static_cast<int32_t>(std::trunc(dispatch.pan_carry_y));
    dispatch.pan_carry_x -= static_cast<double>(move_x);
    dispatch.pan_carry_y -= static_cast<double>(move_z);
    stop_match_tracking();
    zoom_anchored_ = false;
    if (move_x == 0 && move_z == 0)
        return;
    // The stored camera stays where view_camera would hold it, so a drag
    // past the map's edge does not have to be undone before the view moves
    // back.
    const auto [map_width, map_height] = shown_map_size();
    match_camera_x_ =
        std::clamp(match_camera_x_ + move_x, 0, std::max(0, map_width - visible_map_width()));
    match_camera_z_ =
        std::clamp(match_camera_z_ + move_z, 0, std::max(0, map_height - visible_map_height()));
    camera_moved_ = true;
}

void Runtime::zoom_match_about(float factor, float x, float y) {
    if (screen_ != Screen::match || !selected_tnt_ || director_mode() || !(factor > 0.0F))
        return;
    // A point off the battlefield zooms about the nearest point on it.
    const int px = std::clamp(
        static_cast<int>(x),
        match_layout_.left,
        match_layout_.left + std::max(1, match_layout_.battlefield_width()) - 1
    );
    const int py = std::clamp(
        static_cast<int>(y),
        match_layout_.top,
        match_layout_.top + std::max(1, match_layout_.battlefield_height()) - 1
    );
    const auto viewport = live_viewport(
        static_cast<uint32_t>(std::max(0, match_camera_x_)),
        static_cast<uint32_t>(std::max(0, match_camera_z_))
    );
    // The map pixel under the point and the exact point a view drawn between
    // map pixels shows there, anchored as the wheel's zoom anchors them.
    const auto before = oa::present::world_renderer::screen_to_map_pixel(viewport, {px, py});
    if (before) {
        zoom_anchor_map_x_ = before->x;
        zoom_anchor_map_y_ = before->y;
        zoom_anchor_sx_ = px - match_layout_.left;
        zoom_anchor_sy_ = py - match_layout_.top;
        const auto offset = view_offset();
        const auto zoom = static_cast<double>(match_zoom() <= 0.0F ? 1.0F : match_zoom());
        zoom_anchor_fraction_x_ = std::clamp(
            static_cast<double>(viewport.source_x) + offset.x +
                static_cast<double>(zoom_anchor_sx_) / zoom - static_cast<double>(before->x),
            kLeastAnchorFraction,
            kMostAnchorFraction
        );
        zoom_anchor_fraction_y_ = std::clamp(
            static_cast<double>(viewport.source_y) + offset.y +
                static_cast<double>(zoom_anchor_sy_) / zoom - static_cast<double>(before->y),
            kLeastAnchorFraction,
            kMostAnchorFraction
        );
        zoom_anchored_ = true;
    }
    // The zoom and its target together: nothing eases, so the map stays
    // under the fingers.
    const float zoom = std::clamp(match_zoom_ * factor, least_match_zoom(), kMaxBattlefieldZoom);
    match_zoom_ = zoom;
    match_zoom_target_ = zoom;
    apply_zoom_anchor();
    zoom_anchored_ = false;
    camera_moved_ = true;
    stop_match_tracking();
}

void TouchDispatchAccess::step_inertia(Runtime& runtime, uint64_t elapsed_ns) {
    auto& dispatch = runtime.touch_state().dispatch;
    if (dispatch.inertia_x == 0.0F && dispatch.inertia_y == 0.0F)
        return;
    // A finger on the battlefield, another screen or a camera that cannot
    // move ends it.
    if (runtime.screen_ != Screen::match || dispatch.battlefield.fingers_down() != 0) {
        dispatch.inertia_x = 0.0F;
        dispatch.inertia_y = 0.0F;
        return;
    }
    if (elapsed_ns == 0)
        return;
    const double seconds = static_cast<double>(elapsed_ns) / 1e9;
    // The map keeps following the fingers' last motion.
    runtime.pan_match_camera_by(
        static_cast<float>(-static_cast<double>(dispatch.inertia_x) * seconds),
        static_cast<float>(-static_cast<double>(dispatch.inertia_y) * seconds)
    );
    const auto decay = static_cast<float>(std::exp(-seconds / kInertiaDecaySeconds));
    dispatch.inertia_x *= decay;
    dispatch.inertia_y *= decay;
    const float stop = points_to_pixels(runtime, kInertiaStopPointsPerSecond);
    if (std::hypot(dispatch.inertia_x, dispatch.inertia_y) < stop) {
        dispatch.inertia_x = 0.0F;
        dispatch.inertia_y = 0.0F;
    }
}

void TouchDispatchAccess::step_auto_scroll(Runtime& runtime, uint64_t elapsed_ns) {
    auto& state = runtime.touch_state();
    auto& dispatch = state.dispatch;
    dispatch.auto_scrolling = false;
    if (runtime.screen_ != Screen::match || !runtime.match_ ||
        !(dispatch.box_active || dispatch.ghost_drag))
        return;
    // The band lies inside the battlefield and the safe area, where a
    // finger can reach.
    const auto& layout = runtime.match_layout_;
    const int left = std::max(layout.left, layout.safe.left);
    const int top = std::max(layout.top, layout.safe.top);
    const int right =
        std::min(layout.left + layout.battlefield_width(), layout.width - layout.safe.right);
    const int bottom =
        std::min(layout.top + layout.battlefield_height(), layout.height - layout.safe.bottom);
    const float band = points_to_pixels(runtime, oa::ui::touch_hud::edge_scroll_points);
    const float x = dispatch.auto_scroll_x;
    const float y = dispatch.auto_scroll_y;
    const int way_x = x < static_cast<float>(left) + band     ? -1
                      : x >= static_cast<float>(right) - band ? 1
                                                              : 0;
    const int way_y = y < static_cast<float>(top) + band       ? -1
                      : y >= static_cast<float>(bottom) - band ? 1
                                                               : 0;
    if (way_x == 0 && way_y == 0)
        return;
    dispatch.auto_scrolling = true;
    // The keyboard's scroll rate: map pixels at zoom 1, which the pan
    // divides by the zoom as scroll_match_view does.
    const auto speed = runtime.match_->state().game.scroll_speed;
    const auto step = static_cast<float>(frame_pacing::scroll_distance(speed, elapsed_ns));
    if (step <= 0.0F)
        return;
    runtime.pan_match_camera_by(static_cast<float>(way_x) * step, static_cast<float>(way_y) * step);
    if (dispatch.box_active && runtime.match_drag_) {
        // The box's far corner is the ground now under the finger.
        runtime.match_pointer_x_ = x;
        runtime.match_pointer_y_ = y;
        runtime.track_match_drag();
    }
}

} // namespace oa::app
