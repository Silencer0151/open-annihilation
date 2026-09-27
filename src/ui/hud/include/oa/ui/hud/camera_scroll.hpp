// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Keyboard and screen-edge scrolling of the battlefield camera.
#pragma once

#include "oa/core/game_state.h"

#include <cstdint>

namespace oa::ui::hud {

/// Most pixels the camera moves in one frame.
inline constexpr int32_t kMaxScrollStep = 0x80;
/// How far past the right/bottom window edge the desktop cursor still
/// counts as touching that edge.
inline constexpr int32_t kEdgeCatchPixels = 100;
/// Units per real second of the clock that paces scrolling.
inline constexpr uint32_t kScrollClockHz = 30;

/// The frame timer scrolling is paced by. Each frame takes the whole clock
/// units elapsed since the previous one, so a stretch of real time moves the
/// camera the same distance however many frames it is drawn as.
struct ScrollClock {
    uint32_t last_units{}; // previous frame's clock reading, as Game.last_frame_time holds it
    bool started{};
};

/// Advances the clock to `now_ms` and returns the clock units since the previous frame.
///
/// The result is what the frame loop stores in Game.frame_elapsed at the start
/// of every frame.
///
/// @param[in,out] clock Scroll clock; its last reading moves to `now_ms`.
/// @param now_ms Current real time in milliseconds.
/// @return Whole kScrollClockHz units elapsed since the previous call; 0 on the
///         first call, which only starts the clock.
uint32_t scroll_clock_advance(ScrollClock& clock, uint32_t now_ms) noexcept;

/// Computes the pixels one frame scrolls: speed times elapsed units, capped at kMaxScrollStep.
///
/// @param scroll_speed Game.scroll_speed, pixels per clock unit.
/// @param frame_elapsed Clock units since the previous frame (Game.frame_elapsed).
/// @return Scroll distance in pixels, 0..kMaxScrollStep.
[[nodiscard]] int32_t scroll_step(uint8_t scroll_speed, uint32_t frame_elapsed) noexcept;

/// Pointer position the edge test uses: the game cursor, pinned to the last
/// pixel. When the pointer is not captured, a desktop cursor within
/// kEdgeCatchPixels past the right or bottom edge of a focused window counts
/// as sitting on that edge.
struct ScrollPointer {
    int32_t cursor_x, cursor_y;   // game cursor
    bool captured;                // exclusive pointer: only the game cursor counts
    int32_t desktop_x, desktop_y; // desktop cursor
    bool focused;                 // the game window has keyboard focus
};

/// Arrow keys held this frame and whether the chat line (TALK.GUI) is open,
/// which keeps the arrows for text editing.
struct ScrollKeys {
    bool left, up, right, down;
    bool talk_open;
};

/// Moves the camera (the renderer clamps it); called when the position changed.
struct CameraMover {
    void* user;
    void (*set_position)(void* user, int32_t x, int32_t y);
};

/// Resolves the pointer position the edge test uses.
///
/// A captured pointer keeps the game cursor, pinned to the last pixel of the
/// view. An uncaptured pointer takes the desktop cursor instead when it lies
/// within kEdgeCatchPixels past the right or bottom edge of a focused window,
/// pinned to that edge.
///
/// @param pointer Game and desktop cursor positions, capture and focus state.
/// @param width View width in pixels.
/// @param height View height in pixels.
/// @return `pointer` with cursor_x/cursor_y replaced by the position to test.
[[nodiscard]] ScrollPointer
scroll_pointer_position(const ScrollPointer& pointer, int32_t width, int32_t height) noexcept;

/// Scrolls the camera for one frame toward a held arrow key or a screen edge the pointer touches.
///
/// The step is scroll_step(Game.scroll_speed, Game.frame_elapsed); horizontal
/// and vertical moves apply independently. A move calls `mover` with the new
/// position and clears the camera follow (follow_point_ticks, follow_unit,
/// follow_target). Touching the left edge only counts above the bottom edge,
/// and the top edge only left of the right edge.
///
/// @param[in,out] game Game block: reads camera position, view size and scroll
///                     speed; clears the follow fields on a move.
/// @param pointer Pointer state before scroll_pointer_position resolves it.
/// @param keys Arrow keys held this frame and whether the chat line is open.
/// @param mover Receives the new camera position in map pixels.
/// @return Whether the camera moved.
bool scroll_camera(
    Game& game, const ScrollPointer& pointer, const ScrollKeys& keys, const CameraMover& mover
);

} // namespace oa::ui::hud
