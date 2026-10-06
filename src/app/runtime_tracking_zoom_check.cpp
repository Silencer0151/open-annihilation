// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Headless checks of the wheel's zoom: about the pointer as far as the map's
// edges allow, within the limits the zoom's settings set, with the far
// view's dots and the black past the map taking presses, and about a unit
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
#include <utility>
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
/// Canvas pixels a creeping pointer moves across and down at each wheel
/// step, and at each of a trackpad's small steps, a quarter of a notch.
constexpr int kPointerCreep = 6;
constexpr int kTrackpadCreep = 2;
constexpr float kTrackpadStep = 0.25F;
/// Frames a pinch or the pad's zoom is held for at the nearest zoom, and
/// out and in at the map's corners, edges and middle, and the zoom of each
/// of those frames over the one before.
constexpr int kHeldZoomFrames = 60;
constexpr int kPinchFrames = 12;
constexpr float kPinchFactor = 1.05F;
/// Map pixels the point under the pointer may move at a step where the
/// camera's limits do not stop it: the camera keeps to whole map pixels.
constexpr double kMostSlide = 1.0;
/// Map pixels short of the map's far corner the camera begins at, before
/// steps out take it there and steps in about the corner; and how near the
/// far corner the steps in keep the camera: a battlefield pixel covers two
/// map pixels at the farthest of those steps, and the camera rounds a map
/// pixel either way.
constexpr int32_t kEdgeShort = 64;
constexpr int32_t kEdgeReach = 3;
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

/// Dots' widths from the centre of the commander's dot in the far view to
/// the enemy's, so that the two stand apart.
constexpr int32_t kDotsApart = 2;
/// Screen pixels of black past the map a press there needs at least.
constexpr int kLeastMargin = 8;

/// Ends the check with a failure.
///
/// @param what what failed
[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("tracking zoom check: " + what);
}

} // namespace

