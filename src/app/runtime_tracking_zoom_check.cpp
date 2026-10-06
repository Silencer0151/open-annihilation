// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Headless checks of the battlefield's zoom and of the view past the map's
// edges: every frame of a zoom by the wheel, a trackpad's small steps, a
// pinch and the pad's zoom keeps the exact map point under the pointer
// there, about a resting pointer and a moving one, on the map and past its
// edges, and steps out and as many back return the zoom and the view; a
// zoom in from the whole map lands on the point under the pointer; the
// limits the zoom's settings set; the view going past the map's edges until
// the map's edge reaches the battlefield's middle, or the map's centre the
// view's edge, and held there from a zoom past them; an aircraft past the
// map's edge drawn, hovered and selected, as a model near and far and as a
// dot; the camera saves and the digest take, held on the map; the far
// view's dots and the black past the map taking presses; and a zoom ending
// the follow of a unit, which the settings dialog's zoom keeps.
#include "oa/app/runtime.hpp"
#include "oa/app/far_view.hpp"
#include "engine_settings_state.hpp"
#include "match_fault.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
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
/// Wheel steps from the default zoom out to the farthest and in to the
/// nearest the follow is checked over.
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
/// Screen pixels inside the battlefield's edges the pointer rests at.
constexpr int kMapEdgeInset = 3;
/// Canvas pixels a creeping pointer moves across and down at each frame,
/// with the wheel's notches and with a trackpad's small steps, a quarter of
/// a notch each frame.
constexpr int kPointerCreep = 2;
constexpr int kTrackpadCreep = 1;
constexpr float kTrackpadStep = 0.25F;
/// Frames between two notches of the wheel, as a hand turns it.
constexpr int kNotchFrames = 6;
/// Frames a pinch or the pad's zoom is held for out and in, and the zoom of
/// each of those frames over the one before.
constexpr int kPinchFrames = 12;
constexpr float kPinchFactor = 1.05F;
/// Map pixels the exact map point under the pointer may move in a frame of
/// a zoom, and the view's exact place may lie from where steps out and as
/// many back began: what double arithmetic leaves.
constexpr double kMostAnchorStray = 1.0e-6;
/// Map pixels the camera may lie from the view's exact place: the nearest
/// whole map pixel.
constexpr double kMostCameraStray = 0.5 + 1.0e-9;
/// A zoom off the wheel's steps from the default, as a pinch leaves.
constexpr float kOffStepZoom = 1.52F;
/// Wheel steps that take the zoom from the nearest to past the farthest
/// any view reaches: 1.15 to the 41st is past 4 times 64.
constexpr int kFarSteps = 41;
/// Windows the zoom's limits are tried on, beside the game's own screen.
constexpr std::array<std::array<int, 2>, 3> kLimitWindows{
    {{1366, 768}, {1920, 1080}, {2560, 1440}}
};
/// Points of the shown map, as shares of its width and height, a zoom in
/// from the whole map is aimed at.
constexpr std::array<std::array<double, 2>, 4> kWholeMapAims{
    {{0.1, 0.1}, {0.75, 0.6}, {0.9, 0.5}, {0.5, 0.95}}
};
/// Screen pixels a scroll moves the view by in each frame that runs it to
/// the view's limits, and frames it is given to get there.
constexpr double kLimitScrollStep = 96.0;
constexpr int kLimitScrollFrames = 2000;

/// Dots' widths from the centre of the commander's dot in the far view to
/// the enemy's, so that the two stand apart.
constexpr int32_t kDotsApart = 2;
/// Screen pixels of black past the map a press there needs at least.
constexpr int kLeastMargin = 8;
/// Map pixels past the map's left edge an aircraft is put, within the view
/// whose camera lies half the battlefield past that edge.
constexpr int32_t kAircraftPast = 160;
/// Map pixels above the ground the aircraft flies at.
constexpr int32_t kAircraftHeight = 96;
/// Screen pixels either side of where the aircraft is drawn its picture is
/// looked for in.
constexpr int kDrawnReach = 8;

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
    set_camera_position(0, 0, 0);
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
            live_viewport(match_camera_x_, match_camera_z_), slots[id].unit->position
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
    // A click on any pixel of the commander's dot selects it: drawn as a
    // dot with Zoomed out units at Dots, and drawn as a model, a pixel or
    // two at this zoom, with Rendered.
    const auto own = dot_at(commander);
    constexpr int32_t reach = far_view_dot_side;
    for (const auto units : {settings::ZoomedOutUnits::rendered, settings::ZoomedOutUnits::dots}) {
        chosen.zoomed_out_units = units;
        apply_engine_settings(chosen);
        render_match_surface();
        if (dots_frame() != (units == settings::ZoomedOutUnits::dots))
            fail("the far view did not draw its units as Zoomed out units says");
        const std::string drawn = units == settings::ZoomedOutUnits::dots ? " as a dot" : "";
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
                        std::to_string(y - own.y) + " from its centre" + zoom_named + drawn +
                        " selected unit " + std::to_string(selected_match_unit_)
                    );
            }
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
    // A click on the black past the map, right of it with the camera at the
    // map's corner and left of it with the map in the middle, moves the
    // commander to the ground at the nearest point of the shown map.
    const int bf_w = match_layout_.battlefield_width();
    const auto drawn_w = static_cast<int>(std::floor(map_width * zoom));
    const auto drawn_h = static_cast<int>(std::floor(map_height * zoom));
    if (bf_w - drawn_w < 2 * kLeastMargin)
        fail("Whole map on the game's screen leaves no black beside the map");
    const auto middle_camera = -static_cast<int32_t>(std::lround((bf_w - drawn_w) / 2.0 / zoom));
    for (const bool left : {false, true}) {
        set_camera_position(left ? middle_camera : 0, 0, 0);
        render_match_surface();
        const auto map_left =
            static_cast<int>(std::lround(static_cast<double>(-match_camera_x_) * zoom));
        const int32_t press_x =
            match_layout_.left + (left ? map_left / 2 : (map_left + drawn_w + bf_w) / 2);
        const int32_t press_y = match_layout_.top + drawn_h / 2;
        auto pressed = oa::present::world_renderer::screen_to_map_pixel(
            live_viewport(match_camera_x_, match_camera_z_), {press_x, press_y}, view_offset()
        );
        if (!pressed)
            fail("the press past the map is off the battlefield");
        if (left ? pressed->x >= 0 : pressed->x < map_width)
            fail("the press beside the map is on the map");
        pressed->x = std::clamp(pressed->x, 0, map_width - 1);
        pressed->y = std::clamp(pressed->y, 0, map_height - 1);
        const oa::sim::unit_movement::Terrain terrain(*selected_tnt_);
        const auto edge = oa::sim::gameplay_input::terrain_intersection(
            terrain,
            pressed->x,
            pressed->y,
            static_cast<int32_t>(selected_tnt_->attribute_width * 16U),
            static_cast<int32_t>(selected_tnt_->attribute_height * 16U)
        );
        match_->stop_orders(commander);
        click(press_x, press_y);
        if (const auto [order, count] = first_order();
            count != 1 || order.point != oa::sim::ground_orders::Point{edge.x, edge.y, edge.z})
            fail(
                "a click on the black " + std::string(left ? "left" : "right") + " of the map" +
                zoom_named + " sent the commander to " + std::to_string(order.point[0] >> 16) +
                "," + std::to_string(order.point[2] >> 16) + ", not to the map's edge at " +
                std::to_string(edge.x >> 16) + "," + std::to_string(edge.z >> 16)
            );
    }
    match_->stop_orders(commander);
    match_->kill_unit(enemy, static_cast<uint8_t>(match_runtime::DeathKind::dismissed));
    clear_local_selection();
    match_command_ = MatchCommand::none;
    std::cout << "tracking zoom check: at Whole map on the game's screen, zoom " << match_zoom()
              << ", a click on any pixel of the commander's dot selects it, drawn as a model "
                 "and as a dot, one on an enemy's dot attacks it, and one on the black right or "
                 "left of the map moves to the map's edge\n";
}

