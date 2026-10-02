// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unit build restrictions (RESTRICT2.GUI).
#include "oa/ui/frontend_multiplayer/restrict.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace oa::ui::frontend_multiplayer {

namespace {

constexpr uint32_t kPictureInterval = 2;
constexpr std::size_t kRestrictLinesPerRow = 2;
// A unit's row in PICLIST and DESCLIST is this many pixels tall, as in 3.1c.
constexpr int32_t kRestrictRowHeight = 32;

bool hosting(Lobby& lobby) noexcept {
    const auto host = lobby_host_slot(lobby);
    if (host == kNoSlot)
        return false;
    const auto status = slot_player(lobby, host).status;
    return slot_player(lobby, host).in_use != 0 &&
           (status == kSlotLocal || status == kSlotComputer);
}

uint32_t now(Lobby& lobby) noexcept {
    return lobby.services.tick != nullptr ? lobby.services.tick(lobby.services.context) : 0;
}

void play(Lobby& lobby, const char* name) noexcept {
    if (lobby.services.play_sound != nullptr)
        lobby.services.play_sound(lobby.services.context, name);
}

// UnitDef.abilities bit 15 (OA_UNIT_DEF_ABILITY_NO_RESTRICT): the unit has no restriction row.
bool unrestricted(const LobbyUnit& unit) noexcept {
    return (unit.abilities & kUnitNoRestrict) != 0;
}

uint32_t unit_key(Lobby& lobby, const RestrictEntry& entry) noexcept {
    if (entry.unit <= 0 || entry.unit >= lobby.unit_count)
        return 0;
    return lobby.units[entry.unit].fbi_hash;
}

void set_row_flags(RestrictPanel& restrict, int32_t row) noexcept {
    const auto& entry = restrict.entries[row];
    uint8_t flags = entry.available == 0 ? kRestrictRowUnavailable : 0;
    if (entry.limit == 0)
        flags |= kRestrictRowDisabled;
    restrict.flags[row] = flags;
}

int32_t visible_row(const RestrictPanel& restrict, int32_t slider) noexcept {
    return restrict.first_visible + slider;
}

/// Shows SCROLLSLIDER when the units do not all fit in DESCLIST's 32-pixel
/// rows, and sizes its knob from PICLIST's, as 3.1c sizes a list of pictures:
/// knob = whole rows * bar height / units and range = bar height - knob,
/// in whole numbers. The knob starts at the first row, and its moves reach
/// restrict_handle_event.
void bind_scroll_bar(const RestrictPanel& restrict, Panel& panel) noexcept {
    auto* bar = panel_control(panel, "SCROLLSLIDER");
    if (bar == nullptr)
        return;
    const auto* descriptions = panel_control(panel, "DESCLIST");
    const auto* pictures = panel_control(panel, "PICLIST");
    const int32_t shown = descriptions != nullptr ? descriptions->height : 0;
    bar->active = restrict.count * kRestrictRowHeight > shown ? 1 : 0;
    bar->scroll.knob = 0;
    bar->notifies_panel = true;
    if (restrict.count <= 0)
        return;
    const int32_t rows = pictures != nullptr ? pictures->height / kRestrictRowHeight : 0;
    bar->scroll.knob_size = static_cast<int16_t>(rows * bar->height / restrict.count);
    bar->scroll.range = static_cast<int16_t>(bar->height - bar->scroll.knob_size);
}

/// Returns the first unit row SCROLLSLIDER's knob shows:
/// (units - 12) * knob / (range - 1), truncated, or 0 with fewer than two steps.
int32_t scrolled_row(const RestrictPanel& restrict, const Control& bar) noexcept {
    if (bar.scroll.range < 2)
        return 0;
    const double row = static_cast<double>(restrict.count - kRestrictSliders) *
                       static_cast<double>(bar.scroll.knob) /
                       static_cast<double>(bar.scroll.range - 1);
    return std::max(0, static_cast<int32_t>(row));
}

// Each entry is two list lines split at its '\r' (the list item height is
// set to two text lines).
void list_rows(RestrictPanel& restrict, Panel& panel) {
    std::vector<std::string> rows;
    rows.reserve(static_cast<std::size_t>(restrict.count) * kRestrictLinesPerRow);
    for (int32_t row = 0; row < restrict.count; ++row) {
        const std::string text(
            restrict.entries[row].text, ::strnlen(restrict.entries[row].text, kRestrictTextBytes)
        );
        const auto split = text.find('\r');
        rows.push_back(text.substr(0, split));
        rows.push_back(split == std::string::npos ? std::string() : text.substr(split + 1));
    }
    panel_set_items(panel, "DESCLIST", std::move(rows));
}

} // namespace

