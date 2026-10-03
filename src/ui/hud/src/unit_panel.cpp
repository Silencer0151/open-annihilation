// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/unit_panel.hpp"

#include "oa/ui/hud/chat_panel.hpp"
#include "oa/ui/hud/health_bar.hpp"

#include "oa/ui/console/unit_tags.hpp"

#include "oa/data/languages/unit_texts.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace oa::ui::hud {
namespace {

// The line an unseen unit shows, after its "R: " or "S: " prefix.
constexpr const char* kUnidentifiedObject = "Unidentified object";
constexpr const char* kRadarPrefix = "R: ";
constexpr const char* kSonarPrefix = "S: ";
// What UNITNAME2 shows over a stockpile build's bar.
constexpr const char* kStockpileLabel = "Weapon";

/// Returns a word in the player's language, or the word itself without a lookup.
const char* localized(Localize localize, void* context, const char* text) {
    const char* out = localize != nullptr ? localize(context, text) : nullptr;
    return out != nullptr ? out : text;
}

/// Compares two names, ignoring case.
bool same_name(const char* a, const char* b) noexcept {
    for (;; ++a, ++b) {
        const auto ca = std::tolower(static_cast<unsigned char>(*a));
        if (ca != std::tolower(static_cast<unsigned char>(*b)))
            return false;
        if (ca == 0)
            return true;
    }
}

/// Copies a fixed-size name field that need not end in a NUL.
template <size_t N>
void copy_field(char* out, size_t size, const char (&field)[N]) {
    std::snprintf(out, size, "%.*s", static_cast<int>(N), field);
}

/// Copies a text, cut to the room an output has.
///
/// @param[out] out the output
/// @param size bytes of `out`
/// @param text the text
void copy_text(char* out, size_t size, std::string_view text) {
    std::snprintf(out, size, "%.*s", static_cast<int>(text.size()), text.data());
}

/// Converts a cost to the whole number the panel prints: truncated toward
/// zero through 64 bits, the low 32 bits kept; a NaN or out-of-range cost is 0.
int32_t whole_cost(float value) noexcept {
    constexpr double limit = 9223372036854775808.0;
    const auto wide = static_cast<double>(value);
    if (!std::isfinite(wide) || wide >= limit || wide < -limit)
        return 0;
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<int64_t>(std::trunc(wide))));
}

/// Returns the mission tag table in name order, built once.
const console::MissionTagTable& mission_tags() {
    static const console::MissionTagTable table = [] {
        console::MissionTagTable built{};
        (void)console::mission_tags_register_static(&built);
        return built;
    }();
    return table;
}

/// Tells whether the viewer sees a unit, through UnitPanelHooks::can_see.
bool sees(const UnitPanelHooks& hooks, const Player* viewer, const Unit& unit) {
    return viewer != nullptr && hooks.can_see != nullptr &&
           hooks.can_see(hooks.context, *viewer, unit);
}

/// Tells whether the panel names a unit by its owner: a commander, or a type
/// that shows its player's name, in a multiplayer game.
bool shows_owner_name(const UnitDef& def, oa::data::campaign::SessionKind session_kind) {
    constexpr uint32_t owner_named =
        OA_UNIT_DEF_ABILITY_SHOW_PLAYER_NAME | OA_UNIT_DEF_ABILITY_COMMANDER;
    return session_kind == oa::data::campaign::SessionKind::multiplayer &&
           (def.abilities & owner_named) != 0;
}

/// Tells whether the panel shows a unit as the viewer's own: its owner is the
/// viewer or, with UnitPanelHooks::allied_units_shown, allies the viewer
/// (Player.alliance of the owner, which holds the owner itself, or
/// UnitPanelHooks::viewer_allies_every_player).
bool shown_as_own(const World& world, const Unit& unit, const UnitPanelHooks& hooks) noexcept {
    const auto viewer = world.game.viewpoint_player;
    if (!hooks.allied_units_shown)
        return unit.owner_index == viewer;
    const Player* owner = world_unit_owner(&world, &unit);
    if (hooks.viewer_allies_every_player)
        return owner != nullptr;
    return owner != nullptr && viewer < sizeof owner->alliance && owner->alliance[viewer] != 0;
}

} // namespace

