// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Radar interaction, zoom, camera panning and move/patrol orders.
#include "oa/app/runtime.hpp"
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
    if (!match_ || selected_match_unit_ == 0)
        return false;
    const auto world = radar_world_point(x, y);
    return issue_map_orders(world, pick_radar_unit(x, y));
}

bool Runtime::issue_map_orders(
    const std::optional<oa::sim::ground_orders::Point>& world, uint16_t target
) {
    namespace input = oa::sim::gameplay_input;
    if (!match_ || selected_match_unit_ == 0)
        return false;
    auto& slots = match_->world().slots;
    const auto enemy = target != 0 && slots[target].unit != nullptr &&
                               slots[target].owner_index != match_local_player_
                           ? target
                           : uint16_t{0};
    try {
        if (match_command_ == MatchCommand::patrol) {
            if (!world)
                return false;
            for_each_selected([&](uint16_t id) {
                if (!cancels_queued_command(id, input::OrderCommand::patrol, 0, world, queueing()))
                    match_->issue_patrol(id, *world, queueing());
            });
            finish_issued_command();
            status_ = "Patrol";
            return true;
        }
        if (match_command_ == MatchCommand::build) {
            if (!world)
                return false;
            place_pending_build_at(*world);
            return true;
        }
        // The order table gives AttackSpecial only to the units that can D-gun.
        const auto blasts = [&](uint16_t source) {
            auto& state = match_->state();
            const auto* actor = oa::world_unit_at(&state, source);
            const auto* aimed = enemy != 0 ? oa::world_unit_at(&state, enemy) : nullptr;
            const oa::FixedVec3 position =
                world ? oa::FixedVec3{(*world)[0], (*world)[1], (*world)[2]} : oa::FixedVec3{};
            return actor != nullptr && input::unit_order(
                                           state,
                                           input::OrderCommand::blast,
                                           *actor,
                                           aimed,
                                           world ? &position : nullptr,
                                           order_cursor_hooks()
                                       ) == input::UnitOrder::attack_special;
        };
        auto armed = input::OrderCommand::default_order;
        if (match_command_ == MatchCommand::dgun)
            armed = input::OrderCommand::blast;
        else if (match_command_ == MatchCommand::attack)
            armed = input::OrderCommand::attack;
        else if (match_command_ == MatchCommand::move)
            armed = input::OrderCommand::move;
        if (match_command_ == MatchCommand::attack || match_command_ == MatchCommand::dgun) {
            if (enemy != 0) {
                for_each_selected([&](uint16_t source) {
                    if (cancels_queued_command(source, armed, enemy, world, queueing()))
                        return;
                    // A unit no attack resolves for is given no order.
                    if (match_command_ == MatchCommand::dgun) {
                        if (!blasts(source))
                            return;
                        auto dest = world.value_or(oa::sim::ground_orders::Point{});
                        auto& slot = slots[enemy];
                        if (slot.unit) {
                            const std::array<uint32_t, 3> position = slot.unit->position;
                            dest = {
                                std::bit_cast<int32_t>(position[0]),
                                std::bit_cast<int32_t>(position[1]),
                                std::bit_cast<int32_t>(position[2])
                            };
                        }
                        match_->issue_attack_special(source, dest, queueing(), enemy);
                    } else
                        std::ignore = match_->issue_attack_command(
                            source, enemy, queueing(), world ? &*world : nullptr
                        );
                });
                status_ = match_command_ == MatchCommand::dgun ? "D-Gun" : "Attack";
            } else if (world) {
                for_each_selected([&](uint16_t source) {
                    if (cancels_queued_command(source, armed, 0, world, queueing()))
                        return;
                    if (match_command_ == MatchCommand::dgun) {
                        if (blasts(source))
                            match_->issue_attack_special(source, *world, queueing());
                    } else
                        std::ignore = match_->issue_attack_ground(source, *world, queueing());
                });
                status_ = match_command_ == MatchCommand::dgun ? "D-Gun ground" : "Attack ground";
            } else
                return false;
            finish_issued_command();
            return true;
        }
        if (enemy != 0) {
            for_each_selected([&](uint16_t id) {
                if (!cancels_queued_command(id, armed, enemy, world, queueing()))
                    std::ignore = match_->issue_attack_command(
                        id, enemy, queueing(), world ? &*world : nullptr
                    );
            });
            status_ = "Attack";
            finish_issued_command();
            return true;
        }
        if (!world)
            return false;
        if (match_command_ == MatchCommand::move || match_command_ == MatchCommand::none) {
            for_each_selected([&](uint16_t id) {
                if (!cancels_queued_command(id, armed, 0, world, queueing()) &&
                    match_->takes_move_order(id))
                    match_->issue_ground_move(id, *world, queueing());
            });
            finish_issued_command();
            status_ = "Move";
            return true;
        }
    } catch (const std::exception& error) {
        status_ = std::string("radar order: ") + error.what();
        std::cerr << "unsupported operation: " << status_ << '\n';
        return true;
    }
    return false;
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
    match_camera_x_ = static_cast<int32_t>(std::llround(
        static_cast<double>(zoom_anchor_map_x_) - static_cast<double>(zoom_anchor_sx_) / zoom
    ));
    match_camera_z_ = static_cast<int32_t>(std::llround(
        static_cast<double>(zoom_anchor_map_y_) - static_cast<double>(zoom_anchor_sy_) / zoom
    ));
    // A view drawn between map pixels keeps the exact point under the
    // anchor, where the camera's rounding allows: the camera stays where
    // the anchor rounds it to.
    if (!smooth_view_.on || !selected_tnt_)
        return;
    const auto camera = view_camera();
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
    // The point at the battlefield's centre stays there while the zoom eases.
    const auto& layout = runtime.match_layout_;
    const int centre_x = layout.battlefield_width() / 2;
    const int centre_y = layout.battlefield_height() / 2;
    const auto zoom = static_cast<double>(runtime.match_zoom_ <= 0.0F ? 1.0F : runtime.match_zoom_);
    runtime.zoom_anchor_map_x_ = static_cast<uint32_t>(std::max<int64_t>(
        0,
        std::llround(
            static_cast<double>(runtime.match_camera_x_) + static_cast<double>(centre_x) / zoom
        )
    ));
    runtime.zoom_anchor_map_y_ = static_cast<uint32_t>(std::max<int64_t>(
        0,
        std::llround(
            static_cast<double>(runtime.match_camera_z_) + static_cast<double>(centre_y) / zoom
        )
    ));
    // The exact point at the centre, which a view drawn between map pixels
    // shows there and keeps there: the whole map pixel above is the
    // camera's, as the camera rounds it, and the offset lies in the fraction.
    const auto offset = runtime.view_offset();
    runtime.zoom_anchor_fraction_x_ = std::clamp(
        static_cast<double>(runtime.match_camera_x_) + offset.x +
            static_cast<double>(centre_x) / zoom - static_cast<double>(runtime.zoom_anchor_map_x_),
        kLeastAnchorFraction,
        kMostAnchorFraction
    );
    runtime.zoom_anchor_fraction_y_ = std::clamp(
        static_cast<double>(runtime.match_camera_z_) + offset.y +
            static_cast<double>(centre_y) / zoom - static_cast<double>(runtime.zoom_anchor_map_y_),
        kLeastAnchorFraction,
        kMostAnchorFraction
    );
    runtime.zoom_anchor_sx_ = centre_x;
    runtime.zoom_anchor_sy_ = centre_y;
    runtime.zoom_anchored_ = true;
    runtime.match_zoom_target_ =
        std::clamp(target, runtime.least_match_zoom(), kMaxBattlefieldZoom);
    runtime.stop_match_tracking();
}

