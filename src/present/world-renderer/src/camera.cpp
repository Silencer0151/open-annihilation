// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/world_renderer/world_camera.hpp"

#include <cstdint>

namespace oa::present::world_renderer {
namespace {

int32_t camera_x(const Game& game) noexcept {
    return static_cast<int32_t>(game.camera_x);
}

int32_t camera_y(const Game& game) noexcept {
    return static_cast<int32_t>(game.camera_y);
}

void set_camera_x(Game& game, int32_t v) noexcept {
    game.camera_x = static_cast<uint32_t>(v);
}

void set_camera_y(Game& game, int32_t v) noexcept {
    game.camera_y = static_cast<uint32_t>(v);
}

void mark_radar_redraw(Game& game) noexcept {
    game.radar_blink_flags = static_cast<uint16_t>(game.radar_blink_flags | radar_flag_redraw);
}

void invalidate_fog_mask(Game& game) noexcept {
    game.visibility_flags =
        static_cast<uint8_t>(game.visibility_flags & ~visibility_flag_fog_mask_current);
}

// Clamp shared by the current and target camera: [0, map - view].
int32_t clamp_axis(int32_t value, int32_t limit) noexcept {
    if (value < 0)
        return 0;
    return limit < value ? limit : value;
}

// One glide step: halve the gap, but never move more than the max step.
int32_t glide_axis(int32_t current, int32_t target) noexcept {
    const auto gap = current - target;
    if (gap < 1) {
        if (-(camera_glide_max_step + 1) < gap)
            return current - gap / 2;
        return current + camera_glide_max_step;
    }
    if (gap < camera_glide_max_step + 1)
        return current - gap / 2;
    return current - camera_glide_max_step;
}

// Map pixel of a view dimension halved with C truncation.
int32_t half_view_width(const Game& game) noexcept {
    return game.viewport_width / 2;
}

int32_t half_view_height(const Game& game) noexcept {
    return game.viewport_height / 2;
}

// Clears the follow point countdown, the follow unit and the follow target.
void clear_follow(Game& game) noexcept {
    game.follow_point_ticks = 0;
    game.follow_unit = 0;
    game.follow_target = 0;
}

} // namespace

uint8_t game_ui_color(const Game& game, int32_t index) noexcept {
    return game.ui_colors[index & 0xf];
}

void camera_stop_follow(Game& game) noexcept {
    clear_follow(game);
}

void camera_clamp_position(Game& game) noexcept {
    const auto limit_x = game.map_pixel_width - game.viewport_width;
    const auto limit_y = game.map_pixel_height - game.viewport_height;
    set_camera_x(game, clamp_axis(camera_x(game), limit_x));
    set_camera_y(game, clamp_axis(camera_y(game), limit_y));
    Rect32 rect = game.radar_view_rect;
    if (radar_view_rect(game, rect))
        game.radar_view_rect = rect;
}

void camera_clamp_target(Game& game) noexcept {
    const auto limit_x = game.map_pixel_width - game.viewport_width;
    const auto limit_y = game.map_pixel_height - game.viewport_height;
    game.camera_target_x = clamp_axis(game.camera_target_x, limit_x);
    game.camera_target_y = clamp_axis(game.camera_target_y, limit_y);
}

void camera_set_position(Game& game, int32_t x, int32_t y, int32_t glide) noexcept {
    if (glide == 0) {
        set_camera_x(game, x);
        set_camera_y(game, y);
        mark_radar_redraw(game);
        camera_clamp_position(game);
        game.camera_target_x = camera_x(game);
        game.camera_target_y = camera_y(game);
    } else {
        game.camera_target_x = x;
        game.camera_target_y = y;
        camera_clamp_target(game);
    }
    invalidate_fog_mask(game);
}

void camera_shake(Game& game, const RandomSource& random) noexcept {
    const uint8_t flags = game.camera_flags;
    if ((flags & camera_flag_shake) == 0)
        return;
    const int32_t remaining = game.shake_remaining;
    if (remaining <= 0) {
        game.camera_flags = static_cast<uint8_t>(flags & ~camera_flag_shake);
        return;
    }
    const int32_t duration = game.shake_duration;
    const auto span_x = game.shake_amplitude_x * remaining / duration;
    const auto span_y = game.shake_amplitude_y * remaining / duration;
    const auto roll_x = random.next != nullptr ? random.next(random.user) : 0;
    const auto offset_x = static_cast<int32_t>(
        static_cast<int64_t>(roll_x) * static_cast<int64_t>(span_x) / random_range
    );
    const auto roll_y = random.next != nullptr ? random.next(random.user) : 0;
    const auto offset_y = static_cast<int32_t>(
        static_cast<int64_t>(roll_y) * static_cast<int64_t>(span_y) / random_range
    );
    set_camera_x(game, camera_x(game) + (offset_x - span_x / 2));
    set_camera_y(game, camera_y(game) + (offset_y - span_y / 2));
    game.shake_remaining = remaining - 1;
}

int32_t world_screen_x(const FixedVec3& position) noexcept {
    return static_cast<int16_t>(static_cast<uint32_t>(position.x) >> 16);
}

int32_t world_screen_y(const FixedVec3& position) noexcept {
    const auto lifted = static_cast<uint32_t>(position.z) - static_cast<uint32_t>(position.y >> 1);
    return static_cast<int16_t>(lifted >> 16);
}

void camera_center(Game& game, int32_t x, int32_t y, int32_t glide) noexcept {
    camera_set_position(game, x - half_view_width(game), y - half_view_height(game), glide);
}

void camera_center_on_position(Game& game, const FixedVec3& position, int32_t glide) noexcept {
    camera_set_position(
        game,
        world_screen_x(position) - half_view_width(game),
        world_screen_y(position) - half_view_height(game),
        glide
    );
}

void camera_tick(Game& game, const CameraFollow& follow, const RandomSource& random) noexcept {
    const FixedVec3* focus = nullptr;
    const int16_t ticks = game.follow_point_ticks;
    if (ticks != 0) {
        game.follow_point_ticks = static_cast<int16_t>(ticks - 1);
        const FixedVec3 point = game.follow_point;
        camera_center_on_position(game, point, 1);
    } else if (game.follow_target != 0) {
        focus = follow.target;
    } else if (game.follow_unit != 0) {
        if (follow.unit != nullptr && (follow.unit->flags & OA_UNIT_FLAG_LIVE) != 0) {
            focus = &follow.unit->position;
        } else {
            clear_follow(game);
        }
    }
    if (focus != nullptr)
        camera_center_on_position(game, *focus, 1);

    if (camera_x(game) != game.camera_target_x) {
        mark_radar_redraw(game);
        invalidate_fog_mask(game);
        set_camera_x(game, glide_axis(camera_x(game), game.camera_target_x));
    }
    if (camera_y(game) != game.camera_target_y) {
        mark_radar_redraw(game);
        invalidate_fog_mask(game);
        set_camera_y(game, glide_axis(camera_y(game), game.camera_target_y));
    }
    camera_shake(game, random);
    camera_clamp_position(game);
}

void mouse_look_begin(Game& game, const CursorSink& cursor) noexcept {
    clear_follow(game);
    if (cursor.poll != nullptr)
        cursor.poll(cursor.user, game);
    game.mouse_look_active = 1;
    for (int32_t word = 0; word < pointer_state_words; ++word)
        game.saved_pointer_state[word] = game.pointer_state[word];
    game.mouse_look_cell_x = camera_x(game) / map_cell_pixels;
    game.mouse_look_cell_y = camera_y(game) / map_cell_pixels;
    const auto width = cursor.screen_width != nullptr ? cursor.screen_width(cursor.user) : 0;
    game.mouse_look_anchor_x = width / 2;
    const auto height = cursor.screen_height != nullptr ? cursor.screen_height(cursor.user) : 0;
    game.mouse_look_anchor_y = height / 2;
    if (cursor.set_position != nullptr)
        cursor.set_position(cursor.user, width / 2, height / 2);
}

void mouse_look_end(Game& game, const CursorSink& cursor) noexcept {
    game.mouse_look_active = 0;
    if (cursor.set_position != nullptr)
        cursor.set_position(
            cursor.user,
            static_cast<int32_t>(game.saved_pointer_state[0]),
            static_cast<int32_t>(game.saved_pointer_state[1])
        );
    if (cursor.draw != nullptr)
        cursor.draw(cursor.user);
}

void mouse_look_update(Game& game, const CursorSink& cursor) noexcept {
    int32_t state[pointer_state_words];
    for (int32_t word = 0; word < pointer_state_words; ++word)
        state[word] = static_cast<int32_t>(game.pointer_state[word]);
    const int32_t anchor_x = game.mouse_look_anchor_x;
    const int32_t anchor_y = game.mouse_look_anchor_y;
    camera_set_position(
        game,
        ((state[0] - anchor_x) / mouse_look_pixels_per_cell + game.mouse_look_cell_x) *
            map_cell_pixels,
        ((state[1] - anchor_y) / mouse_look_pixels_per_cell + game.mouse_look_cell_y) *
            map_cell_pixels,
        0
    );
    game.mouse_look_cell_x = camera_x(game) / map_cell_pixels;
    game.mouse_look_cell_y = camera_y(game) / map_cell_pixels;
    if (cursor.set_position != nullptr)
        cursor.set_position(cursor.user, anchor_x, anchor_y);
    if ((static_cast<uint32_t>(state[2]) & pointer_button_look) == 0)
        mouse_look_end(game, cursor);
}

void camera_to_start_entry(Game& game, const StartEntry* entries, int32_t count) noexcept {
    if (entries == nullptr)
        return;
    for (int32_t index = 0; index < count; ++index) {
        const auto& entry = entries[index];
        if (entry.kind == 1 && entry.index == 0) {
            camera_set_position(
                game, entry.x - half_view_width(game), entry.y - half_view_height(game), 0
            );
            return;
        }
    }
}

void camera_store_slot(Game& game, int32_t slot) noexcept {
    game.camera_slot_x[slot] = camera_x(game);
    game.camera_slot_y[slot] = camera_y(game);
    game.camera_slot_valid[slot] = 1;
}

void camera_recall_slot(Game& game, int32_t slot) noexcept {
    clear_follow(game);
    camera_set_position(game, game.camera_slot_x[slot], game.camera_slot_y[slot], 0);
}

bool radar_view_rect(const Game& game, Rect32& rect) noexcept {
    const int32_t map_w = game.map_pixel_width;
    const int32_t map_h = game.map_pixel_height;
    if (map_w == 0 || map_h == 0)
        return false;
    const int32_t radar_w = game.radar_width;
    const int32_t radar_h = game.radar_height;
    rect.x1 = radar_w * camera_x(game) / map_w + game.radar_offset_x;
    rect.y1 = radar_h * camera_y(game) / map_h + game.radar_offset_y;
    rect.x2 = radar_w * game.view_cells_width * map_cell_pixels / map_w - 1 + rect.x1;
    rect.y2 = radar_h * game.view_cells_height * map_cell_pixels / map_h - 1 + rect.y1;
    return true;
}

void project_unit_to_screen(
    const Game& game, const FixedVec3& position, int32_t& x, int32_t& y
) noexcept {
    const auto hi = [](oa_fixed v) {
        return static_cast<int32_t>(static_cast<int16_t>(static_cast<uint32_t>(v) >> 16));
    };
    const int32_t cam_x = static_cast<int16_t>(game.camera_x);
    const int32_t cam_y = static_cast<int16_t>(game.camera_y);
    x = hi(position.x) - cam_x + battlefield_origin_x;
    y = hi(position.z) - cam_y - (hi(position.y) >> 1) + battlefield_origin_y;
}

} // namespace oa::present::world_renderer