int restrict_compare(const RestrictEntry& a, const RestrictEntry& b) noexcept {
    return std::strncmp(a.text, b.text, kRestrictTextBytes);
}

void restrict_open(Lobby& lobby, RestrictPanel& restrict, Panel& panel) noexcept {
    restrict.host = (local_info(lobby).role & kRoleHost) != 0;
    restrict_free_pictures(lobby, restrict);
    for (auto& entry : restrict.entries)
        entry = RestrictEntry{};
    restrict.count = 0;
    restrict.anim_tick = 0;
    restrict.first_visible = 0;
    for (int32_t type = 1;
         type < lobby.unit_count&& restrict.count < static_cast<int32_t>(kMaxSyncUnits);
         ++type) {
        const auto& unit = lobby.units[type];
        if (unrestricted(unit) || unit.name == nullptr)
            continue;
        auto& entry = restrict.entries[restrict.count];
        entry = RestrictEntry{};
        std::snprintf(
            entry.text,
            sizeof(entry.text),
            "%s\r%s %dM  %dE",
            unit.name,
            unit.side != nullptr ? unit.side : "",
            static_cast<int>(unit.cost_metal),
            static_cast<int>(unit.cost_energy)
        );
        entry.unit = type;
        UnitSyncRecord record{};
        (void)unit_sync_lookup(lobby, unit.fbi_hash, &record);
        entry.limit = record.limit == -1 ? kRestrictNoLimit : record.limit;
        restrict.saved[restrict.count] = entry.limit;
        entry.available = record.remote;
        ++restrict.count;
    }
    std::stable_sort(
        restrict.entries,
        restrict.entries + restrict.count,
        [](const RestrictEntry& a, const RestrictEntry& b) { return restrict_compare(a, b) < 0; }
    );
    for (int32_t row = 0; row < restrict.count; ++row)
        set_row_flags(restrict, row);
    list_rows(restrict, panel);
    for (int32_t slider = 0; slider < kRestrictSliders; ++slider) {
        char name[kControlNameBytes];
        std::snprintf(name, sizeof(name), "SLIDER%d", static_cast<int>(slider));
        if (auto* control = panel_control(panel, name)) {
            control->scroll.maximum = kRestrictNoLimit;
            control->notifies_panel = true;
        }
    }
    bind_scroll_bar(restrict, panel);
    restrict_update_sliders(lobby, restrict, panel);
    restrict_update_totals(lobby, restrict, panel);
    const bool client = !restrict.host;
    panel_set_grayed(panel, "Load", client);
    panel_set_grayed(panel, "Save", client);
    panel_set_grayed(panel, "Reset", client);
}

void restrict_on_count_slider(
    Lobby& lobby, RestrictPanel& restrict, Panel& panel, int32_t slider
) noexcept {
    const auto row = visible_row(restrict, slider);
    char name[kControlNameBytes];
    std::snprintf(name, sizeof(name), "SLIDER%d", static_cast<int>(slider));
    const auto* control = panel_control(panel, name);
    if (control == nullptr || row < 0 || row >= restrict.count)
        return;
    auto value = oa::ui::gui_input::scroll_value(control->scroll);
    char text[32];
    if (value < kRestrictNoLimit) {
        std::snprintf(text, sizeof(text), "%d", static_cast<int>(value));
    } else {
        std::snprintf(text, sizeof(text), "%s", "No Limit");
        value = -1;
    }
    auto& entry = restrict.entries[row];
    entry.limit = value;
    unit_sync_set_limit(lobby, unit_key(lobby, entry), value);
    set_row_flags(restrict, row);
    std::snprintf(name, sizeof(name), "COUNT%d", static_cast<int>(slider));
    panel_set_text(panel, name, text);
}

