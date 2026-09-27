// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// In-match pointer orders: the armed order command, the order cursor it shows
// over a unit or map position, and the unit eligibility tests behind it.
#pragma once

#include "oa/core/world.h"

#include <cstdint>

namespace oa::sim::gameplay_input {

// Game.pointer_command, set by the order buttons.
enum class OrderCommand : uint8_t {
    none = 0,
    default_order = 1, // no button armed: the pointer acts by context
    move = 2,
    attack = 3,
    blast = 4, // d-gun
    unload = 5,
    load = 6,
    guard = 7, // DEFEND
    repair = 8,
    patrol = 9,
    stop = 10,
    teleport = 11,
    reclaim = 12,
    capture = 13,
    build = 14,
};

// Index into the pointer animation table: the CURSORS.GAF sequence shown.
enum class OrderCursor : uint8_t {
    attack = 1,
    attack_dropped = 2, // armed with a dropped (bomb) weapon /* ? */
    attack_out_of_range = 3,
    capture = 4,
    guard = 5,
    repair = 6,
    patrol = 7,
    load_by_air = 8,
    teleport = 9, // an armed teleport command
    resurrect = 10,
    reclaim = 11,
    load = 12,
    unload = 13,
    move = 14,
    select = 15,
    build = 16,
    enemy = 17,
    friendly = 18,
    normal = 19,
};

// [Interface Type] values (Game.interface_type, LEFTCLICK stage): in the left-click
// interface the left button orders and the right one deselects; in the
// right-click interface the pointer only highlights select/enemy/friendly and
// orders come from the right button.
inline constexpr int32_t interface_left_click = 0;
inline constexpr int32_t interface_right_click = 1;

// Services owned by other systems. A null predicate answers "no".
struct OrderCursorHooks {
    void* context;
    // Line of sight of `player` to a map position.
    bool (*position_visible)(
        void* context, const World& world, const Player& player, const FixedVec3& position
    );
    // The feature standing on the map position, or null (feature_at_position
    // on a match's plots).
    const FeatureDef* (*feature_at)(void* context, const World& world, const FixedVec3& position);
    // First-weapon reach, to a unit or to a map position.
    bool (*unit_in_range)(void* context, const World& world, const Unit& actor, const Unit& target);
    bool (*position_in_range)(
        void* context, const World& world, const Unit& actor, const FixedVec3& position
    );
    // Whether the unit has a movement object (Unit.movement, made only for
    // bmcode 1 types). Null reads Unit.movement.
    bool (*movement_object)(void* context, const World& world, const Unit& unit){};
};

/// Returns the armed order command (Game.pointer_command).
///
/// @param game game record
/// @return the armed command
[[nodiscard]] OrderCommand pointer_command(const Game& game) noexcept;
/// Arms an order command (Game.pointer_command).
///
/// @param[in,out] game game record
/// @param command command to arm
void set_pointer_command(Game& game, OrderCommand command) noexcept;
/// Returns the pointer flags (Game.pointer_flags), the pointer_* bits.
///
/// @param game game record
/// @return the flag byte
[[nodiscard]] uint8_t pointer_flags(const Game& game) noexcept;
/// Stores the pointer flags (Game.pointer_flags).
///
/// @param[in,out] game game record
/// @param flags pointer_* bits
void set_pointer_flags(Game& game, uint8_t flags) noexcept;
/// Returns the map position under the pointer (Game.cursor_position).
///
/// @param game game record
/// @return 16.16 world coordinates
[[nodiscard]] FixedVec3 pointer_position(const Game& game) noexcept;
/// Stores the map position under the pointer (Game.cursor_position).
///
/// @param[in,out] game game record
/// @param position 16.16 world coordinates
void set_pointer_position(Game& game, const FixedVec3& position) noexcept;

// Bits of pointer_flags.
inline constexpr uint8_t pointer_over_radar = 0x01; /* ? */
inline constexpr uint8_t pointer_over_view = 0x02;
inline constexpr uint8_t pointer_over_map = 0x04; // over the radar or the view
inline constexpr uint8_t pointer_box_drag = 0x08;
inline constexpr uint8_t pointer_radar_scroll = 0x10;

// Bits of the pointer event's key word (Game.pointer_state[2]): the
// buttons and modifiers held with the event.
inline constexpr uint32_t pointer_key_left = 0x01;
inline constexpr uint32_t pointer_key_right = 0x02;
inline constexpr uint32_t pointer_key_shift = 0x04;
inline constexpr uint32_t pointer_key_control = 0x08;

/// Records which part of the screen the pointer is over.
///
/// The radar wins unless a drag box is open; otherwise the game view. Bit 0x04 marks
/// either. Other bits are kept.
///
/// @param[in,out] game pointer flags are updated
/// @param in_radar whether the pointer is over the radar
/// @param in_view whether the pointer is over the game view
void set_pointer_area(Game& game, bool in_radar, bool in_view) noexcept;

// What a right button press on the game screen does.
enum class RightPress : uint8_t {
    none,
    cancel_command,  // an armed command drops back to the default order
    mouse_look,      // control held over the view, left-click interface
    clear_selection, // over the view, left-click interface
    radar_scroll,    // over the radar, left-click interface: the camera follows it
    default_order,   // right-click interface: the default order at the pointer
};

/// Decides what a right button press on the game screen does.
///
/// Uses the armed command, the interface type, the pointer area and the event's key
/// word; a radar scroll's flag is set here.
///
/// @param[in,out] game pointer state; gains pointer_radar_scroll for a radar scroll
/// @return the action to take
[[nodiscard]] RightPress right_press(Game& game) noexcept;

/// Starts a radar scroll for a left press in the right-click interface.
///
/// Only with no command armed, no scroll, mouse look or drag box active, and the
/// pointer over the radar rather than the view.
///
/// @param[in,out] game pointer state; gains pointer_radar_scroll when it starts
/// @return true when the scroll started
bool start_left_radar_scroll(Game& game) noexcept;

/// Ends a radar scroll on the matching button release.
///
/// The right button ends it in the left-click interface, the left button in the
/// right-click interface.
///
/// @param[in,out] game pointer state; loses pointer_radar_scroll when it ends
/// @param right_button true for a right button release, false for the left
/// @return true when a scroll ended
bool end_radar_scroll(Game& game, bool right_button) noexcept;

/// Returns the feature on the map cell a position falls in.
///
/// The cell is x and z shifted down 20 bits, nothing off the map; the height plays no
/// part. Every cell of a footprint names its feature. Order resolution looks up the
/// feature under the pointer this way at the ground point under it (Game.cursor_position).
///
/// @param world map plots and features
/// @param position 16.16 world coordinates
/// @return the feature, or null on an empty or off-map cell
[[nodiscard]] const FeatureDef*
feature_at_position(const World& world, const FixedVec3& position) noexcept;

/// Tests whether a unit can currently take orders or be selected.
///
/// @param world world the unit lives in
/// @param unit unit to test
/// @return true with the spawn-complete flag, a finished build (an unordered NaN counts
///         as finished), no capture cooldown, and no carrier unless the carrier is an
///         air base
[[nodiscard]] bool unit_accepts_order(const World& world, const Unit& unit) noexcept;

/// Tests whether a reclaimer may take a unit.
///
/// @param world world both units live in
/// @param actor reclaiming unit
/// @param target unit to reclaim
/// @return true for any uncarried unit except capture-capable ones (commanders)
[[nodiscard]] bool
can_reclaim_unit(const World& world, const Unit& actor, const Unit& target) noexcept;

/// Tests whether a builder may repair or assist a unit.
///
/// @param world world both units live in
/// @param actor repairing unit
/// @param target unit to repair
/// @return true for a damaged, uncarried target within the builder's water depth
///         (aircraft: above the sea unless amphibious)
[[nodiscard]] bool
can_repair_unit(const World& world, const Unit& actor, const Unit& target) noexcept;

/// Tests whether a transport may pick up a unit.
///
/// @param world world both units live in
/// @param actor transport
/// @param target unit to load
/// @return true with room left, cargo small enough, finished, above the sea, and either
///         an air transport or a cargo that may enter water
[[nodiscard]] bool
can_load_unit(const World& world, const Unit& actor, const Unit& target) noexcept;

/// Returns the cursor one actor shows for a command over a target unit and map position.
///
/// The default order re-enters as ATTACK or RECLAIM by context, at most twice.
///
/// @param world world the units live in
/// @param command armed command
/// @param actor selected unit
/// @param target unit under the pointer, or null
/// @param position map position under the pointer, 16.16 world coordinates
/// @param hooks visibility, feature and range services
/// @return the cursor; lower values take precedence across a selection
[[nodiscard]] OrderCursor order_cursor(
    const World& world,
    OrderCommand command,
    const Unit& actor,
    const Unit* target,
    const FixedVec3& position,
    const OrderCursorHooks& hooks
) noexcept;

/// Writes the local player's selected units in slot order.
///
/// @param world world the units live in
/// @param[out] out receives up to `capacity` units
/// @param capacity room in `out`
/// @return the number written
uint32_t collect_selected_units(const World& world, const Unit** out, uint32_t capacity) noexcept;

/// Returns the cursor for the whole selection.
///
/// The lowest order_cursor over the selected units other than the unit under the
/// pointer. With nothing else selected, a default-order pointer over a local unit
/// that accepts orders shows the select cursor.
///
/// @param world world the units live in; Game.cursor_unit_id names the unit under the pointer
/// @param command armed command
/// @param hooks visibility, feature and range services
/// @return the cursor
[[nodiscard]] OrderCursor selection_order_cursor(
    const World& world, OrderCommand command, const OrderCursorHooks& hooks
) noexcept;

/// Chooses the pointer cursor for the current frame.
///
/// The caller has already stored the unit under the pointer in Game.cursor_unit_id.
///
/// @param world world, with the pointer state in its Game record
/// @param hooks visibility, feature and range services
/// @param[out] cursor receives the cursor: normal off the map, else the selection's
/// @return false when build placement owns the pointer, leaving `cursor` untouched
[[nodiscard]] bool
pointer_cursor(const World& world, const OrderCursorHooks& hooks, OrderCursor* cursor) noexcept;

// Order names of the order table, each with a VTOL form for aircraft.
enum class UnitOrder : uint8_t {
    none = 0,
    move_ground,
    vtol_move,
    qmove, // an immobile actor's move
    attack_chase,
    attack_nomove,
    attack_kamikaze,
    suppress, // ground attack on a position or an allied unit
    air_to_ground,
    air_to_ground_hover,
    air_to_air,
    air_strike,
    attack_special, // d-gun
    ground_unload,
    vtol_unload,
    vtol_landing,
    ground_pickup,
    vtol_pickup,
    follow_ground, // guard
    vtol_follow,
    repair_unit,
    vtol_repair_unit,
    help_build,
    vtol_help_build,
    patrol,
    vtol_patrol,
    repair_patrol,
    vtol_repair_patrol,
    qpatrol,
    stop,
    teleport,
    reclaim, // the feature at the map position
    vtol_reclaim,
    reclaim_unit,
    vtol_reclaim_unit,
    resurrect,
    capture,
    mobile_build,
    vtol_mobile_build,
};

/// Resolves the order one actor takes for a command over a target unit and map position.
///
/// The default order re-enters as ATTACK, RECLAIM or REPAIR by context.
///
/// @param world world the units live in
/// @param command armed command
/// @param actor selected unit
/// @param target unit under the pointer, or null; a target that is not live yields no order
/// @param position map position under the pointer, or null
/// @param hooks visibility, feature and range services
/// @return the order, or UnitOrder::none
[[nodiscard]] UnitOrder unit_order(
    const World& world,
    OrderCommand command,
    const Unit& actor,
    const Unit* target,
    const FixedVec3* position,
    const OrderCursorHooks& hooks
) noexcept;

/// Returns the order-table name the game looks an order up by.
///
/// The table is searched case-insensitively.
///
/// @param order order to name
/// @return the name, or an empty string for none
[[nodiscard]] const char* unit_order_name(UnitOrder order) noexcept;

// What a left click does for the cursor it shows.
enum class ClickAction : uint8_t {
    none,            // no effect; the command stays armed
    select_unit,     // the select cursor picks the unit under the pointer
    clear_selection, // a highlight cursor in the right-click interface
    issue_command,   // every order cursor issues the armed command
};

/// Decides what a left click does for the cursor it shows.
///
/// Build placement (command 14) is decided before this.
///
/// @param world world, for the interface type
/// @param command armed command
/// @param cursor cursor the pointer shows
/// @return the click's effect
[[nodiscard]] ClickAction
click_action(const World& world, OrderCommand command, OrderCursor cursor) noexcept;

struct SelectionOrder {
    const Unit* actor;
    UnitOrder order;
};

/// Resolves a click's order for each selected local unit.
///
/// Each is resolved against the unit under the pointer (for the commands that bind
/// it) and the pointer position; the pointer unit never acts on itself.
///
/// @param world world, with the pointer state in its Game record
/// @param command armed command
/// @param hooks visibility, feature and range services
/// @param[out] out receives the units that get an order, with the order
/// @param capacity room in `out`
/// @return the number written
/// @quirk The game also offsets positioned orders by each unit's displacement from the
///        selection's centre; that formation offset is left to the caller.
uint32_t selection_orders(
    const World& world,
    OrderCommand command,
    const OrderCursorHooks& hooks,
    SelectionOrder* out,
    uint32_t capacity
) noexcept;

} // namespace oa::sim::gameplay_input
