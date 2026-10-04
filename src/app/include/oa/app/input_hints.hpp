// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The touch and pen hints the touch controls read input with.
#pragma once

namespace oa::app {

/// Sets the touch and pen hints the touch controls read input with: no mouse events made
/// from fingers or touch events made from the mouse, the pen as a mouse, no touch events
/// made from the pen.
///
/// Called before SDL starts video when touch controls are on from the start, and when the
/// first finger switches them on (SDL reads these hints live). A hint that an environment
/// variable already holds (SDL refuses a normal-priority hint then) is reported once on
/// std::clog and left as the variable says; any other refusal throws std::runtime_error.
/// Safe to call more than once.
void set_input_hints();

} // namespace oa::app
