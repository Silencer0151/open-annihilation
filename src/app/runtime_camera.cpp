// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Radar interaction, zoom, camera panning and move/patrol orders.
#include "oa/app/runtime.hpp"
#include "oa/app/far_view.hpp"
#include "oa/core/map_plot.h"
#include "engine_settings_state.hpp"
#include "oa/sim/feature_runtime.hpp"
#include "oa/sim/selection.hpp"
#include "oa/present/world_renderer/world_camera.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <tuple>

namespace oa::app {

namespace {

// The longest step the zoom eases by, in seconds, however long since the
// last frame.
constexpr float kLongestZoomStep = 0.05F;

// The range of the map pixels a zoom's exact anchor lies past its whole map
// pixel: half a pixel either side of the point on the camera's map pixel,
// which the whole one is rounded from, and the view's offset, up to a map
// pixel, past that.
constexpr double kLeastAnchorFraction = -0.5;
constexpr double kMostAnchorFraction = 1.5;

// The wheel steps short of an end of the zoom's range that count as at
// it, well above the rounding of a target to float and below any step a
// trackpad sends.
constexpr double kZoomWheelEndSlack = 1.0e-4;

static_assert(
    oa::ui::engine_settings::closest_zoom(oa::ui::engine_settings::ZoomInLimit::four_times) ==
        kMaxBattlefieldZoom,
    "Maximum zoom in's closest choice is the closest any view zooms in"
);

} // namespace

bool Runtime::radar_contains(float x, float y) const {
    return radar_picture_.width > 0 && radar_picture_.height > 0 &&
           x >= static_cast<float>(radar_picture_.x) && y >= static_cast<float>(radar_picture_.y) &&
           x < static_cast<float>(radar_picture_.x + radar_picture_.width) &&
           y < static_cast<float>(radar_picture_.y + radar_picture_.height);
}

std::optional<oa::sim::ground_orders::Point> Runtime::radar_world_point(float x, float y) {
    if (!radar_contains(x, y) || !selected_tnt_ || radar_map_w_ <= 0 || radar_map_h_ <= 0)
        return std::nullopt;
    const auto rx = static_cast<int>(x) - radar_picture_.x;
    const auto ry = static_cast<int>(y) - radar_picture_.y;
    return map_world_point(
        rx * radar_map_w_ / radar_picture_.width, ry * radar_map_h_ / radar_picture_.height
    );
}

std::optional<oa::sim::ground_orders::Point>
Runtime::map_world_point(int32_t map_x, int32_t map_z) {
    if (!selected_tnt_)
        return std::nullopt;
    const oa::sim::unit_movement::Terrain terrain(*selected_tnt_);
    const auto target = oa::sim::gameplay_input::terrain_intersection(
        terrain,
        map_x,
        map_z,
        static_cast<int32_t>(selected_tnt_->attribute_width * 16U),
        static_cast<int32_t>(selected_tnt_->attribute_height * 16U)
    );
    return oa::sim::ground_orders::Point{target.x, target.y, target.z};
}

uint16_t Runtime::pick_radar_unit(float x, float y) {
    if (!match_ || !radar_contains(x, y) || radar_state_.built_for != &match_->state())
        return 0;
    auto& world = match_->state();
    const auto pointer = game_screen_point(x, y);
    world.game.pointer_state[0] = static_cast<uint32_t>(pointer.x);
    world.game.pointer_state[1] = static_cast<uint32_t>(pointer.y);
    return oa::sim::selection::unit_under_pointer(world, on_screen_lists(), selection_hooks());
}

void Runtime::bind_match_view() {
    namespace wr = oa::present::world_renderer;
    namespace layout = oa::ui::display_layout;
    auto& game = match_->state().game;
    game.camera_x = static_cast<uint32_t>(match_camera_x_);
    game.camera_y = static_cast<uint32_t>(match_camera_z_);
    game.view_cells_width = visible_map_width() / OA_MAP_CELL_PIXELS;
    game.view_cells_height = visible_map_height() / OA_MAP_CELL_PIXELS;
    // The off-screen surface is the screen: the visible battlefield with the
    // side column and bars around it (640x480 unzoomed).
    game.offscreen_width = static_cast<uint32_t>(layout::kSourceLeft + visible_map_width());
    game.offscreen_height =
        static_cast<uint32_t>(layout::kSourceTop + visible_map_height() + layout::kSourceBottom);
    // The game view on the game's screen (game_screen_point): the visible map
    // from (128, 32), which is the 640x480 screen's view unzoomed.
    game.battlefield_rect = oa::Rect32{
        layout::kSourceLeft,
        layout::kSourceTop,
        layout::kSourceLeft + visible_map_width() - 1,
        layout::kSourceTop + visible_map_height() - 1
    };
    oa::Rect32 view{};
    if (wr::radar_view_rect(game, view) &&
        std::memcmp(&view, &game.radar_view_rect, sizeof view) != 0) {
        game.radar_view_rect = view;
        game.radar_blink_flags =
            static_cast<uint16_t>(game.radar_blink_flags | wr::radar_flag_redraw);
    }
}

bool Runtime::issue_radar_orders(float x, float y) {
    namespace input = oa::sim::gameplay_input;
    if (!match_ || !radar_contains(x, y))
        return false;
    // The frame's pointer pass at the press: the unit whose dot is under the
    // pointer, the map point under it and the cursor shown there.
    update_pointer(x, y);
    const auto cursor = static_cast<input::OrderCursor>(pick_match_cursor());
    auto& world = match_->state();
    const auto command = input::pointer_command(world.game);
    if (command == input::OrderCommand::build)
        return place_pending_build_on_radar(x, y);
    switch (input::click_action(world, command, cursor)) {
    case input::ClickAction::none:
        return false;
    case input::ClickAction::select_unit:
        select_match_unit(x, y, 1);
        return true;
    case input::ClickAction::clear_selection:
        clear_local_selection();
        apply_match_hud_for_selection();
        return true;
    case input::ClickAction::issue_command:
        break;
    }
    const auto issued =
        issue_selection_orders(command, hovered_match_unit_, radar_world_point(x, y), queueing());
    if (!issued.empty())
        status_ = std::string(issued);
    // The command ends with the press whether or not a unit took an order.
    finish_issued_command();
    return true;
}

bool Runtime::issue_radar_default_order(float x, float y) {
    namespace input = oa::sim::gameplay_input;
    if (!match_ || !radar_contains(x, y) || selected_match_unit_ == 0)
        return false;
    update_pointer(x, y);
    // The pick is wanted for what it writes into the Game block; the cursor
    // it returns is not needed.
    std::ignore = pick_match_cursor();
    const auto issued = issue_selection_orders(
        input::OrderCommand::default_order, hovered_match_unit_, radar_world_point(x, y), queueing()
    );
    if (issued.empty())
        return false;
    status_ = std::string(issued);
    return true;
}

bool Runtime::issue_map_orders(
    const std::optional<oa::sim::ground_orders::Point>& world, uint16_t target
) {
    if (!match_ || selected_match_unit_ == 0 || match_command_ != MatchCommand::patrol || !world)
        return false;
    if (issue_selection_patrol(*world, target, queueing()))
        finish_issued_command();
    return true;
}

bool Runtime::pan_camera_from_radar(float x, float y) {
    if (!radar_contains(x, y) || radar_map_w_ <= 0 || radar_map_h_ <= 0)
        return false;
    center_camera_on_radar_point(x, y);
    status_ = "Radar";
    return true;
}

void Runtime::apply_zoom_anchor() {
    if (!zoom_anchored_)
        return;
    const auto zoom = static_cast<double>(match_zoom_ <= 0.0F ? 1.0F : match_zoom_);
    // The camera that shows the anchor's map pixel under the anchor, the
    // map pixels from the camera's to it rounded as screen_to_map_pixel
    // rounds them: a step that leaves the zoom as it was leaves the camera
    // where it was, and where the camera's limits never stop it, the steps
    // back find the same map pixel under the anchor and return the camera.
    const auto map_pixels_to = [zoom](int at) {
        return static_cast<int32_t>(std::llround(static_cast<double>(at) / zoom));
    };
    match_camera_x_ = zoom_anchor_map_x_ - map_pixels_to(zoom_anchor_sx_);
    match_camera_z_ = zoom_anchor_map_y_ - map_pixels_to(zoom_anchor_sy_);
    // Where the camera's limits stop it, it sits at the limit, and the next
    // step anchors on what is then under the pointer (anchor_zoom_at).
    const auto camera = view_camera();
    match_camera_x_ = camera[0];
    match_camera_z_ = camera[1];
    // A view drawn between map pixels keeps the exact point under the
    // anchor, where the camera's rounding allows: the camera stays where
    // the anchor rounds it to.
    if (!smooth_view_.on || !selected_tnt_)
        return;
    const auto most = most_view_offsets(camera[0], camera[1]);
    smooth_view_.camera_x = camera[0];
    smooth_view_.camera_z = camera[1];
    smooth_view_.offset.x = view_offset_at(
        static_cast<double>(zoom_anchor_map_x_) + zoom_anchor_fraction_x_ -
            static_cast<double>(zoom_anchor_sx_) / zoom,
        camera[0],
        most.x
    );
    smooth_view_.offset.y = view_offset_at(
        static_cast<double>(zoom_anchor_map_y_) + zoom_anchor_fraction_y_ -
            static_cast<double>(zoom_anchor_sy_) / zoom,
        camera[1],
        most.y
    );
}

std::array<int32_t, 2> Runtime::shown_map_size() const noexcept {
    namespace features = oa::sim::feature_runtime;
    if (match_) {
        const auto& game = match_->state().game;
        if (game.map_pixel_width > 0 && game.map_pixel_height > 0)
            return {game.map_pixel_width, game.map_pixel_height};
    }
    if (!selected_tnt_)
        return {0, 0};
    const auto width = static_cast<int32_t>(selected_tnt_->tile_width * 32U);
    const auto height = static_cast<int32_t>(selected_tnt_->tile_height * 32U);
    return {
        std::max(0, width - features::hidden_right_edge),
        std::max(0, height - features::hidden_bottom_edge)
    };
}

std::array<uint32_t, 2> Runtime::shown_tile_grid() const noexcept {
    if (!selected_tnt_)
        return {0, 0};
    const auto shown = shown_map_size();
    const auto tiles = [](int32_t pixels, uint32_t most) {
        const auto whole = static_cast<uint32_t>((std::max(pixels, 0) + 31) / 32);
        return std::clamp<uint32_t>(whole, std::min<uint32_t>(1, most), most);
    };
    return {
        tiles(shown[0], selected_tnt_->tile_width), tiles(shown[1], selected_tnt_->tile_height)
    };
}

std::array<int32_t, 2> Runtime::view_camera() const {
    if (!selected_tnt_)
        return {match_camera_x_, match_camera_z_};
    const auto [map_width, map_height] = shown_map_size();
    return {
        std::clamp(match_camera_x_, 0, std::max(0, map_width - visible_map_width())),
        std::clamp(match_camera_z_, 0, std::max(0, map_height - visible_map_height()))
    };
}

oa::present::world_renderer::ViewOffset
Runtime::most_view_offsets(int32_t camera_x, int32_t camera_z) const {
    if (!selected_tnt_)
        return {};
    // The camera's farthest places, as view_camera holds it.
    const auto [map_width, map_height] = shown_map_size();
    return {
        most_view_offset(camera_x, std::max(0, map_width - visible_map_width())),
        most_view_offset(camera_z, std::max(0, map_height - visible_map_height()))
    };
}

oa::present::world_renderer::ViewOffset Runtime::view_offset() const {
    if (!smooth_view_.on || screen_ != Screen::match)
        return {};
    const auto camera = view_camera();
    if (camera[0] != smooth_view_.camera_x || camera[1] != smooth_view_.camera_z)
        return {};
    return smooth_view_.offset;
}

oa::present::world_renderer::ViewOffset
Runtime::settle_view_offset(uint32_t camera_x, uint32_t camera_y, bool between) {
    // A reader's draw of the standard tier's picture leaves the view alone.
    if (accelerated_.suspended)
        return {};
    if (!between) {
        smooth_view_ = {};
        return {};
    }
    const auto x = static_cast<int32_t>(camera_x);
    const auto z = static_cast<int32_t>(camera_y);
    if (!smooth_view_.on || smooth_view_.camera_x != x || smooth_view_.camera_z != z)
        smooth_view_.offset = {};
    smooth_view_.on = true;
    smooth_view_.camera_x = x;
    smooth_view_.camera_z = z;
    // A zoom that changed since the offset was found may leave the view less room.
    const auto most = most_view_offsets(x, z);
    smooth_view_.offset.x = std::clamp(smooth_view_.offset.x, 0.0, most.x);
    smooth_view_.offset.y = std::clamp(smooth_view_.offset.y, 0.0, most.y);
    return smooth_view_.offset;
}

void Runtime::EngineSettingsState::ease_zoom_about_centre(Runtime& runtime, float target) {
    if (!runtime.match_ || !runtime.selected_tnt_)
        return;
    // A camera tracking a unit eases about the unit, which stays at the
    // centre (step_match_zoom), and goes on tracking it.
    if (runtime.match_tracking_ && runtime.match_unit_present(runtime.tracked_match_unit_)) {
        runtime.zoom_anchored_ = false;
        runtime.match_zoom_target_ =
            std::clamp(target, runtime.least_match_zoom(), runtime.most_match_zoom());
        return;
    }
    // The point at the battlefield's centre stays there while the zoom
    // eases, anchored as the wheel's zoom anchors it.
    const auto& layout = runtime.match_layout_;
    runtime.anchor_zoom_at(
        layout.left + layout.battlefield_width() / 2, layout.top + layout.battlefield_height() / 2
    );
    runtime.match_zoom_target_ =
        std::clamp(target, runtime.least_match_zoom(), runtime.most_match_zoom());
    runtime.stop_match_tracking();
}

float Runtime::least_match_zoom() const noexcept {
    // Settings not yet read hold their defaults, Automatic among them.
    const auto limit = engine_settings_ ? engine_settings_->current.max_zoom_out
                                        : oa::ui::engine_settings::ZoomOutLimit::automatic;
    const auto [map_width, map_height] = shown_map_size();
    return least_battlefield_zoom(
        limit,
        detail_zoom_floor(),
        map_width,
        map_height,
        match_layout_.battlefield_width(),
        match_layout_.battlefield_height()
    );
}

float Runtime::detail_zoom_floor() const noexcept {
    return full_presentation() ? kMinFullBattlefieldZoom : kMinBattlefieldZoom;
}

bool Runtime::far_view_frame() const noexcept {
    return screen_ == Screen::match && !director_mode() &&
           far_view_zoom(match_zoom_, detail_zoom_floor());
}

float Runtime::most_match_zoom() const noexcept {
    namespace settings = oa::ui::engine_settings;
    return settings::closest_zoom(
        engine_settings_ ? engine_settings_->current.max_zoom_in : settings::ZoomInLimit::four_times
    );
}

void Runtime::step_match_zoom() {
    // Each frame of a match: a match that turns out to be shared or a replay
    // goes back to the base path credit (Common Tweaks' pathfinding setting).
    EngineSettingsState::hold_path_credit(*this, current_extension_state());
    // The director sets the zoom of every frame itself, off the wall clock.
    if (director_mode())
        return;
    // The limits move with the tier, the window, the map and the zoom's
    // settings: a view zoomed past them comes back to them at once.
    const float least = least_match_zoom();
    const float most = most_match_zoom();
    match_zoom_target_ = std::clamp(match_zoom_target_, least, most);
    const bool floored = match_zoom_ < least || match_zoom_ > most;
    if (floored) {
        match_zoom_ = std::clamp(match_zoom_, least, most);
        camera_moved_ = true;
    }
    // A camera tracking a unit keeps the unit at the centre at every scale,
    // as far as the map's edges let it, while a menu holds the match too.
    const auto centre_tracked_unit = [this] {
        if (match_tracking_ && match_unit_present(tracked_match_unit_))
            center_camera_on_unit(tracked_match_unit_);
    };
    // Seconds since the zoom last eased, from the frames' times; the first
    // frame takes a frame at the full rate, and a long gap counts as
    // kLongestZoomStep.
    float dt = 1.0F / static_cast<float>(kDefaultMaxFramesPerSecond);
    if (zoom_clock_valid_ && frame_time_ns_ >= zoom_clock_) {
        dt = static_cast<float>(
            static_cast<double>(frame_time_ns_ - zoom_clock_) /
            static_cast<double>(frame_pacing::kNanosecondsPerSecond)
        );
        dt = std::clamp(dt, 0.0F, kLongestZoomStep);
    }
    zoom_clock_ = frame_time_ns_;
    zoom_clock_valid_ = true;
    if (std::abs(match_zoom_ - match_zoom_target_) < 1.0e-4F) {
        const bool changed = match_zoom_ != match_zoom_target_;
        match_zoom_ = match_zoom_target_;
        if (zoom_anchored_) {
            apply_zoom_anchor();
            zoom_anchored_ = false;
            camera_moved_ = true;
        }
        if (changed || floored)
            centre_tracked_unit();
        return;
    }
    const auto t = 1.0F - std::exp(-kZoomLerpHz * dt);
    match_zoom_ += (match_zoom_target_ - match_zoom_) * t;
    apply_zoom_anchor();
    centre_tracked_unit();
    camera_moved_ = true;
}

void Runtime::anchor_zoom_at(int px, int py) {
    // The view as drawn, wherever the camera's limits stopped the last
    // zoom: each step zooms about what is under the pointer at that step.
    const auto camera = view_camera();
    const auto viewport =
        live_viewport(static_cast<uint32_t>(camera[0]), static_cast<uint32_t>(camera[1]));
    // The map pixel under the pointer on the camera's map pixel, which the
    // camera rounds about as it always has, and the exact point a view
    // drawn between map pixels shows under the pointer and keeps there,
    // the offset lying in the fraction.
    const auto before = oa::present::world_renderer::screen_to_map_pixel(viewport, {px, py});
    if (!before)
        return;
    const auto zoom = static_cast<double>(match_zoom() <= 0.0F ? 1.0F : match_zoom());
    const auto offset = view_offset();
    const auto fraction = [zoom](int at, uint32_t drawn, uint32_t source, double past) {
        return std::clamp(
            static_cast<double>(source) + past + static_cast<double>(at) / zoom -
                static_cast<double>(drawn),
            kLeastAnchorFraction,
            kMostAnchorFraction
        );
    };
    zoom_anchor_sx_ = px - match_layout_.left;
    zoom_anchor_sy_ = py - match_layout_.top;
    zoom_anchor_map_x_ = static_cast<int32_t>(before->x);
    zoom_anchor_map_y_ = static_cast<int32_t>(before->y);
    zoom_anchor_fraction_x_ = fraction(zoom_anchor_sx_, before->x, viewport.source_x, offset.x);
    zoom_anchor_fraction_y_ = fraction(zoom_anchor_sy_, before->y, viewport.source_y, offset.y);
    zoom_anchored_ = true;
}

void Runtime::handle_match_zoom(float wheel_y, float pointer_x, float pointer_y) {
    if (wheel_y == 0.0F || !selected_tnt_)
        return;
    const int px = static_cast<int>(pointer_x);
    const int py = static_cast<int>(pointer_y);
    if (px < match_layout_.left || py < match_layout_.top ||
        px >= match_layout_.left + match_layout_.battlefield_width() ||
        py >= match_layout_.top + match_layout_.battlefield_height())
        return;
    // A camera tracking a unit zooms about the unit, which stays at the
    // centre (step_match_zoom), and goes on tracking it; the wheel changes
    // only the scale.
    if (match_tracking_ && match_unit_present(tracked_match_unit_)) {
        zoom_anchored_ = false;
        match_zoom_target_ = wheel_zoom_target(wheel_y);
        return;
    }
    anchor_zoom_at(px, py);
    match_zoom_target_ = wheel_zoom_target(wheel_y);
    stop_match_tracking();
}

float Runtime::wheel_zoom_target(float wheel_y) {
    // A target something else set since the wheel's last step begins the
    // count again.
    const float least = least_match_zoom();
    const float most = most_match_zoom();
    if (zoom_wheel_.target != match_zoom_target_)
        zoom_wheel_ = {std::clamp(match_zoom_target_, least, most), 0.0, match_zoom_target_};
    const auto from = static_cast<double>(zoom_wheel_.from);
    const auto factor = static_cast<double>(kZoomWheelFactor);
    // The steps in all, so that as many back return the target exactly. A
    // step toward an end counts in whole, though the target stops at the
    // end, but no further than the first whole step at or past it, and a
    // step toward an end the steps are already at counts nothing: the
    // first step back leaves the end, and as many back as reached it
    // return the target.
    const double way = wheel_y > 0.0F ? 1.0 : -1.0;
    const double end =
        std::log(static_cast<double>(wheel_y > 0.0F ? most : least) / from) / std::log(factor);
    const double short_of_end = way * (end - zoom_wheel_.steps);
    if (short_of_end > kZoomWheelEndSlack) {
        const double counted = std::min(
            std::abs(static_cast<double>(wheel_y)), std::ceil(short_of_end - kZoomWheelEndSlack)
        );
        zoom_wheel_.steps += way * counted;
    }
    zoom_wheel_.target =
        std::clamp(static_cast<float>(from * std::pow(factor, zoom_wheel_.steps)), least, most);
    return zoom_wheel_.target;
}

void Runtime::pan_match_camera() {
    // Neither keys nor the pointer move the director's camera.
    if (screen_ != Screen::match || !selected_tnt_ || director_mode())
        return;
    // The map pixels moved in a frame are the scroll speed, pixels per 30 Hz
    // clock unit, times the clock units the frame's real time is worth, so a
    // second of scrolling covers the same ground at any frame rate, at the
    // match record's speed (the preference, or the console's ScrollSpeed),
    // and the camera moves a steady amount each frame.
    const uint64_t elapsed_ns =
        scroll_clock_ != 0 && frame_time_ns_ > scroll_clock_ ? frame_time_ns_ - scroll_clock_ : 0;
    scroll_clock_ = frame_time_ns_;
    const auto speed =
        match_ != nullptr ? match_->state().game.scroll_speed : preferences_.scroll_speed;
    const double step = frame_pacing::scroll_distance(speed, elapsed_ns);
    const bool* keys = SDL_GetKeyboardState(nullptr);
    int dx = 0, dz = 0;
    // A --frame-rate run's held scroll is an arrow key.
    if (frame_run_clock_ns_)
        dx += frame_run_scroll_;
    // Only the arrow keys scroll, as in 3.1c: the letter keys stay the
    // game's own commands (A attack, S stop and so on).
    if (keys[SDL_SCANCODE_LEFT])
        --dx;
    if (keys[SDL_SCANCODE_RIGHT])
        ++dx;
    if (keys[SDL_SCANCODE_UP])
        --dz;
    if (keys[SDL_SCANCODE_DOWN])
        ++dz;
    // The pointer on the screen's outermost pixels scrolls toward that edge,
    // and in a corner both ways, whatever panel lies under it, as in 3.1c: in
    // full screen and in a window alike, while the pointer is in the window
    // at a place SDL has reported on this screen.
    const auto flags = sdl_.window != nullptr ? SDL_GetWindowFlags(sdl_.window) : 0u;
    if (match_pointer_known_ && (flags & SDL_WINDOW_MOUSE_FOCUS) != 0) {
        // A window point covers several pixels on a high-density display, and
        // the pointer rests on the outermost point, not the outermost pixel;
        // on a window at native density the layout is in window points. A
        // scaled frame's pixels to a window point are its width over the
        // points it is presented across.
        float per_point = SDL_GetWindowPixelDensity(sdl_.window);
        SDL_FRect presented{};
        const bool scaled = scaled_frame_width_ > 0 &&
                            SDL_GetRenderLogicalPresentationRect(sdl_.renderer, &presented) &&
                            presented.w > 0.0F;
        if (scaled)
            per_point = per_point * static_cast<float>(match_layout_.width) / presented.w;
        const auto edge = edge_scroll_depth(at_native_density(flags) && !scaled, per_point);
        const auto way = oa::ui::hud::edge_scroll(
            static_cast<int32_t>(std::floor(match_pointer_x_)),
            static_cast<int32_t>(std::floor(match_pointer_y_)),
            match_layout_.width,
            match_layout_.height,
            edge
        );
        dx += way.x;
        dz += way.y;
    }
    if (dx == 0 && dz == 0) {
        scroll_zoom_carry_ = 0.0;
        return;
    }
    scroll_match_view(dx, dz, step);
}

void Runtime::scroll_match_view(int32_t way_x, int32_t way_z, double step) {
    way_x = std::clamp(way_x, -1, 1);
    way_z = std::clamp(way_z, -1, 1);
    // The view the last frame drew, before the camera moves.
    const auto camera_before = view_camera();
    const auto offset_before = view_offset();
    // Zoom keeps the on-screen rate constant; the carried fraction keeps the
    // world rate exact rather than rounding it every frame.
    const auto zoom = static_cast<double>(match_zoom() <= 0.0F ? 1.0F : match_zoom());
    const double travel = step / zoom;
    const double carry_before = scroll_zoom_carry_;
    scroll_zoom_carry_ += travel;
    const auto move = static_cast<int32_t>(std::floor(scroll_zoom_carry_));
    scroll_zoom_carry_ -= static_cast<double>(move);
    if (move != 0) {
        stop_match_tracking();
        zoom_anchored_ = false;
        match_camera_x_ += way_x * move;
        match_camera_z_ += way_z * move;
        camera_moved_ = true;
    }
    // A view drawn between map pixels follows the scroll's exact travel
    // within the camera's map pixel, and an axis that trails the carry both
    // axes step on, as one joining a scroll under way does, catches up
    // with it before its camera steps; a zoom easing about its anchor
    // places it instead. The camera above moved as it always has.
    if (!smooth_view_.on || zoom_anchored_ || !selected_tnt_)
        return;
    const auto camera = view_camera();
    const auto most = most_view_offsets(camera[0], camera[1]);
    const oa::present::world_renderer::ViewOffset offset{
        scrolled_view_offset(
            offset_before.x, way_x * travel, carry_before, camera[0] - camera_before[0], most.x
        ),
        scrolled_view_offset(
            offset_before.y, way_z * travel, carry_before, camera[1] - camera_before[1], most.y
        )
    };
    // The picture moves with the view, whether or not the camera stepped.
    if (offset.x != smooth_view_.offset.x || offset.y != smooth_view_.offset.y ||
        camera[0] != smooth_view_.camera_x || camera[1] != smooth_view_.camera_z)
        camera_moved_ = true;
    smooth_view_.camera_x = camera[0];
    smooth_view_.camera_z = camera[1];
    smooth_view_.offset = offset;
}

void Runtime::issue_resume_or_repair(uint16_t id) {
    issue_resume_or_repair_from(selected_match_unit_, id, queueing());
}

void Runtime::issue_resume_or_repair_from(uint16_t source, uint16_t id, bool queue) {
    if (source == 0 || id == 0 || source == id)
        return;
    auto& slot = match_->world().slots[id];
    const auto unfinished = slot.unit && slot.build_remaining != 0.0F;
    if (unfinished) {
        match_->issue_help_build(source, id, queue);
        status_ = "Resume construction";
    } else {
        match_->issue_repair(source, id, queue);
        status_ = "Repair";
    }
}

void Runtime::issue_match_move(float x, float y, bool queue) {
    if (!match_ || !selected_tnt_ || selected_match_unit_ == 0)
        return;
    if (radar_contains(x, y)) {
        // A press on the radar is the radar's whether or not it gave orders.
        std::ignore = issue_radar_orders(x, y);
        return;
    }
    // The ground drawn under the point, a whole map pixel as orders take.
    const auto target = ground_point_under(x, y);
    if (!target)
        return;
    issue_selection_move(*target, hovered_match_unit_, queue);
}

void Runtime::issue_selection_move(
    const oa::sim::ground_orders::Point& point, uint16_t pointer_unit, bool queue
) {
    if (!match_ || selected_match_unit_ == 0)
        return;
    try {
        // Each unit moves to its own point, keeping its place in the
        // selection around the ground under the pointer. The unit under the
        // pointer is not counted in the centre and takes no order.
        const auto command = oa::sim::gameplay_input::OrderCommand::move;
        const auto bound = group_order_bound_unit(command, pointer_unit);
        const auto centre = local_selection_centre(bound);
        for_each_selected([&](uint16_t id) {
            if (id == bound)
                return;
            const auto at = group_order_destination(centre, command, id, 0, point);
            if (!cancels_queued_command(id, command, 0, at, queue) && match_->takes_move_order(id))
                match_->issue_ground_move(id, at, queue);
        });
    } catch (const std::exception& error) {
        status_ = std::string("move command: ") + error.what();
        std::cerr << "unsupported operation: " << status_ << '\n';
    }
}

void Runtime::issue_match_patrol(float x, float y, bool queue) {
    if (!match_ || !selected_tnt_ || selected_match_unit_ == 0)
        return;
    // The ground drawn under the point, a whole map pixel as orders take.
    const auto target = ground_point_under(x, y);
    if (!target)
        return;
    std::ignore = issue_selection_patrol(*target, hovered_match_unit_, queue);
}

bool Runtime::issue_selection_patrol(
    const oa::sim::ground_orders::Point& point, uint16_t pointer_unit, bool queue
) {
    namespace input = oa::sim::gameplay_input;
    if (!match_ || selected_match_unit_ == 0)
        return false;
    try {
        // Each unit patrols to its own point, keeping its place in the
        // selection around the point. The unit under the pointer is not
        // counted in the centre and takes no order. A unit the order table
        // gives no patrol, one that cannot patrol, is counted in the centre
        // but takes no order either.
        const auto command = input::OrderCommand::patrol;
        const auto bound = group_order_bound_unit(command, pointer_unit);
        const auto centre = local_selection_centre(bound);
        const auto& world = match_->state();
        const auto* aimed = bound != 0 ? oa::world_unit_at(&world, bound) : nullptr;
        const oa::FixedVec3 position{point[0], point[1], point[2]};
        const auto hooks = order_cursor_hooks();
        for_each_selected([&](uint16_t id) {
            const auto* actor = oa::world_unit_at(&world, id);
            if (id == bound || actor == nullptr ||
                input::unit_order(world, command, *actor, aimed, &position, hooks) ==
                    input::UnitOrder::none)
                return;
            const auto at = group_order_destination(centre, command, id, 0, point);
            if (!cancels_queued_command(id, command, 0, at, queue))
                match_->issue_patrol(id, at, queue);
        });
    } catch (const std::exception& error) {
        status_ = std::string("patrol command: ") + error.what();
        std::cerr << "unsupported operation: " << status_ << '\n';
        return false;
    }
    status_ = "Patrol";
    return true;
}

} // namespace oa::app
