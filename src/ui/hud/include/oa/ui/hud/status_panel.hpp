// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Space-bar status strip: LIGHTBAR frame 1 of COMMONGUI.GAF slides up over
// the bottom of the game view with the game time, the local player's unit
// count and the game speed, written in the GUI's second font (hattfont11.gaf).
#pragma once

#include "oa/core/game_state.h"

#include <cstddef>
#include <cstdint>

namespace oa::ui::hud {

inline constexpr int32_t kStatusPanelRise = 31;    // rows above the view bottom when fully up
inline constexpr uint32_t kStatusPanelStepMs = 15; // between animation steps
inline constexpr int32_t kStatusPanelLightbarFrame = 1;
inline constexpr int32_t kStatusPanelTextDrop = 10; // text row below the strip top
inline constexpr int32_t kStatusPanelTimeX = 0x19;  // columns right of the view left
inline constexpr int32_t kStatusPanelUnitsX = 0xbe;
inline constexpr int32_t kStatusPanelSpeedX = 0x17c;
inline constexpr uint16_t kNormalGameSpeed = 10;

/// Resets the top bar and the strip for a mission start.
///
/// Clears the shown energy and metal and their capacities, the strip offset
/// and its LIGHTBAR frame; the caller then binds LIGHTBAR frame 1 of
/// COMMONGUI.GAF again with its origin at zero.
///
/// @param[in,out] game Game block whose resource_readout, status_panel_offset
///                     and status_lightbar are cleared.
void reset_status_panel(Game& game) noexcept;

/// Takes one animation step of Game.status_panel_offset once the step timer has passed.
///
/// While held the strip rises a third of the remaining way to
/// -kStatusPanelRise, at least a row; released, it drops a third of its
/// height, at least a row. The HUD redraws every frame, so no gadget is
/// invalidated when the strip starts or stops moving.
///
/// @param[in,out] game Game block whose status_panel_offset moves (rows, 0 down
///                     to -kStatusPanelRise).
/// @param[in,out] next_step_ms Time of the next step in milliseconds; advanced
///                             to now_ms + kStatusPanelStepMs when a step is due.
/// @param now_ms Current time in milliseconds.
/// @param held Whether the space bar is held and no text field has focus.
/// @return Whether the offset moved.
bool status_panel_step(Game& game, uint32_t& next_step_ms, uint32_t now_ms, bool held) noexcept;

// UI text lookup; a null lookup or a null result leaves the text as written.
using TranslateText = const char* (*)(void* context, const char* text);

/// Formats "Game Time : hh:mm:ss" from the 30 Hz Game.tick.
///
/// The strip and the console's clock show the time this way; the label goes
/// through `translate`.
///
/// @param game Game block holding the tick.
/// @param translate UI text lookup; may be null.
/// @param context Context passed to `translate`.
/// @param[out] out Buffer the line is written to, cut to fit.
/// @param bytes Size of `out`.
void format_game_time(
    const Game& game, TranslateText translate, void* context, char* out, std::size_t bytes
) noexcept;

struct StatusPanelText {
    char time[256]{};
    char units[256]{};
    char speed[256]{};
};

/// Formats the strip's readouts.
///
/// "Game Time : hh:mm:ss" from the 30 Hz tick, "Total Units : n  (Max m)" for
/// the local player, and "Game Speed Normal" or the signed step from normal,
/// followed by the requested step in parentheses while the game has not
/// reached it. The labels go through `translate`.
///
/// @param game Game block holding the tick, the local player and the speeds.
/// @param translate UI text lookup; may be null.
/// @param context Context passed to `translate`.
/// @param[out] out The three readout lines.
void format_status_panel(
    const Game& game, TranslateText translate, void* context, StatusPanelText& out
) noexcept;

} // namespace oa::ui::hud
