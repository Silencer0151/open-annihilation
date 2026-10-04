// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Headless check of zooming a camera that tracks a unit.
#include "oa/app/runtime.hpp"
#include "engine_settings_state.hpp"
#include "match_fault.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace oa::app {
namespace {

/// Nanoseconds a frame of the check stands for: 60 frames a second.
constexpr uint64_t kFrameNs = 1'000'000'000ULL / 60ULL;
/// Frames the zoom is given to settle at its target.
constexpr int kSettleFrames = 240;
/// Wheel steps between the default zoom and either end of its range.
constexpr int kWheelSteps = 12;
/// Map pixels the walking unit is sent, and where the corner unit stands from the map's corner.
constexpr int32_t kWalkTrip = 400;
constexpr int32_t kCornerInset = 40;
/// Screen pixels a step scrolls the camera by.
constexpr double kScrollStep = 32.0;

/// Ends the check with a failure.
///
/// @param what what failed
[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("tracking zoom check: " + what);
}

} // namespace

void Runtime::check_tracking_zoom() {
    exercise_click(skirmish::resource_name(skirmish::Button::start));
    if (screen_ != Screen::match || !match_ || !selected_tnt_)
        fail("Start did not enter a match");
    auto& slots = match_->world().slots;
    uint16_t commander = 0;
    for (const auto& slot : slots)
        if (slot.unit_index != 0 && slot.unit != nullptr && slot.record.type_index != 0 &&
            slot.record.owner_index == match_local_player_) {
            commander = slot.unit_index;
            break;
        }
    if (commander == 0)
        fail("the match has no local unit");
    const auto [map_width, map_height] = shown_map_size();

    // A frame: every other one a tick runs, unless a menu holds the match;
    // then the camera moves as the application loop moves it: the zoom
    // eases, the camera scrolls and follows the tracked unit.
    int frames = 0;
    const auto frame = [&] {
        if (!match_paused_ && (++frames % 2) == 0) {
            ++match_timing_.tick;
            match_->simulation().tick = match_timing_.tick;
            tick_or_raise(*match_);
        }
        frame_time_ns_ += kFrameNs;
        move_match_camera();
    };
    // The camera a tracking camera has: the unit at the battlefield's
    // centre, as far as the map's edges let it.
    const auto tracking_camera = [&](uint16_t id) {
        const auto* unit = slots[id].unit;
        const auto x = static_cast<int32_t>(unit->position[0] >> 16) - visible_map_width() / 2;
        const auto z = static_cast<int32_t>(unit->position[2] >> 16) - visible_map_height() / 2;
        return std::array<int32_t, 2>{
            std::clamp(x, 0, std::max(0, map_width - visible_map_width())),
            std::clamp(z, 0, std::max(0, map_height - visible_map_height()))
        };
    };
    const auto require_tracking = [&](uint16_t id, const std::string& when) {
        if (!match_tracking_ || tracked_match_unit_ != id ||
            match_->state().game.follow_unit != oa::oa_unit_ref_from_slot(id))
            fail("tracking ended " + when);
        if (view_camera() != tracking_camera(id))
            fail("the tracked unit left the centre " + when);
    };
    // Frames with the wheel turned `steps` notches at a point away from the
    // battlefield's centre, until the zoom settles.
    const auto wheel = [&](uint16_t id, int steps, const std::string& what) {
        const float pointer_x =
            static_cast<float>(match_layout_.left + match_layout_.battlefield_width() / 4);
        const float pointer_y =
            static_cast<float>(match_layout_.top + match_layout_.battlefield_height() / 4);
        for (int step = 0; step < std::abs(steps); ++step) {
            handle_match_zoom(steps > 0 ? 1.0F : -1.0F, pointer_x, pointer_y);
            frame();
            require_tracking(id, "while the wheel zoomed " + what);
        }
        for (int settle = 0; settle < kSettleFrames; ++settle) {
            frame();
            require_tracking(id, "while the zoom eased " + what);
        }
    };

    // The commander walks while the camera tracks it and the wheel zooms in
    // and out about a point away from it.
    // The pointer rests inside the battlefield, away from the edges that scroll.
    match_paused_ = false;
    match_pointer_x_ =
        static_cast<float>(match_layout_.left + match_layout_.battlefield_width() / 2);
    match_pointer_y_ =
        static_cast<float>(match_layout_.top + match_layout_.battlefield_height() / 2);
    const auto& walker = *slots[commander].unit;
    const auto walk_x = static_cast<int32_t>(walker.position[0] >> 16);
    const auto walk_z = static_cast<int32_t>(walker.position[2] >> 16);
    const auto goal_x =
        std::clamp(walk_x + (walk_x < map_width / 2 ? kWalkTrip : -kWalkTrip), 0, map_width - 1);
    (void)match_->issue_ground_move(
        commander,
        {goal_x * 0x10000,
         match_->map_height(
             static_cast<uint32_t>(goal_x) << 16, static_cast<uint32_t>(walk_z) << 16
         ) * 0x10000,
         walk_z * 0x10000},
        false
    );
    begin_match_tracking(commander);
    require_tracking(commander, "as it began");
    const uint32_t walked_from = walker.position[0];
    wheel(commander, kWheelSteps, "in");
    if (match_zoom() != kMaxBattlefieldZoom)
        fail("the wheel did not zoom in to the nearest zoom");
    wheel(commander, -2 * kWheelSteps, "out");
    if (match_zoom() != least_match_zoom())
        fail("the wheel did not zoom out to the farthest zoom");
    wheel(commander, kWheelSteps, "back");
    if (walker.position[0] == walked_from)
        fail("the tracked commander did not walk while the zoom changed");

    // The settings dialog's ease about the centre, from the zoom it is at
    // and while a menu holds the match.
    for (const bool held : {false, true}) {
        match_paused_ = held;
        const auto what = std::string(held ? "under a menu" : "in play");
        EngineSettingsState::ease_zoom_about_centre(*this, kMaxBattlefieldZoom);
        for (int settle = 0; settle < kSettleFrames; ++settle) {
            frame();
            require_tracking(commander, "while the dialog's zoom eased " + what);
        }
        if (match_zoom() != kMaxBattlefieldZoom)
            fail("the dialog's ease did not reach its zoom " + what);
        EngineSettingsState::ease_zoom_about_centre(*this, kDefaultBattlefieldZoom);
        for (int settle = 0; settle < kSettleFrames; ++settle) {
            frame();
            require_tracking(commander, "while the dialog's zoom eased back " + what);
        }
    }
    match_paused_ = false;

    // A unit in the map's corner: the camera stays at the map's edges at
    // every zoom and goes on tracking it.
    oa::sim::unit_spawn::Request request;
    request.player = match_local_player_;
    request.type = slots[commander].record.type_index;
    request.finished = true;
    request.state = kGroundOccupancyState;
    request.position = {
        static_cast<uint32_t>(kCornerInset) << 16,
        static_cast<uint32_t>(match_->map_height(
            static_cast<uint32_t>(kCornerInset) << 16, static_cast<uint32_t>(kCornerInset) << 16
        )) << 16,
        static_cast<uint32_t>(kCornerInset) << 16
    };
    auto* corner = match_->create(request);
    if (corner == nullptr || corner->unit == nullptr)
        fail("no unit could be placed in the map's corner");
    const auto cornered = corner->unit_index;
    begin_match_tracking(cornered);
    wheel(cornered, kWheelSteps, "in at the map's corner");
    wheel(cornered, -2 * kWheelSteps, "out at the map's corner");
    if (view_camera() != std::array<int32_t, 2>{0, 0})
        fail("the camera tracking the corner unit left the map's corner");
    wheel(cornered, kWheelSteps, "back at the map's corner");

    // A scroll still ends the tracking, and so does the minimap.
    begin_match_tracking(commander);
    scroll_match_view(1, 0, kScrollStep);
    if (match_tracking_ || match_->state().game.follow_unit != 0)
        fail("a scroll did not end the tracking");
    begin_match_tracking(commander);
    render_match_surface();
    if (radar_picture_.width <= 0 || radar_picture_.height <= 0)
        fail("the minimap was not drawn");
    if (!pan_camera_from_radar(
            static_cast<float>(radar_picture_.x + radar_picture_.width / 2),
            static_cast<float>(radar_picture_.y + radar_picture_.height / 2)
        ))
        fail("a click on the minimap did not move the camera");
    if (match_tracking_ || match_->state().game.follow_unit != 0)
        fail("a click on the minimap did not end the tracking");

    std::cout << "tracking zoom check: the wheel and the dialog zoomed between "
              << least_match_zoom() << " and " << kMaxBattlefieldZoom
              << " with the tracked unit at the centre, in play, under a menu and at the "
                 "map's corner; a scroll and the minimap ended the tracking\n";
    stop_match_tracking();
    match_zoom_ = match_zoom_target_ = kDefaultBattlefieldZoom;
    zoom_anchored_ = false;
    return_to_skirmish_menu();
}

} // namespace oa::app