float Runtime::least_match_zoom() const noexcept {
    return full_presentation() ? kMinFullBattlefieldZoom : kMinBattlefieldZoom;
}

void Runtime::step_match_zoom() {
    // Each frame of a match: a match that turns out to be shared or a replay
    // goes back to the base path credit (the AI & Pathfinding setting).
    EngineSettingsState::hold_path_credit(*this, current_extension_state());
    // The director sets the zoom of every frame itself, off the wall clock.
    if (director_mode())
        return;
    // The floor moves with the tier: a view zoomed out past the processor's
    // floor when the card stops drawing the battlefield comes back to it at
    // once.
    const float least = least_match_zoom();
    if (match_zoom_target_ < least)
        match_zoom_target_ = least;
    if (match_zoom_ < least) {
        match_zoom_ = least;
        camera_moved_ = true;
    }
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
        match_zoom_ = match_zoom_target_;
        if (zoom_anchored_) {
            apply_zoom_anchor();
            zoom_anchored_ = false;
            camera_moved_ = true;
        }
        return;
    }
    const auto t = 1.0F - std::exp(-kZoomLerpHz * dt);
    match_zoom_ += (match_zoom_target_ - match_zoom_) * t;
    apply_zoom_anchor();
    camera_moved_ = true;
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
    const auto viewport = live_viewport(
        static_cast<uint32_t>(std::max(0, match_camera_x_)),
        static_cast<uint32_t>(std::max(0, match_camera_z_))
    );
    // The map pixel under the pointer on the camera's map pixel, which the
    // camera rounds about as it always has, and the exact point a view
    // drawn between map pixels shows under the pointer and keeps there,
    // the offset lying in the fraction.
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
    match_zoom_target_ = std::clamp(
        match_zoom_target_ * std::pow(kZoomWheelFactor, wheel_y),
        least_match_zoom(),
        kMaxBattlefieldZoom
    );
    stop_match_tracking();
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
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A])
        --dx;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D])
        ++dx;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W])
        --dz;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S])
        ++dz;
    // The pointer on the screen's outermost pixels scrolls toward that edge,
    // and in a corner both ways, whatever panel lies under it, as in 3.1c: in
    // full screen and in a window alike, while the pointer is in the window
    // at a place SDL has reported on this screen.
    const auto flags = sdl_.window != nullptr ? SDL_GetWindowFlags(sdl_.window) : 0u;
    if (match_pointer_known_ && (flags & SDL_WINDOW_MOUSE_FOCUS) != 0) {
        // A window point covers several pixels on a high-density display, and
        // the pointer rests on the outermost point, not the outermost pixel;
        // on a window at native density the layout is in window points.
        const auto edge =
            edge_scroll_depth(at_native_density(flags), SDL_GetWindowPixelDensity(sdl_.window));
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
    const auto viewport = live_viewport(
        static_cast<uint32_t>(match_camera_x_), static_cast<uint32_t>(match_camera_z_)
    );

    // The map pixel drawn under the point, a whole one as orders take.
    const auto screen_map = oa::present::world_renderer::screen_to_map_pixel(
        viewport, {static_cast<int32_t>(x), static_cast<int32_t>(y)}, view_offset()
    );
    if (!screen_map)
        return;
    const oa::sim::unit_movement::Terrain terrain(*selected_tnt_);
    const auto target = oa::sim::gameplay_input::terrain_intersection(
        terrain,
        static_cast<int32_t>(screen_map->x),
        static_cast<int32_t>(screen_map->y),
        static_cast<int32_t>(selected_tnt_->attribute_width * 16U),
        static_cast<int32_t>(selected_tnt_->attribute_height * 16U)
    );
    const oa::sim::ground_orders::Point point{target.x, target.y, target.z};
    try {
        for_each_selected([&](uint16_t id) {
            if (!cancels_queued_command(
                    id, oa::sim::gameplay_input::OrderCommand::move, 0, point, queue
                ) &&
                match_->takes_move_order(id))
                match_->issue_ground_move(id, point, queue);
        });
    } catch (const std::exception& error) {
        status_ = std::string("move command: ") + error.what();
        std::cerr << "unsupported operation: " << status_ << '\n';
    }
}

