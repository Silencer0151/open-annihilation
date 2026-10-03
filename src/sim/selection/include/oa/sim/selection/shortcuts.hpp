// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The selection shortcuts a profile adds (ui.selection-shortcuts), on top of
// the game's own: a double-click that selects the selected types on screen,
// Ctrl+S for the mobile combat units on screen, Ctrl+B and Ctrl+F cycling
// idle constructors and factories, and the W, B and Y keys that filter a
// drag box. They act on the local player's units, in this machine's view.
#pragma once

#include "oa/sim/selection.hpp"

#include "oa/core/world.h"
#include "oa/data/defs/categories.hpp"

#include <cstdint>

namespace oa::sim::selection {

/// Categories the shortcut sets start from.
inline constexpr const char* mobile_combat_category = "CTRL_W";
inline constexpr const char* constructor_category = "CTRL_B";
inline constexpr const char* factory_category = "CTRL_F";

/// Mission kinds (OrderOverlay.mission numbering) of a constructor's head
/// order that still count it as idle: Standby and VTOL_Standby.
inline constexpr int32_t idle_standby_mission = 41;
inline constexpr int32_t idle_air_standby_mission = 64;
/// Mission kind of a factory's head order that counts it as busy: BuildingBuild.
inline constexpr int32_t factory_busy_mission = 12;

/// Number of the drag-box filters W, B and Y pick between.
enum class DragFilter : uint8_t {
    none,          ///< no filter key held
    mobile_combat, ///< W: mobile combat units only
    constructors,  ///< B: constructors only
    factories,     ///< Y: factories only
};

/// The unit-type sets the shortcuts pick from.
///
/// Mobile combat units are the CTRL_W types that do not fly. Constructors
/// are the CTRL_B types or, when no type is in CTRL_B, the builders that
/// move and are no air base and no commander; factories likewise from CTRL_F
/// and the builders that do not move. A commander is a type that both shows
/// its player's name and hides its damage.
struct ShortcutSets {
    data::defs::CategoryMaskStorage mobile_combat_words{};
    data::defs::CategoryMaskStorage constructor_words{};
    data::defs::CategoryMaskStorage factory_words{};
    TypeMask mobile_combat{}; ///< over mobile_combat_words
    TypeMask constructors{};  ///< over constructor_words
    TypeMask factories{};     ///< over factory_words
};

/// Builds the shortcut sets from the world's unit types and the category registry.
///
/// @param world unit types (World.unit_defs)
/// @param categories category registry the CTRL_ categories are read from;
///     a category it lacks is added empty
/// @param type_bits type ids the sets hold
/// @param[out] sets the sets
void build_shortcut_sets(
    const World& world,
    data::defs::CategoryRegistry& categories,
    uint32_t type_bits,
    ShortcutSets& sets
) noexcept;

/// Services of the shortcuts beyond the selection's own Hooks.
struct ShortcutHooks {
    void* context{};
    /// Mission kind of a unit's head order (OrderOverlay.mission numbering),
    /// or -1 for a unit without orders; null reads every unit as without orders.
    int32_t (*head_mission)(void* context, const Unit& unit){};
};

/// The next unit each idle cycle starts after, an index into the local
/// player's unit range; kept by the caller from one key press to the next.
struct IdleCycle {
    int32_t constructor{}; ///< Ctrl+B's place
    int32_t factory{};     ///< Ctrl+F's place
};

/// Double-click on one of the local player's units: replaces the selection
/// with the local player's finished units on screen of the selected types.
///
/// The types of every selected unit of the local player's range are
/// gathered first. The finished selectable units of the range are then
/// deselected, and each unit of the on-screen list that is selectable,
/// finished, of a gathered type and owned (through its economy record) by
/// the local player is selected. The order panel is refreshed and the armed
/// command dropped.
///
/// @param[in,out] world units, players and game fields
/// @param lists the on-screen list
/// @param type_bits type ids the gathered set holds
/// @param hooks command and panel services
/// @return the units selected
uint32_t select_selected_types_on_screen(
    World& world, const VisibleLists& lists, uint32_t type_bits, const Hooks& hooks
);

/// Ctrl+S: replaces the selection with the local player's finished mobile
/// combat units on screen, as select_selected_types_on_screen picks them but
/// from `sets.mobile_combat`.
///
/// @param[in,out] world units, players and game fields
/// @param lists the on-screen list
/// @param sets the shortcut sets
/// @param hooks command and panel services
/// @return the units selected
uint32_t select_mobile_combat_on_screen(
    World& world, const VisibleLists& lists, const ShortcutSets& sets, const Hooks& hooks
);

/// Ctrl+B: selects the next idle constructor of the local player and centres on it.
///
/// The finished selectable units of the range are deselected. The range is
/// walked by index from the cycle's place up to Player.unit_count inclusive;
/// a unit counts when its health-percent byte (Unit.previous_health_percent)
/// is above 1, it has a movement object, its type is a constructor and its
/// head order is none, Standby or VTOL_Standby. The first such unit past the
/// place is selected and becomes the place. Past the end the place returns to 0
/// and the walk starts again once, so the range's first unit is never
/// picked after the walk wraps, nor while the place is 0.
///
/// @param[in,out] world units, players and game fields
/// @param sets the shortcut sets
/// @param[in,out] cycle the cycle's places
/// @param hooks camera, command and panel services
/// @param shortcuts order service
/// @return the unit selected, or null
Unit* cycle_idle_constructor(
    World& world,
    const ShortcutSets& sets,
    IdleCycle& cycle,
    const Hooks& hooks,
    const ShortcutHooks& shortcuts
);

/// Ctrl+F: selects the next idle factory of the local player and centres on it.
///
/// As cycle_idle_constructor, but a unit counts when it is selectable,
/// finished, has an economy record, its type is a factory and its head order
/// is not BuildingBuild.
///
/// @param[in,out] world units, players and game fields
/// @param sets the shortcut sets
/// @param[in,out] cycle the cycle's places
/// @param hooks camera, command and panel services
/// @param shortcuts order service
/// @return the unit selected, or null
Unit* cycle_idle_factory(
    World& world,
    const ShortcutSets& sets,
    IdleCycle& cycle,
    const Hooks& hooks,
    const ShortcutHooks& shortcuts
);

/// The drag-box filter of the keys held as a drag box closes: W before B before Y.
///
/// @param w_held W is held
/// @param b_held B is held
/// @param y_held Y is held
/// @return the filter
[[nodiscard]] constexpr DragFilter drag_filter(bool w_held, bool b_held, bool y_held) noexcept {
    if (w_held)
        return DragFilter::mobile_combat;
    if (b_held)
        return DragFilter::constructors;
    if (y_held)
        return DragFilter::factories;
    return DragFilter::none;
}

/// Filters the selection a drag box made: among the local player's
/// selected, selectable, finished units with an economy record, those whose
/// type is outside the filter's set are deselected.
///
/// @param[in,out] world units and players
/// @param sets the shortcut sets
/// @param filter the filter; DragFilter::none changes nothing
/// @return the units left selected that the filter looked at
uint32_t filter_box_selection(World& world, const ShortcutSets& sets, DragFilter filter) noexcept;

/// The queue step a shift-click on a build button takes, given whether
/// Ctrl is held: 100 with it under ui.selection-shortcuts, else the game's 5.
///
/// @param ctrl_held Ctrl is held
/// @return the step
[[nodiscard]] constexpr int32_t shift_queue_step(bool ctrl_held) noexcept {
    return ctrl_held ? 100 : 5;
}

} // namespace oa::sim::selection
