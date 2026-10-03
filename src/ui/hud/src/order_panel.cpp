// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/order_panel.hpp"

#include "oa/data/defs/unit_catalog.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace oa::ui::hud {
namespace {

// Replaces the 3-bit page field with the page field of `source`.
constexpr uint32_t with_page_of(uint32_t flags, uint32_t source) noexcept {
    return ((source ^ flags) & kUnitBuildPageMask) ^ flags;
}

// The flag word with its page field set to the first page.
constexpr uint32_t on_first_page(uint32_t flags) noexcept {
    return (flags & ~kUnitBuildPageMask) | (1u << kUnitBuildPageShift);
}

// The 16-bit flag word with the bits of `field` cleared.
constexpr uint32_t without(uint32_t flags, uint16_t field) noexcept {
    return flags & ~static_cast<uint32_t>(field);
}

void set_group_value(const PanelControls& controls, int32_t index, int32_t value) {
    if (index != -1 && controls.set_group_value != nullptr)
        controls.set_group_value(controls.user, index, value);
}

void set_value(const PanelControls& controls, int32_t index, int32_t value) {
    if (index != -1 && controls.set_value != nullptr)
        controls.set_value(controls.user, index, value);
}

void disable(const PanelControls& controls, int32_t index) {
    if (index != -1 && controls.disable != nullptr)
        controls.disable(controls.user, index);
}

void apply_order(const HudEvents& events, const char* tag, int32_t value) {
    if (events.apply_standing_order != nullptr)
        events.apply_standing_order(events.user, tag, value);
}

// Folds one unit's value into a selection summary: `none` until the first
// contributor, then the shared value or `mixed`.
constexpr uint32_t fold(uint32_t summary, uint32_t value, uint32_t none, uint32_t mixed) noexcept {
    if (summary == none)
        return value;
    return summary == value ? summary : mixed;
}

} // namespace

OrderPanelState order_panel_load(const Game& game) noexcept {
    return {
        game.panel_unit_id,
        game.panel_unit_type,
        game.frame_flags,
        game.order_summary,
        game.order_summary_ext,
    };
}

void order_panel_store(Game& game, const OrderPanelState& state) noexcept {
    game.panel_unit_id = state.unit_id;
    game.panel_unit_type = state.unit_type;
    game.frame_flags = state.frame_flags;
    game.order_summary = state.order_flags;
    game.order_summary_ext = state.order_flags2;
}

uint32_t build_menu_forward(uint32_t flags, uint8_t page_count, bool cycle) noexcept {
    const bool last =
        static_cast<int32_t>(build_page(flags)) == static_cast<int32_t>(page_count) - 1;
    const auto next =
        with_page_of(flags, (flags & kUnitBuildPageMask) + (1u << kUnitBuildPageShift));
    if (!cycle) {
        const auto paged = last ? on_first_page(flags) : next;
        return paged | kUnitFlagBuildMenu;
    }
    if ((flags & kUnitFlagBuildMenu) == 0)
        return on_first_page(flags) | kUnitFlagBuildMenu;
    return last ? flags & ~kUnitFlagBuildMenu : next;
}

uint32_t build_menu_back(uint32_t flags, uint8_t page_count, bool cycle) noexcept {
    const auto last_page = static_cast<uint32_t>(page_count) << kUnitBuildPageShift;
    if (!cycle) {
        const auto base = (flags & kUnitBuildPageMask) < (2u << kUnitBuildPageShift)
                              ? last_page
                              : flags & kUnitBuildPageMask;
        return with_page_of(flags, base - 1u) | kUnitFlagBuildMenu;
    }
    if ((flags & kUnitFlagBuildMenu) == 0) {
        flags |= kUnitFlagBuildMenu;
        return with_page_of(flags, last_page - 1u);
    }
    if ((flags & kUnitBuildPageMask) == (1u << kUnitBuildPageShift))
        return flags & ~kUnitFlagBuildMenu;
    return with_page_of(flags, (flags & kUnitBuildPageMask) - 1u);
}

uint32_t build_menu_select(uint32_t flags, uint8_t page_count, uint32_t page) noexcept {
    if (static_cast<int32_t>(page) >= static_cast<int32_t>(page_count))
        return flags;
    const auto shown = static_cast<int32_t>(page) > 0 ? kUnitFlagBuildMenu : 0u;
    if (page == 0)
        return shown | (flags & ~kUnitFlagBuildMenu);
    return ((page & 7u) << kUnitBuildPageShift) | shown |
           (flags & ~(kUnitBuildPageMask | kUnitFlagBuildMenu));
}

