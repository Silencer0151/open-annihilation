// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Headless checks of the wheel's zoom: about the pointer as far as the map's
// edges allow, within the limits the zoom's settings set, and about a unit
// the camera tracks.
#include "oa/app/runtime.hpp"
#include "oa/app/far_view.hpp"
#include "engine_settings_state.hpp"
#include "match_fault.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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
/// Wheel steps out from the default zoom short of the farthest, and in from
/// the default or the farthest short of the nearest.
constexpr int kZoomOutSteps = 4;
constexpr int kZoomInSteps = 8;
/// Screen pixels inside the map's far edges the pointer rests at.
constexpr int kMapEdgeInset = 3;
/// How much wider and taller than the map at the farthest zoom the wide
/// battlefield is, and the layouts tried to fit it around the side column
/// and bars at their scale.
constexpr float kPastMap = 1.25F;
constexpr int kLayoutFits = 3;
/// Scroll steps the view takes away from the map's corner, and then back.
constexpr int kScrollAwaySteps = 8;
/// Canvas pixels a creeping pointer moves across and down at each wheel step.
constexpr int kPointerCreep = 6;
/// Frames a pinch or the pad's zoom is held for.
constexpr int kHeldZoomFrames = 60;
/// A zoom off the wheel's steps from the default, as a pinch leaves, and a
/// battlefield column and row that lie halfway between two map pixels
/// there, as every odd multiple of 19 does.
constexpr float kOffStepZoom = 1.52F;
constexpr std::array<int, 2> kHalfwayAtOffStep{247, 209};

/// Wheel steps that take the zoom from the nearest to past the farthest
/// any view reaches: 1.15 to the 41st is past 4 times 64.
constexpr int kFarSteps = 41;
/// Windows the zoom's limits are tried on, beside the game's own screen.
constexpr std::array<std::array<int, 2>, 3> kLimitWindows{
    {{1366, 768}, {1920, 1080}, {2560, 1440}}
};

/// Ends the check with a failure.
///
/// @param what what failed
[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("tracking zoom check: " + what);
}

} // namespace