void restrict_update_sliders(Lobby& lobby, RestrictPanel& restrict, Panel& panel) noexcept {
    if (const auto* list = panel_control(panel, "DESCLIST"))
        restrict.first_visible = list->list_first / static_cast<int32_t>(kRestrictLinesPerRow);
    const bool host = hosting(lobby);
    for (int32_t slider = 0; slider < kRestrictSliders; ++slider) {
        char name[kControlNameBytes];
        std::snprintf(name, sizeof(name), "SLIDER%d", static_cast<int>(slider));
        auto* control = panel_control(panel, name);
        if (control == nullptr)
            continue;
        const auto row = visible_row(restrict, slider);
        if (row >= restrict.count) {
            control->active = 0;
            continue;
        }
        const bool grayed = !host || restrict.entries[row].available == 0;
        restrict.flags[row] = grayed ? 1 : 0;
        const auto limit = restrict.entries[row].limit;
        oa::ui::gui_input::scroll_set_value(control->scroll, limit < 0 ? kRestrictNoLimit : limit);
        control->grayed = grayed;
        control->active = 1;
        restrict_on_count_slider(lobby, restrict, panel, slider);
    }
}

void restrict_load_next_picture(Lobby& lobby, RestrictPanel& restrict, Panel& panel) noexcept {
    const auto row = restrict.picture_rows++;
    if (row >= lobby.unit_count || row >= static_cast<int32_t>(kMaxSyncUnits))
        return;
    const auto unit = restrict.entries[row].unit;
    if (unit < 0 || unit >= lobby.unit_count || unrestricted(lobby.units[unit]))
        return;
    const char* name = lobby.units[unit].unit_name != nullptr ? lobby.units[unit].unit_name : "";
    char path[data::campaign::kCampaignPathBytes];
    data::campaign::build_variant_path(
        lobby.services.files, path, sizeof path, "unitpics", name, "PCX"
    );
    int32_t width = 0;
    int32_t height = 0;
    const oa_ref32 image =
        lobby.services.load_picture != nullptr
            ? lobby.services.load_picture(lobby.services.context, path, &width, &height)
            : 0;
    auto& picture = restrict.pictures[restrict.picture_count++];
    picture.image = image;
    if (image == 0) {
        const auto* list = panel_control(panel, "PICLIST");
        picture.width = static_cast<uint16_t>(list != nullptr ? list->width : 0);
        picture.height = kRestrictMissingPictureHeight;
    } else {
        picture.width = static_cast<uint16_t>(width);
        picture.height = static_cast<uint16_t>(height);
        picture.load_state = kRestrictPictureLoaded;
    }
    panel.dirty = true;
}

void restrict_free_pictures(Lobby& lobby, RestrictPanel& restrict) noexcept {
    for (int32_t index = 0; index < restrict.picture_count; ++index) {
        auto& picture = restrict.pictures[index];
        if (picture.image != 0 && lobby.services.free_picture != nullptr)
            lobby.services.free_picture(lobby.services.context, picture.image);
        picture = RestrictPicture{};
    }
    restrict.picture_count = 0;
    restrict.picture_rows = 0;
}

void restrict_tick(Lobby& lobby, RestrictPanel& restrict, Panel& panel) noexcept {
    const auto tick = static_cast<int32_t>(now(lobby));
    if (restrict.anim_tick < tick) {
        restrict.anim_tick = tick + static_cast<int32_t>(kPictureInterval);
        restrict_load_next_picture(lobby, restrict, panel);
    }
    UnitSyncRecord record{};
    int32_t updates = 0;
    while (unit_sync_pop_changed(lobby, &record)) {
        ++updates;
        for (int32_t row = 0; row < restrict.count; ++row) {
            auto& entry = restrict.entries[row];
            if (unit_key(lobby, entry) != record.key)
                continue;
            entry.available = record.remote;
            entry.limit = record.limit;
            restrict.flags[row] = record.remote == 0 ? kRestrictRowUnavailable : 0;
            if (entry.limit == 0)
                restrict.flags[row] |= kRestrictRowDisabled;
        }
    }
    if (updates != 0) {
        restrict_update_sliders(lobby, restrict, panel);
        panel.dirty = true;
    }
}