Unit* order_panel_unit(const OrderPanelState& state, const UnitTable& table) noexcept {
    if (state.unit_id == 0 || table.units == nullptr || state.unit_id >= table.unit_count)
        return nullptr;
    auto& unit = table.units[state.unit_id];
    return unit.type_index == 0 ? nullptr : &unit;
}

void order_panel_page_forward(
    OrderPanelState& state, const UnitTable& table, bool cycle, const HudEvents& events
) {
    if (auto* unit = order_panel_unit(state, table)) {
        if (const auto* def = unit_def(table, *unit)) {
            unit->flags = build_menu_forward(unit->flags, def->gui_page_count, cycle);
            state.frame_flags |= kFrameRedrawBuildMenu;
        }
    }
    play_sound(events, "nextbuildmenu");
}

void order_panel_page_back(
    OrderPanelState& state, const UnitTable& table, bool cycle, const HudEvents& events
) {
    if (auto* unit = order_panel_unit(state, table)) {
        if (const auto* def = unit_def(table, *unit)) {
            unit->flags = build_menu_back(unit->flags, def->gui_page_count, cycle);
            state.frame_flags |= kFrameRedrawBuildMenu;
        }
    }
    play_sound(events, "nextbuildmenu");
}

void order_panel_select_page(
    OrderPanelState& state, const UnitTable& table, uint32_t page, const HudEvents& events
) {
    auto* unit = order_panel_unit(state, table);
    const auto* def = unit != nullptr ? unit_def(table, *unit) : nullptr;
    if (def == nullptr || static_cast<int32_t>(page) >= static_cast<int32_t>(def->gui_page_count))
        return;
    unit->flags = build_menu_select(unit->flags, def->gui_page_count, page);
    state.frame_flags |= kFrameRedrawBuildMenu;
    play_sound(events, "nextbuildmenu");
}

void order_panel_handle_requests(
    OrderPanelState& state, const UnitTable& table, const HudEvents& events
) {
    const auto flags = state.frame_flags;
    if ((flags & kFramePageForward) != 0) {
        state.frame_flags = static_cast<uint16_t>(flags & ~kFramePageForward);
        order_panel_page_forward(state, table, false, events);
        return;
    }
    if ((flags & kFramePageBack) != 0) {
        state.frame_flags = static_cast<uint16_t>(flags & ~kFramePageBack);
        order_panel_page_back(state, table, false, events);
        return;
    }
    bool show = false;
    if ((flags & kFrameBuildMenuOn) != 0) {
        state.frame_flags = static_cast<uint16_t>(flags & ~kFrameBuildMenuOn);
        show = true;
    } else if ((flags & kFrameBuildMenuOff) != 0) {
        state.frame_flags = static_cast<uint16_t>(flags & ~kFrameBuildMenuOff);
    } else {
        return;
    }
    auto* unit = order_panel_unit(state, table);
    if (unit == nullptr)
        return;
    unit->flags = show ? unit->flags | kUnitFlagBuildMenu : unit->flags & ~kUnitFlagBuildMenu;
    state.frame_flags |= kFrameRedrawBuildMenu;
}