void Runtime::check_far_view_presses() {
    namespace settings = oa::ui::engine_settings;
    namespace match_runtime = oa::sim::match_runtime;
    auto& slots = match_->world().slots;
    uint16_t commander = 0;
    for (const auto& slot : slots)
        if (slot.unit_index != 0 && slot.unit != nullptr && slot.record.type_index != 0 &&
            slot.record.owner_index == match_local_player_) {
            if (const auto* def = definition_for(slot.unit_index);
                def != nullptr && def->can_dgun) {
                commander = slot.unit_index;
                break;
            }
        }
    if (commander == 0)
        fail("the far view's presses found no local commander");
    const auto [map_width, map_height] = shown_map_size();
    match_layout_ = lay_out_match(kCanvasWidth, kCanvasHeight);
    auto chosen = engine_settings();
    chosen.max_zoom_out = settings::ZoomOutLimit::whole_map;
    chosen.max_zoom_in = settings::ZoomInLimit::four_times;
    apply_engine_settings(chosen);
    match_zoom_ = match_zoom_target_ = least_match_zoom();
    zoom_anchored_ = false;
    match_camera_x_ = 0;
    match_camera_z_ = 0;
    const auto zoom = static_cast<double>(match_zoom());
    if (!far_view_frame())
        fail("Whole map on the game's screen is not the far view");
    // An enemy far enough from the commander that their dots stand apart
    // and near enough that the commander sees it, toward the map's middle,
    // holding its fire and its ground.
    const auto type = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "CORAK");
    if (type == 0)
        fail("the game lacks CORAK");
    const auto home_x = static_cast<int32_t>(slots[commander].unit->position[0] >> 16);
    const auto home_z = static_cast<int32_t>(slots[commander].unit->position[2] >> 16);
    const auto apart =
        static_cast<int32_t>(std::ceil(static_cast<double>(kDotsApart * far_view_dot_side) / zoom));
    const auto enemy_x = static_cast<uint32_t>(
        std::clamp(home_x + (home_x < map_width / 2 ? apart : -apart), 0, map_width - 1)
    );
    const auto enemy_z = static_cast<uint32_t>(home_z) << 16;
    oa::sim::unit_spawn::Request request;
    request.player = static_cast<uint8_t>(match_local_player_ == 0 ? 1 : 0);
    request.type = type;
    request.finished = true;
    request.state = kGroundOccupancyState;
    request.position = {
        enemy_x << 16,
        static_cast<uint32_t>(match_->map_height(enemy_x << 16, enemy_z)) << 16,
        enemy_z
    };
    auto* spawned = match_->create(request);
    if (spawned == nullptr || spawned->unit == nullptr)
        fail("could not spawn an enemy for the far view's presses");
    const uint16_t enemy = spawned->unit_index;
    ++match_timing_.tick;
    match_->simulation().tick = match_timing_.tick;
    tick_or_raise(*match_);
    match_->stop_orders(enemy);
    slots[enemy].record.flags &= ~(OA_UNIT_FLAG_MOVE_ORDER_MASK | OA_UNIT_FLAG_FIRE_ORDER_MASK);
    render_match_surface();
    const auto dot_at = [&](uint16_t id) {
        return project_match_point(
            live_viewport(
                static_cast<uint32_t>(match_camera_x_), static_cast<uint32_t>(match_camera_z_)
            ),
            slots[id].unit->position
        );
    };
    const auto click = [&](int32_t x, int32_t y) {
        update_pointer(static_cast<float>(x), static_cast<float>(y));
        handle_match_left_click(static_cast<float>(x), static_cast<float>(y), 1);
    };
    const auto first_order = [&] {
        std::array<match_runtime::Match::OrderRecordView, 4> records{};
        const auto count = match_->queue_records(commander, false, records.data(), records.size());
        return std::pair{records[0], count};
    };
    const auto zoom_named = " at zoom " + std::to_string(match_zoom());
    // A click on any pixel of the commander's dot selects it.
    const auto own = dot_at(commander);
    constexpr int32_t reach = far_view_dot_side;
    for (int32_t y = own.y - reach; y <= own.y + reach; ++y)
        for (int32_t x = own.x - reach; x <= own.x + reach; ++x) {
            if (!far_view_dot_covers(own.x, own.y, x, y))
                continue;
            clear_local_selection();
            match_command_ = MatchCommand::none;
            click(x, y);
            if (selected_match_unit_ != commander)
                fail(
                    "a click on the commander's dot at " + std::to_string(x - own.x) + "," +
                    std::to_string(y - own.y) + " from its centre" + zoom_named +
                    " selected unit " + std::to_string(selected_match_unit_)
                );
        }
    // With the commander selected, a click on the enemy's dot attacks it.
    match_->stop_orders(commander);
    const auto foe = dot_at(enemy);
    click(foe.x, foe.y);
    if (const auto [order, count] = first_order();
        count != 1 || order.kind != match_runtime::attack_chase_kind || order.target != enemy)
        fail(
            "a click on the enemy's dot" + zoom_named + " left " + std::to_string(count) +
            " orders, the first of kind " + std::to_string(order.kind) + " on unit " +
            std::to_string(order.target) + ", not an attack on unit " + std::to_string(enemy)
        );
    // A click on the black past the map moves the commander to the ground
    // at the shown map's nearest edge.
    const int bf_w = match_layout_.battlefield_width();
    const int bf_h = match_layout_.battlefield_height();
    const auto drawn_w = static_cast<int>(std::floor(map_width * zoom));
    const auto drawn_h = static_cast<int>(std::floor(map_height * zoom));
    const bool right = bf_w - drawn_w >= kLeastMargin;
    if (!right && bf_h - drawn_h < kLeastMargin)
        fail("Whole map on the game's screen leaves no black past the map");
    const int32_t press_x = match_layout_.left + (right ? (drawn_w + bf_w) / 2 : drawn_w / 2);
    const int32_t press_y = match_layout_.top + (right ? drawn_h / 2 : (drawn_h + bf_h) / 2);
    auto pressed = oa::present::world_renderer::screen_to_map_pixel(
        live_viewport(
            static_cast<uint32_t>(match_camera_x_), static_cast<uint32_t>(match_camera_z_)
        ),
        {press_x, press_y},
        view_offset()
    );
    if (!pressed)
        fail("the press past the map is off the battlefield");
    pressed->x = std::min(pressed->x, static_cast<uint32_t>(map_width - 1));
    pressed->y = std::min(pressed->y, static_cast<uint32_t>(map_height - 1));
    const oa::sim::unit_movement::Terrain terrain(*selected_tnt_);
    const auto edge = oa::sim::gameplay_input::terrain_intersection(
        terrain,
        static_cast<int32_t>(pressed->x),
        static_cast<int32_t>(pressed->y),
        static_cast<int32_t>(selected_tnt_->attribute_width * 16U),
        static_cast<int32_t>(selected_tnt_->attribute_height * 16U)
    );
    match_->stop_orders(commander);
    click(press_x, press_y);
    if (const auto [order, count] = first_order();
        count != 1 || order.point != oa::sim::ground_orders::Point{edge.x, edge.y, edge.z})
        fail(
            "a click on the black " + std::string(right ? "right" : "below") + " of the map" +
            zoom_named + " sent the commander to " + std::to_string(order.point[0] >> 16) + "," +
            std::to_string(order.point[2] >> 16) + ", not to the map's edge at " +
            std::to_string(edge.x >> 16) + "," + std::to_string(edge.z >> 16)
        );
    match_->stop_orders(commander);
    match_->kill_unit(enemy, static_cast<uint8_t>(match_runtime::DeathKind::dismissed));
    clear_local_selection();
    match_command_ = MatchCommand::none;
    std::cout << "tracking zoom check: at Whole map on the game's screen, zoom " << match_zoom()
              << ", a click on any pixel of the commander's dot selects it, "
              << "one on an enemy's dot attacks it, and one on the black "
              << (right ? "right" : "below") << " of the map moves to the map's edge\n";
}

