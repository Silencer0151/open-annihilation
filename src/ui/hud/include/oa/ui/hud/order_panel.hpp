// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unit order panel: build-menu paging, selection summary, order buttons.
#pragma once

#include "oa/ui/hud/boundary.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::data::defs {
struct DownloadMenuGroup;
} // namespace oa::data::defs

namespace oa::ui::hud {

// Unit.flags bits owned by the order panel.
inline constexpr uint32_t kUnitFlagPanelSelected = 0x00000010u; // member of the selection
inline constexpr unsigned kUnitMoveOrderShift = 18;             // 2-bit standing move order
inline constexpr unsigned kUnitFireOrderShift = 20;             // 2-bit standing fire order
inline constexpr uint32_t kUnitFlagBuildMenu = 0x00400000u;     // build pages shown
inline constexpr unsigned kUnitBuildPageShift = 23;
inline constexpr uint32_t kUnitBuildPageMask = 0x03800000u;

// Unit.state_flags bit.
inline constexpr uint8_t kUnitStateActive = 0x01u;

// UnitDef.abilities bits consulted by the order panel.
inline constexpr uint32_t kAbilityMoveOrders = 0x0001u;
inline constexpr uint32_t kAbilityFireOrders = 0x0002u;
inline constexpr uint32_t kAbilityOnOff = 0x0004u;
inline constexpr uint32_t kAbilityStop = 0x0008u;
inline constexpr uint32_t kAbilityAttack = 0x0010u;
inline constexpr uint32_t kAbilityGuard = 0x0020u;
inline constexpr uint32_t kAbilityPatrol = 0x0040u;
inline constexpr uint32_t kAbilityMove = 0x0080u;
inline constexpr uint32_t kAbilityTransport = 0x0100u;
inline constexpr uint32_t kAbilityRepair = 0x0200u;
inline constexpr uint32_t kAbilityReclaim = 0x0400u;
inline constexpr uint32_t kAbilityCapture = 0x1000u;
inline constexpr uint32_t kAbilityCloak = 0x2000u;
inline constexpr uint32_t kAbilityBlast = 0x4000u;
// UnitDef.flags bit: the order panel opens straight onto the build pages.
inline constexpr uint32_t kDefFlagBuildMenuDefault = 0x80000000u;

// OrderPanelState.frame_flags bits.
inline constexpr uint16_t kFrameReloadOrders = 0x0002u;
inline constexpr uint16_t kFrameRedrawBuildMenu = 0x0010u;
inline constexpr uint16_t kFrameSharePanelOpen = 0x0040u;
inline constexpr uint16_t kFramePageForward = 0x0080u;
inline constexpr uint16_t kFramePageBack = 0x0100u;
inline constexpr uint16_t kFrameBuildMenuOff = 0x0200u;
inline constexpr uint16_t kFrameBuildMenuOn = 0x0400u;
inline constexpr unsigned kFrameFireOrderShift = 12; // 3-bit selection summary

// OrderPanelState.order_flags fields (selection summaries and ability bits).
inline constexpr unsigned kOrderMoveShift = 0;  // 3 bits
inline constexpr unsigned kOrderCloakShift = 3; // 2 bits
inline constexpr unsigned kOrderOnOffShift = 5; // 2 bits
inline constexpr uint16_t kOrderCanMove = 0x0080u;
inline constexpr uint16_t kOrderCanStop = 0x0100u;
inline constexpr uint16_t kOrderCanAttack = 0x0200u;
inline constexpr uint16_t kOrderCanGuard = 0x0400u;
inline constexpr uint16_t kOrderCanPatrol = 0x0800u;
inline constexpr uint16_t kOrderCanTransport = 0x1000u;
inline constexpr uint16_t kOrderCanReclaim = 0x2000u;
inline constexpr uint16_t kOrderCanCapture = 0x4000u;
inline constexpr uint16_t kOrderCanRepair = 0x8000u;
// OrderPanelState.order_flags2 bit.
inline constexpr uint16_t kOrder2CanBlast = 0x0001u;

// Selection summary values: a 2- or 3-bit summary holds the shared value,
// kSummaryMixed when units disagree, and its "none" value when no selected
// unit has the ability.
inline constexpr uint32_t kStandingNone = 4;  // 3-bit move/fire summary
inline constexpr uint32_t kStandingMixed = 3; // 3-bit move/fire summary
inline constexpr uint32_t kToggleNone = 3;    // 2-bit cloak/on-off summary
inline constexpr uint32_t kToggleMixed = 2;   // 2-bit cloak/on-off summary

/// The order panel's words in the game state (Game.panel_unit_id,
/// panel_unit_type, frame_flags, order_summary, order_summary_ext).
struct OrderPanelState {
    uint16_t unit_id;      // unit whose build/order panel is loaded; 0 none
    uint16_t unit_type;    // that unit's type index
    uint16_t frame_flags;  // Game.frame_flags
    uint16_t order_flags;  // selection summaries and ability bits
    uint16_t order_flags2; // further ability bits
};

/// Reads the order panel's words from the game block.
///
/// @param game Game block holding panel_unit_id, panel_unit_type, frame_flags,
///             order_summary and order_summary_ext.
/// @return A copy of those words.
[[nodiscard]] OrderPanelState order_panel_load(const Game& game) noexcept;

/// Writes the order panel's words back to the game block.
///
/// @param[out] game Game block whose panel words are replaced.
/// @param state Words to store.
void order_panel_store(Game& game, const OrderPanelState& state) noexcept;

/// Extracts the build page field (3 bits) of a Unit.flags word.
[[nodiscard]] constexpr uint32_t build_page(uint32_t flags) noexcept {
    return (flags & kUnitBuildPageMask) >> kUnitBuildPageShift;
}

/// Extracts the standing move order (2 bits) of a Unit.flags word.
[[nodiscard]] constexpr uint32_t standing_move_order(uint32_t flags) noexcept {
    return (flags >> kUnitMoveOrderShift) & 3u;
}

/// Extracts the standing fire order (2 bits) of a Unit.flags word.
[[nodiscard]] constexpr uint32_t standing_fire_order(uint32_t flags) noexcept {
    return (flags >> kUnitFireOrderShift) & 3u;
}

/// Computes the Unit.flags word after a forward build-menu page step.
///
/// With `cycle` the step walks orders -> page 1 -> ... -> last page -> orders;
/// otherwise it wraps from the last page back to page 1 and always shows the
/// build menu. order_panel_page_forward applies it to the panel unit.
///
/// @param flags Unit.flags of the panel unit.
/// @param page_count UnitDef.gui_page_count (first missing page index).
/// @param cycle Whether the order page takes part in the cycle.
/// @return The new Unit.flags word.
[[nodiscard]] uint32_t build_menu_forward(uint32_t flags, uint8_t page_count, bool cycle) noexcept;

/// Computes the Unit.flags word after a backward build-menu page step.
///
/// With `cycle` the step walks orders -> last page -> ... -> page 1 -> orders;
/// otherwise it wraps from page 1 to the last page and always shows the build
/// menu. order_panel_page_back applies it to the panel unit.
///
/// @param flags Unit.flags of the panel unit.
/// @param page_count UnitDef.gui_page_count (first missing page index).
/// @param cycle Whether the order page takes part in the cycle.
/// @return The new Unit.flags word.
[[nodiscard]] uint32_t build_menu_back(uint32_t flags, uint8_t page_count, bool cycle) noexcept;

/// Computes the Unit.flags word after choosing a build page directly.
///
/// Page 0 hides the build menu and keeps the page field; any other page sets
/// the page field and shows the build menu. order_panel_select_page applies it.
///
/// @param flags Unit.flags of the panel unit.
/// @param page_count UnitDef.gui_page_count (first missing page index).
/// @param page Page to show; 0 selects the order page.
/// @return The new Unit.flags word, or `flags` unchanged when `page` is not
///         below `page_count`.
[[nodiscard]] uint32_t
build_menu_select(uint32_t flags, uint8_t page_count, uint32_t page) noexcept;

/// Resolves the unit whose build/order panel is loaded.
///
/// @param state Order panel words; unit_id names the panel unit.
/// @param table Unit pool and type table.
/// @return The panel unit, or nullptr when unit_id is 0, past the pool or an
///         empty slot.
[[nodiscard]] Unit* order_panel_unit(const OrderPanelState& state, const UnitTable& table) noexcept;

/// Pages the loaded unit's build menu forward and asks for a redraw.
///
/// Rewrites the panel unit's Unit.flags through build_menu_forward and sets
/// kFrameRedrawBuildMenu when a typed panel unit is loaded; plays
/// "nextbuildmenu" either way.
///
/// @param[in,out] state Order panel words; frame_flags gains the redraw request.
/// @param table Unit pool and type table; the panel unit's flags are written.
/// @param cycle Whether the order page takes part in the page cycle.
/// @param events Receives the page sound.
void order_panel_page_forward(
    OrderPanelState& state, const UnitTable& table, bool cycle, const HudEvents& events
);

/// Pages the loaded unit's build menu back and asks for a redraw.
///
/// Rewrites the panel unit's Unit.flags through build_menu_back and sets
/// kFrameRedrawBuildMenu when a typed panel unit is loaded; plays
/// "nextbuildmenu" either way.
///
/// @param[in,out] state Order panel words; frame_flags gains the redraw request.
/// @param table Unit pool and type table; the panel unit's flags are written.
/// @param cycle Whether the order page takes part in the page cycle.
/// @param events Receives the page sound.
void order_panel_page_back(
    OrderPanelState& state, const UnitTable& table, bool cycle, const HudEvents& events
);

/// Shows build page `page` of the loaded unit (0 = its order page).
///
/// Does nothing, and plays no sound, without a typed panel unit or when the
/// type has no such page; otherwise rewrites Unit.flags through
/// build_menu_select, asks for a redraw and plays "nextbuildmenu".
///
/// @param[in,out] state Order panel words; frame_flags gains the redraw request.
/// @param table Unit pool and type table; the panel unit's flags are written.
/// @param page Build page to show; 0 selects the order page.
/// @param events Receives the page sound.
void order_panel_select_page(
    OrderPanelState& state, const UnitTable& table, uint32_t page, const HudEvents& events
);

/// Consumes one pending page/menu request from frame_flags.
///
/// Requests are taken in the order page forward, page back, build menu on,
/// build menu off; the first one set is cleared and serviced. Paging never
/// cycles through the order page. Build menu on/off sets or clears
/// kUnitFlagBuildMenu on the panel unit and asks for a redraw.
///
/// @param[in,out] state Order panel words; the serviced request bit is cleared.
/// @param table Unit pool and type table; the panel unit's flags are written.
/// @param events Receives the page sound.
void order_panel_handle_requests(
    OrderPanelState& state, const UnitTable& table, const HudEvents& events
);

/// Aggregated state of the local player's selection.
struct SelectionSummary {
    uint16_t frame_flags;
    uint16_t order_flags;
    uint16_t order_flags2;
    int32_t count; // selected units
    Unit* first;   // first selected unit, or null
};

/// Summarises the selected units in units[first..last] into the panel words.
///
/// A unit counts when its slot is typed and kUnitFlagPanelSelected is set. The
/// fire summary goes into frame_flags bits 12-14 over the other bits of
/// `state.frame_flags`; the move, cloak and on/off summaries and the ability
/// bits make up order_flags; the blast bit goes into order_flags2 over the
/// other bits of `state.order_flags2`.
///
/// @param state Current panel words the summary is merged over.
/// @param table Unit pool and type table.
/// @param first First unit index to visit.
/// @param last Last unit index to visit (inclusive).
/// @return The merged panel words, the selected count and the first selected unit.
/// @quirk A second cloaking unit always makes the cloak summary mixed, even
///        when both units agree.
[[nodiscard]] SelectionSummary summarize_selection(
    const OrderPanelState& state, const UnitTable& table, uint16_t first, uint16_t last
) noexcept;

/// Build-page gadget index of button slot 0.
inline constexpr int32_t kFirstBuildButtonGadget = 4;

/// Panel loading done by the gadget engine.
struct PanelLoader {
    void* user;
    /// Pops panels until the root HUD is on top; false when that fails.
    bool (*close_to_root)(void* user);
    /// Whether the named panel is currently loaded.
    bool (*is_loaded)(void* user, const char* name);
    /// Loads the named panel for `unit` (null for none); `page` is the build
    /// page, or the panel flags for other panels. False when loading failed.
    bool (*load)(void* user, const char* name, const Unit* unit, int32_t page);
    /// Download menus linked into build pages, download_count of them.
    const data::defs::DownloadMenuGroup* downloads{};
    uint32_t download_count{};
    /// Turns build-page gadget `gadget` into a live button for `unit_name`.
    void (*link_button)(void* user, int32_t gadget, const char* unit_name){};
    /// Redraws the page after buttons were linked.
    void (*redraw)(void* user){};
    /// Whether guis\<name> has a GUI file; null counts every panel as present.
    bool (*exists)(void* user, const char* name){};
    /// Writes the queued counts into the loaded build page's captions
    /// (format_build_counts over the loaded page).
    void (*format_counts)(void* user, const Unit& builder){};
    /// Greys the loaded page's unit buttons whose types are gone
    /// (update_build_button_validity over the loaded page).
    void (*check_validity)(void* user){};
};

/// Links the download buttons of builder `def` whose MENU is `page` + 1 into the loaded build page.
///
/// Each matching entry of the loader's download menus turns gadget
/// BUTTON + kFirstBuildButtonGadget into a live button for its unit; the page
/// is redrawn once anything was linked.
///
/// @param def Type of the builder whose page is loaded (matched by type_id).
/// @param page Build page index the panel shows.
/// @param loader Download menus and the link/redraw callbacks.
/// @return How many buttons were linked; 0 without download menus or a
///         link_button callback.
int32_t link_download_buttons(const UnitDef& def, int32_t page, const PanelLoader& loader);

/// Game.gui_flags bits that hold the order panel closed.
inline constexpr uint8_t kGuiFlagsPanelBusy = 0xe0u;
/// frame_flags bits that hold the order panel closed.
inline constexpr uint16_t kFramePanelBusy = 0x0865u;

/// Closes open panels back to the root HUD unless a modal state holds them.
///
/// A hold (kFramePanelBusy in frame_flags or kGuiFlagsPanelBusy in
/// `gui_flags`) is remembered as a redraw request in frame_flags. Otherwise
/// the panel unit is forgotten and the loader pops to the root HUD.
///
/// @param[in,out] state Order panel words: unit_id is cleared, or frame_flags
///                      gains kFrameRedrawBuildMenu when held.
/// @param gui_flags Game.gui_flags.
/// @param loader Supplies close_to_root.
/// @return Whether the panels were closed back to the root HUD.
bool release_panels(OrderPanelState& state, uint8_t gui_flags, const PanelLoader& loader);

// Bits of a build page button's commonattribs (BuildPageRecord.common_attributes).
inline constexpr uint8_t kCommonUnitButton = 0x04;   // builds the unit it is named after
inline constexpr uint8_t kCommonWeaponButton = 0x08; // builds the weapon its unit stockpiles
inline constexpr uint8_t kGadgetTypeButton = 1;
inline constexpr std::size_t kBuildCaptionBytes = 0x20;

// One record of a loaded build page as update_build_button_validity and
// format_build_counts read it; record 0 is the page itself.
struct BuildPageRecord {
    const char* name{};                 // control name
    uint8_t gadget_type{};              // gadget type; kGadgetTypeButton for a button
    uint8_t common_attributes{};        // commonattribs; kCommonUnitButton, kCommonWeaponButton
    bool grayed{};                      // the button is greyed out
    char caption[kBuildCaptionBytes]{}; // the button's text
};

// Queued builds of `type` (0 for the stockpiled weapon) in the builder's
// order lists.
using QueuedBuilds = int32_t (*)(void* user, const Unit& builder, uint16_t type);

/// Writes each build button's queued count into its caption.
///
/// Visits the page's buttons, records 1..gadget_count. A unit button gets
/// "+N", or an empty caption when nothing is queued; a unit button whose name
/// does not resolve keeps its caption. A weapon button gets the first
/// weapon's stockpile followed by " +N" when builds are queued.
///
/// @param[in,out] records Page records; record 0 is the page itself.
/// @param gadget_count Number of button records after record 0.
/// @param builder Unit whose queue and stockpile are shown.
/// @param type_for_name Maps a unit name to its type index (0 unknown); may be null.
/// @param queued Counts the builder's queued builds of a type; may be null.
/// @param user Context passed to `type_for_name` and `queued`.
void format_build_counts(
    BuildPageRecord* records,
    int32_t gadget_count,
    const Unit& builder,
    uint16_t (*type_for_name)(void* user, const char* name),
    QueuedBuilds queued,
    void* user
);

/// Greys each unit button whose name no longer resolves to a unit type and ungreys the rest.
///
/// Any record with the unit-button attribute is checked, whatever its gadget type.
///
/// @param[in,out] records Page records; record 0 is the page itself.
/// @param gadget_count Number of button records after record 0.
/// @param type_for_name Maps a unit name to its type index (0 unknown); may be null.
/// @param user Context passed to `type_for_name`.
/// @quirk Visits records 0..gadget_count-1, so the page's last button record
///        is never checked.
void update_build_button_validity(
    BuildPageRecord* records,
    int32_t gadget_count,
    uint16_t (*type_for_name)(void* user, const char* name),
    void* user
);

/// Loads a finished unit's build page and refreshes its controls and counts.
///
/// Does nothing while the unit is unfinished. Loads `name`, or the side's
/// "<prefix>DL" download page when `name` has no GUI file, then links the
/// download buttons, greys PREV/NEXT on single-page types, refreshes the order
/// buttons, rewrites the queued counts, mirrors ONOFF for a building and, for
/// pages other than 0, runs the validity pass. The unit becomes the panel unit.
///
/// @param[in,out] state Order panel words; unit_id and unit_type are set on success.
/// @param unit Panel unit (must have build_remaining 0).
/// @param def The unit's type.
/// @param name Build page layout name ("<unit_name><page>.GUI").
/// @param page Build page index.
/// @param side_prefix Side prefix of the local player ("ARM", "COR"); may be null.
/// @param controls Named controls of the loaded panel.
/// @param loader Panel loading callbacks.
void load_build_page(
    OrderPanelState& state,
    Unit& unit,
    const UnitDef& def,
    const char* name,
    int32_t page,
    const char* side_prefix,
    const PanelControls& controls,
    const PanelLoader& loader
);

/// Loads the side's general order page and refreshes its order buttons.
///
/// @param[in,out] state Order panel words; unit_id and unit_type take `unit`
///                      (0 for a group) once the page loaded.
/// @param unit Single selected unit, or null for a group.
/// @param def Type of `unit`, or null.
/// @param side_prefix Side prefix of the local player; may be null.
/// @param controls Named controls of the loaded panel.
/// @param loader Panel loading callbacks.
void load_general_page(
    OrderPanelState& state,
    const Unit* unit,
    const UnitDef* def,
    const char* side_prefix,
    const PanelControls& controls,
    const PanelLoader& loader
);

/// Re-summarises the local player's selection and loads the build or general order page that fits it.
///
/// Without a panel unit the selection summary replaces the panel words and an
/// empty selection releases the panels. A single unit with build pages loads
/// its current page (or page 0 when its type opens on the build pages) unless
/// that page is already loaded for it; everything else loads the general page.
/// A panel unit that has gone clears unit_id and loads nothing.
///
/// @param[in,out] state Order panel words.
/// @param table Unit pool and type table.
/// @param first_unit First unit index of the local player.
/// @param last_unit Last unit index of the local player (inclusive).
/// @param gui_flags Game.gui_flags (modal holds).
/// @param side_prefix Side prefix of the local player; may be null.
/// @param controls Named controls of the loaded panel.
/// @param loader Panel loading callbacks.
void update_order_panel(
    OrderPanelState& state,
    const UnitTable& table,
    uint16_t first_unit,
    uint16_t last_unit,
    uint8_t gui_flags,
    const char* side_prefix,
    const PanelControls& controls,
    const PanelLoader& loader
);

/// Formats a type's build page layout name, "<unit_name><page>.GUI".
///
/// @param[out] out Destination buffer.
/// @param size Size of `out` in bytes.
/// @param def Type whose unit_name (first 31 characters) is used.
/// @param page Build page index.
void format_build_page_name(char* out, std::size_t size, const UnitDef& def, uint32_t page);

/// Formats the general order page layout name, "<side prefix>GEN.GUI".
///
/// @param[out] out Destination buffer.
/// @param size Size of `out` in bytes.
/// @param side_prefix Side prefix; null is taken as empty.
void format_general_page_name(char* out, std::size_t size, const char* side_prefix);

/// Greys the side's PREV and NEXT buttons (value 0) when a type has fewer than two build pages.
///
/// @param controls Named controls of the loaded panel.
/// @param def Type of the panel unit.
/// @param side_prefix Side prefix of the control names; null is taken as empty.
void disable_page_buttons(
    const PanelControls& controls, const UnitDef& def, const char* side_prefix
);

/// Mirrors the unit's on/off state (Unit.state_flags kUnitStateActive) onto the ONOFF control.
///
/// @param controls Named controls of the loaded panel.
/// @param unit Unit whose state is shown.
void sync_onoff_control(const PanelControls& controls, const Unit& unit);

/// Greys or sets every order button from the selection summary.
///
/// BUILD and ORDERS are greyed without a panel unit with build pages and
/// otherwise show whether the build menu is up. CLOAK, ONOFF, MOVEORD and
/// FIREORD are greyed when no selected unit has the ability and otherwise show
/// the summary. The command buttons are greyed when no selected unit can
/// carry them out; without transport ability LOAD is cleared, UNLOAD greyed
/// and BLAST greyed unless a unit can blast, while with it BLAST is cleared.
///
/// @param controls Named controls of the loaded panel.
/// @param state Order panel words holding the selection summary.
/// @param unit The unit whose panel is loaded, or null.
/// @param def Type of `unit`, or null.
void refresh_order_buttons(
    const PanelControls& controls,
    const OrderPanelState& state,
    const Unit* unit,
    const UnitDef* def
);

/// Handles a click on MOVEORD, FIREORD, ONOFF (or STATUS) or CLOAK.
///
/// MOVEORD and FIREORD step their summary through next_standing_order and
/// apply STANDING_MOVEORDER/STANDING_FIREORDER; ONOFF applies ACTIVATE from
/// off or mixed and DEACTIVATE from on; CLOAK applies CLOAK_ON from off and
/// CLOAK_OFF otherwise. A move, fire or on/off summary of none is left alone.
/// The order sound always plays, the clicked control shows the new summary
/// and every order button is refreshed.
///
/// @param[in,out] state Order panel words; the toggled summary is updated.
/// @param table Unit pool and type table.
/// @param controls Named controls of the loaded panel.
/// @param control Index of the clicked control.
/// @param name Name of the clicked control; matched by substring.
/// @param events Receives the standing order and the sound.
/// @return Whether the control was one of the toggles; false for a null name.
bool order_panel_toggle(
    OrderPanelState& state,
    const UnitTable& table,
    const PanelControls& controls,
    int32_t control,
    const char* name,
    const HudEvents& events
);

/// Steps a standing move/fire order summary after a click: 0 -> 1 -> 2 -> 0, mixed -> 0.
///
/// @param summary Current 3-bit summary (kStandingMixed, kStandingNone or a value).
/// @return The next value; kStandingNone and larger values are returned unchanged.
[[nodiscard]] uint32_t next_standing_order(uint32_t summary) noexcept;

/// Tells whether a group order a toggle applies reaches a selected unit of the given type.
///
/// The standing fire and move orders reach only the types that take them;
/// every other tag reaches each selected unit. Tags compare case-insensitively.
///
/// @param tag Standing order tag ("Standing_FireOrder", "CLOAK_ON", ...); null reaches nothing.
/// @param def Type of the selected unit.
/// @return Whether the order applies to that type.
[[nodiscard]] bool group_order_reaches(const char* tag, const UnitDef& def) noexcept;

/// Kind of queue marker a build-queue change raises.
enum class BuildQueueKind : uint8_t { none, weapon, mobile, building };

struct BuildQueueChange {
    BuildQueueKind kind;
    uint16_t type;   // unit type for mobile/building entries; 0 for a weapon
    const char* tag; // mission tag of the queued order
};

/// Classifies a build-queue click on control `name`.
///
/// MAKENUKE and MAKEANTI queue the stockpiled weapon ("BUILDWEAPON"); a unit
/// name queues a building ("BUILDINGBUILD") for a builder without movement and
/// a mobile unit ("MOBILEBUILD") otherwise. The caller changes the builder's
/// queued count of the tag's orders for `type` by `count`.
///
/// @param name Clicked control name; null classifies as none.
/// @param builder Unit whose queue changes.
/// @param viewpoint_player Player index the screen shows.
/// @param count Queue change; below 1 is a removal.
/// @param type_for_name Maps a unit name to its type index (0 unknown); may be null.
/// @param user Context passed to `type_for_name`.
/// @param events Receives "addbuild" or "subbuild" when the builder belongs to
///               the viewpoint player, whatever the name.
/// @return The queue marker kind, the unit type and the order tag.
[[nodiscard]] BuildQueueChange classify_build_queue_change(
    const char* name,
    const Unit& builder,
    uint8_t viewpoint_player,
    int32_t count,
    uint16_t (*type_for_name)(void* user, const char* name),
    void* user,
    const HudEvents& events
);

/// What a click on a build or order page button asks of the caller.
enum class BuildPanelClick : uint8_t {
    none,         // no effect, or an order button `order_click` took
    page_back,    // PREV: frame_flags kFramePageBack
    page_forward, // NEXT: kFramePageForward
    orders,       // ORDERS: kFrameBuildMenuOff
    build,        // BUILD: kFrameBuildMenuOn
    place,        // a building type: the placement cursor for it
    queued,       // the panel unit's build queue changed
};

struct BuildPanelClickResult {
    BuildPanelClick action;
    uint16_t type; // the building type to place
};

/// The rest of the game a build page click reaches.
struct BuildPanelHost {
    void* user;
    uint16_t (*type_for_name)(void* user, const char* name);
    /// Order toggles and order commands; true when the click was one of them.
    bool (*order_click)(void* user, const char* name);
    /// Whether shift is held (key state of control code 0xF9).
    bool (*shift_down)(void* user);
    /// Changes `builder`'s queue for button `name` by `count`.
    void (*change_queue)(void* user, const char* name, Unit& builder, int32_t count);
    /// Whether the unit's first weapon stockpiles (WeaponDef.flags, OA_WEAPON_FLAG_STOCKPILE).
    bool (*stockpiles)(void* user, const Unit& unit);
    /// Rewrites the loaded page's queued counts.
    void (*format_counts)(void* user, const Unit& builder);
};

/// Handles a click on a button of the loaded build or order page.
///
/// PREV, NEXT, ORDERS and BUILD raise their frame_flags requests (the last two
/// with a sound). A building type arms placement, even when the panel unit is
/// not selected. Any other button the order handlers do not take changes the
/// panel unit's queue by one (five with shift), down with the right button,
/// while that unit is selected; the counts are then rewritten for a building
/// or a stockpiling unit only.
///
/// @param[in,out] state Order panel words; page and menu requests are set here.
/// @param table Unit pool and type table.
/// @param name Clicked button name, matched by substring; null does nothing.
/// @param left_button true for the left button, false for the right.
/// @param host Type lookup, order handlers, shift state and queue callbacks.
/// @param events Receives the button sounds.
/// @return The action the caller completes and, for placement, the building type.
[[nodiscard]] BuildPanelClickResult on_build_panel_click(
    OrderPanelState& state,
    const UnitTable& table,
    const char* name,
    bool left_button,
    const BuildPanelHost& host,
    const HudEvents& events
);

/// Resets the transient radar/scroll block of the game state, keeping the scroll speed.
///
/// Zeroes the 0x5c bytes from Game.follow_unit through Game.camera_flags, the
/// last field before Game.pool_units_per_player, then restores
/// Game.scroll_speed.
///
/// @param[in,out] game Game block to reset.
void clear_scroll_state(Game& game) noexcept;

} // namespace oa::ui::hud
