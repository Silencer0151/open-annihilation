// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The unit panel of the bottom bar: the unit under the cursor with its
// owner's logo, rates, kills, the status of its head order and the unit that
// order works on; the cost line of the build button under the pointer; the
// feature under the cursor.
#pragma once

#include "oa/ui/hud/order_overlays.hpp"
#include "oa/ui/hud/unit_info.hpp"
#include "oa/ui/hud/unit_labels.hpp"

#include "oa/core/world.h"
#include "oa/data/campaign/campaign_file.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace oa::ui::hud {

/// Room for one line of the unit panel, its terminator included.
inline constexpr size_t kPanelLineBytes = 0x100;
/// Percent a stockpile bar runs to.
inline constexpr int32_t kStockpileBarFull = 100;
/// Build-page button name that shows no cost line, even where a unit type bears the name.
inline constexpr const char* kBuildMenuButton = "CORBUILD";

/// Services of the unit panel beyond the canonical records.
struct UnitPanelHooks {
    void* context{};
    /// Whether a player sees a unit (its own always); null sees nothing.
    bool (*can_see)(void* context, const Player& viewer, const Unit& unit){};
    /// Language lookup of the panel's words, given `context`; null keeps them as they are.
    Localize localize{};
    /// Veterancy level a unit's kill line is labelled with (ui.veterancy-label,
    /// veterancy_label_level); null labels it "Veteran" from kVeteranKills on.
    uint32_t (*veterancy_level)(void* context, const Unit& unit){};
    /// Units whose owner allies the viewer show what the viewer's own units do
    /// (ui.allied-unit-display): rates, kills, mission, head-order target and
    /// stockpile, and the damage bar of a type that hides it. False: own units only.
    bool allied_units_shown{};
    /// The viewer is every player's ally: a replay viewer without a slot of
    /// its own under recorder.ten-player-replay allied-fake-player. With
    /// allied_units_shown every player's units then show what own units do.
    bool viewer_allies_every_player{};
};

/// What the unit panel shows at UNITNAME2 and DAMAGEBAR2.
enum class PanelSecondUnit : uint8_t {
    none,      ///< nothing
    target,    ///< the head order's target unit, its name and damage bar
    stockpile, ///< "Weapon" and the stockpile build's progress bar
};

/// The unit panel's content for the unit under the cursor.
struct UnitPanelSnapshot {
    uint16_t unit{};                      ///< unit shown; 0 when there is none
    bool unidentified{};                  ///< the viewer does not see it: only `name` is drawn
    char name[kPanelLineBytes]{};         ///< centred at UNITNAME: the type's or the owner's name
    bool show_damage{};                   ///< its damage bar at DAMAGEBAR
    uint8_t logo_player{};                ///< Player.index of the owner, whose logo is at LOGO2
    bool show_rates{};                    ///< its make and use rates
    bool show_kills{};                    ///< `kills` below DAMAGEBAR
    char kills[kPanelLineBytes]{};        ///< the kill count and the veteran label
    char mission_text[kPanelLineBytes]{}; ///< centred at MISSIONTEXT; empty for none
    PanelSecondUnit second{};             ///< what UNITNAME2 and DAMAGEBAR2 show
    uint16_t second_unit{};               ///< the target unit, for PanelSecondUnit::target
    bool second_damage{};                 ///< the target's damage bar at DAMAGEBAR2
    int32_t stockpile_percent{};          ///< the stockpile bar, of kStockpileBarFull
};

/// Returns the status text of a unit's head order: the mission's own ("Moving", "Nanolathing", ...).
///
/// @param overlay order-queue service (OverlaySink::orders)
/// @param unit unit whose primary queue is read; null for none
/// @return the status text of the head order's mission, or "Ready" for none
[[nodiscard]] const char* head_order_status_text(const OverlayContext& overlay, const Unit* unit);

/// Returns the unit a unit's head order works on.
///
/// @param overlay order-queue service (OverlaySink::orders)
/// @param unit unit whose primary queue is read
/// @return the target's id, or 0 when the queue is empty or its head has none
[[nodiscard]] uint16_t head_order_target(const OverlayContext& overlay, const Unit& unit);

/// Builds the unit panel for the unit under the cursor.
///
/// A unit the viewpoint player (Game.viewpoint_player) does not see shows
/// only "R: Unidentified object", "S: " instead of "R: " for a sonar contact
/// (OA_UNIT_FLAG_VIEWPOINT_OWNED). A seen unit shows its type's name, or in a
/// multiplayer game its owner's name when the type is a commander or shows
/// its player's name (OA_UNIT_DEF_ABILITY_COMMANDER,
/// OA_UNIT_DEF_ABILITY_SHOW_PLAYER_NAME), its damage bar unless it is an
/// enemy whose type hides damage, and its owner's logo. The viewer's own units, and every unit while the debug keys are on,
/// add the rates, the kill count (armed units with kills), the head order's
/// status text and then, for the viewer's own units only, either the
/// stockpile build's progress or the head order's target when the viewer
/// sees it.
///
/// @param world units, players and Game.viewpoint_player
/// @param cursor_unit unit under the cursor (Game.cursor_unit_id); 0 for none
/// @param debug_keys whether the debug keys are on
/// @param session_kind CampaignFile.kind of the game
/// @param overlay order-queue service (OverlaySink::orders) and the world
/// @param hooks sight and language services
/// @return the panel; unit 0 draws nothing
[[nodiscard]] UnitPanelSnapshot unit_panel_snapshot(
    const World& world,
    uint16_t cursor_unit,
    bool debug_keys,
    oa::data::campaign::SessionKind session_kind,
    const OverlayContext& overlay,
    const UnitPanelHooks& hooks
);

/// Returns the word UNITNAME2 shows over a stockpile build's bar.
///
/// @param localize language lookup; may be null
/// @param context context passed to `localize`
/// @return "Weapon", localised
[[nodiscard]] const char* panel_stockpile_label(Localize localize, void* context);

/// Formats the cost line and finds the description a build button shows at NAME and DESCRIPTION.
///
/// The button names a unit type by its unit name, of which the first
/// kButtonUnitNameBytes characters count; kBuildMenuButton shows nothing.
/// The line is the type's name in the player's language
/// (oa::data::languages::unit_display_name), then "  M:" and "E:" with its
/// whole metal and energy costs.
///
/// @param world unit types
/// @param button_name the hovered button's name
/// @param[out] name_line receives the cost line
/// @param name_size bytes of `name_line`
/// @param[out] description receives the type's description in the player's
///     language; empty when nothing is shown
/// @return false when the button names no unit type or is kBuildMenuButton
bool build_button_readout(
    const World& world,
    const char* button_name,
    char* name_line,
    size_t name_size,
    std::string_view* description
);

/// Formats the line the feature under the cursor (Game.cursor_feature) shows at NAME.
///
/// The feature's description (its name while the debug keys are on), then
/// " M:" and " E:" with its whole metal and energy when they are not 0; an
/// indestructible feature shows its name alone; a feature marked to show no
/// information shows nothing unless the debug keys are on.
///
/// @param world feature types and the cursor feature
/// @param debug_keys whether the debug keys are on
/// @param localize language lookup; may be null
/// @param context context passed to `localize`
/// @param[out] out receives the line
/// @param size bytes of `out`
/// @return false when there is no feature under the cursor or it shows nothing
bool feature_readout(
    const World& world, bool debug_keys, Localize localize, void* context, char* out, size_t size
);

} // namespace oa::ui::hud