SelectionSummary summarize_selection(
    const OrderPanelState& state, const UnitTable& table, uint16_t first, uint16_t last
) noexcept {
    uint32_t fire = kStandingNone;
    uint32_t move = kStandingNone;
    uint32_t onoff = kToggleNone;
    uint32_t cloak = kToggleNone;
    uint16_t can = 0;
    uint16_t can2 = 0;
    SelectionSummary summary{};
    for (uint32_t id = first; table.units != nullptr && id <= last && id < table.unit_count; ++id) {
        auto& unit = table.units[id];
        if (unit.type_index == 0 || (unit.flags & kUnitFlagPanelSelected) == 0)
            continue;
        if (summary.first == nullptr)
            summary.first = &unit;
        const auto* def = unit_def(table, unit);
        const auto abilities = def != nullptr ? def->abilities : 0u;
        if ((abilities & kAbilityFireOrders) != 0)
            fire = fold(fire, standing_fire_order(unit.flags), kStandingNone, kStandingMixed);
        if ((abilities & kAbilityMoveOrders) != 0)
            move = fold(move, standing_move_order(unit.flags), kStandingNone, kStandingMixed);
        if ((abilities & kAbilityOnOff) != 0)
            onoff = fold(onoff, unit.state_flags & kUnitStateActive, kToggleNone, kToggleMixed);
        // A second cloaker always reports mixed, even when both agree.
        if ((abilities & kAbilityCloak) != 0)
            cloak = cloak == kToggleNone ? (unit.flags >> 11) & 1u : kToggleMixed;

        const struct {
            uint32_t ability;
            uint16_t bit;
        } gates[] = {
            {kAbilityMove, kOrderCanMove},
            {kAbilityStop, kOrderCanStop},
            {kAbilityAttack, kOrderCanAttack},
            {kAbilityGuard, kOrderCanGuard},
            {kAbilityPatrol, kOrderCanPatrol},
            {kAbilityTransport, kOrderCanTransport},
            {kAbilityRepair, kOrderCanRepair},
            {kAbilityCapture, kOrderCanCapture},
            {kAbilityReclaim, kOrderCanReclaim},
        };

        for (const auto& gate : gates)
            if ((abilities & gate.ability) != 0)
                can |= gate.bit;
        if ((abilities & kAbilityBlast) != 0)
            can2 |= kOrder2CanBlast;
        ++summary.count;
    }
    summary.frame_flags = static_cast<uint16_t>(
        (fire << kFrameFireOrderShift) | without(state.frame_flags, kFrameFireOrderMask)
    );
    summary.order_flags = static_cast<uint16_t>(
        (move & 7u) | (cloak << kOrderCloakShift) | (onoff << kOrderOnOffShift) | can
    );
    summary.order_flags2 = static_cast<uint16_t>((state.order_flags2 & ~1u) | can2);
    return summary;
}

bool release_panels(OrderPanelState& state, uint8_t gui_flags, const PanelLoader& loader) {
    if ((state.frame_flags & kFramePanelBusy) != 0 || (gui_flags & kGuiFlagsPanelBusy) != 0) {
        state.frame_flags |= kFrameRedrawBuildMenu;
        return false;
    }
    state.unit_id = 0;
    return loader.close_to_root != nullptr && loader.close_to_root(loader.user);
}

void format_build_page_name(char* out, std::size_t size, const UnitDef& def, uint32_t page) {
    char name[32];
    std::memcpy(name, def.unit_name, 31);
    name[31] = '\0';
    std::snprintf(out, size, "%s%u.GUI", name, page);
}

void format_general_page_name(char* out, std::size_t size, const char* side_prefix) {
    std::snprintf(out, size, "%sGEN.GUI", side_prefix != nullptr ? side_prefix : "");
}

void disable_page_buttons(
    const PanelControls& controls, const UnitDef& def, const char* side_prefix
) {
    if (def.gui_page_count >= 2)
        return;
    char name[256];
    const char* prefix = side_prefix != nullptr ? side_prefix : "";
    std::snprintf(name, sizeof name, "%sPREV", prefix);
    set_value(controls, find_control(controls, name), 0);
    std::snprintf(name, sizeof name, "%sNEXT", prefix);
    set_value(controls, find_control(controls, name), 0);
}

void sync_onoff_control(const PanelControls& controls, const Unit& unit) {
    set_group_value(controls, find_control(controls, "ONOFF"), unit.state_flags & kUnitStateActive);
}