void Runtime::check_wheel_zoom_limits(const std::function<void()>& frame) {
    const auto saved_layout = match_layout_;
    const bool saved_paused = match_paused_;
    // No tick runs and nothing but the zoom moves the camera.
    match_paused_ = true;
    match_pointer_known_ = false;
    stop_match_tracking();
    const auto [map_width, map_height] = shown_map_size();
    // The camera's farthest place at the zoom, as view_camera holds it.
    const auto farthest = [&] {
        return std::array<int32_t, 2>{
            std::max(0, map_width - visible_map_width()),
            std::max(0, map_height - visible_map_height())
        };
    };
    // The exact map point the view as drawn shows at a battlefield point.
    const auto point_at = [&](std::array<int, 2> at) {
        const auto camera = view_camera();
        const auto offset = view_offset();
        const auto zoom = static_cast<double>(match_zoom());
        return std::array<double, 2>{
            camera[0] + offset.x + at[0] / zoom, camera[1] + offset.y + at[1] / zoom
        };
    };
    // The camera and zoom a case began at, and whether the camera's limits
    // stopped any of its steps.
    std::array<int32_t, 2> began{};
    float began_zoom = 0.0F;
    bool limited = false;
    // A case begins at a zoom and camera, from a frame drawn there.
    const auto place = [&](float zoom, std::array<int32_t, 2> camera) {
        match_zoom_ = match_zoom_target_ = zoom;
        zoom_anchored_ = false;
        match_camera_x_ = camera[0];
        match_camera_z_ = camera[1];
        render_match_surface();
        began = view_camera();
        began_zoom = match_zoom_;
        limited = false;
    };
    // After a step at a battlefield point, on each axis: the map point
    // that was under the pointer before the step is under it still, to
    // within a map pixel, or, where the camera's limits stop the camera
    // short of that, the camera sits at the limit.
    const auto stepped =
        [&](const std::string& what, std::array<int, 2> at, std::array<double, 2> before) {
            const auto now = view_camera();
            const auto most = farthest();
            const auto after = point_at(at);
            const auto zoom = static_cast<double>(match_zoom());
            for (std::size_t axis = 0; axis < 2; ++axis) {
                const std::string way = axis == 0 ? " across" : " down";
                const double wanted = before[axis] - at[axis] / zoom;
                if (wanted < 0.0 || wanted > static_cast<double>(most[axis])) {
                    limited = true;
                    const int32_t limit = wanted < 0.0 ? 0 : most[axis];
                    if (now[axis] != limit)
                        fail(
                            what + " at zoom " + std::to_string(match_zoom()) +
                            " left the camera at " + std::to_string(now[axis]) + way +
                            ", not at its limit " + std::to_string(limit)
                        );
                } else if (std::abs(after[axis] - before[axis]) > kMostSlide)
                    fail(
                        what + " at zoom " + std::to_string(match_zoom()) +
                        " moved the point under the pointer " +
                        std::to_string(after[axis] - before[axis]) + " map pixels" + way +
                        ", where the camera's limits let it stay"
                    );
            }
        };
    // A wheel step at a battlefield point, with frames until the zoom
    // settles, at least one as the application always runs, and then a
    // frame drawn.
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
    // Where the pointer is at each step of a case, counted from 0.
    using Path = std::function<std::array<int, 2>(int)>;
    const auto rests_at = [](std::array<int, 2> at) { return Path([at](int) { return at; }); };
    // Wheel steps of `way` notches each, the pointer at `path(first)` and on.
    const auto wheel =
        [&](const std::string& what, const Path& path, int first, int steps, float way) {
            for (int step = first; step < first + steps; ++step) {
                const auto at = path(step);
                const auto before = point_at(at);
                notch(at, way);
                stepped(
                    what + ": step " + std::to_string(step + 1) + (way > 0.0F ? " in" : " out"),
                    at,
                    before
                );
            }
        };
    // `out` steps out, `out` and `in` steps in and `in` steps out, of
    // `notches` each: the zoom comes back exactly, and so does the camera
    // where the pointer rests and the camera's limits stopped no step.
    const auto out_in_out =
        [&](const std::string& what, const Path& path, bool rests, int out, int in, float notches) {
            wheel(what, path, 0, out, -notches);
            wheel(what, path, out, out + in, notches);
            wheel(what, path, 2 * out + in, in, -notches);
            if (match_zoom_ != began_zoom)
                fail(
                    what + ": the steps left the zoom at " + std::to_string(match_zoom_) +
                    ", not where they began at " + std::to_string(began_zoom)
                );
            if (rests && !limited && view_camera() != began)
                fail(
                    what + ": the steps left the camera at " + std::to_string(view_camera()[0]) +
                    ", " + std::to_string(view_camera()[1]) + ", not where they began at " +
                    std::to_string(began[0]) + ", " + std::to_string(began[1])
                );
        };
    // A pinch or the pad's zoom held for `frames` frames at a battlefield
    // point, by `factor` a frame, each frame drawn.
    const auto pinch =
        [&](const std::string& what, std::array<int, 2> at, int frames, float factor) {
            for (int held = 0; held < frames; ++held) {
                const auto before = point_at(at);
                zoom_match_about(
                    factor,
                    static_cast<float>(match_layout_.left + at[0]),
                    static_cast<float>(match_layout_.top + at[1])
                );
                frame();
                render_match_surface();
                stepped(what + ": frame " + std::to_string(held + 1), at, before);
            }
        };
    // The game's screen, at the game's scale, with the camera at each of the
    // map's corners, the middle of each of its edges and its middle.
    match_layout_ = lay_out_match(kCanvasWidth, kCanvasHeight);
    const int bf_w = match_layout_.battlefield_width();
    const int bf_h = match_layout_.battlefield_height();
    const std::array<int, 2> centre{bf_w / 2, bf_h / 2};
    place(kDefaultBattlefieldZoom, {0, 0});
    const auto most = farthest();
    const std::array<int32_t, 2> middle{most[0] / 2, most[1] / 2};
    // Points across the battlefield a moving pointer goes between.
    const std::array<std::array<int, 2>, 5> across_battlefield{
        {{bf_w / 8, bf_h / 8},
         {bf_w * 7 / 8, bf_h / 8},
         {bf_w * 7 / 8, bf_h * 7 / 8},
         {bf_w / 8, bf_h * 7 / 8},
         centre}
    };
    const std::array<const char*, 3> columns{"left", "", "right"};
    const std::array<const char*, 3> rows{"top", "", "bottom"};
    for (int down = 0; down < 3; ++down)
        for (int across = 0; across < 3; ++across) {
            const std::array<int32_t, 2> camera{most[0] * across / 2, most[1] * down / 2};
            const std::string side = std::string(rows[static_cast<std::size_t>(down)]) +
                                     (down != 1 && across != 1 ? " " : "") +
                                     columns[static_cast<std::size_t>(across)];
            const std::string where = side.empty()               ? "the map's middle"
                                      : down != 1 && across != 1 ? "the map's " + side + " corner"
                                                                 : "the map's " + side + " edge";
            // Beside the battlefield's edges that show the map's edges, and a
            // quarter of the way in from the top left on an axis in the
            // map's middle.
            const auto beside = [](int third, int extent) {
                return third == 0   ? kMapEdgeInset
                       : third == 1 ? extent / 4
                                    : extent - 1 - kMapEdgeInset;
            };
            const std::array<int, 2> edge{beside(across, bf_w), beside(down, bf_h)};
            // From the centre toward that point, a few pixels at each step.
            const auto creep = [&](int pixels) {
                return Path([=](int step) {
                    std::array<int, 2> at = centre;
                    const std::array<int, 2> extent{bf_w, bf_h};
                    for (std::size_t axis = 0; axis < 2; ++axis) {
                        const int toward = edge[axis] > centre[axis] ? 1 : -1;
                        at[axis] =
                            std::clamp(centre[axis] + toward * pixels * step, 0, extent[axis] - 1);
                    }
                    return at;
                });
            };
            place(kDefaultBattlefieldZoom, camera);
            out_in_out(
                where + ", the pointer resting at the battlefield's centre",
                rests_at(centre),
                true,
                kZoomOutSteps,
                kZoomInSteps,
                1.0F
            );
            place(kDefaultBattlefieldZoom, camera);
            out_in_out(
                where + ", the pointer resting beside the edge",
                rests_at(edge),
                true,
                kZoomOutSteps,
                kZoomInSteps,
                1.0F
            );
            place(kDefaultBattlefieldZoom, camera);
            out_in_out(
                where + ", the pointer creeping toward the edge",
                creep(kPointerCreep),
                false,
                kZoomOutSteps,
                kZoomInSteps,
                1.0F
            );
            place(kDefaultBattlefieldZoom, camera);
            out_in_out(
                where + ", the pointer moving across the battlefield",
                [&](int step) {
                    return across_battlefield
                        [static_cast<std::size_t>(step) % across_battlefield.size()];
                },
                false,
                kZoomOutSteps,
                kZoomInSteps,
                1.0F
            );
            const int quarters = static_cast<int>(1.0F / kTrackpadStep);
            place(kDefaultBattlefieldZoom, camera);
            out_in_out(
                where + ", a trackpad's small steps with the pointer creeping",
                creep(kTrackpadCreep),
                false,
                kZoomOutSteps * quarters,
                kZoomInSteps * quarters,
                kTrackpadStep
            );
            place(kDefaultBattlefieldZoom, camera);
            const std::string pinched = where + ", a pinch or the pad's zoom held";
            pinch(pinched + " out", centre, kPinchFrames, 1.0F / kPinchFactor);
            pinch(pinched + " in", centre, 2 * kPinchFrames, kPinchFactor);
            pinch(pinched + " out again", centre, kPinchFrames, 1.0F / kPinchFactor);
        }
    // Zooming in at the map's edge reaches the edge: from a camera short of
    // the map's far corner, steps out about the battlefield's centre take
    // the camera to the corner, where its limits stop it, and steps in to
    // the nearest zoom with the pointer on the battlefield's last column
    // and row, where the map's far corner is drawn, keep it there.
    const std::array<int, 2> last{bf_w - 1, bf_h - 1};
    place(kDefaultBattlefieldZoom, {most[0] - kEdgeShort, most[1] - kEdgeShort});
    const std::string far_corner = "zooming in at the map's far corner";
    wheel(far_corner, rests_at(centre), 0, kZoomOutSteps, -1.0F);
    if (view_camera() != farthest())
        fail(far_corner + ": the steps out did not take the camera to the map's far corner");
    const int to_nearest =
        kZoomOutSteps +
        static_cast<int>(std::ceil(std::log(most_match_zoom()) / std::log(kZoomWheelFactor)));
    for (int step = 0; step < to_nearest; ++step) {
        wheel(far_corner, rests_at(last), kZoomOutSteps + step, 1, 1.0F);
        const auto now = view_camera();
        const auto reach = farthest();
        if (now[0] < reach[0] - kEdgeReach || now[1] < reach[1] - kEdgeReach)
            fail(
                far_corner + ": step " + std::to_string(kZoomOutSteps + step + 1) + " in at zoom " +
                std::to_string(match_zoom()) + " left the camera at " + std::to_string(now[0]) +
                ", " + std::to_string(now[1]) + ", not within " + std::to_string(kEdgeReach) +
                " map pixels of the map's far corner at " + std::to_string(reach[0]) + ", " +
                std::to_string(reach[1])
            );
    }
    if (match_zoom_ != most_match_zoom())
        fail(far_corner + ": the steps in did not reach the nearest zoom");
    // At the nearest zoom, with the pointer on a column and row halfway
    // between two map pixels there: steps in, which leave the zoom as it
    // is, leave the camera where it is, and so does a pinch or the pad's
    // zoom held in; steps out and as many back return it.
    const int nearest = static_cast<int>(kMaxBattlefieldZoom);
    const std::array<int, 2> halfway{
        centre[0] - centre[0] % nearest + nearest / 2, centre[1] - centre[1] % nearest + nearest / 2
    };
    place(kMaxBattlefieldZoom, middle);
    wheel("the nearest zoom", rests_at(halfway), 0, kZoomInSteps, 1.0F);
    if (view_camera() != began)
        fail("the nearest zoom: steps in moved the camera");
    pinch("a zoom held in at the nearest zoom", halfway, kHeldZoomFrames, kZoomWheelFactor);
    if (view_camera() != began)
        fail("the nearest zoom: a zoom held in moved the camera");
    out_in_out("the nearest zoom", rests_at(halfway), true, kZoomInSteps, 0, 1.0F);
    // At a zoom off the wheel's steps, with the pointer halfway between two
    // map pixels: steps out and as many back return the zoom exactly, and
    // so the camera.
    place(kOffStepZoom, middle);
    out_in_out(
        "a zoom off the wheel's steps", rests_at(kHalfwayAtOffStep), true, kZoomOutSteps, 0, 1.0F
    );
    // From the default zoom, the steps that reach the farthest zoom, and the
    // nearest, then one past it, which counts nothing, and as many back as
    // reached it: the zoom comes back exactly, and so the camera, which the
    // map's middle never holds back.
    const auto to_end_and_back = [&](const std::string& what, float end, float way) {
        const int reach = static_cast<int>(std::ceil(
            std::abs(std::log(end / kDefaultBattlefieldZoom) / std::log(kZoomWheelFactor))
        ));
        place(kDefaultBattlefieldZoom, middle);
        wheel(what, rests_at(centre), 0, reach + 1, way);
        if (match_zoom_ != end)
            fail(
                what + ": " + std::to_string(reach + 1) + " steps left the zoom at " +
                std::to_string(match_zoom_) + ", short of " + std::to_string(end)
            );
        wheel(what, rests_at(centre), reach + 1, reach, -way);
        if (match_zoom_ != kDefaultBattlefieldZoom)
            fail(
                what + ": " + std::to_string(reach) + " steps back left the zoom at " +
                std::to_string(match_zoom_) + ", not where they began"
            );
        if (limited || view_camera() != began)
            fail(what + ": the steps back did not return the camera");
    };
    to_end_and_back("the farthest zoom and back", least_match_zoom(), -1.0F);
    to_end_and_back("the nearest zoom and back", most_match_zoom(), 1.0F);
    // A battlefield a quarter wider and taller than the map at the farthest
    // zoom, which shows the whole map in its top left with the camera in the
    // corner: steps in and back out about the battlefield's centre and the
    // map's far corner, where the camera's limits stop the camera at the
    // corner until the map fills the view.
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
    place(least, {0, 0});
    out_in_out(
        "a battlefield wider than the map, about its centre",
        rests_at({match_layout_.battlefield_width() / 2, match_layout_.battlefield_height() / 2}),
        true,
        0,
        kZoomInSteps,
        1.0F
    );
    place(least, {0, 0});
    out_in_out(
        "a battlefield wider than the map, about the map's far corner",
        rests_at(map_corner),
        true,
        0,
        kZoomInSteps,
        1.0F
    );
    match_layout_ = saved_layout;
    match_zoom_ = match_zoom_target_ = kDefaultBattlefieldZoom;
    match_paused_ = saved_paused;
    std::cout << "tracking zoom check: at every step of the wheel, a trackpad, a pinch and the "
                 "pad's zoom, at the map's corners, edges and middle, with the pointer resting "
                 "and moving, the map point under the pointer stays there, or the camera sits "
                 "at the limit that stops it; zooming in at the map's far corner reaches it, the "
                 "nearest zoom does not creep, and steps out and back return the zoom, and the "
                 "camera where no limit stopped it\n";
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
        match_camera_x_ = camera[0];
        match_camera_z_ = camera[1];
        render_match_surface();
    };
    const auto zoom_text = [](float zoom) { return std::to_string(zoom); };
    // The exact map point the view as drawn shows at a battlefield point.
    const auto point_at = [&](std::array<int, 2> at) {
        const auto camera = view_camera();
        const auto offset = view_offset();
        const auto zoom = static_cast<double>(match_zoom());
        return std::array<double, 2>{
            camera[0] + offset.x + at[0] / zoom, camera[1] + offset.y + at[1] / zoom
        };
    };
    // Wheel steps at a battlefield point: after each, on each axis, the map
    // point under the pointer before it is under it still, to within a map
    // pixel, or, where the camera's limits stop the camera short of that,
    // the camera sits at the limit.
    const auto turn = [&](const std::string& what, std::array<int, 2> at, int steps) {
        for (int step = 0; step < std::abs(steps); ++step) {
            const auto before = point_at(at);
            wheel(steps > 0 ? 1 : -1, at);
            const auto now = view_camera();
            const auto after = point_at(at);
            const auto zoom = static_cast<double>(match_zoom());
            const std::array<int32_t, 2> most{
                std::max(0, map_width - visible_map_width()),
                std::max(0, map_height - visible_map_height())
            };
            for (std::size_t axis = 0; axis < 2; ++axis) {
                const std::string way = axis == 0 ? " across" : " down";
                const double wanted = before[axis] - at[axis] / zoom;
                const std::string stepped = what + ": step " + std::to_string(step + 1) +
                                            " at zoom " + zoom_text(match_zoom());
                if (wanted < 0.0 || wanted > static_cast<double>(most[axis])) {
                    const int32_t limit = wanted < 0.0 ? 0 : most[axis];
                    if (now[axis] != limit)
                        fail(
                            stepped + " left the camera at " + std::to_string(now[axis]) + way +
                            ", not at its limit " + std::to_string(limit)
                        );
                } else if (std::abs(after[axis] - before[axis]) > kMostSlide)
                    fail(
                        stepped + " moved the point under the pointer " +
                        std::to_string(after[axis] - before[axis]) + " map pixels" + way
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
        // past its edge, each step keeps the point under the pointer, or the
        // camera at the limit that stops it.
        choose(settings::ZoomOutLimit::whole_map, settings::ZoomInLimit::four_times);
        place(kDefaultBattlefieldZoom, middle);
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
        turn(size + ": in about the map's far corner", far_corner, kZoomInSteps);
        // The battlefield's far corner, past the map on the axis it fits.
        place(whole, {0, 0});
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
    check_far_view_presses();
    apply_engine_settings(saved);
    match_layout_ = saved_layout;
    match_zoom_ = match_zoom_target_ = kDefaultBattlefieldZoom;
    zoom_anchored_ = false;
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
        fail("moving the view from the minimap did not move the camera");
    if (match_tracking_ || match_->state().game.follow_unit != 0)
        fail("moving the view from the minimap did not end the tracking");

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
