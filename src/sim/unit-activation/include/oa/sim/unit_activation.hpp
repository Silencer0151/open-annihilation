// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace oa::sim::unit_activation {
inline constexpr uint8_t active_mask = 0x01, cloaked_mask = 0x04, building_mask = 0x08;
inline constexpr uint32_t cloak_notification = 0x10000;
// Unit.flags masks: mark_owned_selection keeps owned_selection_keep and sets
// owned_selection_bit; clear_cycle_marks keeps cycle_marks_keep.
inline constexpr uint32_t owned_selection_keep = 0xffffff7fu; // clears bit 0x80
inline constexpr uint32_t owned_selection_bit = 0x40u;
inline constexpr uint32_t cycle_marks_keep = 0xffffff3fu; // clears bits 0x40 and 0x80
enum class Sound : uint8_t { activate = 3, deactivate = 4, cloak = 14, decloak = 15 };

// Fields mark_owned_selection reads. Slot index is the selection id, not a pointer.
struct ListedUnit {
    uint8_t owner{};  // Unit.owner_index
    uint32_t flags{}; // Unit.flags
};

struct Host {
    virtual ~Host() = default;
    /// Calls a named function of the unit's script with no arguments, deferred.
    ///
    /// @param name script function, such as "Activate"
    virtual void script(std::string_view name) = 0;
    /// Plays one of the unit type's sounds.
    ///
    /// @param which sound to play
    virtual void sound(Sound which) = 0;
    /// Notifies the units attached to this one.
    ///
    /// @param value notification code; cloak_notification when the unit cloaks
    virtual void notify_attachments(uint32_t value) = 0;
    /// Refreshes the selected-unit display.
    virtual void refresh_selected_unit() = 0;
    /// Tests whether the unit's owner is present and simulated here (status 1 or 2).
    ///
    /// @return true for a local or computer owner
    virtual bool owner_simulates_here() = 0;
    /// Reports the changed flags of a unit its owner simulates here, for the other players.
    ///
    /// @param unit unit index
    /// @param flags the unit's Unit.state_flags as they stand after the callbacks
    virtual void flags_changed(uint16_t unit, uint8_t flags) = 0;
};

/// Sets or clears activation bits of a unit and runs the callbacks for each change.
///
/// Rising and falling bits are derived once. In order: Activate or Deactivate and its
/// sound, StartBuilding or StopBuilding, the cloak or decloak sound (cloaking also
/// notifies attachments), the selected-unit refresh, and, when the owner simulates
/// the unit here, the change report. Unknown bits still take part. Nothing happens
/// when the byte does not change.
///
/// @param[in,out] flags the unit's Unit.state_flags; changed before any callback
/// @param unit_index unit index; reread after the callbacks for the report
/// @param mask bits to change
/// @param enabled true sets the bits, false clears them
/// @param host script, sound, attachment, display and reporting services
/// @quirk The report carries the flags as the callbacks left them.
void change(uint8_t& flags, const uint16_t& unit_index, uint8_t mask, bool enabled, Host& host);

/// Marks the local player's selected units.
///
/// Each selected id is a unit slot. When its owner is the local player, its flags
/// gain bit 0x40 and lose bit 0x80; other bits stay and other owners are not
/// written. An empty list does nothing.
///
/// @param[in,out] units unit table indexed by slot
/// @param selected selection ids (Game.hot_units, Game.hot_unit_count of them)
/// @param local_player local player index (Game.local_player_index)
/// @quirk Ids are not bounds-checked; they must address units.
void mark_owned_selection(
    std::span<ListedUnit> units, std::span<const uint16_t> selected, uint8_t local_player
);

/// Clears bits 0x40 and 0x80 of every unit's flags.
///
/// Walks the unit table inclusively from Game.units through Game.units_last; the
/// other bits stay. Reads neither the selection list nor the player. Called before
/// mark_owned_selection, and when no unit is left with both bits clear.
///
/// @param[in,out] units the walked range; empty when the start is above the end,
///        in which case nothing is written
void clear_cycle_marks(std::span<ListedUnit> units);
} // namespace oa::sim::unit_activation