void refresh_order_buttons(
    const PanelControls& controls,
    const OrderPanelState& state,
    const Unit* unit,
    const UnitDef* def
) {
    const bool no_pages = unit == nullptr || def == nullptr || def->gui_page_count == 0;
    const bool build_shown = unit != nullptr && (unit->flags & kUnitFlagBuildMenu) != 0;
    auto index = find_control(controls, "BUILD");
    if (no_pages)
        disable(controls, index);
    else
        set_group_value(controls, index, build_shown ? 1 : 0);
    index = find_control(controls, "ORDERS");
    if (no_pages)
        disable(controls, index);
    else
        set_group_value(controls, index, build_shown ? 0 : 1);

    const auto orders = state.order_flags;
    const auto cloak = (orders >> kOrderCloakShift) & 3u;
    index = find_control(controls, "CLOAK");
    if (cloak == kToggleNone)
        disable(controls, index);
    else
        set_group_value(controls, index, static_cast<int32_t>(cloak));
    const auto onoff = (orders >> kOrderOnOffShift) & 3u;
    index = find_control(controls, "ONOFF");
    if (onoff == kToggleNone)
        disable(controls, index);
    else
        set_group_value(controls, index, static_cast<int32_t>(onoff));
    const auto move = orders & 7u;
    index = find_control(controls, "MOVEORD");
    if (move == kStandingNone)
        disable(controls, index);
    else
        set_group_value(controls, index, static_cast<int32_t>(move));
    const auto fire = (state.frame_flags >> kFrameFireOrderShift) & 7u;
    index = find_control(controls, "FIREORD");
    if (fire == kStandingNone)
        disable(controls, index);
    else
        set_group_value(controls, index, static_cast<int32_t>(fire));

    const struct {
        uint16_t bit;
        const char* name;
    } gated[] = {
        {kOrderCanMove, "MOVE"},
        {kOrderCanStop, "STOP"},
        {kOrderCanAttack, "ATTACK"},
        {kOrderCanGuard, "DEFEND"},
        {kOrderCanPatrol, "PATROL"},
        {kOrderCanReclaim, "RECLAIM"},
        {kOrderCanRepair, "REPAIR"},
        {kOrderCanCapture, "CAPTURE"},
    };

    for (const auto& gate : gated)
        if ((orders & gate.bit) == 0)
            disable(controls, find_control(controls, gate.name));
    if ((orders & kOrderCanTransport) == 0) {
        set_value(controls, find_control(controls, "LOAD"), 0);
        disable(controls, find_control(controls, "UNLOAD"));
        if ((state.order_flags2 & kOrder2CanBlast) == 0)
            disable(controls, find_control(controls, "BLAST"));
    } else {
        set_value(controls, find_control(controls, "BLAST"), 0);
    }
}

uint32_t next_standing_order(uint32_t summary) noexcept {
    switch (summary) {
    case 0:
        return 1;
    case 1:
        return 2;
    case 2:
    case 3:
        return 0;
    default:
        return summary;
    }
}

// The group order issuer (sim::gameplay_input::selection_orders) resolves both
// tags through the case-insensitive order table and compares the entries.
bool group_order_reaches(const char* tag, const UnitDef& def) noexcept {
    if (tag == nullptr)
        return false;
    const auto same = [tag](const char* name) {
        std::size_t at = 0;
        for (; tag[at] != '\0' && name[at] != '\0'; ++at)
            if (std::tolower(static_cast<unsigned char>(tag[at])) !=
                std::tolower(static_cast<unsigned char>(name[at])))
                return false;
        return tag[at] == name[at];
    };
    if (same("Standing_FireOrder"))
        return (def.abilities & kAbilityFireOrders) != 0;
    if (same("Standing_MoveOrder"))
        return (def.abilities & kAbilityMoveOrders) != 0;
    return true;
}