void Runtime::check_zoom_about_pointer(const std::function<void()>& frame) {
    const auto saved_layout = match_layout_;
    const bool saved_paused = match_paused_;
    // No tick runs and nothing but the zoom moves the camera.
    match_paused_ = true;
    match_pointer_known_ = false;
    stop_match_tracking();
    bool running = true;
    const auto [map_width, map_height] = shown_map_size();
    const std::array<int32_t, 2> map_size{map_width, map_height};
    // The exact map point under a battlefield point.
    const auto point_at = [&](std::array<double, 2> at) {
        const auto place = match_view_place();
        const auto zoom = static_cast<double>(match_zoom());
        return std::array<double, 2>{place[0] + at[0] / zoom, place[1] + at[1] / zoom};
    };
    // The pointer moved to a battlefield point, as SDL reports a mouse's
    // motion; the screen's edges scroll nothing, as in the frames of the
    // checks around.
    const auto move_pointer = [&](std::array<double, 2> at) {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.which = 1;
        event.motion.x = static_cast<float>(static_cast<double>(match_layout_.left) + at[0]);
        event.motion.y = static_cast<float>(static_cast<double>(match_layout_.top) + at[1]);
        handle_sdl_event(event, running);
        match_pointer_known_ = false;
    };
    // A case begins at a zoom and a view, from a frame drawn there.
    std::array<double, 2> began{};
    float began_zoom = 0.0F;
    const auto place = [&](float zoom, std::array<int32_t, 2> camera) {
        match_zoom_ = match_zoom_target_ = zoom;
        zoom_wheel_ = {};
        set_camera_position(camera[0], camera[1], 0);
        view_hold_ = {};
        render_match_surface();
        began = match_view_place();
        began_zoom = match_zoom_;
    };

    // A gesture: what it does at each of its steps, a frame apart, given
    // the battlefield point the pointer is at.
    struct Gesture {
        int steps{};
        std::function<void(int, std::array<double, 2>)> act;
    };

    using Path = std::function<std::array<double, 2>(int)>;
    // A zoom's frames: before each of the gesture's, the pointer moved to
    // the path's point for it and the gesture's step; after each, drawn,
    // the exact map point that was under the pointer before the frame is
    // under it still, wherever it lies on the map, and the camera is the
    // nearest whole map pixel to the view's exact place. After the
    // gesture, frames with the pointer resting until the zoom settles.
    const auto zoom_frames =
        [&](const std::string& what, const Path& path, const Gesture& gesture) {
            std::array<double, 2> at{};
            for (int step = 0; step < gesture.steps + kSettleFrames; ++step) {
                const bool acting = step < gesture.steps;
                if (!acting && match_zoom_ == match_zoom_target_)
                    return;
                if (acting)
                    at = path(step);
                move_pointer(at);
                const auto before = point_at(at);
                if (acting)
                    gesture.act(step, at);
                frame();
                render_match_surface();
                const auto after = point_at(at);
                const auto view = match_view_place();
                const std::array<int32_t, 2> camera{match_camera_x_, match_camera_z_};
                for (std::size_t axis = 0; axis < 2; ++axis) {
                    const std::string way = axis == 0 ? " across" : " down";
                    const bool on_map =
                        before[axis] >= 0.0 && before[axis] <= static_cast<double>(map_size[axis]);
                    if (on_map && std::abs(after[axis] - before[axis]) > kMostAnchorStray)
                        fail(
                            what + ": frame " + std::to_string(step + 1) + " at zoom " +
                            std::to_string(match_zoom()) + " moved the point under the pointer " +
                            std::to_string(after[axis] - before[axis]) + " map pixels" + way
                        );
                    if (std::abs(static_cast<double>(camera[axis]) - view[axis]) > kMostCameraStray)
                        fail(
                            what + ": frame " + std::to_string(step + 1) + " drew the camera " +
                            std::to_string(camera[axis]) + way + " for a view at " +
                            std::to_string(view[axis])
                        );
                }
            }
            fail(what + ": the zoom did not settle");
        };
    // Wheel notches at the pointer, `out` out, `out` and `in` in and `in`
    // out again, of `notches` each, a notch every `apart` frames.
    const auto wheel = [this](int out, int in, float notches, int apart) {
        return Gesture{2 * (out + in) * apart, [=, this](int step, std::array<double, 2> at) {
                           if (step % apart != 0)
                               return;
                           const int notch = step / apart;
                           const float way =
                               notch < out || notch >= 2 * out + in ? -notches : notches;
                           handle_match_zoom(
                               way,
                               static_cast<float>(static_cast<double>(match_layout_.left) + at[0]),
                               static_cast<float>(static_cast<double>(match_layout_.top) + at[1]),
                               true
                           );
                       }};
    };
    // Wheel notches all one way, `count` of `notches` each, a notch every
    // `apart` frames.
    const auto notches_one_way = [this](int count, float notches, int apart) {
        return Gesture{
            count * apart, [=, this](int step, std::array<double, 2> at) {
                if (step % apart == 0)
                    handle_match_zoom(
                        notches,
                        static_cast<float>(static_cast<double>(match_layout_.left) + at[0]),
                        static_cast<float>(static_cast<double>(match_layout_.top) + at[1]),
                        true
                    );
            }
        };
    };
    // A pinch or the pad's zoom held for `frames` frames, by `factor` a
    // frame, or out, in and out again when `factor` is none.
    const auto pinch = [this](int frames, std::optional<float> factor) {
        return Gesture{factor ? frames : 4 * frames, [=, this](int step, std::array<double, 2> at) {
                           const bool in = step >= frames && step < 3 * frames;
                           zoom_match_about(
                               factor ? *factor
                               : in   ? kPinchFactor
                                      : 1.0F / kPinchFactor,
                               static_cast<float>(static_cast<double>(match_layout_.left) + at[0]),
                               static_cast<float>(static_cast<double>(match_layout_.top) + at[1])
                           );
                       }};
    };
    // The gesture's steps ended where they began: the zoom exactly, and the
    // view where the pointer rested.
    const auto returned = [&](const std::string& what, bool view) {
        if (match_zoom_ != began_zoom)
            fail(
                what + ": the steps left the zoom at " + std::to_string(match_zoom_) +
                ", not where they began at " + std::to_string(began_zoom)
            );
        const auto now = match_view_place();
        if (view && (std::abs(now[0] - began[0]) > kMostAnchorStray ||
                     std::abs(now[1] - began[1]) > kMostAnchorStray))
            fail(
                what + ": the steps left the view at " + std::to_string(now[0]) + ", " +
                std::to_string(now[1]) + ", not where they began at " + std::to_string(began[0]) +
                ", " + std::to_string(began[1])
            );
    };
    match_layout_ = lay_out_match(kCanvasWidth, kCanvasHeight);
    const int bf_w = match_layout_.battlefield_width();
    const int bf_h = match_layout_.battlefield_height();
    const std::array<double, 2> centre{bf_w / 2.0 + 0.37, bf_h / 2.0 + 0.61};
    const std::array<int32_t, 2> most{
        std::max(0, map_width - visible_map_width()), std::max(0, map_height - visible_map_height())
    };
    // Points across the battlefield a moving pointer goes between.
    const std::array<std::array<double, 2>, 5> across_battlefield{
        {{bf_w / 8.0, bf_h / 8.0},
         {bf_w * 7.0 / 8.0, bf_h / 8.0},
         {bf_w * 7.0 / 8.0, bf_h * 7.0 / 8.0},
         {bf_w / 8.0, bf_h * 7.0 / 8.0},
         centre}
    };
    const std::array<const char*, 3> columns{"left", "", "right"};
    const std::array<const char*, 3> rows{"top", "", "bottom"};
    int cases = 0;

    // The view at each of the map's corners, the middle of each of its
    // edges and its middle, and with the map's corner and the middle of
    // its left edge at the battlefield's middle, past the map.
    struct Place {
        std::array<int32_t, 2> camera;
        std::string where;
        std::array<int, 2> thirds;
    };

    std::vector<Place> places;
    for (int down = 0; down < 3; ++down)
        for (int across = 0; across < 3; ++across) {
            const std::string side = std::string(rows[static_cast<std::size_t>(down)]) +
                                     (down != 1 && across != 1 ? " " : "") +
                                     columns[static_cast<std::size_t>(across)];
            places.push_back(
                {{most[0] * across / 2, most[1] * down / 2},
                 side.empty()               ? "the map's middle"
                 : down != 1 && across != 1 ? "the map's " + side + " corner"
                                            : "the map's " + side + " edge",
                 {across, down}}
            );
        }
    places.push_back({{-bf_w / 2, -bf_h / 2}, "past the map's top left corner", {0, 0}});
    places.push_back({{-bf_w / 2, most[1] / 2}, "past the map's left edge", {0, 1}});
    for (const auto& at : places) {
        // Beside the battlefield's edges that show the map's edges, and a
        // quarter of the way in from the top left on an axis in the
        // map's middle.
        const auto beside = [](int third, int extent) {
            return third == 0   ? kMapEdgeInset + 0.25
                   : third == 1 ? extent / 4.0
                                : extent - 1 - kMapEdgeInset + 0.75;
        };
        const std::array<double, 2> edge{beside(at.thirds[0], bf_w), beside(at.thirds[1], bf_h)};
        // From the centre toward that point, a few pixels at each frame.
        const auto creep = [&](int pixels) {
            return Path([=](int step) {
                std::array<double, 2> to = centre;
                const std::array<double, 2> extent{static_cast<double>(bf_w), bf_h * 1.0};
                for (std::size_t axis = 0; axis < 2; ++axis) {
                    const double toward = edge[axis] > centre[axis] ? 1.0 : -1.0;
                    to[axis] =
                        std::clamp(centre[axis] + toward * pixels * step, 0.0, extent[axis] - 1.0);
                }
                return to;
            });
        };
        const auto rests_at = [](std::array<double, 2> point) {
            return Path([point](int) { return point; });
        };
        const int quarters = static_cast<int>(1.0F / kTrackpadStep);
        // A view zoomed about a point past the map's edge is held within its
        // limits, so that only a point of the map returns the view.
        const auto on_map = [&](std::array<double, 2> point) {
            const auto under = point_at(point);
            return under[0] >= 0.0 && under[0] <= map_width && under[1] >= 0.0 &&
                   under[1] <= map_height;
        };
        place(kDefaultBattlefieldZoom, at.camera);
        zoom_frames(
            at.where + ", the wheel, the pointer resting at the battlefield's centre",
            rests_at(centre),
            wheel(kZoomOutSteps, kZoomInSteps, 1.0F, kNotchFrames)
        );
        returned(at.where + ", the pointer resting at the battlefield's centre", on_map(centre));
        place(kDefaultBattlefieldZoom, at.camera);
        zoom_frames(
            at.where + ", the wheel, the pointer resting beside the edge",
            rests_at(edge),
            wheel(kZoomOutSteps, kZoomInSteps, 1.0F, kNotchFrames)
        );
        returned(at.where + ", the pointer resting beside the edge", on_map(edge));
        place(kDefaultBattlefieldZoom, at.camera);
        zoom_frames(
            at.where + ", the wheel, the pointer creeping toward the edge",
            creep(kPointerCreep),
            wheel(kZoomOutSteps, kZoomInSteps, 1.0F, kNotchFrames)
        );
        returned(at.where + ", the pointer creeping toward the edge", false);
        place(kDefaultBattlefieldZoom, at.camera);
        zoom_frames(
            at.where + ", the wheel, the pointer moving across the battlefield",
            [&](int step) {
                return across_battlefield
                    [static_cast<std::size_t>(step / kNotchFrames) % across_battlefield.size()];
            },
            wheel(kZoomOutSteps, kZoomInSteps, 1.0F, kNotchFrames)
        );
        returned(at.where + ", the pointer moving across the battlefield", false);
        place(kDefaultBattlefieldZoom, at.camera);
        zoom_frames(
            at.where + ", a trackpad's small steps, the pointer creeping",
            creep(kTrackpadCreep),
            wheel(kZoomOutSteps * quarters, kZoomInSteps * quarters, kTrackpadStep, 1)
        );
        returned(at.where + ", a trackpad's small steps", false);
        place(kDefaultBattlefieldZoom, at.camera);
        zoom_frames(
            at.where + ", a pinch or the pad's zoom", rests_at(centre), pinch(kPinchFrames, {})
        );
        cases += 6;
    }
    // At the nearest zoom: steps in, which leave the zoom as it is, leave
    // the view where it is, and so does a pinch or the pad's zoom held in;
    // steps out and as many back return it.
    const std::array<int32_t, 2> middle{most[0] / 2, most[1] / 2};
    const auto quiet = [&](const std::string& what) {
        const auto now = match_view_place();
        if (now != began || match_zoom_ != began_zoom)
            fail(what + " moved the view or the zoom");
    };
    place(kMaxBattlefieldZoom, middle);
    zoom_frames(
        "the nearest zoom", [&](int) { return centre; }, notches_one_way(kZoomInSteps, 1.0F, 1)
    );
    quiet("steps in at the nearest zoom");
    zoom_frames(
        "a zoom held in at the nearest zoom",
        [&](int) { return centre; },
        pinch(kPinchFrames, kZoomWheelFactor)
    );
    quiet("a zoom held in at the nearest zoom");
    place(kMaxBattlefieldZoom, middle);
    zoom_frames(
        "the nearest zoom, out and back",
        [&](int) { return centre; },
        wheel(kZoomInSteps, 0, 1.0F, kNotchFrames)
    );
    returned("the nearest zoom, out and back", true);
    // At a zoom off the wheel's steps: steps out and as many back return
    // the zoom exactly, and so the view.
    place(kOffStepZoom, middle);
    zoom_frames(
        "a zoom off the wheel's steps",
        [&](int) { return centre; },
        wheel(kZoomOutSteps, 0, 1.0F, kNotchFrames)
    );
    returned("a zoom off the wheel's steps", true);
    // From the default zoom, the steps that reach the farthest zoom, and the
    // nearest, then one past it, which counts nothing, and as many back as
    // reached it: the zoom comes back exactly, and so the view.
    const auto to_end_and_back = [&](const std::string& what, float end, float way) {
        const int reach = static_cast<int>(std::ceil(
            std::abs(std::log(end / kDefaultBattlefieldZoom) / std::log(kZoomWheelFactor))
        ));
        place(kDefaultBattlefieldZoom, middle);
        zoom_frames(
            what,
            [&](int) { return centre; },
            Gesture{(2 * reach + 1) * kNotchFrames, [=, this](int step, std::array<double, 2> at) {
                        if (step % kNotchFrames != 0)
                            return;
                        if (step / kNotchFrames == reach + 1 && match_zoom_target_ != end)
                            fail(what + ": the steps out did not reach " + std::to_string(end));
                        handle_match_zoom(
                            step / kNotchFrames <= reach ? way : -way,
                            static_cast<float>(static_cast<double>(match_layout_.left) + at[0]),
                            static_cast<float>(static_cast<double>(match_layout_.top) + at[1]),
                            true
                        );
                    }}
        );
        returned(what, true);
    };
    to_end_and_back("the farthest zoom and back", least_match_zoom(), -1.0F);
    to_end_and_back("the nearest zoom and back", most_match_zoom(), 1.0F);
    match_layout_ = saved_layout;
    match_zoom_ = match_zoom_target_ = kDefaultBattlefieldZoom;
    match_paused_ = saved_paused;
    std::cout << "tracking zoom check: on every frame of " << cases
              << " zooms by the wheel, a trackpad, a pinch and the pad, at the map's corners, "
                 "edges and middle and past them, with the pointer resting and moving, the map "
                 "point under the pointer stays there and the camera on the nearest map pixel; "
                 "the nearest zoom does not creep, and steps out and back return the zoom, and "
                 "the view where the pointer rested\n";
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
    const std::array<int32_t, 2> map_size{map_width, map_height};
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
            render_match_surface();
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
                static_cast<float>(match_layout_.top + at[1]),
                true
            );
            settle();
        }
    };
    const auto place = [&](float zoom, std::array<int32_t, 2> camera) {
        match_zoom_ = match_zoom_target_ = zoom;
        set_camera_position(camera[0], camera[1], 0);
        view_hold_ = {};
        render_match_surface();
    };
    const auto zoom_text = [](float zoom) { return std::to_string(zoom); };
    // The exact map point under a battlefield point.
    const auto point_at = [&](std::array<int, 2> at) {
        const auto place = match_view_place();
        const auto zoom = static_cast<double>(match_zoom());
        return std::array<double, 2>{place[0] + at[0] / zoom, place[1] + at[1] / zoom};
    };
    // The view's centre, in map pixels.
    const auto centre_of_view = [&] {
        const auto place = match_view_place();
        const auto zoom = static_cast<double>(match_zoom());
        return std::array<double, 2>{
            place[0] + match_layout_.battlefield_width() / zoom / 2.0,
            place[1] + match_layout_.battlefield_height() / zoom / 2.0
        };
    };
    // The map pixels the view shows along each axis.
    const auto visible_extent = [&] {
        const auto zoom = static_cast<double>(match_zoom());
        return std::array<double, 2>{
            match_layout_.battlefield_width() / zoom, match_layout_.battlefield_height() / zoom
        };
    };
    // Wheel steps at a battlefield point: after each, on each axis, the map
    // point under the pointer before it is under it still where it lies on
    // the map; where it lies past the map's edge and the step leaves it
    // there, the view's centre is within the view's limits at the zoom
    // before the step or after it, or no further from them than it was. A
    // point past the edge the step brings onto the map is held there from
    // then on, wherever that takes the view.
    const auto turn = [&](const std::string& what, std::array<int, 2> at, int steps) {
        for (int step = 0; step < std::abs(steps); ++step) {
            const auto before = point_at(at);
            const auto centre_before = centre_of_view();
            const auto visible_before = visible_extent();
            wheel(steps > 0 ? 1 : -1, at);
            const auto after = point_at(at);
            const auto centre_after = centre_of_view();
            const auto visible = visible_extent();
            for (std::size_t axis = 0; axis < 2; ++axis) {
                const std::string way = axis == 0 ? " across" : " down";
                const std::string stepped = what + ": step " + std::to_string(step + 1) +
                                            " at zoom " + zoom_text(match_zoom());
                if (before[axis] >= 0.0 && before[axis] <= map_size[axis]) {
                    if (std::abs(after[axis] - before[axis]) > kMostAnchorStray)
                        fail(
                            stepped + " moved the point under the pointer " +
                            std::to_string(after[axis] - before[axis]) + " map pixels" + way
                        );
                    continue;
                }
                if (after[axis] >= 0.0 && after[axis] <= map_size[axis])
                    continue;
                const auto span = view_centre_span(map_size[axis], visible[axis]);
                const auto span_before = view_centre_span(map_size[axis], visible_before[axis]);
                if (centre_after[axis] <
                        std::min({span.least, span_before.least, centre_before[axis]}) - 1.0 ||
                    centre_after[axis] >
                        std::max({span.most, span_before.most, centre_before[axis]}) + 1.0)
                    fail(stepped + " took the view past its limits" + way);
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
        const std::array<int32_t, 2> middle{(map_width - bf_w) / 2, (map_height - bf_h) / 2};
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
            // Zoomed out about the map's middle, the whole map in view: it
            // fills the battlefield one way and fits within it the other.
            const int wide = visible_map_width(), high = visible_map_height();
            if (floor < kDefaultBattlefieldZoom &&
                (wide < map_width - 1 || high < map_height - 1 ||
                 (std::abs(wide - map_width) > 1 && std::abs(high - map_height) > 1)))
                fail(
                    size + ": Whole map shows " + std::to_string(wide) + "x" +
                    std::to_string(high) + " map pixels of a map of " + std::to_string(map_width) +
                    "x" + std::to_string(map_height)
                );
            const auto view = match_view_place();
            if (floor < kDefaultBattlefieldZoom &&
                (view[0] > 1.0 || view[1] > 1.0 || view[0] + wide < map_width - 1.0 ||
                 view[1] + high < map_height - 1.0))
                fail(size + ": Whole map about the map's middle left part of the map out of view");
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
        // past its edge, each step keeps the point under the pointer.
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
        // From the whole map, a zoom in aimed at points across the map lands
        // on each: the point stays under the pointer.
        for (const auto& share : kWholeMapAims) {
            place(whole, {(map_width - static_cast<int32_t>(bf_w / whole)) / 2, 0});
            const std::array<double, 2> aim{share[0] * map_width, share[1] * map_height};
            const auto view = match_view_place();
            const std::array<int, 2> pointer{
                static_cast<int>(std::lround((aim[0] - view[0]) * whole)),
                static_cast<int>(std::lround((aim[1] - view[1]) * whole))
            };
            if (pointer[0] < 0 || pointer[1] < 0 || pointer[0] >= bf_w || pointer[1] >= bf_h)
                fail(size + ": a point of the whole map is out of view");
            const auto under = point_at(pointer);
            turn(size + ": in from the whole map at a point of it", pointer, kZoomInSteps * 2);
            const auto landed = point_at(pointer);
            if (std::abs(landed[0] - under[0]) > kMostAnchorStray ||
                std::abs(landed[1] - under[1]) > kMostAnchorStray)
                fail(size + ": a zoom in from the whole map did not land where it was aimed");
        }
        // A choice changed in play: the view eases within the new limits
        // about the battlefield's centre, half a pixel in from an even
        // battlefield's middle column and row.
        const auto middle_point = [&] {
            const auto view = match_view_place();
            const auto zoom = static_cast<double>(match_zoom());
            return std::array<double, 2>{view[0] + bf_w / 2.0 / zoom, view[1] + bf_h / 2.0 / zoom};
        };
        place(whole, {0, 0});
        const auto held = middle_point();
        choose(settings::ZoomOutLimit::one_half, settings::ZoomInLimit::four_times);
        settle();
        if (match_zoom() < least_match_zoom() || least_match_zoom() < 0.5F)
            fail(size + ": a view past the new limit stayed past it");
        if (const auto eased = middle_point(); std::abs(eased[0] - held[0]) > kMostAnchorStray ||
                                               std::abs(eased[1] - held[1]) > kMostAnchorStray)
            fail(size + ": a view brought within the new limit moved off the battlefield's centre");
        choose(settings::ZoomOutLimit::whole_map, settings::ZoomInLimit::none);
        place(kMaxBattlefieldZoom, middle);
        settle();
        if (match_zoom() != kDefaultBattlefieldZoom)
            fail(size + ": a view past the new ceiling stayed past it");
    }
    check_far_view_presses();
    apply_engine_settings(saved);
    match_layout_ = saved_layout;
    match_zoom_ = match_zoom_target_ = kDefaultBattlefieldZoom;
    match_paused_ = saved_paused;
    render_match_surface();
    std::cout << "tracking zoom check: the wheel, a pinch and the pad's zoom stop at each "
                 "Maximum zoom out and Maximum zoom in choice, Whole map shows the whole map, "
                 "past the drawing's floor as the far view, the point under the pointer stays "
                 "on the way to it and back and a zoom in from it lands where it is aimed, and a "
                 "choice changed in play eases the view within it\n";
}

void Runtime::check_view_past_map(const std::function<void()>& frame) {
    namespace settings = oa::ui::engine_settings;
    namespace match_runtime = oa::sim::match_runtime;
    const auto saved_layout = match_layout_;
    const bool saved_paused = match_paused_;
    const settings::EngineSettings saved = engine_settings();
    match_paused_ = true;
    match_pointer_known_ = false;
    stop_match_tracking();
    const auto [map_width, map_height] = shown_map_size();
    const std::array<int32_t, 2> map_size{map_width, map_height};
    match_layout_ = lay_out_match(kCanvasWidth, kCanvasHeight);
    const int bf_w = match_layout_.battlefield_width();
    const int bf_h = match_layout_.battlefield_height();
    const auto place = [&](float zoom, std::array<int32_t, 2> camera) {
        match_zoom_ = match_zoom_target_ = zoom;
        set_camera_position(camera[0], camera[1], 0);
        view_hold_ = {};
        render_match_surface();
    };
    const auto visible = [&] {
        const auto zoom = static_cast<double>(match_zoom());
        return std::array<double, 2>{bf_w / zoom, bf_h / zoom};
    };
    const auto centre_of_view = [&] {
        const auto view = match_view_place();
        const auto extent = visible();
        return std::array<double, 2>{view[0] + extent[0] / 2.0, view[1] + extent[1] / 2.0};
    };
    // A scroll each way until the view stops: it stops where the view's
    // centre reaches the end of its span, and goes no further.
    const auto scroll_to_limits = [&](const std::string& what) {
        const auto start = match_view_place();
        for (std::size_t axis = 0; axis < 2; ++axis)
            for (const int32_t way : {-1, 1}) {
                set_camera_position(
                    static_cast<int32_t>(start[0]), static_cast<int32_t>(start[1]), 0
                );
                render_match_surface();
                std::array<double, 2> last{};
                int frames = 0;
                for (; frames < kLimitScrollFrames; ++frames) {
                    scroll_match_view(axis == 0 ? way : 0, axis == 1 ? way : 0, kLimitScrollStep);
                    frame();
                    render_match_surface();
                    const auto now = match_view_place();
                    if (frames != 0 && now == last)
                        break;
                    last = now;
                }
                const auto span = view_centre_span(map_size[axis], visible()[axis]);
                const double reached = centre_of_view()[axis];
                const double wanted = way < 0 ? span.least : span.most;
                if (frames == kLimitScrollFrames || std::abs(reached - wanted) > 1.0)
                    fail(
                        what + ": a scroll " + (way < 0 ? "back" : "on") +
                        (axis == 0 ? " across" : " down") + " stopped with the view's centre at " +
                        std::to_string(reached) + ", not at " + std::to_string(wanted)
                    );
            }
    };
    // At zoom 1 the view's centre stays on the map: the map's edges reach
    // the battlefield's middle.
    auto chosen = engine_settings();
    chosen.max_zoom_out = settings::ZoomOutLimit::whole_map;
    chosen.max_zoom_in = settings::ZoomInLimit::four_times;
    apply_engine_settings(chosen);
    place(kDefaultBattlefieldZoom, {map_width / 2 - bf_w / 2, map_height / 2 - bf_h / 2});
    scroll_to_limits("zoom 1");
    // At the whole map the map's centre stays in the view.
    const float whole = least_match_zoom();
    place(whole, {0, 0});
    scroll_to_limits("the whole map");
    // A minimap press at its corner centres the view on the map's corner.
    place(kDefaultBattlefieldZoom, {map_width / 2, map_height / 2});
    if (radar_picture_.width <= 0 || radar_picture_.height <= 0)
        fail("the minimap was not drawn");
    if (!pan_camera_from_radar(
            static_cast<float>(radar_picture_.x), static_cast<float>(radar_picture_.y)
        ))
        fail("a press on the minimap's corner did not move the view");
    render_match_surface();
    if (const auto centre = centre_of_view();
        std::abs(centre[0]) > 1.0 || std::abs(centre[1]) > 1.0)
        fail("a press on the minimap's corner did not bring the map's corner to the middle");
    // A zoom out about a point of the map near the battlefield's right
    // edge leaves the view past its limits, and a scroll then goes no
    // further from the map but back toward it at once.
    place(kDefaultBattlefieldZoom, {-bf_w / 2, map_height / 2});
    const std::array<int, 2> right_edge{bf_w - kMapEdgeInset, bf_h / 2};
    for (int step = 0; step < kZoomOutSteps * 2; ++step) {
        handle_match_zoom(
            -1.0F,
            static_cast<float>(match_layout_.left + right_edge[0]),
            static_cast<float>(match_layout_.top + right_edge[1]),
            true
        );
        for (int settle = 0; settle < kSettleFrames && match_zoom_ != match_zoom_target_;
             ++settle) {
            frame();
            render_match_surface();
        }
    }
    const auto out_of_limits = centre_of_view();
    const auto out_span = view_centre_span(map_width, visible()[0]);
    if (out_of_limits[0] >= out_span.least)
        fail("a zoom out about a point of the map near its edge left the view within its limits");
    scroll_match_view(-1, 0, kScrollStep);
    frame();
    render_match_surface();
    if (std::abs(centre_of_view()[0] - out_of_limits[0]) > 1.0)
        fail("a scroll away from the map took a view past its limits further from it");
    scroll_match_view(1, 0, kScrollStep);
    frame();
    render_match_surface();
    const double moved = centre_of_view()[0] - out_of_limits[0];
    if (std::abs(moved - kScrollStep / static_cast<double>(match_zoom())) > 1.0)
        fail("a scroll toward the map from past the view's limits did not move it by the scroll");

    // An aircraft past the map's left edge, with the view's camera half the
    // battlefield past it: drawn there, hovered and selected, as a model at
    // zoom 1 and in the far view with Zoomed out units at Rendered, and as a
    // dot there with Dots.
    auto& slots = match_->world().slots;
    const auto type = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "ARMPEEP");
    if (type == 0)
        fail("the game lacks ARMPEEP");
    const int32_t row = map_height / 2;
    oa::sim::unit_spawn::Request request;
    request.player = static_cast<uint8_t>(match_local_player_);
    request.type = type;
    request.finished = true;
    request.state = kGroundOccupancyState;
    request.position = {
        static_cast<uint32_t>(kAircraftPast) << 16,
        static_cast<uint32_t>(
            match_->map_height(
                static_cast<uint32_t>(kAircraftPast) << 16, static_cast<uint32_t>(row) << 16
            ) +
            kAircraftHeight
        ) << 16,
        static_cast<uint32_t>(row) << 16
    };
    auto* spawned = match_->create(request);
    if (spawned == nullptr || spawned->unit == nullptr)
        fail("could not spawn an aircraft");
    const uint16_t aircraft = spawned->unit_index;
    auto& record = slots[aircraft].record;
    const auto on_map_x = record.position.x;
    record.position.x = -kAircraftPast * 0x10000;
    const auto drawn_at = [&] {
        return project_match_point(
            live_viewport(match_camera_x_, match_camera_z_), slots[aircraft].unit->position
        );
    };

    // A view of the aircraft: near or in the far view, and how the far
    // view draws its units.
    struct AircraftView {
        bool far;
        settings::ZoomedOutUnits units;
    };

    for (const auto [far, units] :
         {AircraftView{false, settings::ZoomedOutUnits::rendered},
          AircraftView{true, settings::ZoomedOutUnits::rendered},
          AircraftView{true, settings::ZoomedOutUnits::dots}}) {
        chosen.zoomed_out_units = units;
        apply_engine_settings(chosen);
        const float zoom = far ? whole : kDefaultBattlefieldZoom;
        const auto half = static_cast<int32_t>(std::lround(bf_w / 2.0 / zoom));
        place(zoom, {-half, row - static_cast<int32_t>(std::lround(bf_h / 2.0 / zoom))});
        const bool dots = far && units == settings::ZoomedOutUnits::dots;
        if (far != far_view_frame() || dots != dots_frame())
            fail("the aircraft's view is not the frame it was meant to be");
        const std::string what = !far   ? "at zoom 1"
                                 : dots ? "as a dot in the far view"
                                        : "as a model in the far view";
        const auto at = drawn_at();
        if (at.x < match_layout_.left || at.y < match_layout_.top ||
            at.x >= match_layout_.left + bf_w || at.y >= match_layout_.top + bf_h)
            fail("the aircraft past the map's edge is out of view " + what);
        if (!oa::sim::selection::unit_listed(match_->state(), on_screen_lists(), aircraft))
            fail("the aircraft past the map's edge is not on screen " + what);
        // Its picture over the black past the map.
        const auto& layer = match_world_cpu_;
        int drawn = 0;
        for (int y = at.y - match_layout_.top - kDrawnReach;
             y <= at.y - match_layout_.top + kDrawnReach;
             ++y)
            for (int x = at.x - match_layout_.left - kDrawnReach;
                 x <= at.x - match_layout_.left + kDrawnReach;
                 ++x) {
                if (x < 0 || y < 0 || x >= static_cast<int>(layer.width) ||
                    y >= static_cast<int>(layer.height))
                    continue;
                const std::size_t pixel =
                    (static_cast<std::size_t>(y) * layer.width + static_cast<std::size_t>(x)) * 3U;
                if (layer.rgb[pixel] != 0 || layer.rgb[pixel + 1] != 0 || layer.rgb[pixel + 2] != 0)
                    ++drawn;
            }
        if (drawn == 0)
            fail("the aircraft past the map's edge was not drawn " + what);
        clear_local_selection();
        match_command_ = MatchCommand::none;
        update_pointer(static_cast<float>(at.x), static_cast<float>(at.y));
        if (hovered_match_unit_ != aircraft)
            fail("the pointer did not find the aircraft past the map's edge " + what);
        handle_match_left_click(static_cast<float>(at.x), static_cast<float>(at.y), 1);
        if (selected_match_unit_ != aircraft)
            fail("a click did not select the aircraft past the map's edge " + what);
    }
    clear_local_selection();
    record.position.x = on_map_x;
    match_->kill_unit(aircraft, static_cast<uint8_t>(match_runtime::DeathKind::dismissed));

    // Saves and the digest take the camera held on the map: a view past
    // its edges digests as the view at the map's corner does.
    place(kDefaultBattlefieldZoom, {-bf_w / 2, -bf_h / 2});
    if (on_map_camera() != std::array<int32_t, 2>{0, 0})
        fail("the camera held on the map is not at its corner for a view past it");
    const uint64_t past = match_world_digest();
    place(kDefaultBattlefieldZoom, {0, 0});
    if (match_world_digest() != past)
        fail("a view past the map's edges changed the match's digest");

    apply_engine_settings(saved);
    match_layout_ = saved_layout;
    match_zoom_ = match_zoom_target_ = kDefaultBattlefieldZoom;
    match_paused_ = saved_paused;
    render_match_surface();
    std::cout << "tracking zoom check: a scroll stops with the map's edge at the battlefield's "
                 "middle, and at the whole map with the map's centre at the view's edge; the "
                 "minimap brings the map's corner to the middle; a view a zoom left past the "
                 "limits goes no further and comes back at once; an aircraft past the map's "
                 "edge is drawn, hovered and selected as a model, near and far, and as a dot; "
                 "and the digest takes the camera held on the map\n";
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
    // centre, wherever it is on the map.
    const auto tracking_camera = [&](uint16_t id) {
        const auto* unit = slots[id].unit;
        return std::array<int32_t, 2>{
            static_cast<int32_t>(unit->position[0] >> 16) - visible_map_width() / 2,
            static_cast<int32_t>(unit->position[2] >> 16) - visible_map_height() / 2
        };
    };
    const auto require_tracking = [&](uint16_t id, const std::string& when) {
        if (!match_tracking_ || tracked_match_unit_ != id ||
            match_->state().game.follow_unit != oa::oa_unit_ref_from_slot(id))
            fail("tracking ended " + when);
        render_match_surface();
        if (view_camera() != tracking_camera(id))
            fail("the tracked unit left the centre " + when);
    };
    // The settings dialog's ease about the centre to a zoom, on every frame
    // the unit tracked and at the centre.
    const auto dialog_ease = [&](uint16_t id, float zoom, const std::string& what) {
        EngineSettingsState::ease_zoom_about_centre(*this, zoom);
        for (int settle = 0; settle < kSettleFrames; ++settle) {
            frame();
            require_tracking(id, "while the dialog's zoom eased " + what);
        }
        if (match_zoom() != std::clamp(zoom, least_match_zoom(), most_match_zoom()))
            fail("the dialog's ease did not reach its zoom " + what);
    };

    check_zoom_about_pointer(frame);
    check_zoom_limit_choices(frame);
    check_view_past_map(frame);

    // Ctrl+C follows the commander as it walks, or, where the side's
    // commander is not in the commander category Ctrl+C looks for, the
    // camera follows it as T does; the wheel turned at a point away from it
    // ends the follow and zooms about that point, every frame keeping the
    // map point under the pointer there.
    match_paused_ = false;
    match_pointer_known_ = false;
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
    select_and_follow_commander(false);
    if (!match_tracking_ || tracked_match_unit_ != commander)
        begin_match_tracking(commander);
    if (!match_tracking_ || tracked_match_unit_ != commander)
        fail("the camera did not follow the commander");
    for (int step = 0; step < 3; ++step)
        frame();
    require_tracking(commander, "as the follow began");
    const uint32_t walked_from = walker.position[0];
    const std::array<double, 2> aim{
        match_layout_.battlefield_width() * 0.85, match_layout_.battlefield_height() * 0.15
    };
    const auto under_aim = [&] {
        const auto view = match_view_place();
        const auto zoom = static_cast<double>(match_zoom());
        return std::array<double, 2>{view[0] + aim[0] / zoom, view[1] + aim[1] / zoom};
    };
    for (int step = 0; step < kWheelSteps; ++step) {
        auto before = under_aim();
        handle_match_zoom(
            step < kWheelSteps / 2 ? 1.0F : -1.0F,
            static_cast<float>(match_layout_.left + aim[0]),
            static_cast<float>(match_layout_.top + aim[1]),
            true
        );
        if (match_tracking_ || match_->state().game.follow_unit != 0)
            fail("the wheel did not end the follow of the commander");
        for (int settle = 0; settle < kSettleFrames && match_zoom_ != match_zoom_target_;
             ++settle) {
            frame();
            render_match_surface();
            const auto after = under_aim();
            if (std::abs(after[0] - before[0]) > kMostAnchorStray ||
                std::abs(after[1] - before[1]) > kMostAnchorStray)
                fail("the wheel during the follow did not zoom about the pointer");
            before = after;
        }
    }
    if (walker.position[0] == walked_from)
        fail("the commander did not walk while the zoom changed");

    // The settings dialog's ease about the centre keeps the follow, in
    // play and while a menu holds the match.
    begin_match_tracking(commander);
    for (const bool held : {false, true}) {
        match_paused_ = held;
        const auto what = std::string(held ? "under a menu" : "in play");
        dialog_ease(commander, kMaxBattlefieldZoom, what);
        dialog_ease(commander, kDefaultBattlefieldZoom, "back " + what);
    }
    match_paused_ = false;

    // A unit in the map's corner: the camera centres it at every zoom,
    // past the map's edges, and goes on tracking it.
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
    dialog_ease(cornered, kMaxBattlefieldZoom, "in at the map's corner");
    dialog_ease(cornered, least_match_zoom(), "out at the map's corner");
    if (match_camera_x_ >= 0 || match_camera_z_ >= 0)
        fail("the camera tracking the corner unit was held on the map");
    dialog_ease(cornered, kDefaultBattlefieldZoom, "back at the map's corner");

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

    std::cout << "tracking zoom check: the wheel ended the commander's follow and zoomed about "
                 "the pointer; the dialog zoomed between "
              << least_match_zoom() << " and " << kMaxBattlefieldZoom
              << " with the tracked unit at the centre, in play, under a menu and at the "
                 "map's corner, past its edges; a scroll and the minimap ended the tracking\n";
    stop_match_tracking();
    match_zoom_ = match_zoom_target_ = kDefaultBattlefieldZoom;
    return_to_skirmish_menu();
}

} // namespace oa::app
