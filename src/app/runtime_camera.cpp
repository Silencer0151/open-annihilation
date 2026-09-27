// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Radar interaction, zoom, camera panning and move/patrol orders.
#include "oa/app/runtime.hpp"
#include "oa/sim/selection.hpp"
#include "oa/present/world_renderer/world_camera.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>

namespace oa::app {

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
    const auto map_x = rx * radar_map_w_ / radar_picture_.width;
    const auto map_z = ry * radar_map_h_ / radar_picture_.height;
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
    const auto pointer = oa::ui::display_layout::canvas_to_source(
        match_layout_, static_cast<int>(x), static_cast<int>(y)
    );
    world.game.pointer_state[0] = static_cast<uint32_t>(pointer.x);
    world.game.pointer_state[1] = static_cast<uint32_t>(pointer.y);
    oa::sim::selection::VisibleLists lists{};
    lists.radar = radar_state_.hot_units.data();
    lists.radar_capacity = static_cast<uint32_t>(radar_state_.hot_units.size());
    return oa::sim::selection::unit_under_pointer(world, lists, {});
}

void Runtime::bind_match_view() {
    namespace wr = oa::present::world_renderer;
    namespace layout = oa::ui::display_layout;
    auto& game = match_->state().game;
    game.camera_x = static_cast<uint32_t>(match_camera_x_);
    game.camera_y = static_cast<uint32_t>(match_camera_z_);
    game.view_cells_width = visible_map_width() / wr::map_cell_pixels;
    game.view_cells_height = visible_map_height() / wr::map_cell_pixels;
    // The off-screen surface is the screen: the visible battlefield with the
    // side column and bars around it (640x480 unzoomed).
    game.offscreen_width = static_cast<uint32_t>(layout::kSourceLeft + visible_map_width());
    game.offscreen_height =
        static_cast<uint32_t>(layout::kSourceTop + visible_map_height() + layout::kSourceBottom);
    game.battlefield_rect = oa::Rect32{
        layout::kSourceLeft,
        layout::kSourceTop,
        layout::kSourceWidth - 1,
        layout::kSourceBottomBarY - 1
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
    if (!match_ || selected_match_unit_ == 0)
        return false;
    const auto world = radar_world_point(x, y);
    const auto target = pick_radar_unit(x, y);
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
                    (void)match_->issue_patrol(id, *world, queueing());
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
                    if (match_command_ == MatchCommand::dgun) {
                        if (!blasts(source))
                            return;
                        auto dest = world.value_or(oa::sim::ground_orders::Point{});
                        auto& slot = slots[enemy];
                        if (slot.unit) {
                            dest = {
                                std::bit_cast<int32_t>(slot.unit->position[0]),
                                std::bit_cast<int32_t>(slot.unit->position[1]),
                                std::bit_cast<int32_t>(slot.unit->position[2])
                            };
                        }
                        (void)match_->issue_attack_special(source, dest, queueing(), enemy);
                    } else
                        (void)match_->issue_attack(source, enemy, queueing());
                });
                status_ = match_command_ == MatchCommand::dgun ? "D-Gun" : "Attack";
            } else if (world) {
                for_each_selected([&](uint16_t source) {
                    if (cancels_queued_command(source, armed, 0, world, queueing()))
                        return;
                    if (match_command_ == MatchCommand::dgun) {
                        if (blasts(source))
                            (void)match_->issue_attack_special(source, *world, queueing());
                    } else
                        (void)match_->issue_attack_ground(source, *world, queueing());
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
                    (void)match_->issue_attack(id, enemy, queueing());
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
}

void Runtime::step_match_zoom() {
    const auto now = std::chrono::steady_clock::now();
    float dt = 1.0F / 120.0F;
    if (zoom_clock_valid_) {
        dt = std::chrono::duration<float>(now - zoom_clock_).count();
        dt = std::clamp(dt, 0.0F, 0.05F);
    }
    zoom_clock_ = now;
    zoom_clock_valid_ = true;
    if (std::abs(match_zoom_ - match_zoom_target_) < 1.0e-4F) {
        match_zoom_ = match_zoom_target_;
        if (zoom_anchored_) {
            apply_zoom_anchor();
            zoom_anchored_ = false;
        }
        return;
    }
    const auto t = 1.0F - std::exp(-kZoomLerpHz * dt);
    match_zoom_ += (match_zoom_target_ - match_zoom_) * t;
    apply_zoom_anchor();
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
    const auto before = oa::present::world_renderer::screen_to_map_pixel(viewport, {px, py});
    if (before) {
        zoom_anchor_map_x_ = before->x;
        zoom_anchor_map_y_ = before->y;
        zoom_anchor_sx_ = px - match_layout_.left;
        zoom_anchor_sy_ = py - match_layout_.top;
        zoom_anchored_ = true;
    }
    match_zoom_target_ = std::clamp(
        match_zoom_target_ * std::pow(kZoomWheelFactor, wheel_y),
        kMinBattlefieldZoom,
        kMaxBattlefieldZoom
    );
    stop_match_tracking();
}

void Runtime::pan_match_camera() {
    if (screen_ != Screen::match || !selected_tnt_)
        return;
    // Paced as the game paces it: the map pixels moved in a frame are the
    // scroll speed times the whole 30 Hz clock units since the previous frame,
    // so a second of scrolling covers the same ground at any frame rate, at
    // the match record's speed (the preference, or the console's ScrollSpeed).
    const auto elapsed =
        oa::ui::hud::scroll_clock_advance(scroll_clock_, static_cast<uint32_t>(SDL_GetTicks()));
    const auto speed =
        match_ != nullptr ? match_->state().game.scroll_speed : preferences_.scroll_speed;
    const auto step = oa::ui::hud::scroll_step(speed, elapsed);
    const bool* keys = SDL_GetKeyboardState(nullptr);
    int dx = 0, dz = 0;
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A])
        --dx;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D])
        ++dx;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W])
        --dz;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S])
        ++dz;
    const auto flags = sdl_.window != nullptr ? SDL_GetWindowFlags(sdl_.window) : 0u;
    if ((flags & SDL_WINDOW_MOUSE_FOCUS) != 0 && !hovered_) {
        const int left = match_layout_.left;
        const int top = match_layout_.top;
        const int right = left + match_layout_.battlefield_width();
        const int bottom = match_layout_.height - match_layout_.bottom;
        const int mx = static_cast<int>(match_pointer_x_);
        const int my = static_cast<int>(match_pointer_y_);
        if (mx >= left && mx < right && my >= top && my < bottom) {
            const auto lip = std::max(
                4,
                static_cast<int>(
                    std::lround(static_cast<double>(kEdgeScrollLipSource) * match_layout_.scale)
                )
            );
            if (mx < left + lip)
                --dx;
            if (mx >= right - lip)
                ++dx;
            if (my < top + lip)
                --dz;
            if (my >= bottom - lip)
                ++dz;
        }
    }
    if (dx == 0 && dz == 0) {
        scroll_zoom_carry_ = 0.0;
        return;
    }
    // Zoom keeps the on-screen rate constant; the carried fraction keeps the
    // world rate exact rather than rounding it every frame.
    const auto zoom = static_cast<double>(match_zoom() <= 0.0F ? 1.0F : match_zoom());
    scroll_zoom_carry_ += static_cast<double>(step) / zoom;
    const auto move = static_cast<int32_t>(std::floor(scroll_zoom_carry_));
    scroll_zoom_carry_ -= static_cast<double>(move);
    if (move == 0)
        return;
    stop_match_tracking();
    zoom_anchored_ = false;
    match_camera_x_ += std::clamp(dx, -1, 1) * move;
    match_camera_z_ += std::clamp(dz, -1, 1) * move;
}