bool order_panel_toggle(
    OrderPanelState& state,
    const UnitTable& table,
    const PanelControls& controls,
    int32_t control,
    const char* name,
    const HudEvents& events
) {
    if (name == nullptr)
        return false;
    uint32_t shown = 0;
    if (std::strstr(name, "MOVEORD") != nullptr) {
        const auto move = state.order_flags & 7u;
        if (move <= 3) {
            const auto next = next_standing_order(move);
            apply_order(events, "STANDING_MOVEORDER", static_cast<int32_t>(next));
            state.order_flags =
                static_cast<uint16_t>(without(state.order_flags, kOrderMoveMask) | next);
        }
        play_sound(events, "setmoveorders");
        shown = state.order_flags & 7u;
    } else if (std::strstr(name, "FIREORD") != nullptr) {
        const auto fire = (state.frame_flags & kFrameFireOrderMask) >> kFrameFireOrderShift;
        if (fire <= 3) {
            const auto next = next_standing_order(fire);
            apply_order(events, "STANDING_FIREORDER", static_cast<int32_t>(next));
            state.frame_flags = static_cast<uint16_t>(
                without(state.frame_flags, kFrameFireOrderMask) | (next << kFrameFireOrderShift)
            );
        }
        play_sound(events, "setfireorders");
        shown = (state.frame_flags >> kFrameFireOrderShift) & 7u;
    } else if (std::strstr(name, "STATUS") != nullptr || std::strstr(name, "ONOFF") != nullptr) {
        const auto onoff = (state.order_flags & kOrderOnOffMask) >> kOrderOnOffShift;
        if (onoff == 0 || onoff == 2) {
            apply_order(events, "ACTIVATE", 0);
            state.order_flags = static_cast<uint16_t>(
                without(state.order_flags, kOrderOnOffMask) | kOrderOnOffActive
            );
        } else if (onoff == 1) {
            apply_order(events, "DEACTIVATE", 0);
            state.order_flags = static_cast<uint16_t>(without(state.order_flags, kOrderOnOffMask));
        }
        play_sound(events, "specialorders");
        shown = (state.order_flags >> kOrderOnOffShift) & 3u;
    } else if (std::strstr(name, "CLOAK") != nullptr) {
        if ((state.order_flags & kOrderCloakMask) == 0) {
            apply_order(events, "CLOAK_ON", 0);
            state.order_flags =
                static_cast<uint16_t>(without(state.order_flags, kOrderCloakMask) | kOrderCloakOn);
        } else {
            apply_order(events, "CLOAK_OFF", 0);
            state.order_flags = static_cast<uint16_t>(without(state.order_flags, kOrderCloakMask));
        }
        play_sound(events, "specialorders");
        shown = (state.order_flags >> kOrderCloakShift) & 3u;
    } else {
        return false;
    }
    set_group_value(controls, control, static_cast<int32_t>(shown));
    const Unit* unit =
        state.unit_id != 0 && table.units != nullptr && state.unit_id < table.unit_count
            ? &table.units[state.unit_id]
            : nullptr;
    refresh_order_buttons(
        controls, state, unit, unit != nullptr ? unit_def(table, *unit) : nullptr
    );
    return true;
}

int32_t link_download_buttons(const UnitDef& def, int32_t page, const PanelLoader& loader) {
    if (loader.downloads == nullptr || loader.link_button == nullptr)
        return 0;
    int32_t linked = 0;
    for (uint32_t menu = 0; menu < loader.download_count; ++menu) {
        const data::defs::DownloadMenuGroup& group = loader.downloads[menu];
        for (int32_t index = 0; index < group.count; ++index) {
            const data::defs::DownloadMenuEntry& entry = group.entries[index];
            if (entry.builder_index != def.type_id || entry.menu - 1 != page)
                continue;
            char name[sizeof entry.unit_name + 1]{};
            std::memcpy(name, entry.unit_name, sizeof entry.unit_name);
            loader.link_button(loader.user, entry.button + kFirstBuildButtonGadget, name);
            ++linked;
        }
    }
    if (linked != 0 && loader.redraw != nullptr)
        loader.redraw(loader.user);
    return linked;
}

void format_build_counts(
    BuildPageRecord* records,
    int32_t gadget_count,
    const Unit& builder,
    uint16_t (*type_for_name)(void* user, const char* name),
    QueuedBuilds queued,
    void* user
) {
    for (int32_t index = 1; index <= gadget_count; ++index) {
        BuildPageRecord& record = records[index];
        if (record.gadget_type != kGadgetTypeButton)
            continue;
        char* caption = record.caption;
        if ((record.common_attributes & kCommonUnitButton) != 0) {
            const auto type =
                type_for_name != nullptr ? type_for_name(user, record.name) : uint16_t{0};
            if (type == 0)
                continue;
            const int32_t count = queued != nullptr ? queued(user, builder, type) : 0;
            if (count == 0)
                caption[0] = '\0';
            else
                std::snprintf(caption, kBuildCaptionBytes, "+%d", count);
        } else if ((record.common_attributes & kCommonWeaponButton) != 0) {
            const uint8_t stockpile = builder.weapons[0].stockpile;
            const int32_t count = queued != nullptr ? queued(user, builder, 0) : 0;
            caption[0] = '\0';
            if (stockpile != 0)
                std::snprintf(caption, kBuildCaptionBytes, "%d", stockpile);
            if (count != 0) {
                const std::size_t used = std::strlen(caption);
                std::snprintf(caption + used, kBuildCaptionBytes - used, " +%d", count);
            }
        }
    }
}

