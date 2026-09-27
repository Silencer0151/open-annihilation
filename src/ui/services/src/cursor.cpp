// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/services/cursor.hpp"

#include "oa/platform/allocator.hpp"
#include "oa/platform/system.hpp"

#include <cstdint>

namespace oa::ui::services {
namespace {

constexpr int32_t cursor_hidden = 1;
constexpr std::size_t cursor_thread_stack = 0x8000;

// Give a scratch surface the sprite's extent with a tightly packed pitch.
void shape_like(Surface* surface, const Sprite* sprite) noexcept {
    surface->width = sprite->width;
    surface->height = sprite->height;
    surface->pitch = sprite->width;
}

void cursor_thread_entry(void* argument) {
    cursor_thread_run(static_cast<CursorState*>(argument));
}

} // namespace

bool pointer_capture_init(CursorState* state, int32_t capacity, int32_t threaded) noexcept {
    const CursorDraw* draw = state->draw;
    state->pointer.capacity = capacity;
    state->pointer.events = static_cast<PointerEvent*>(platform::heap_alloc(
        capacity > 0 ? static_cast<std::size_t>(capacity) * sizeof(PointerEvent) : 0, "mouse events"
    ));
    pointer_queue_reset(&state->pointer);
    state->image = nullptr;
    state->saved_background =
        draw->create_surface(draw->context, "savemouse 1", cursor_save_bytes, 1);
    state->scratch_background =
        draw->create_surface(draw->context, "savemouse 2", cursor_save_bytes, 1);
    state->scratch_composite =
        draw->create_surface(draw->context, "savemouse 3", cursor_save_bytes, 1);
    state->hide_count = cursor_hidden;
    state->overlay_enabled = 0;
    state->threaded = 0;
    state->stop_request.store(0);
    if (threaded == 1) {
        cursor_thread_start(state);
    } else {
        state->thread_started = 0;
    }
    return state->pointer.events != nullptr && state->saved_background != nullptr &&
           state->scratch_background != nullptr && state->scratch_composite != nullptr;
}

void pointer_capture_shutdown(CursorState* state) noexcept {
    if (state->pointer.events == nullptr) {
        return;
    }
    if ((state->display_flags & display_flag_threaded_cursor) != 0) {
        cursor_thread_stop(state);
    }
    const CursorDraw* draw = state->draw;
    draw->free_surface(draw->context, state->scratch_composite);
    draw->free_surface(draw->context, state->scratch_background);
    draw->free_surface(draw->context, state->saved_background);
    platform::heap_free(state->pointer.events);
    state->pointer.events = nullptr;
}

void cursor_draw(CursorState* state) noexcept {
    if (state->threaded == 1 || state->hide_count >= 1) {
        return;
    }
    const PointerEvent current = state->pointer.current;
    state->draw->draw_sprite(state->draw->context, nullptr, state->image, current.x, current.y);
}

void cursor_hide(CursorState* state) noexcept {
    if (state->threaded == 1) {
        return;
    }
    const int32_t previous = state->hide_count++;
    if (previous == 0) {
        state->draw->blit(
            state->draw->context, nullptr, state->saved_background, state->x, state->y
        );
    }
}

void cursor_show(CursorState* state) noexcept {
    if (state->threaded == 1) {
        return;
    }
    state->hide_count -= 1;
    if (state->hide_count >= 1) {
        return;
    }
    int32_t x = 0;
    int32_t y = 0;
    state->input->cursor_position(state->input->context, &x, &y);
    state->pointer.current.x = x;
    state->pointer.current.y = y;
    const Sprite* image = state->image;
    if (image != nullptr) {
        state->x = x - image->origin_x;
        state->y = y - image->origin_y;
        shape_like(state->saved_background, image);
    }
    state->draw->blit(state->draw->context, state->saved_background, nullptr, -state->x, -state->y);
    cursor_draw(state);
}

void cursor_repaint(CursorState* state) noexcept {
    if (state->overlay_enabled == 0) {
        return;
    }
    const CursorDraw* draw = state->draw;
    void* context = draw->context;
    Surface screen{};
    if (!draw->lock_screen(context, &screen)) {
        return;
    }
    int32_t pointer_x = 0;
    int32_t pointer_y = 0;
    state->input->cursor_position(state->input->context, &pointer_x, &pointer_y);
    const Sprite* image = state->image;
    state->pointer.current.x = pointer_x;
    state->pointer.current.y = pointer_y;
    const int32_t x = pointer_x - image->origin_x;
    const int32_t y = pointer_y - image->origin_y;
    Surface* saved = state->saved_background;
    Surface* background = state->scratch_background;
    Surface* composite = state->scratch_composite;
    shape_like(background, image);
    shape_like(composite, image);
    // Background at the new position, with the old cursor replaced by what it covered.
    draw->copy_clipped(context, background, &screen, -x, -y);
    draw->copy_clipped(context, background, saved, state->x - x, state->y - y);
    draw->copy_clipped(context, composite, background, 0, 0);
    draw->reset_clip(context, composite);
    draw->draw_sprite(context, composite, image, image->origin_x, image->origin_y);
    // Old area: its saved background, with any overlap of the new cursor on top.
    draw->copy_clipped(context, saved, composite, x - state->x, y - state->y);
    const Rect32 old_area{state->x, state->y, saved->width + state->x, saved->height + state->y};
    const Rect32 new_area{x, y, image->width + x, image->height + y};
    draw->copy_clipped(context, &screen, saved, state->x, state->y);
    draw->copy_clipped(context, &screen, composite, x, y);
    shape_like(saved, image);
    draw->copy_clipped(context, saved, background, 0, 0);
    state->x = x;
    state->y = y;
    draw->present(context, &screen, &old_area, &new_area);
}

void cursor_thread_run(CursorState* state) noexcept {
    while (state->stop_request.load() == 0) {
        const uint32_t started = platform::tick_ms();
        const platform::TokenLockHold hold =
            platform::token_lock_enter(state->frame_lock, cursor_thread_lock_token);
        if (state->image != nullptr) {
            cursor_repaint(state);
        }
        platform::token_lock_leave(state->frame_lock, &hold);
        uint32_t pause = 1;
        if (static_cast<int32_t>(started + cursor_frame_ms - platform::tick_ms()) >= 1) {
            pause = started + cursor_frame_ms - platform::tick_ms();
        }
        platform::sleep_ms(pause);
    }
    state->stop_request.store(0);
}

bool cursor_thread_start(CursorState* state) noexcept {
    state->stop_request.store(0);
    const bool started = platform::start_thread(cursor_thread_entry, cursor_thread_stack, state);
    state->thread_started = started ? 1 : 0;
    if (started) {
        state->threaded = 1;
    }
    return started;
}

bool cursor_thread_stop(CursorState* state) noexcept {
    if (state->thread_started == 0) {
        return true;
    }
    state->stop_request.store(1);
    int32_t polls = 0;
    do {
        ++polls;
        if (polls > cursor_stop_poll_limit) {
            return false;
        }
        platform::sleep_ms(cursor_stop_poll_ms);
    } while (state->stop_request.load() != 0);
    state->thread_started = 0;
    return true;
}

void cursor_set_image(CursorState* state, const Sprite* image) noexcept {
    const platform::TokenLockHold hold =
        platform::token_lock_enter(state->frame_lock, main_thread_lock_token);
    state->image = image;
    platform::token_lock_leave(state->frame_lock, &hold);
}

const Sprite* cursor_image(const CursorState* state) noexcept {
    return state->image;
}

} // namespace oa::ui::services
