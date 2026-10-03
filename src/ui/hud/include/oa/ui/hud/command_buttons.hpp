// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The order page's command buttons (MOVE, STOP, ATTACK, ...): the order a lit
// button arms and the sound a click on one plays.
#pragma once

#include "oa/ui/hud/boundary.hpp"

#include "oa/core/world.h"

#include <cstddef>
#include <cstdint>

namespace oa::ui::hud {

/// Game.pointer_flags bit set while a drag box is open
/// (sim::gameplay_input::pointer_box_drag).
inline constexpr uint8_t kPointerFlagDragBox = 0x08;

// Values of Game.pointer_command (sim::gameplay_input::OrderCommand) the
// command buttons arm.
namespace armed_order {
inline constexpr uint8_t default_order = 1; // nothing armed: the pointer acts by context
inline constexpr uint8_t move = 2;
inline constexpr uint8_t attack = 3;
inline constexpr uint8_t blast = 4;
inline constexpr uint8_t unload = 5;
inline constexpr uint8_t load = 6;
inline constexpr uint8_t defend = 7;
inline constexpr uint8_t repair = 8;
inline constexpr uint8_t patrol = 9;
inline constexpr uint8_t reclaim = 0x0c;
inline constexpr uint8_t capture = 0x0d;
} // namespace armed_order

// allsound.tdf entries a command button click plays.
inline constexpr const char* kImmediateOrdersSound = "immediateorders";
inline constexpr const char* kSpecialOrdersSound = "specialorders";

// Group order the STOP button gives at once.
inline constexpr const char* kStopOrderTag = "STOP";

/// Tells whether the unit the order panel shows is gone: Game.panel_unit_id
/// names a slot that holds no unit type. While it is, an armed order is
/// disarmed each frame (ui.interface-fixes cursor-reset).
///
/// @param world World holding the panel's unit
/// @return true when the panel shows a unit whose slot is now empty
[[nodiscard]] bool panel_unit_vanished(const World& world) noexcept;

/// Reads the order the pointer gives.
///
/// @param game Game block.
/// @return Game.pointer_command, one of the armed_order values.
[[nodiscard]] uint8_t armed_order_of(const Game& game) noexcept;

/// Handles a click on a command button of the order page.
///
/// The button is the first of MOVE, STOP, ATTACK, BLAST, DEFEND, REPAIR,
/// PATROL, RECLAIM, CAPTURE, UNLOAD and LOAD that `name` holds: a lit button
/// arms its order and one turned off the default order; STOP arms the default
/// order and gives the group STOP. The drag box bit is cleared. REPAIR,
/// RECLAIM, CAPTURE and UNLOAD play specialorders, the rest immediateorders.
/// The panel tries the standing order toggles (order_panel_toggle) first, so
/// MOVEORD never reaches here.
///
/// @param[in,out] game Game block: Game.pointer_command and Game.pointer_flags change.
/// @param name Clicked control name, matched by substring; null matches nothing.
/// @param status Button status the click has just set; nonzero while the button is lit.
/// @param events Receives the group STOP and the sound.
/// @return Whether `name` was a command button; false leaves everything unchanged.
bool order_panel_command(Game& game, const char* name, int16_t status, const HudEvents& events);

} // namespace oa::ui::hud