void update_build_button_validity(
    BuildPageRecord* records,
    int32_t gadget_count,
    uint16_t (*type_for_name)(void* user, const char* name),
    void* user
) {
    for (int32_t index = 0; index < gadget_count; ++index) {
        BuildPageRecord& record = records[index];
        if ((record.common_attributes & kCommonUnitButton) == 0)
            continue;
        const auto type = type_for_name != nullptr ? type_for_name(user, record.name) : uint16_t{0};
        record.grayed = type == 0;
    }
}

void load_build_page(
    OrderPanelState& state,
    Unit& unit,
    const UnitDef& def,
    const char* name,
    int32_t page,
    const char* side_prefix,
    const PanelControls& controls,
    const PanelLoader& loader
) {
    if (unit.build_remaining != 0.0F)
        return;
    char panel[256];
    if (loader.exists != nullptr && !loader.exists(loader.user, name))
        std::snprintf(panel, sizeof panel, "%sDL", side_prefix != nullptr ? side_prefix : "");
    else
        std::snprintf(panel, sizeof panel, "%s", name);
    if (loader.load == nullptr || !loader.load(loader.user, panel, &unit, page))
        return;
    link_download_buttons(def, page, loader);
    disable_page_buttons(controls, def, side_prefix);
    refresh_order_buttons(controls, state, &unit, &def);
    if (loader.format_counts != nullptr)
        loader.format_counts(loader.user, unit);
    if ((unit.flags & OA_UNIT_FLAG_BUILDING) != 0)
        sync_onoff_control(controls, unit);
    if (page != 0 && loader.check_validity != nullptr)
        loader.check_validity(loader.user);
    state.unit_id = unit.id;
    state.unit_type = unit.type_index;
}

void load_general_page(
    OrderPanelState& state,
    const Unit* unit,
    const UnitDef* def,
    const char* side_prefix,
    const PanelControls& controls,
    const PanelLoader& loader
) {
    char name[256];
    format_general_page_name(name, sizeof name, side_prefix);
    if (loader.load == nullptr || !loader.load(loader.user, name, nullptr, 0))
        return;
    refresh_order_buttons(controls, state, unit, def);
    state.unit_id = unit != nullptr ? unit->id : 0;
    state.unit_type = unit != nullptr ? unit->type_index : 0;
}

void update_order_panel(
    OrderPanelState& state,
    const UnitTable& table,
    uint16_t first_unit,
    uint16_t last_unit,
    uint8_t gui_flags,
    const char* side_prefix,
    const PanelControls& controls,
    const PanelLoader& loader
) {
    state.frame_flags |= kFrameReloadOrders;
    Unit* unit = nullptr;
    int32_t count = 1;
    if (state.unit_id == 0) {
        const auto summary = summarize_selection(state, table, first_unit, last_unit);
        state.frame_flags = static_cast<uint16_t>(
            (summary.frame_flags & kFrameFireOrderMask) |
            without(state.frame_flags, kFrameFireOrderMask)
        );
        state.order_flags = summary.order_flags;
        state.order_flags2 = summary.order_flags2;
        count = summary.count;
        unit = summary.first;
        if (count == 0) {
            (void)release_panels(state, gui_flags, loader);
            state.frame_flags = static_cast<uint16_t>(state.frame_flags & ~kFrameReloadOrders);
        }
    } else {
        unit = order_panel_unit(state, table);
        if (unit == nullptr) {
            state.unit_id = 0;
            return;
        }
    }
    const auto* def = unit != nullptr ? unit_def(table, *unit) : nullptr;
    if (count == 1 && def != nullptr && def->gui_page_count != 0) {
        const auto page = (unit->flags & kUnitFlagBuildMenu) != 0 ? build_page(unit->flags) : 0u;
        if (page != 0 || (def->flags & kDefFlagBuildMenuDefault) != 0) {
            char name[256];
            format_build_page_name(name, sizeof name, *def, page);
            const bool loaded = loader.is_loaded != nullptr && loader.is_loaded(loader.user, name);
            if ((!loaded || state.unit_id != unit->id) && release_panels(state, gui_flags, loader))
                load_build_page(
                    state,
                    *unit,
                    *def,
                    name,
                    static_cast<int32_t>(page),
                    side_prefix,
                    controls,
                    loader
                );
            state.frame_flags = static_cast<uint16_t>(state.frame_flags & ~kFrameReloadOrders);
        }
    }
    if ((state.frame_flags & kFrameReloadOrders) != 0 && release_panels(state, gui_flags, loader)) {
        const Unit* general = count == 1 ? unit : nullptr;
        load_general_page(
            state,
            general,
            general != nullptr ? unit_def(table, *general) : nullptr,
            side_prefix,
            controls,
            loader
        );
    }
}