void Runtime::issue_match_patrol(float x, float y, bool queue) {
    if (!match_ || !selected_tnt_ || selected_match_unit_ == 0)
        return;
    const auto viewport = live_viewport(
        static_cast<uint32_t>(match_camera_x_), static_cast<uint32_t>(match_camera_z_)
    );

    // The map pixel drawn under the point, a whole one as orders take.
    const auto screen_map = oa::present::world_renderer::screen_to_map_pixel(
        viewport, {static_cast<int32_t>(x), static_cast<int32_t>(y)}, view_offset()
    );
    if (!screen_map)
        return;
    const oa::sim::unit_movement::Terrain terrain(*selected_tnt_);
    const auto target = oa::sim::gameplay_input::terrain_intersection(
        terrain,
        static_cast<int32_t>(screen_map->x),
        static_cast<int32_t>(screen_map->y),
        static_cast<int32_t>(selected_tnt_->attribute_width * 16U),
        static_cast<int32_t>(selected_tnt_->attribute_height * 16U)
    );
    const oa::sim::ground_orders::Point point{target.x, target.y, target.z};
    try {
        for_each_selected([&](uint16_t id) {
            if (!cancels_queued_command(
                    id, oa::sim::gameplay_input::OrderCommand::patrol, 0, point, queue
                ))
                match_->issue_patrol(id, point, queue);
        });
        status_ = "Patrol";
    } catch (const std::exception& error) {
        status_ = std::string("patrol command: ") + error.what();
        std::cerr << "unsupported operation: " << status_ << '\n';
    }
}

} // namespace oa::app
