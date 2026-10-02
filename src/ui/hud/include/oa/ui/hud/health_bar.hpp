// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Health bar and squad digit drawn under a unit on the battlefield, and
// whether the unit panel shows a unit's damage bar.
#pragma once

#include "oa/ui/hud/boundary.hpp"

#include "oa/core/world.h"

#include <cstdint>

namespace oa::ui::hud {

// Game.ui_colors slots of the bar.
inline constexpr int32_t kHealthTroughColor = 0;
inline constexpr int32_t kHealthHighColor = 10; // above two thirds
inline constexpr int32_t kHealthLowColor = 12;  // a third or less
inline constexpr int32_t kHealthMidColor = 14;

inline constexpr int32_t kHealthBarHalfWidth = 0x11;
inline constexpr int32_t kHealthBarHalfHeight = 2;
// Health fills 32 pixels of the inset trough at full health.
inline constexpr uint32_t kHealthBarFillShift = 5;

/// Game.graphics_flags bit of the DamageBars option.
inline constexpr uint16_t kGraphicsDamageBars = 0x0001;

/// Tells whether the battlefield draws a unit's health bar.
///
/// @param world World holding the DamageBars option and the viewpoint player.
/// @param unit Unit under consideration.
/// @return true with damage bars on (kGraphicsDamageBars) for the viewpoint
///         player's own units only.
[[nodiscard]] bool draws_health_bar(const World& world, const Unit& unit) noexcept;

/// Tells whether the battlefield draws a unit's squad digit (the Ctrl+digit
/// group it is in) below its health bar.
///
/// The digit follows the health bar's rule: another player's squads, an
/// ally's or a computer player's, never show, and watching another player
/// (Game.viewpoint_player) shows that player's squads instead.
///
/// @param world World holding the DamageBars option and the viewpoint player.
/// @param unit Unit under consideration.
/// @return true when the unit is in a squad (Unit.squad nonzero), damage bars
///         are on (kGraphicsDamageBars) and the viewpoint player owns it.
/// @quirk With damage bars off no squad digit shows either.
[[nodiscard]] bool draws_squad_digit(const World& world, const Unit& unit) noexcept;

/// Gives the character drawn as a unit's squad digit.
///
/// @param unit Unit in a squad.
/// @return '0' plus the low byte of Unit.squad: '1'..'9' for the squads the
///         keys make.
[[nodiscard]] char squad_digit(const Unit& unit) noexcept;

/// Tells whether the unit panel draws a unit's damage bar.
///
/// @param world World holding the viewpoint player.
/// @param unit Unit shown in the panel.
/// @param def The unit's type.
/// @return true for the viewpoint player's own units, and for another
///         player's unless its type hides damage (OA_UNIT_DEF_FLAG_HIDE_DAMAGE).
[[nodiscard]] bool
panel_shows_damage(const World& world, const Unit& unit, const UnitDef& def) noexcept;

/// Two solid rectangles with inclusive corners, the fill drawn over the trough.
struct HealthBar {
    Rect32 trough;
    uint8_t trough_color{};
    Rect32 fill;
    uint8_t fill_color{};
};

/// Lays out the health bar for a unit centred on (x, y).
///
/// The trough spans kHealthBarHalfWidth and kHealthBarHalfHeight around the
/// centre in UI colour 0; the fill starts one pixel inside it and is
/// (health << 5) / max_damage pixels long, in UI colour 10 above two thirds of
/// max_damage, 14 above one third and 12 otherwise.
///
/// @param game Game block holding the UI colours.
/// @param unit Unit whose health is shown.
/// @param def The unit's type (max_damage).
/// @param x Centre column in screen pixels.
/// @param y Centre row in screen pixels.
/// @param[out] bar Rectangles and colours; written only on success.
/// @return false when the unit has no health left or its type no max_damage.
/// @quirk The fill is not clamped to the trough, so an overhealed unit's bar
///        runs past it.
[[nodiscard]] bool unit_health_bar(
    const Game& game, const Unit& unit, const UnitDef& def, int32_t x, int32_t y, HealthBar& bar
) noexcept;

} // namespace oa::ui::hud
