// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Software mouse cursor and pointer capture. The cursor sprite is composited
// over the screen with a saved copy of the background, either synchronously
// (hide/show around each frame) or by a redraw thread that repaints it about
// 30 times a second under the frame lock.

#include "oa/platform/lock.hpp"
#include "oa/present/surface.h"
#include "oa/ui/services/input.hpp"

#include <atomic>
#include <cstdint>

namespace oa::ui::services {

// Drawing boundary for the cursor, one callback per drawing operation it needs.
// A null Surface pointer stands for the screen.
struct CursorDraw {
    void* context;
    // Keyed blit of src (or the screen) into dst (or the screen) at (x, y).
    void (*blit)(void* context, Surface* dst, const Surface* src, int32_t x, int32_t y);
    // Opaque copy of src into dst at (x, y), clipped to dst.
    void (*copy_clipped)(void* context, Surface* dst, const Surface* src, int32_t x, int32_t y);
    // Draw a sprite with its hotspot at (x, y). The sprite is null when no
    // cursor image has been set; the call is made anyway.
    void (*draw_sprite)(void* context, Surface* dst, const Sprite* sprite, int32_t x, int32_t y);
    // Reset dst's clip rectangle to its full extent.
    void (*reset_clip)(void* context, Surface* dst);
    // Describe the screen back buffer; false when it is unavailable.
    bool (*lock_screen)(void* context, Surface* out);
    // Show the two changed screen rectangles.
    void (*present)(
        void* context, const Surface* screen, const Rect32* old_area, const Rect32* new_area
    );
    Surface* (*create_surface)(void* context, const char* tag, int32_t width, int32_t height);
    void (*free_surface)(void* context, Surface* surface);
};

// Frame-lock tokens of the cursor redraw thread and the main thread (ASCII
// "MOUS", "MAIN").
inline constexpr int32_t cursor_thread_lock_token = 0x4d4f5553;
inline constexpr int32_t main_thread_lock_token = 0x4d41494e;
// Bytes of each scratch surface; the largest cursor it can hold is 1600 pixels.
inline constexpr int32_t cursor_save_bytes = 0x640;
// Display-context flag bit tested before stopping the redraw thread at shutdown:
// the mouse-capture bit of the display flags, as it appears in their high byte.
inline constexpr uint8_t display_flag_threaded_cursor = 0x4;

inline constexpr uint32_t cursor_frame_ms = 0x21;
inline constexpr uint32_t cursor_stop_poll_ms = 100;
inline constexpr int32_t cursor_stop_poll_limit = 20;

// Cursor and pointer fields of the display context.
struct CursorState {
    const CursorDraw* draw;
    const Input* input;
    platform::TokenLock* frame_lock;
    uint8_t display_flags; // high byte of the display flags
    PointerQueue pointer;
    int32_t hide_count; // cursor on screen while below 1
    const Sprite* image;
    int32_t x; // top-left of the saved background
    int32_t y;
    Surface* saved_background;   // what the cursor covers on screen
    Surface* scratch_background; // off-screen work areas of the threaded repaint
    Surface* scratch_composite;
    int32_t thread_started;            // 1 from a successful start until the thread stops
    int32_t threaded;                  // 1 while the redraw thread owns the cursor
    int32_t overlay_enabled;           // gates the threaded repaint
    std::atomic<int32_t> stop_request; // set to stop the thread, cleared by it on exit
};

/// Allocates the pointer ring and the cursor scratch surfaces.
///
/// The cursor starts hidden (hide count 1) with no image and the threaded
/// overlay disabled.
///
/// @param[in,out] state cursor state; draw must be set
/// @param capacity pointer events the ring holds
/// @param threaded 1 to also start the redraw thread; any other value draws synchronously
/// @return false when an allocation failed
bool pointer_capture_init(CursorState* state, int32_t capacity, int32_t threaded) noexcept;
/// Stops the redraw thread and frees the pointer ring and scratch surfaces.
///
/// The thread is stopped only when display_flag_threaded_cursor is set in the
/// display flags. Does nothing when the ring was never allocated.
///
/// @param[in,out] state cursor state
void pointer_capture_shutdown(CursorState* state) noexcept;

/// Draws the cursor sprite at the latest pointer position.
///
/// Does nothing while the redraw thread owns the cursor or the cursor is hidden.
///
/// @param state cursor state
void cursor_draw(CursorState* state) noexcept;
/// Hides the cursor one level deeper; the first hide restores the saved background.
///
/// Does nothing while the redraw thread owns the cursor.
///
/// @param[in,out] state cursor state
void cursor_hide(CursorState* state) noexcept;
/// Undoes one hide and redraws the cursor once it is visible again.
///
/// On becoming visible it samples the pointer, saves the background under the
/// sprite and draws it. Does nothing while the redraw thread owns the cursor.
///
/// @param[in,out] state cursor state
void cursor_show(CursorState* state) noexcept;
/// Moves the threaded cursor to the pointer in one composited update.
///
/// Composites the old and new areas off screen, copies both to the screen and
/// presents them. Does nothing while the overlay is disabled or the screen
/// cannot be locked.
///
/// @param[in,out] state cursor state; image must be set
void cursor_repaint(CursorState* state) noexcept;
/// Runs the redraw thread: repaints under the frame lock every 33 ms until stopped.
///
/// Clears stop_request on exit to acknowledge the stop.
///
/// @param[in,out] state cursor state shared with the main thread
void cursor_thread_run(CursorState* state) noexcept;
/// Starts the redraw thread and hands it the cursor.
///
/// @param[in,out] state cursor state; threaded becomes 1 when the thread starts
/// @return true when the thread started
bool cursor_thread_start(CursorState* state) noexcept;
/// Asks the redraw thread to stop and waits up to 20 polls of 100 ms.
///
/// @param[in,out] state cursor state
/// @return true when the thread stopped or was not running, false on timeout
bool cursor_thread_stop(CursorState* state) noexcept;
/// Replaces the cursor sprite under the frame lock.
///
/// @param[in,out] state cursor state
/// @param image new sprite, or null for none
void cursor_set_image(CursorState* state, const Sprite* image) noexcept;
/// Returns the current cursor sprite.
///
/// @param state cursor state
/// @return the sprite, or null when none is set
[[nodiscard]] const Sprite* cursor_image(const CursorState* state) noexcept;

} // namespace oa::ui::services