void Runtime::check_wheel_zoom_limits(const std::function<void()>& frame) {
    const auto saved_layout = match_layout_;
    const bool saved_paused = match_paused_;
    // No tick runs and nothing but the wheel moves the camera.
    match_paused_ = true;
    match_pointer_known_ = false;
    stop_match_tracking();
    const auto [map_width, map_height] = shown_map_size();
    const auto farthest = [&] {
        return std::array<int32_t, 2>{
            std::max(0, map_width - visible_map_width()),
            std::max(0, map_height - visible_map_height())
        };
    };
    // The exact map point the view shows at a battlefield point.
    const auto point_at = [&](std::array<int, 2> at) {
        const auto camera = view_camera();
        const auto zoom = static_cast<double>(match_zoom());
        return std::array<double, 2>{camera[0] + at[0] / zoom, camera[1] + at[1] / zoom};
    };
    // The camera a case's steps began from, and the map point the pointer
    // aimed at there.
    std::array<int32_t, 2> began{};
    std::array<double, 2> anchored{};
    const auto aim = [&](std::array<int, 2> at) {
        began = view_camera();
        anchored = point_at(at);
    };
    // A case begins at a zoom and camera, from a frame drawn there.
    const auto place = [&](float zoom, std::array<int32_t, 2> camera, std::array<int, 2> at) {
        match_zoom_ = match_zoom_target_ = zoom;
        zoom_anchored_ = false;
        zoom_hold_ = {};
        match_camera_x_ = camera[0];
        match_camera_z_ = camera[1];
        render_match_surface();
        aim(at);
    };
    // A wheel step at a battlefield point, with frames until the zoom
    // settles, at least one as the application always runs, and then a
    // frame drawn, which holds the stored camera within the map as every
    // drawn frame does.
    const auto notch = [&](std::array<int, 2> at, float way) {
        handle_match_zoom(
            way,
            static_cast<float>(match_layout_.left + at[0]),
            static_cast<float>(match_layout_.top + at[1])
        );
        for (int settle = 0; settle < kSettleFrames; ++settle) {
            frame();
            if (match_zoom_ == match_zoom_target_)
                break;
        }
        render_match_surface();
    };
    // Wheel steps at a battlefield point: after each, on each axis the
    // camera keeps the point aimed at under the pointer to within a map
    // pixel, or is as near that as the camera's limits allow.
    const auto turn = [&](const std::string& what, std::array<int, 2> at, int steps, float way) {
        for (int step = 0; step < steps; ++step) {
            notch(at, way);
            const auto now = view_camera();
            const auto most = farthest();
            const auto zoom = static_cast<double>(match_zoom());
            for (std::size_t axis = 0; axis < 2; ++axis) {
                const double wanted = std::clamp(
                    anchored[axis] - at[axis] / zoom, 0.0, static_cast<double>(most[axis])
                );
                if (std::abs(now[axis] - wanted) > 1.0)
                    fail(
                        what + ": step " + std::to_string(step + 1) +
                        (way > 0.0F ? " in" : " out") + " left the camera " +
                        std::to_string(now[axis] - wanted) + " map pixels " +
                        (axis == 0 ? "across" : "down") +
                        " from where the point under the pointer stays as far as the "
                        "map's edges allow"
                    );
            }
        }
    };
    // The steps back, or steps that leave the zoom as it was, put the
    // camera back where the case's steps began.
    const auto returned = [&](const std::string& what) {
        if (view_camera() != began)
            fail(
                what + ": the steps left the camera at " + std::to_string(view_camera()[0]) + ", " +
                std::to_string(view_camera()[1]) + ", not where they began at " +
                std::to_string(began[0]) + ", " + std::to_string(began[1])
            );
    };
    // `out` steps out and `in` steps in at one point, then as many back.
    const auto wheel_at = [&](const std::string& what,
                              float zoom,
                              std::array<int32_t, 2> camera,
                              std::array<int, 2> at,
                              int out,
                              int in) {
        place(zoom, camera, at);
        turn(what, at, out, -1.0F);
        turn(what, at, in + out, 1.0F);
        turn(what, at, in, -1.0F);
        returned(what);
    };
    // The game's screen, at the game's scale: about the battlefield's centre
    // with the camera in the map's top left and bottom right corners, where
    // zooming out takes the view past the map's edges, and in the middle.
    match_layout_ = lay_out_match(kCanvasWidth, kCanvasHeight);
    const std::array<int, 2> centre{
        match_layout_.battlefield_width() / 2, match_layout_.battlefield_height() / 2
    };
    wheel_at("the top left corner", kDefaultBattlefieldZoom, {0, 0}, centre, kZoomOutSteps, 0);
    wheel_at(
        "the bottom right corner", kDefaultBattlefieldZoom, farthest(), centre, kZoomOutSteps, 0
    );
    wheel_at(
        "the middle",
        kDefaultBattlefieldZoom,
        {farthest()[0] / 2, farthest()[1] / 2},
        centre,
        0,
        kZoomInSteps
    );
    // After a zoom out past the top left corner, the pointer moved to aim
    // elsewhere, and the view scrolled away and back: each zooms in about
    // the point the pointer then rests on.
    const std::array<int, 2> aside{
        match_layout_.battlefield_width() * 3 / 4, match_layout_.battlefield_height() * 3 / 4
    };
    place(kDefaultBattlefieldZoom, {0, 0}, centre);
    turn("a new aim", centre, kZoomOutSteps, -1.0F);
    aim(aside);
    turn("a new aim", aside, kZoomOutSteps, 1.0F);
    turn("a new aim", aside, kZoomOutSteps, -1.0F);
    returned("a new aim");
    place(kDefaultBattlefieldZoom, {0, 0}, centre);
    turn("a view scrolled away and back", centre, kZoomOutSteps, -1.0F);
    for (const int way : {1, -1})
        for (int scroll = 0; scroll < kScrollAwaySteps; ++scroll) {
            scroll_match_view(way, way, kScrollStep);
            frame();
            render_match_surface();
        }
    if (view_camera() != std::array<int32_t, 2>{0, 0})
        fail("the view did not scroll back to the map's top left corner");
    aim(centre);
    turn("a view scrolled away and back", centre, kZoomOutSteps, 1.0F);
    // After a zoom out past the top left corner, a pointer creeping a few
    // pixels at each step in: once it has strayed farther than
    // kZoomHoldPointerSlack from where it rested, each step zooms about the
    // point then under it.
    place(kDefaultBattlefieldZoom, {0, 0}, centre);
    turn("a creeping pointer", centre, kZoomOutSteps, -1.0F);
    std::array<int, 2> creeping = centre;
    for (int step = 0; step < kZoomOutSteps; ++step) {
        creeping = {creeping[0] + kPointerCreep, creeping[1] + kPointerCreep};
        if (creeping[0] - centre[0] <= kZoomHoldPointerSlack) {
            notch(creeping, 1.0F);
            continue;
        }
        aim(creeping);
        turn(
            "a pointer crept " + std::to_string(creeping[0] - centre[0]) + " pixels",
            creeping,
            1,
            1.0F
        );
    }
    // At the nearest zoom, with the pointer on a column and row halfway
    // between two map pixels there: steps in, which leave the zoom as it
    // is, leave the camera where it is, and so does a pinch or the pad's
    // zoom held in; steps out and as many back return it.
    const int nearest = static_cast<int>(kMaxBattlefieldZoom);
    const std::array<int, 2> halfway{
        centre[0] - centre[0] % nearest + nearest / 2, centre[1] - centre[1] % nearest + nearest / 2
    };
    const std::array<int32_t, 2> middle{farthest()[0] / 2, farthest()[1] / 2};
    place(kMaxBattlefieldZoom, middle, halfway);
    turn("the nearest zoom", halfway, kZoomInSteps, 1.0F);
    returned("the nearest zoom");
    for (int held = 0; held < kHeldZoomFrames; ++held) {
        zoom_match_about(
            kZoomWheelFactor,
            static_cast<float>(match_layout_.left + halfway[0]),
            static_cast<float>(match_layout_.top + halfway[1])
        );
        frame();
        render_match_surface();
    }
    returned("a zoom held in at the nearest zoom");
    wheel_at("the nearest zoom", kMaxBattlefieldZoom, middle, halfway, kZoomInSteps, 0);
    // At a zoom off the wheel's steps, with the pointer halfway between two
    // map pixels: steps out and as many back return the zoom exactly, and
    // so the camera.
    wheel_at(
        "a zoom off the wheel's steps", kOffStepZoom, middle, kHalfwayAtOffStep, kZoomOutSteps, 0
    );
    // A battlefield a quarter wider and taller than the map at the farthest
    // zoom, which shows the whole map in its top left with the camera in the
    // corner: about the battlefield's centre and the map's bottom right
    // corner, which the camera's limits hold back for the first steps in.
    const float least = least_match_zoom();
    const auto past_map = [&](int map_pixels) {
        return static_cast<int>(std::ceil(static_cast<float>(map_pixels) * least * kPastMap));
    };
    for (int fit = 0; fit < kLayoutFits; ++fit)
        match_layout_ = lay_out_match(
            match_layout_.left + past_map(map_width),
            match_layout_.top + match_layout_.bottom + past_map(map_height)
        );
    if (static_cast<float>(match_layout_.battlefield_width()) <= map_width * least ||
        static_cast<float>(match_layout_.battlefield_height()) <= map_height * least)
        fail("no battlefield wider and taller than the map at the farthest zoom");
    const std::array<int, 2> map_corner{
        static_cast<int>(static_cast<float>(map_width) * least) - kMapEdgeInset,
        static_cast<int>(static_cast<float>(map_height) * least) - kMapEdgeInset
    };
    wheel_at(
        "the battlefield's centre past the map",
        least,
        {0, 0},
        {match_layout_.battlefield_width() / 2, match_layout_.battlefield_height() / 2},
        0,
        kZoomInSteps
    );
    wheel_at("the map's far corner", least, {0, 0}, map_corner, 0, kZoomInSteps);
    match_layout_ = saved_layout;
    match_zoom_ = match_zoom_target_ = kDefaultBattlefieldZoom;
    zoom_hold_ = {};
    match_paused_ = saved_paused;
    std::cout << "tracking zoom check: the wheel keeps the map point under the pointer as far "
                 "as the map's edges allow, after a new aim, a scroll and a creeping pointer "
                 "too, its steps back return the camera, and steps past the nearest zoom "
                 "leave it\n";
}

