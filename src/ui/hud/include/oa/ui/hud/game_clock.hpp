// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The game clock the console's Clock option shows over the bottom-left
// corner of the battlefield: "Game Time : hh:mm:ss" (format_game_time) in
// UI colour 15, written as a label with no background. It gives way to the
// space bar's status strip, which shows the game time in its place, and
// fades back in once the strip and the kills board have closed.
#pragma once

#include "oa/core/game_state.h"
#include "oa/ui/hud/game_fields.hpp"

#include <cstdint>

namespace oa::ui::hud {

/// Screen column of the clock's pen: two right of the 128-column side panel.
inline constexpr int32_t kClockLeft = 0x82;
/// Rows from the screen's bottom edge up to the clock's pen row, less the
/// font's height: two above the 32-row bottom bar.
inline constexpr int32_t kClockRise = 0x22;
/// Game.ui_colors slot of the clock's text.
inline constexpr uint8_t kClockColorSlot = 15;

/// The label font the clock is written in.
enum class ClockFont : uint8_t {
    /// COMIX, the message log's font.
    message_log,
    /// The viewer's side's font (SIDEDATA.TDF font=, CONSOLE.FNT).
    side_panel,
};

/// Milliseconds the clock takes to fade back in once the strip and the
/// kills board have closed.
inline constexpr uint32_t kClockFadeInMs = 250;
/// The clock's opacity drawn whole, in 256ths.
inline constexpr uint32_t kClockOpaque = 256;

/// How the clock shows while the space bar's status strip comes and goes.
enum class ClockShowing : uint8_t {
    /// drawn whole
    shown,
    /// not drawn: Space is held, or the strip or the board is still closing
    hidden,
    /// fading back in since ClockFade::fade_start_ms
    fading,
};

/// The clock's showing kept between frames.
struct ClockFade {
    ClockShowing showing{ClockShowing::shown};
    uint32_t fade_start_ms{}; ///< when the fade began, on the clock the strip steps on
};

/// Moves the clock's showing on for a frame and returns its opacity.
///
/// Space held hides the clock at once, from the frame the strip starts to
/// rise. Let go, it stays hidden until the strip is down and the kills board
/// has stopped sliding; from the first frame after their last closing frame
/// it fades in over kClockFadeInMs, and Space held again hides it at once.
/// The fade runs on the strip's clock, so it takes the same time at any
/// frame rate or game speed, and while the game is paused.
///
/// @param[in,out] fade the clock's showing
/// @param held whether Space is held while no text field has the keyboard,
///             as the strip rises (status_panel_step)
/// @param closed whether the strip is down and the kills board is at rest
/// @param now_ms the time in milliseconds on the clock the strip steps on
/// @return the opacity in 256ths: 0 hidden up to kClockOpaque whole
uint32_t step_clock_fade(ClockFade& fade, bool held, bool closed, uint32_t now_ms) noexcept;

/// Returns the font the clock is written in.
///
/// The clock takes the label font the HUD left set: the unit markers set
/// the viewer's side's font each frame, and the message log, drawn after
/// them and before the clock, sets COMIX when it shows lines at all
/// (Game.text_lines not 0), whether or not it holds any.
///
/// @param game Game block holding the message log's line count.
/// @return message_log while the log shows lines, side_panel otherwise
[[nodiscard]] inline ClockFont clock_font(const Game& game) noexcept {
    return message_lines(game) != 0 ? ClockFont::message_log : ClockFont::side_panel;
}

/// Returns the clock's pen row on a screen.
///
/// The label's glyph rows start the font's row lift above it (one row for
/// COMIX and CONSOLE).
///
/// @param screen_height Screen height in rows (480 for the game's screen).
/// @param font_height The font header's height: the low byte of its first word.
/// @return the row kClockRise plus the font's height above the bottom edge
[[nodiscard]] inline int32_t clock_pen_row(int32_t screen_height, uint8_t font_height) noexcept {
    return screen_height - kClockRise - font_height;
}

} // namespace oa::ui::hud