const char* head_order_status_text(const OverlayContext& overlay, const Unit* unit) {
    const OrderOverlay* head = unit != nullptr && overlay.sink.orders != nullptr
                                   ? overlay.sink.orders(overlay.sink.user, *unit, false)
                                   : nullptr;
    const auto& table = mission_tags();
    const auto* record =
        console::mission_tags_entry(&table, head != nullptr ? head->mission : uint8_t{0});
    if (record == nullptr)
        record = console::mission_tags_entry(&table, 0);
    return record != nullptr ? record->status_text : "";
}

uint16_t head_order_target(const OverlayContext& overlay, const Unit& unit) {
    const OrderOverlay* head = overlay.sink.orders != nullptr
                                   ? overlay.sink.orders(overlay.sink.user, unit, false)
                                   : nullptr;
    return head != nullptr && head->target != nullptr ? head->target->id : 0;
}

UnitPanelSnapshot unit_panel_snapshot(
    const World& world,
    uint16_t cursor_unit,
    bool debug_keys,
    oa::data::campaign::SessionKind session_kind,
    const OverlayContext& overlay,
    const UnitPanelHooks& hooks
) {
    UnitPanelSnapshot panel{};
    const Unit* unit = cursor_unit != 0 ? world_unit_at(&world, cursor_unit) : nullptr;
    if (unit == nullptr || unit->type_index == 0)
        return panel;
    const UnitDef* def = world_unit_def_of(&world, unit);
    if (def == nullptr)
        return panel;
    panel.unit = cursor_unit;
    const Game& game = world.game;
    const Player* viewer = world_player(&world, game.viewpoint_player);
    if (!sees(hooks, viewer, *unit)) {
        panel.unidentified = true;
        std::snprintf(
            panel.name,
            sizeof panel.name,
            "%s%s",
            (unit->flags & OA_UNIT_FLAG_VIEWPOINT_OWNED) != 0 ? kSonarPrefix : kRadarPrefix,
            localized(hooks.localize, hooks.context, kUnidentifiedObject)
        );
        return panel;
    }
    const Player* owner = world_player(&world, unit->owner_index);
    if (owner != nullptr && shows_owner_name(*def, session_kind))
        copy_field(panel.name, sizeof panel.name, owner->name);
    else
        copy_text(panel.name, sizeof panel.name, oa::data::languages::unit_display_name(*def));
    const bool own = shown_as_own(world, *unit, hooks);
    panel.show_damage = (hooks.allied_units_shown && own) || panel_shows_damage(world, *unit, *def);
    panel.logo_player = unit->owner_index;
    if (!own && !debug_keys)
        return panel;
    panel.show_rates = true;
    if ((unit->flags & OA_UNIT_FLAG_HAS_WEAPONS) != 0 && unit->veteran_level != 0) {
        panel.show_kills = true;
        if (hooks.veterancy_level != nullptr)
            format_kill_count_at_level(
                panel.kills,
                sizeof panel.kills,
                unit->veteran_level,
                hooks.veterancy_level(hooks.context, *unit),
                hooks.localize,
                hooks.context
            );
        else
            format_kill_count(
                panel.kills, sizeof panel.kills, unit->veteran_level, hooks.localize, hooks.context
            );
    }
    std::snprintf(
        panel.mission_text,
        sizeof panel.mission_text,
        "%s",
        localized(hooks.localize, hooks.context, head_order_status_text(overlay, unit))
    );
    // The stockpile build and the head order's target are shown for the
    // viewer's own units only (and its allies' under allied_units_shown).
    if (!own)
        return panel;
    if (const int32_t percent = stockpile_percent(overlay, *unit); percent != 0) {
        panel.second = PanelSecondUnit::stockpile;
        panel.stockpile_percent = percent;
        return panel;
    }
    const uint16_t target_id = head_order_target(overlay, *unit);
    const Unit* target = target_id != 0 ? world_unit_at(&world, target_id) : nullptr;
    if (target == nullptr || target->type_index == 0 || !sees(hooks, viewer, *target))
        return panel;
    const UnitDef* target_def = world_unit_def_of(&world, target);
    if (target_def == nullptr)
        return panel;
    panel.second = PanelSecondUnit::target;
    panel.second_unit = target_id;
    panel.second_damage = panel_shows_damage(world, *target, *target_def);
    return panel;
}

