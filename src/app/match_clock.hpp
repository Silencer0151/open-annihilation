// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// When the running match's clock steps: the rules that hold a match on
// this machine alone for its menus and its outcome, the pause bit of
// Game.sim_run_flags that holds every match, whoever set it, and a frame
// whose clock reading lies behind the clock's last step.
#pragma once

#include <cstdint>

namespace oa::app {

/// Tells whether the running match's clock steps this frame.
///
/// A match played on this machine alone holds while a menu is open or once
/// it is finished. A match shared with other players' machines never holds
/// for a menu, and keeps stepping while it waits on its outcome, until it
/// leaves for the end-of-game screen. The pause bit is apart from this: it
/// holds the steps inside the clock (clock_flags_with_pause).
///
/// @param shared_match the match is played with other players' machines
///        (extension_state::shared_match)
/// @param menu_open an in-game menu or panel is open over the match
/// @param finished the match shows its victory or defeat
/// @return true when the clock advances and offers its pending steps
[[nodiscard]] bool match_clock_runs(bool shared_match, bool menu_open, bool finished) noexcept;

/// Returns the match clock's run flags with the pause bit of Game.sim_run_flags.
///
/// The clock offers no step while bit 0 of its run flags is set and keeps
/// its time moving, so a match that resumes does not catch up the paused
/// time. The other bits, which the clock keeps itself, are kept.
///
/// @param clock_flags the clock's run flags (base::game_loop::Timing::flags)
/// @param sim_run_flags the running match's Game.sim_run_flags
/// @return clock_flags with bit 0 taken from sim_run_flags
[[nodiscard]] uint16_t
clock_flags_with_pause(uint16_t clock_flags, uint16_t sim_run_flags) noexcept;

/// Tells whether a frame's clock reading lies behind the match clock's last step.
///
/// A clock set during a frame past the frame's own time (a load, a
/// screenshot or a film frame sets it to the moment it ends) reads behind
/// it, and the frame holds its step, so that the next frame steps from there
/// and the time left out stays out. The reading turns over to 0 about every
/// 39.8 hours (base::game_loop::scaled_clock_turn); one that has turned over
/// since the last step lies after it (base::game_loop::scaled_clock_before):
/// the frame steps, the step runs no tick for that frame, and the frames
/// after it step as before, as in 3.1c.
///
/// @param reading the frame's clock reading, in clock units
/// @param previous_clock the reading the clock last stepped at
///        (base::game_loop::Timing::previous_clock)
/// @return true when the frame holds its step
[[nodiscard]] bool clock_reading_behind(uint32_t reading, uint32_t previous_clock) noexcept;

} // namespace oa::app