void Runtime::check_zoom_limit_choices(const std::function<void()>& frame) {
    namespace settings = oa::ui::engine_settings;
    const auto saved_layout = match_layout_;
    const bool saved_paused = match_paused_;
    const settings::EngineSettings saved = engine_settings();
    match_paused_ = true;
    match_pointer_known_ = false;
    stop_match_tracking();
    const auto [map_width, map_height] = shown_map_size();
    const auto choose = [&](settings::ZoomOutLimit out, settings::ZoomInLimit in) {
        auto chosen = engine_settings();
        chosen.max_zoom_out = out;
        chosen.max_zoom_in = in;
        apply_engine_settings(chosen);
    };
    // Frames until the zoom settles, and then one drawn.
    const auto settle = [&] {
        for (int step = 0; step < kSettleFrames; ++step) {
            frame();
            if (match_zoom_ == match_zoom_target_)
                break;
        }
        render_match_surface();
    };
    const auto wheel = [&](int steps, std::array<int, 2> at) {
        for (int step = 0; step < std::abs(steps); ++step) {
            handle_match_zoom(
                steps > 0 ? 1.0F : -1.0F,
                static_cast<float>(match_layout_.left + at[0]),
                static_cast<float>(match_layout_.top + at[1])
            );
            settle();
        }
    };
    const auto place = [&](float zoom, std::array<int32_t, 2> camera) {
        match_zoom_ = match_zoom_target_ = zoom;
        zoom_anchored_ = false;
        zoom_hold_ = {};
        match_camera_x_ = camera[0];
        match_camera_z_ = camera[1];
        render_match_surface();
    };
    const auto zoom_text = [](float zoom) { return std::to_string(zoom); };
    // The exact map point the view shows at a battlefield point.
    const auto point_at = [&](std::array<int, 2> at) {
        const auto camera = view_camera();
        const auto zoom = static_cast<double>(match_zoom());
        return std::array<double, 2>{camera[0] + at[0] / zoom, camera[1] + at[1] / zoom};
    };
    // Wheel steps that keep the map point aimed at under the pointer there,
    // to within a map pixel, or as near that as the camera's limits allow:
    // steps in after steps out past the map's edge go on about the point
    // the steps out aimed at.
    std::array<double, 2> anchored{};
    const auto aim = [&](std::array<int, 2> at) { anchored = point_at(at); };
    const auto turn = [&](const std::string& what, std::array<int, 2> at, int steps) {
        for (int step = 0; step < std::abs(steps); ++step) {
            wheel(steps > 0 ? 1 : -1, at);
            const auto now = view_camera();
            const auto zoom = static_cast<double>(match_zoom());
            const std::array<int32_t, 2> most{
                std::max(0, map_width - visible_map_width()),
                std::max(0, map_height - visible_map_height())
            };
            for (std::size_t axis = 0; axis < 2; ++axis) {
                const double wanted = std::clamp(
                    anchored[axis] - at[axis] / zoom, 0.0, static_cast<double>(most[axis])
                );
                if (std::abs(now[axis] - wanted) > 1.0)
                    fail(
                        what + ": step " + std::to_string(step + 1) + " at zoom " +
                        zoom_text(match_zoom()) + " left the camera " +
                        std::to_string(now[axis] - wanted) + " map pixels " +
                        (axis == 0 ? "across" : "down") +
                        " from where the point under the pointer stays"
                    );
            }
        }
    };
    std::vector<std::array<int, 2>> windows{{kCanvasWidth, kCanvasHeight}};
    windows.insert(windows.end(), kLimitWindows.begin(), kLimitWindows.end());
    for (const auto& window : windows) {
        match_layout_ = lay_out_match(window[0], window[1]);
        const auto size = std::to_string(window[0]) + "x" + std::to_string(window[1]);
        const int bf_w = match_layout_.battlefield_width();
        const int bf_h = match_layout_.battlefield_height();
        const std::array<int, 2> centre{bf_w / 2, bf_h / 2};
        const std::array<int32_t, 2> middle{
            std::max(0, map_width - bf_w) / 2, std::max(0, map_height - bf_h) / 2
        };
        // The wheel out stops at each choice's floor.
        for (const auto out : settings::zoom_out_limits) {
            choose(out, settings::ZoomInLimit::four_times);
            place(kDefaultBattlefieldZoom, middle);
            wheel(-kFarSteps, centre);
            const float floor =
                least_battlefield_zoom(out, detail_zoom_floor(), map_width, map_height, bf_w, bf_h);
            if (match_zoom() != floor || least_match_zoom() != floor)
                fail(
                    size + ": the wheel stopped at zoom " + zoom_text(match_zoom()) +
                    ", not at the choice's floor " + zoom_text(floor)
                );
            if (out == settings::ZoomOutLimit::automatic && floor != detail_zoom_floor())
                fail(size + ": Automatic left the drawing's floor");
            // Past the drawing's floor the frame is the far view.
            if (far_view_frame() != (floor < detail_zoom_floor()))
                fail(size + ": the far view does not begin past the drawing's floor");
            if (out != settings::ZoomOutLimit::whole_map)
                continue;
            // The whole map in view, the camera at its corner: it fills the
            // battlefield one way and fits within it the other.
            const int wide = visible_map_width(), high = visible_map_height();
            if (floor < kDefaultBattlefieldZoom &&
                (wide < map_width - 1 || high < map_height - 1 ||
                 (std::abs(wide - map_width) > 1 && std::abs(high - map_height) > 1)))
                fail(
                    size + ": Whole map shows " + std::to_string(wide) + "x" +
                    std::to_string(high) + " map pixels of a map of " + std::to_string(map_width) +
                    "x" + std::to_string(map_height)
                );
            if (view_camera() != std::array<int32_t, 2>{0, 0})
                fail(size + ": Whole map left the camera off the map's corner");
        }
        // The wheel in stops at each choice's ceiling.
        for (const auto in : settings::zoom_in_limits) {
            choose(settings::ZoomOutLimit::automatic, in);
            place(kDefaultBattlefieldZoom, middle);
            wheel(kFarSteps, centre);
            if (match_zoom() != settings::closest_zoom(in))
                fail(
                    size + ": the wheel stopped at zoom " + zoom_text(match_zoom()) +
                    ", not at the choice's ceiling " + zoom_text(settings::closest_zoom(in))
                );
        }
        // A pinch or the pad's zoom held out and in stops at both limits.
        choose(settings::ZoomOutLimit::one_quarter, settings::ZoomInLimit::twice);
        place(kDefaultBattlefieldZoom, middle);
        const float least = least_match_zoom();
        for (const float way : {1.0F / kZoomWheelFactor, kZoomWheelFactor}) {
            for (int held = 0; held < 2 * kFarSteps; ++held) {
                zoom_match_about(
                    way,
                    static_cast<float>(match_layout_.left + centre[0]),
                    static_cast<float>(match_layout_.top + centre[1])
                );
                frame();
            }
            const float limit = way < 1.0F ? least : 2.0F;
            if (match_zoom() != limit)
                fail(
                    size + ": a held zoom stopped at " + zoom_text(match_zoom()) +
                    ", not at the limit " + zoom_text(limit)
                );
        }
        // With Whole map: out from the middle to the whole map and back in,
        // and in from the whole map about its far corner and about a point
        // past its edge, the point under the pointer stays.
        choose(settings::ZoomOutLimit::whole_map, settings::ZoomInLimit::four_times);
        place(kDefaultBattlefieldZoom, middle);
        aim(centre);
        turn(size + ": out to the whole map", centre, -kFarSteps);
        turn(size + ": in from the whole map", centre, kFarSteps);
        const float whole = least_match_zoom();
        if (whole >= kDefaultBattlefieldZoom)
            continue;
        const std::array<int, 2> far_corner{
            std::min(bf_w - 1, static_cast<int>(static_cast<float>(map_width) * whole) - 3),
            std::min(bf_h - 1, static_cast<int>(static_cast<float>(map_height) * whole) - 3)
        };
        place(whole, {0, 0});
        aim(far_corner);
        turn(size + ": in about the map's far corner", far_corner, kZoomInSteps);
        // The battlefield's far corner, past the map on the axis it fits.
        place(whole, {0, 0});
        aim({bf_w - 2, bf_h - 2});
        turn(size + ": in about a point past the map", {bf_w - 2, bf_h - 2}, kZoomInSteps);
        // A choice changed in play: the view comes within the new limits.
        place(whole, {0, 0});
        choose(settings::ZoomOutLimit::one_half, settings::ZoomInLimit::four_times);
        frame();
        if (match_zoom() < least_match_zoom() || least_match_zoom() < 0.5F)
            fail(size + ": a view past the new limit stayed past it");
        choose(settings::ZoomOutLimit::whole_map, settings::ZoomInLimit::none);
        place(kMaxBattlefieldZoom, middle);
        frame();
        if (match_zoom() != kDefaultBattlefieldZoom)
            fail(size + ": a view past the new ceiling stayed past it");
    }
    apply_engine_settings(saved);
    match_layout_ = saved_layout;
    match_zoom_ = match_zoom_target_ = kDefaultBattlefieldZoom;
    zoom_anchored_ = false;
    zoom_hold_ = {};
    match_paused_ = saved_paused;
    render_match_surface();
    std::cout << "tracking zoom check: the wheel, a pinch and the pad's zoom stop at each "
                 "Maximum zoom out and Maximum zoom in choice, Whole map shows the whole map, "
                 "past the drawing's floor as the far view, the point under the pointer stays "
                 "on the way to it and back, and a choice changed in play holds the view at "
                 "once\n";
}

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

    check_wheel_zoom_limits(frame);
    check_zoom_limit_choices(frame);

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
