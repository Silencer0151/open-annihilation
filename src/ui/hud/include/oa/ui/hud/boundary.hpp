// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Platform and gadget-engine boundaries used by the in-game HUD panels.
#pragma once

#include "oa/core/game_state.h"
#include "oa/core/unit.h"
#include "oa/core/unit_def.h"

#include <cstdint>

namespace oa::ui::hud {

/// Named controls of the loaded HUD panel. Every callback is optional.
struct PanelControls {
    void* user;
    /// Index of the named control, or -1 when the panel has none.
    int32_t (*find)(void* user, const char* name);
    /// Sets the value shared by a control's radio/cycle group.
    void (*set_group_value)(void* user, int32_t index, int32_t value);
    /// Sets one control's own value.
    void (*set_value)(void* user, int32_t index, int32_t value);
    /// Greys a control out.
    void (*disable)(void* user, int32_t index);
    /// Sets a control's checked/enabled state.
    void (*set_state)(void* user, int32_t index, int32_t state);
    /// A control's current value (checkbox state, radio group choice).
    int32_t (*value)(void* user, int32_t index){};
    /// A text control's contents.
    const char* (*text)(void* user, int32_t index){};
    void (*set_text)(void* user, int32_t index, const char* text){};
    /// Gives a text control the keyboard focus.
    void (*focus)(void* user, int32_t index){};
};

/// Side effects the HUD raises in the rest of the game.
struct HudEvents {
    void* user;
    /// Plays the named interface sound (the game's named-sound table).
    void (*play_sound)(void* user, const char* name);
    /// Applies a standing order tag ("CLOAK_ON", "STANDING_FIREORDER", ...)
    /// with its value to every selected unit.
    void (*apply_standing_order)(void* user, const char* tag, int32_t value);
};

/// Read-only view over the unit pool and the unit-type table. Unit.def holds
/// a 32-bit handle, so a type is resolved through Unit.type_index instead.
struct UnitTable {
    Unit* units; // units[0] is the reserved empty slot
    uint16_t unit_count;
    const UnitDef* defs; // indexed by Unit.type_index
    int32_t def_count;
};

/// Looks up a unit's type through Unit.type_index.
///
/// @param table Unit pool and unit-type table.
/// @param unit Unit whose type is wanted.
/// @return The unit's type record, or nullptr when the table has no defs or
///         the index is 0 or past def_count.
[[nodiscard]] inline const UnitDef* unit_def(const UnitTable& table, const Unit& unit) noexcept {
    if (table.defs == nullptr || unit.type_index == 0 ||
        static_cast<int32_t>(unit.type_index) >= table.def_count)
        return nullptr;
    return &table.defs[unit.type_index];
}

/// Plays a named interface sound through `events`; does nothing without a callback.
///
/// @param events HUD side-effect callbacks.
/// @param name Sound name in the named-sound table.
inline void play_sound(const HudEvents& events, const char* name) {
    if (events.play_sound != nullptr)
        events.play_sound(events.user, name);
}

/// Finds a named control of the loaded HUD panel.
///
/// @param controls Named-control callbacks of the panel.
/// @param name Control name as the GUI file spells it.
/// @return The control's index, or -1 when the panel has none or no find callback is set.
inline int32_t find_control(const PanelControls& controls, const char* name) {
    return controls.find != nullptr ? controls.find(controls.user, name) : -1;
}

} // namespace oa::ui::hud
