// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unit info panel (UNITINFOx.GUI): picture, costs and movement statistics of
// the unit type under the cursor or behind a build button.
#pragma once

#include "oa/ui/hud/boundary.hpp"
#include "oa/ui/hud/order_panel.hpp"
#include "oa/ui/hud/unit_labels.hpp"

#include "oa/core/world.h"

#include <cstddef>
#include <cstdint>

namespace oa::ui::hud {

inline constexpr size_t kPropertyListBytes = 0xc0;
/// Attribute word every label the panel adds carries. /* ? */
inline constexpr uint32_t kUnitInfoLabelAttributes = 0x411;
/// Build-button names are unit names of at most this many characters.
inline constexpr size_t kButtonUnitNameBytes = 0x10;

/// Writes the panel's statistic lines as consecutive NUL-terminated strings ended by an empty one.
///
/// The lines are a blank, energy cost, metal cost, build time, a blank, then
/// top speed ("m/s", 0.4 m per pixel), acceleration ("m/s/s") and turn rate
/// ("deg/s"), or three "N/A" for a building. Lines that do not fit are dropped.
///
/// @param[out] out Property list buffer; cleared first.
/// @param def Unit type shown.
/// @param ticks_per_second Game ticks per second; scales the per-tick movement values.
/// @param localize Language lookup for the units and "N/A"; may be null.
/// @param user Context passed to `localize`.
void format_unit_properties(
    char (&out)[kPropertyListBytes],
    const UnitDef& def,
    int32_t ticks_per_second,
    Localize localize,
    void* user
);

/// Panel services beyond the named controls.
struct UnitInfoHost {
    void* user{};
    /// Adds a TEXT label to the panel.
    void (*add_label)(void* user, const char* text, int16_t x, int16_t y, uint32_t attributes){};
    /// Loads a picture (e.g. "unitpics\\ARMCOM.PCX") into the HOTR control.
    void (*set_picture)(void* user, const char* path){};
    /// Releases the HOTR picture.
    void (*free_picture)(void* user){};
    Localize localize{};
};

/// Chooses the unit type the unit info key shows.
///
/// @param world World holding the cursor unit and the viewpoint player.
/// @param button_name Build button under the pointer (a unit name, first
///                    kButtonUnitNameBytes characters used), or null when none.
/// @param type_for_name Maps a unit name to its type index; may be null.
/// @param can_see Whether the viewer sees a unit; null sees nothing.
/// @param user Context passed to both callbacks.
/// @return The button's type, else the cursor unit's type when the viewpoint
///         player sees it; 0 when there is nothing to show.
[[nodiscard]] uint16_t unit_info_subject(
    World& world,
    const char* button_name,
    uint16_t (*type_for_name)(void* user, const char* name),
    bool (*can_see)(void* user, const Player& viewer, const Unit& unit),
    void* user
);

/// Loads UNITINFOx.GUI for a unit type and fills it with its picture, costs and statistics.
///
/// The picture is "unitpics\<unit_name>.PCX"; the headings and labels are
/// localised and added with the statistic lines of format_unit_properties;
/// NAME takes the type's name.
///
/// @param world World holding the unit types and frame_flags.
/// @param type Unit type index; 0 or past the table opens nothing.
/// @param ticks_per_second Game ticks per second for the movement values.
/// @param loader Loads the panel.
/// @param controls Named controls of the loaded panel.
/// @param host Label, picture and localisation services.
/// @return false when the panel is already open, the type is invalid or loading failed.
bool open_unit_info_panel(
    World& world,
    uint16_t type,
    int32_t ticks_per_second,
    const PanelLoader& loader,
    const PanelControls& controls,
    const UnitInfoHost& host
);

/// What the host does after a unit info panel click.
enum class UnitInfoClick : uint8_t { closed, done, clear_selection };

/// Handles a click on the unit info panel.
///
/// Closing releases the picture and clears kFrameUnitInfoOpen; DONE plays
/// "smlbutton".
///
/// @param[in,out] game Game block whose frame_flags change on close.
/// @param name Clicked control name, compared case-insensitively; null when the panel closes.
/// @param host Releases the picture.
/// @param events Receives the button sound.
/// @return What the host does next.
UnitInfoClick unit_info_panel_click(
    Game& game, const char* name, const UnitInfoHost& host, const HudEvents& events
);

} // namespace oa::ui::hud