void restrict_update_totals(Lobby& lobby, RestrictPanel& restrict, Panel& panel) noexcept {
    const auto* list = panel_control(panel, "DESCLIST");
    const auto row =
        list != nullptr ? list->list_selection / static_cast<int32_t>(kRestrictLinesPerRow) : 0;
    float energy = 0;
    float metal = 0;
    if (row >= 0 && row < restrict.count) {
        const auto unit = restrict.entries[row].unit;
        if (unit > 0 && unit < lobby.unit_count) {
            energy = lobby.units[unit].cost_energy;
            metal = lobby.units[unit].cost_metal;
        }
    }
    char text[32];
    std::snprintf(text, sizeof(text), "%d", static_cast<int>(energy));
    panel_set_text(panel, "ENERGYTEXT", text);
    std::snprintf(text, sizeof(text), "%d", static_cast<int>(metal));
    panel_set_text(panel, "METALTEXT", text);
}

RestrictAction restrict_handle_event(Lobby& lobby, RestrictPanel& restrict, Panel& panel) noexcept {
    auto action = RestrictAction::none;
    if (panel_selected_is(panel, "Load")) {
        play(lobby, "Options");
        action = RestrictAction::load_list;
    } else if (panel_selected_is(panel, "Save")) {
        play(lobby, "Options");
        action = RestrictAction::save_list;
    } else if (panel_selected_is(panel, "Reset")) {
        play(lobby, "Options");
        for (int32_t row = 0; row < restrict.count; ++row) {
            auto& entry = restrict.entries[row];
            if (entry.unit == 0)
                continue;
            const bool disabled = entry.unit < lobby.unit_count &&
                                  (lobby.units[entry.unit].abilities & kUnitDisabledDefault) != 0;
            entry.limit = disabled ? 0 : kRestrictResetLimit;
            if (entry.limit != entry.synced_limit)
                unit_sync_set_limit(lobby, unit_key(lobby, entry), entry.limit);
        }
        restrict_update_sliders(lobby, restrict, panel);
        panel.dirty = true;
    } else if (panel_selected_is(panel, "OK")) {
        play(lobby, "Options");
        panel.selected = kNoControl;
        return RestrictAction::close;
    } else if (panel_selected_is(panel, "Cancel")) {
        play(lobby, "Previous");
        // The saved limits are replayed in unit-type order, not row order.
        int32_t saved = 0;
        for (int32_t type = 1; type < lobby.unit_count; ++type) {
            const auto& unit = lobby.units[type];
            if (unrestricted(unit))
                continue;
            if (saved >= restrict.count)
                break;
            unit_sync_set_limit(lobby, unit.fbi_hash, restrict.saved[saved++]);
        }
        panel.selected = kNoControl;
        return RestrictAction::close;
    } else if (panel.selected != kNoControl) {
        const auto& control = panel.controls[static_cast<std::size_t>(panel.selected)];
        const auto name = control_name(control);
        if (name == "SCROLLSLIDER") {
            if (auto* list = panel_control(panel, "DESCLIST"))
                list->list_first = static_cast<int16_t>(
                    scrolled_row(restrict, control) * static_cast<int32_t>(kRestrictLinesPerRow)
                );
            restrict_update_sliders(lobby, restrict, panel);
        } else if (name.rfind("SLIDER", 0) == 0) {
            restrict_on_count_slider(
                lobby, restrict, panel, std::atoi(std::string(name.substr(6)).c_str())
            );
        } else if (name == "DESCLIST") {
            restrict_update_totals(lobby, restrict, panel);
        }
    }
    panel.selected = kNoControl;
    return action;
}

void restrict_close(Lobby& lobby, RestrictPanel& restrict) noexcept {
    restrict_free_pictures(lobby, restrict);
    if (!hosting(lobby))
        return;
    for (int32_t row = 0; row < restrict.count; ++row) {
        const auto& entry = restrict.entries[row];
        if (entry.unit == 0)
            continue;
        (void)unit_sync_set_enabled(lobby, unit_key(lobby, entry), entry.limit != 0);
    }
}

} // namespace oa::ui::frontend_multiplayer