void Runtime::issue_resume_or_repair(uint16_t id) {
    issue_resume_or_repair_from(selected_match_unit_, id);
}

void Runtime::issue_resume_or_repair_from(uint16_t source, uint16_t id) {
    if (source == 0 || id == 0 || source == id)
        return;
    auto& slot = match_->world().slots[id];
    const auto unfinished = slot.unit && std::bit_cast<float>(slot.build_remaining_bits) != 0.0F;
    if (unfinished) {
        (void)match_->issue_help_build(source, id, queueing());
        status_ = "Resume construction";
    } else {
        (void)match_->issue_repair(source, id, queueing());
        status_ = "Repair";
    }
}

void Runtime::issue_match_move(float x, float y, bool queue) {
    if (!match_ || !selected_tnt_ || selected_match_unit_ == 0)
        return;
    if (radar_contains(x, y)) {
        (void)issue_radar_orders(x, y);
        return;
    }
    const auto viewport = live_viewport(
        static_cast<uint32_t>(match_camera_x_), static_cast<uint32_t>(match_camera_z_)
    );

    const auto screen_map = oa::present::world_renderer::screen_to_map_pixel(
        viewport, {static_cast<int32_t>(x), static_cast<int32_t>(y)}
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

    const auto screen_map = oa::present::world_renderer::screen_to_map_pixel(
        viewport, {static_cast<int32_t>(x), static_cast<int32_t>(y)}
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
                (void)match_->issue_patrol(id, point, queue);
        });
        status_ = "Patrol";
    } catch (const std::exception& error) {
        status_ = std::string("patrol command: ") + error.what();
        std::cerr << "unsupported operation: " << status_ << '\n';
    }
}

} // namespace oa::app
