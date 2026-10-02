// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Battlefield camera: position/target clamps, glide, shake, follow, stored
// view slots, mouse-look and the radar view rectangle. Everything reads and
// writes the canonical Game block; the references it holds (follow unit,
// follow target, start table) are resolved by the caller.

#include "oa/core/game_state.h"
#include "oa/core/unit.h"

#include <cstddef>
#include <cstdint>

namespace oa::present::world_renderer {

// Battlefield destination origin inside the 640x480 frame (command panel
// width, top bar height).
inline constexpr int32_t battlefield_origin_x = 0x80;
inline constexpr int32_t battlefield_origin_y = 0x20;

// Largest per-tick glide step, in map pixels.
inline constexpr int32_t camera_glide_max_step = 0x140;
inline constexpr int32_t camera_slot_count = 4;
// Mouse-look moves the camera one map cell per this many pointer pixels.
inline constexpr int32_t mouse_look_pixels_per_cell = 4;
// Denominator applied to the 15-bit rand() value.
inline constexpr int32_t random_range = 0x8000;

// Game.radar_blink_flags bits touched by the camera and radar passes.
inline constexpr uint16_t radar_flag_blink = 0x1;
inline constexpr uint16_t radar_flag_redraw = 0x2;
inline constexpr uint16_t radar_flag_mapped_dirty = OA_RADAR_MAPPED_DIRTY;
// Game.visibility_flags bit cleared whenever the camera is set.
inline constexpr uint8_t visibility_flag_fog_mask_current = OA_VISIBILITY_FOG_MASK_CURRENT;
// Game.camera_flags bit 0: shake active.
inline constexpr uint8_t camera_flag_shake = 0x1;

// 32-bit words of Game.pointer_state and Game.saved_pointer_state.
inline constexpr int32_t pointer_state_words = 6;
// Bit of the pointer-state button word that keeps mouse-look engaged.
inline constexpr uint32_t pointer_button_look = 0x2;

/// Returns one of the 16 UI palette indices of Game.ui_colors.
///
/// @param game game block
/// @param index colour slot; only the low 4 bits are used
/// @return the palette index
[[nodiscard]] uint8_t game_ui_color(const Game& game, int32_t index) noexcept;

// rand() replacement: returns 0..0x7fff.
struct RandomSource {
    void* user = nullptr;
    /// Draws the next rand() value.
    ///
    /// @param user RandomSource::user
    /// @return a value in 0..0x7fff
    int32_t (*next)(void* user) = nullptr;
};

// Caller-resolved targets of the follow references in the Game block.
struct CameraFollow {
    const Unit* unit = nullptr;        // Game.follow_unit
    const FixedVec3* target = nullptr; // Game.follow_target: the position after its first word
};

/// Stops the camera following: no follow point countdown, unit or target.
///
/// @param[in,out] game game block holding the follow fields
void camera_stop_follow(Game& game) noexcept;

/// Clamps the current camera to the map and refreshes the radar view rectangle.
///
/// @param[in,out] game game block holding the camera and map size
void camera_clamp_position(Game& game) noexcept;

/// Clamps the glide target to the map.
///
/// Each axis is clamped to [0, map pixels - view]; a negative value becomes 0.
///
/// @param[in,out] game game block holding the glide target and map size
void camera_clamp_target(Game& game) noexcept;

/// Moves the camera at once or sets the glide target.
///
/// Either way the fog edge mask is marked stale; an immediate move also asks
/// for a radar redraw and sets the target to the clamped camera.
///
/// @param[in,out] game game block holding the camera
/// @param x camera map-pixel X
/// @param y camera map-pixel Y
/// @param glide 0 to move at once, otherwise glide towards (x, y)
void camera_set_position(Game& game, int32_t x, int32_t y, int32_t glide) noexcept;

/// Applies one tick of camera shake while the shake bit is set.
///
/// The shake spans amplitude * remaining / duration on each axis, centred on
/// the camera; the bit clears when the countdown runs out.
///
/// @param[in,out] game game block holding the camera and shake fields
/// @param random rand() source
void camera_shake(Game& game, const RandomSource& random) noexcept;

/// Centres the camera on a map pixel.
///
/// @param[in,out] game game block holding the camera
/// @param x map-pixel X to centre on
/// @param y map-pixel Y to centre on
/// @param glide 0 to move at once, otherwise glide
void camera_center(Game& game, int32_t x, int32_t y, int32_t glide) noexcept;

/// Centres the camera on a 16.16 world position; height lifts it by half.
///
/// @param[in,out] game game block holding the camera
/// @param position signed 16.16 world position
/// @param glide 0 to move at once, otherwise glide
void camera_center_on_position(Game& game, const FixedVec3& position, int32_t glide) noexcept;

/// Runs the per-frame camera pass: follow, glide towards the target, shake, clamp.
///
/// A follow-point countdown takes precedence over a follow target, which takes
/// precedence over a follow unit; a follow unit that is no longer live stops the
/// follow. Each glide step halves the gap, at most 320 map pixels.
///
/// @param[in,out] game game block holding the camera fields
/// @param follow caller-resolved follow unit and target
/// @param random rand() source for the shake
void camera_tick(Game& game, const CameraFollow& follow, const RandomSource& random) noexcept;

/// Returns the map-pixel column a 16.16 world position is centred on.
///
/// @param position signed 16.16 world position
/// @return the signed high word of X
[[nodiscard]] int32_t world_screen_x(const FixedVec3& position) noexcept;
/// Returns the map-pixel row of a 16.16 world position, raised by half its height.
///
/// @param position signed 16.16 world position
/// @return the signed high word of Z - Y / 2
[[nodiscard]] int32_t world_screen_y(const FixedVec3& position) noexcept;

// Platform cursor boundary used by mouse-look.
struct CursorSink {
    void* user = nullptr;
    /// Refreshes Game pointer_state from the platform.
    ///
    /// @param user CursorSink::user
    /// @param[out] game game block receiving the pointer state
    void (*poll)(void* user, Game& game) = nullptr;
    /// Returns the screen width in pixels.
    ///
    /// @param user CursorSink::user
    /// @return the width
    int32_t (*screen_width)(void* user) = nullptr;
    /// Returns the screen height in pixels.
    ///
    /// @param user CursorSink::user
    /// @return the height
    int32_t (*screen_height)(void* user) = nullptr;
    /// Moves the platform pointer.
    ///
    /// @param user CursorSink::user
    /// @param x screen X
    /// @param y screen Y
    void (*set_position)(void* user, int32_t x, int32_t y) = nullptr;
    /// Draws the pointer.
    ///
    /// @param user CursorSink::user
    void (*draw)(void* user) = nullptr;
};

/// Starts mouse-look: stops following, saves the pointer and centres it.
///
/// @param[in,out] game game block holding the pointer and mouse-look fields
/// @param cursor platform pointer boundary
void mouse_look_begin(Game& game, const CursorSink& cursor) noexcept;

/// Ends mouse-look and puts the pointer back where it started.
///
/// @param[in,out] game game block holding the mouse-look fields
/// @param cursor platform pointer boundary
void mouse_look_end(Game& game, const CursorSink& cursor) noexcept;

/// Moves the camera by the pointer's travel from the anchor, then recentres the pointer.
///
/// The camera moves one map cell per four pointer pixels; releasing the look
/// button ends mouse-look.
///
/// @param[in,out] game game block holding the pointer and mouse-look fields
/// @param cursor platform pointer boundary
void mouse_look_update(Game& game, const CursorSink& cursor) noexcept;

// One 12-byte entry of a mission's start table, which the campaign file's
// rules give.
struct StartEntry {
    int32_t kind{}; // 1 together with index == 0 marks the view start
    int32_t index{};
    int16_t x{};
    int16_t y{};
};

static_assert(sizeof(StartEntry) == 12);

/// Centres the camera on the first (1, 0) start entry, if any.
///
/// @param[in,out] game game block holding the camera
/// @param entries the mission's start table (the campaign file's rules); may be null
/// @param count number of entries
void camera_to_start_entry(Game& game, const StartEntry* entries, int32_t count) noexcept;

/// Stores the current camera in a view slot.
///
/// @param[in,out] game game block holding the camera and slots
/// @param slot view slot, 0..camera_slot_count-1
void camera_store_slot(Game& game, int32_t slot) noexcept;

/// Stops following and jumps to a stored view slot.
///
/// @param[in,out] game game block holding the camera and slots
/// @param slot view slot, 0..camera_slot_count-1
void camera_recall_slot(Game& game, int32_t slot) noexcept;

/// Computes the radar-picture rectangle covered by the battlefield view.
///
/// @param game game block holding the camera, view cells, map size and radar size
/// @param[out] rect inclusive rectangle in radar-picture pixels
/// @return false, leaving `rect` untouched, for a zero map size
bool radar_view_rect(const Game& game, Rect32& rect) noexcept;

/// Projects a unit's 16.16 position to the screen with 16-bit camera arithmetic.
///
/// @param game game block holding the camera
/// @param position signed 16.16 world position
/// @param[out] x screen X, the battlefield origin added
/// @param[out] y screen Y, raised by half the height
void project_unit_to_screen(
    const Game& game, const FixedVec3& position, int32_t& x, int32_t& y
) noexcept;

} // namespace oa::present::world_renderer