BuildQueueChange classify_build_queue_change(
    const char* name,
    const Unit& builder,
    uint8_t viewpoint_player,
    int32_t count,
    uint16_t (*type_for_name)(void* user, const char* name),
    void* user,
    const HudEvents& events
) {
    if (builder.owner_index == viewpoint_player)
        play_sound(events, count < 1 ? "subbuild" : "addbuild");
    if (name == nullptr)
        return {BuildQueueKind::none, 0, nullptr};
    if (std::strstr(name, "MAKENUKE") != nullptr || std::strstr(name, "MAKEANTI") != nullptr)
        return {BuildQueueKind::weapon, 0, "BUILDWEAPON"};
    const auto type = type_for_name != nullptr ? type_for_name(user, name) : uint16_t{0};
    if (type == 0)
        return {BuildQueueKind::none, 0, nullptr};
    if (builder.movement == 0)
        return {BuildQueueKind::building, type, "BUILDINGBUILD"};
    return {BuildQueueKind::mobile, type, "MOBILEBUILD"};
}

BuildPanelClickResult on_build_panel_click(
    OrderPanelState& state,
    const UnitTable& table,
    const char* name,
    bool left_button,
    const BuildPanelHost& host,
    const HudEvents& events
) {
    if (name == nullptr)
        return {BuildPanelClick::none, 0};
    const uint16_t type = host.type_for_name != nullptr ? host.type_for_name(host.user, name) : 0;
    if (std::strstr(name, "PREV") != nullptr) {
        state.frame_flags |= kFramePageBack;
        return {BuildPanelClick::page_back, 0};
    }
    if (std::strstr(name, "NEXT") != nullptr) {
        state.frame_flags |= kFramePageForward;
        return {BuildPanelClick::page_forward, 0};
    }
    if (std::strstr(name, "ORDERS") != nullptr) {
        state.frame_flags |= kFrameBuildMenuOff;
        play_sound(events, "ordersbutton");
        return {BuildPanelClick::orders, 0};
    }
    if (std::strstr(name, "BUILD") != nullptr) {
        state.frame_flags |= kFrameBuildMenuOn;
        play_sound(events, "buildbutton");
        return {BuildPanelClick::build, 0};
    }
    // A building type is placed on the map; with placement-by-builder, every
    // type a mobile builder's panel offers is, and none a factory's is.
    const auto places = [&] {
        if (!host.placement_by_builder)
            return table.defs[type].bm_code == 0;
        return state.unit_type < table.def_count && table.defs[state.unit_type].bm_code != 0;
    };
    if (type != 0 && table.defs != nullptr && type < table.def_count && places()) {
        play_sound(events, "addbuild");
        return {BuildPanelClick::place, type};
    }
    if (host.order_click != nullptr && host.order_click(host.user, name))
        return {BuildPanelClick::none, 0};
    if (table.units == nullptr || state.unit_id >= table.unit_count)
        return {BuildPanelClick::none, 0};
    Unit& unit = table.units[state.unit_id];
    if ((unit.flags & kUnitFlagPanelSelected) == 0)
        return {BuildPanelClick::none, 0};
    const bool shift = host.shift_down != nullptr && host.shift_down(host.user);
    const int32_t step = shift ? host.shift_step : 1;
    if (host.change_queue != nullptr)
        host.change_queue(host.user, name, unit, left_button ? step : -step);
    const bool stockpiles = host.stockpiles != nullptr && host.stockpiles(host.user, unit);
    if (((unit.flags & OA_UNIT_FLAG_BUILDING) != 0 || stockpiles) && host.format_counts != nullptr)
        host.format_counts(host.user, unit);
    return {BuildPanelClick::queued, 0};
}

void clear_scroll_state(Game& game) noexcept {
    constexpr std::size_t begin = offsetof(Game, follow_unit);
    constexpr std::size_t bytes = offsetof(Game, pool_units_per_player) - begin;
    static_assert(bytes == 0x5c);
    const auto speed = game.scroll_speed;
    std::memset(reinterpret_cast<uint8_t*>(&game) + begin, 0, bytes);
    game.scroll_speed = speed;
}

} // namespace oa::ui::hud