const char* panel_stockpile_label(Localize localize, void* context) {
    return localized(localize, context, kStockpileLabel);
}

bool build_button_readout(
    const World& world,
    const char* button_name,
    char* name_line,
    size_t name_size,
    std::string_view* description
) {
    if (description != nullptr)
        *description = {};
    if (button_name == nullptr || name_line == nullptr || name_size == 0)
        return false;
    char name[kButtonUnitNameBytes + 1]{};
    std::snprintf(name, sizeof name, "%s", button_name);
    uint32_t type = 0;
    for (uint32_t index = 1; index < world.unit_def_count && type == 0; ++index) {
        char unit_name[sizeof world.unit_defs[index].unit_name + 1]{};
        copy_field(unit_name, sizeof unit_name, world.unit_defs[index].unit_name);
        if (unit_name[0] != '\0' && same_name(unit_name, name))
            type = index;
    }
    if (type == 0 || same_name(name, kBuildMenuButton))
        return false;
    const UnitDef& def = world.unit_defs[type];
    const std::string_view display = oa::data::languages::unit_display_name(def);
    std::snprintf(
        name_line,
        name_size,
        "%.*s  M:%d E:%d",
        static_cast<int>(display.size()),
        display.data(),
        static_cast<int>(whole_cost(def.build_cost_metal)),
        static_cast<int>(whole_cost(def.build_cost_energy))
    );
    if (description != nullptr)
        *description = oa::data::languages::unit_display_description(def);
    return true;
}

bool feature_readout(
    const World& world, bool debug_keys, Localize localize, void* context, char* out, size_t size
) {
    if (out == nullptr || size == 0)
        return false;
    out[0] = '\0';
    const uint16_t word = world.game.cursor_feature;
    if (world.feature_defs == nullptr || word >= world.feature_def_count ||
        static_cast<int32_t>(word) >= world.game.feature_def_count)
        return false;
    const FeatureDef& def = world.feature_defs[word];
    if ((def.flags & OA_FEATURE_FLAG_NO_DISPLAY_INFO) != 0 && !debug_keys)
        return false;
    char name[sizeof def.name + 1]{};
    if (debug_keys)
        copy_field(name, sizeof name, def.name);
    else
        copy_field(name, sizeof name, def.description);
    const char* shown = localized(localize, context, name);
    if ((def.flags & OA_FEATURE_FLAG_INDESTRUCTIBLE) != 0) {
        std::snprintf(out, size, "%s", shown);
        return true;
    }
    // A zero amount, or one that does not compare (a NaN), adds nothing.
    char metal[32]{};
    char energy[32]{};
    if (def.metal != 0.0F && !std::isnan(def.metal))
        std::snprintf(metal, sizeof metal, " M:%d", static_cast<int>(whole_cost(def.metal)));
    if (def.energy != 0.0F && !std::isnan(def.energy))
        std::snprintf(energy, sizeof energy, " E:%d", static_cast<int>(whole_cost(def.energy)));
    std::snprintf(out, size, "%s %s%s", shown, metal, energy);
    return true;
}

} // namespace oa::ui::hud
