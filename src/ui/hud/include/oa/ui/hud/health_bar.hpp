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
/// Screen rows from a unit's centre down to its bar's centre at the game's
/// view.
inline constexpr int32_t kHealthBarBelowUnit = 10;
/// The furthest the battlefield zooms out: a sixth of the game's view, in
/// screen pixels per map pixel.
inline constexpr float kHealthBarFurthestZoom = 1.0F / 6.0F;
/// The share of the game's size a bar keeps at kHealthBarFurthestZoom.
inline constexpr float kHealthBarFurthestScale = 1.0F / 3.0F;

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

/// How large a bar is drawn, and how far below its unit, in screen pixels.
/// The defaults are the game's: a trough 35 pixels across and 5 down, its
/// centre 10 rows below the unit's.
struct HealthBarSize {
    int32_t half_width{kHealthBarHalfWidth};   ///< columns either side of the centre
    int32_t half_height{kHealthBarHalfHeight}; ///< rows above and below the centre
    int32_t below_unit{kHealthBarBelowUnit};   ///< rows from the unit's centre to the bar's
};

/// Gives the share of the game's size the bars are drawn at for a zoom.
///
/// At the game's view and zoomed in the bars keep the game's size on
/// screen. Zoomed out they shrink by the same share at each step of the
/// zoom, to kHealthBarFurthestScale at kHealthBarFurthestZoom: the share is
/// the zoom raised to log 3 / log 6, so a view twice as far out draws them
/// at about 0.65 of the game's size and four times as far out at about
/// 0.43. Past the furthest zoom the share stays a third.
///
/// @param zoom screen pixels per map pixel; 1 is the game's view
/// @return the share of the game's size, from a third to 1
[[nodiscard]] float health_bar_scale(float zoom) noexcept;

/// Gives the size of the bars for a zoom, in whole screen pixels.
///
/// The game's half width, half height and distance below the unit are each
/// scaled by health_bar_scale(zoom) and rounded to the nearest pixel, the
/// half height to at least 1, so that the fill inside the trough is at
/// least a pixel tall. The trough stays an odd number of pixels each way,
/// centred on the bar's centre. At zoom 1 and in it is the game's size.
///
/// @param zoom screen pixels per map pixel; 1 is the game's view
/// @return the bar's size: at the furthest zoom out a trough 13 pixels
///         across and 3 down, 3 rows below the unit
[[nodiscard]] HealthBarSize health_bar_size(float zoom) noexcept;

/// Lays out the health bar for a unit centred on (x, y).
///
/// The trough spans the size's half width and half height around the
/// centre in UI colour 0; the fill starts one pixel inside it and is
/// health * (2 * half_width - 2) / max_damage pixels past its first, in
/// UI colour 10 above two thirds of max_damage, 14 above one third and 12
/// otherwise. At the game's size that is (health << 5) / max_damage, the
/// fill spanning the trough's inside at full health; the product keeps its
/// low 32 bits.
///
/// @param game Game block holding the UI colours.
/// @param unit Unit whose health is shown.
/// @param def The unit's type (max_damage).
/// @param x Centre column in screen pixels.
/// @param y Centre row in screen pixels.
/// @param[out] bar Rectangles and colours; written only on success.
/// @param size the bar's size; the game's by default (health_bar_size)
/// @return false when the unit has no health left or its type no max_damage.
/// @quirk The fill is not clamped to the trough, so an overhealed unit's bar
///        runs past it.
[[nodiscard]] bool unit_health_bar(
    const Game& game,
    const Unit& unit,
    const UnitDef& def,
    int32_t x,
    int32_t y,
    HealthBar& bar,
    const HealthBarSize& size = {}
) noexcept;

} // namespace oa::ui::hud
