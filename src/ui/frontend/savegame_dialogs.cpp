// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Load/save game dialogs and unit restriction list dialogs.
#include "oa/ui/frontend/savegame_dialogs.hpp"

#include "oa/ui/frontend_state/dispatcher.hpp"
#include "oa/data/match_rules/difficulty_names.hpp"
#include "oa/data/persist/hapibank.hpp"
#include "oa/data/persist/save_sections.hpp"
#include "oa/present/pcx.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>
#include <cstdint>

namespace oa::ui::frontend {

namespace save_key = data::persist::save_key;

namespace {

constexpr std::string_view kPreviousSound = "Previous";
constexpr std::string_view kSmallButtonSound = "SMLBUTTON";
constexpr std::string_view kSmallButtonLowerSound = "smlbutton";
constexpr std::string_view kSmallButtonMixedSound = "SmallButton";
constexpr std::string_view kOptionsSound = "Options";
constexpr int32_t kDiscMessageWidth = 200;
constexpr uint32_t kEditableAttribute = 2;

template <std::size_t N>
std::string_view view(const std::array<char, N>& text) noexcept {
    return {text.data(), ::strnlen(text.data(), N)};
}

template <std::size_t N>
void copy(std::array<char, N>& out, std::string_view text) noexcept {
    out.fill('\0');
    std::memcpy(out.data(), text.data(), std::min(text.size(), N - 1U));
}

void play(const SaveDialogContext& context, std::string_view name) {
    if (context.host.play_sound != nullptr)
        context.host.play_sound(context.host.context, name.data());
}

void message(const SaveDialogContext& context, const char* text, int32_t width) {
    if (context.host.show_message != nullptr)
        context.host.show_message(context.host.context, text, width);
}

void count_entry(void* user, const data::campaign::FindRecord& record) {
    if (std::strcmp(record.name, ".") != 0 && std::strcmp(record.name, "..") != 0)
        ++*static_cast<int32_t*>(user);
}

// "<directory>\*.<extension>" counted and then listed through the directory
// lister, newest first, as the save and restriction list builders do.
void list_directory(SaveDialogContext& context, std::string_view extension) {
    context.list.entries.clear();
    if (context.files.find == nullptr)
        return;
    char pattern[kSaveFileBytes];
    std::snprintf(
        pattern,
        sizeof pattern,
        "%.*s\\*.%.*s",
        static_cast<int>(context.directory.size()),
        context.directory.data(),
        static_cast<int>(extension.size()),
        extension.data()
    );
    int32_t count = 0;
    context.files.find(context.files.context, pattern, count_entry, &count);
    if (count <= 0)
        return;
    const std::size_t bytes = static_cast<std::size_t>(count) * kSaveFileBytes;
    std::vector<char> names(bytes);
    const data::campaign::DirectoryFind find{context.files.context, context.files.find, nullptr};
    const auto listed = data::campaign::list_directory_entries(
        find,
        pattern,
        names.data(),
        names.size(),
        nullptr,
        0,
        false,
        false,
        data::campaign::ListSort::by_time
    );
    const char* name = names.data();
    for (int32_t index = 0; index < listed; ++index, name += std::strlen(name) + 1) {
        SaveEntry entry{};
        copy(entry.file, name);
        context.list.entries.push_back(entry);
    }
}

void join_path(
    const SaveDialogContext& context, std::string_view name, char* out, std::size_t capacity
) {
    std::snprintf(
        out,
        capacity,
        "%.*s\\%.*s",
        static_cast<int>(context.directory.size()),
        context.directory.data(),
        static_cast<int>(name.size()),
        name.data()
    );
}

// Directory + "\" + name with any extension replaced by `extension`.
void variant_path(
    const SaveDialogContext& context,
    std::string_view name,
    std::string_view extension,
    char* out,
    std::size_t capacity
) {
    const auto dot = name.rfind('.');
    if (dot != std::string_view::npos)
        name = name.substr(0, dot);
    std::snprintf(
        out,
        capacity,
        "%.*s\\%.*s.%.*s",
        static_cast<int>(context.directory.size()),
        context.directory.data(),
        static_cast<int>(name.size()),
        name.data(),
        static_cast<int>(extension.size()),
        extension.data()
    );
}

int32_t games_selection(const Panel& panel) noexcept {
    const auto* games = panel_control(panel, "GAMES");
    return games != nullptr ? games->list_selection : -1;
}

bool selection_valid(const SaveDialogContext& context, int32_t index) noexcept {
    return index >= 0 && static_cast<std::size_t>(index) < context.list.entries.size();
}

void make_directory(SaveDialogContext& context) {
    if (context.files.make_directory == nullptr)
        return;
    char directory[kSaveFileBytes];
    std::snprintf(
        directory,
        sizeof directory,
        "%.*s",
        static_cast<int>(context.directory.size()),
        context.directory.data()
    );
    context.files.make_directory(context.files.context, directory);
}

void set_games_selection(Panel& panel, int16_t index) noexcept {
    if (auto* games = panel_control(panel, "GAMES"))
        games->list_selection = index;
}

// Descriptions of *.LST lists are the file names without their extension.
void strip_extensions(SaveList& list) {
    for (auto& entry : list.entries) {
        auto name = view(entry.file);
        const auto dot = name.rfind('.');
        if (dot != std::string_view::npos)
            name = name.substr(0, dot);
        copy(entry.description, name);
    }
}

void write_u32(std::vector<uint8_t>& out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<uint8_t>(value >> shift));
}

bool read_i32(std::span<const uint8_t> bytes, std::size_t& at, int32_t& value) {
    if (bytes.size() < at + 4U)
        return false;
    value = static_cast<int32_t>(
        static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8) |
        (static_cast<uint32_t>(bytes[at + 2]) << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24)
    );
    at += 4;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Save list

void savegame_release_lists(SaveDialogContext& context) noexcept {
    context.list.entries.clear();
    context.list.entries.shrink_to_fit();
    context.radar_picture = {};
}

std::size_t savegame_build_list(SaveDialogContext& context) {
    list_directory(context, kSaveExtension);
    auto& entries = context.list.entries;
    std::size_t kept = 0;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        char path[kSaveFileBytes * 2];
        join_path(context, view(entries[index].file), path, sizeof path);
        void* bank = context.reader.open != nullptr
                         ? context.reader.open(context.reader.context, path)
                         : nullptr;
        bool described = false;
        std::array<char, kSaveDescriptionBytes> description{};
        if (bank != nullptr) {
            described = context.reader.get_string != nullptr && context.reader.get_string(
                                                                    context.reader.context,
                                                                    bank,
                                                                    save_key::description,
                                                                    description.data(),
                                                                    description.size()
                                                                );
            if (context.reader.close != nullptr)
                context.reader.close(context.reader.context, bank);
        }
        if (!described)
            continue;
        entries[kept].file = entries[index].file;
        entries[kept].description = description;
        ++kept;
    }
    entries.resize(kept);
    return kept;
}

void savegame_entry_path(
    const SaveDialogContext& context, std::size_t index, char* out, std::size_t capacity
) noexcept {
    if (capacity == 0)
        return;
    out[0] = '\0';
    if (index < context.list.entries.size())
        join_path(context, view(context.list.entries[index].file), out, capacity);
}

void savegame_format_time(int32_t ticks, char* out, std::size_t capacity) noexcept {
    constexpr int32_t per_minute = kSaveTicksPerSecond * 60;
    constexpr int32_t per_hour = per_minute * 60;
    std::snprintf(
        out,
        capacity,
        "%02d:%02d:%02d",
        ticks / per_hour,
        (ticks / per_minute) % 60,
        (ticks / kSaveTicksPerSecond) % 60
    );
}

void savegame_fill_preview(Panel& panel, SaveDialogContext& context) {
    if (panel_control(panel, "GAMES") == nullptr)
        return;
    const auto index = games_selection(panel);
    const bool described =
        selection_valid(context, index) &&
        !view(context.list.entries[static_cast<std::size_t>(index)].description).empty();
    void* bank = nullptr;
    if (described) {
        const auto& entry = context.list.entries[static_cast<std::size_t>(index)];
        panel_set_text(panel, "GAMENAME", view(entry.description));
        char path[kSaveFileBytes * 2];
        join_path(context, view(entry.file), path, sizeof path);
        if (context.reader.open != nullptr)
            bank = context.reader.open(context.reader.context, path);
    }
    if (bank == nullptr) {
        for (const auto name :
             {"GAMENAME", "SIDE", "DIFF", "MISSION", "CAMPAIGN", "GAMETYPE", "TIME"})
            panel_set_text(panel, name, "");
        panel.dirty = true;
        return;
    }
    const auto& reader = context.reader;
    const auto get_int = [&](const char* field, int32_t fallback) {
        return reader.get_int != nullptr ? reader.get_int(reader.context, bank, field, fallback)
                                         : fallback;
    };
    const auto get_string = [&](const char* field, char* out, std::size_t capacity) {
        return reader.get_string != nullptr &&
               reader.get_string(reader.context, bank, field, out, capacity);
    };
    // The previous picture goes before the saved one is read.
    context.radar_picture = {};
    const bool radar = reader.load_radar != nullptr &&
                       reader.load_radar(reader.context, bank, context.radar_picture);
    if (!radar)
        context.radar_picture = {};
    panel_set_active(panel, "RADAR", radar ? 1 : 0);
    const auto players = get_int(save_key::players, 0);
    const auto game_type = get_int(save_key::game_type, 0);
    char text[0x40];
    if (players == 0)
        std::snprintf(text, sizeof text, "???");
    else if (game_type == data::persist::game_type_campaign)
        std::snprintf(text, sizeof text, "Single");
    else
        std::snprintf(text, sizeof text, "Skirmish (%d players)", players);
    panel_set_text(panel, "GAMETYPE", text);
    const char* place_field = save_key::map;
    if (game_type == data::persist::game_type_campaign) {
        if (get_string(save_key::campaign, text, sizeof text)) {
            panel_set_text(panel, "CAMPAIGN", text);
            panel_set_active(panel, "CAMPTEXT", 1);
            panel_set_active(panel, "CAMPAIGN", 1);
        }
        place_field = save_key::mission;
    } else {
        panel_set_active(panel, "CAMPTEXT", 0);
        panel_set_active(panel, "CAMPAIGN", 0);
    }
    if (get_string(place_field, text, sizeof text))
        panel_set_text(panel, "MISSION", text);
    savegame_format_time(get_int(save_key::game_time, 0), text, sizeof text);
    panel_set_text(panel, "TIME", text);
    if (context.side_names.empty()) {
        panel_set_text(panel, "SIDE", "???");
    } else {
        const auto side = get_int(save_key::side, 0);
        panel_set_text(
            panel,
            "SIDE",
            side >= 0 && static_cast<std::size_t>(side) < context.side_names.size()
                ? context.side_names[static_cast<std::size_t>(side)]
                : std::string_view{}
        );
    }
    static constexpr const char* difficulty[] = {"Easy", "Medium", "Hard"};
    const auto level = get_int(save_key::difficulty, 0);
    panel_set_text(
        panel,
        "DIFF",
        level >= 0 && level < 3
            ? difficulty[data::match_rules::difficulty_name_index(context.difficulty_names, level)]
            : ""
    );
    if (reader.close != nullptr)
        reader.close(reader.context, bank);
    panel.dirty = true;
}

void savegame_report_invalid(Panel& panel, SaveDialogContext& context) {
    message(context, kInvalidSaveMessage, kInvalidSaveMessageWidth);
    panel_clear_selection(panel);
}

void savegame_on_games_selected(Panel& panel, SaveDialogContext& context) {
    savegame_fill_preview(panel, context);
}

// ---------------------------------------------------------------------------
// Dialog setup

bool savegame_enter_load(Panel& panel, SaveDialogContext& context) {
    if (savegame_build_list(context) == 0) {
        message(context, "There are no saved games to choose from", kNoSavesMessageWidth);
        return false;
    }
    set_games_selection(panel, 0);
    panel_set_active(panel, "DELETE", 0);
    panel_set_active(panel, "GAMENAME", 0);
    savegame_fill_preview(panel, context);
    panel_set_active(panel, "SaveGame", 0);
    panel.dirty = true;
    context.hold_game = true;
    return true;
}

void savegame_enter_save(Panel& panel, SaveDialogContext& context) {
    context.hold_game = true;
    make_directory(context);
    const auto count = savegame_build_list(context);
    panel_set_text(panel, "TITLE", "Save Game");
    if (count == 0)
        panel_set_active(panel, "DELETE", 0);
    const auto name = panel_find(panel, "GAMENAME");
    if (name >= 0)
        panel.controls[static_cast<std::size_t>(name)].attributes |= kEditableAttribute;
    set_games_selection(panel, count == 0 ? int16_t{-1} : int16_t{0});
    savegame_fill_preview(panel, context);
    panel_set_active(panel, "LoadGame", 0);
    panel.dirty = true;
}

// ---------------------------------------------------------------------------
// Clicks

SaveDialogResult savegame_on_load_click(Panel& panel, SaveDialogContext& context) {
    SaveDialogResult result;
    if (panel.selected == kNoSelection)
        return result;
    if (panel_selected_is(panel, "CANCEL")) {
        savegame_release_lists(context);
        play(context, kPreviousSound);
        result.action = SaveDialogAction::cancelled;
        return result;
    }
    if (!panel_selected_is(panel, "LOAD") && !panel_selected_is(panel, "GAMES")) {
        panel_clear_selection(panel);
        return result;
    }
    const auto index = games_selection(panel);
    char path[kSaveFileBytes * 2] = {};
    if (selection_valid(context, index))
        savegame_entry_path(context, static_cast<std::size_t>(index), path, sizeof path);
    void* bank = path[0] != '\0' && context.reader.open != nullptr
                     ? context.reader.open(context.reader.context, path)
                     : nullptr;
    if (bank == nullptr) {
        savegame_report_invalid(panel, context);
        result.action = SaveDialogAction::invalid;
        return result;
    }
    const auto game_type =
        context.reader.get_int != nullptr
            ? context.reader.get_int(context.reader.context, bank, save_key::game_type, 0)
            : 0;
    if (context.reader.close != nullptr)
        context.reader.close(context.reader.context, bank);
    if (game_type != data::persist::game_type_campaign &&
        game_type != data::persist::game_type_skirmish) {
        savegame_report_invalid(panel, context);
        result.action = SaveDialogAction::invalid;
        return result;
    }
    const bool disc =
        context.host.disc_present == nullptr || context.host.disc_present(context.host.context);
    if (!disc) {
        message(
            context,
            game_type == data::persist::game_type_campaign
                ? "Please insert the Campaign CD (Disc 2) and try again"
                : "Please insert the Multiplayer CD (Disc 1) and try again",
            kDiscMessageWidth
        );
        panel_clear_selection(panel);
        return result;
    }
    if (context.host.refresh_archives != nullptr)
        context.host.refresh_archives(context.host.context);
    play(context, kSmallButtonSound);
    result.action = SaveDialogAction::load;
    result.game_type = game_type;
    copy(result.path, path);
    return result;
}

SaveDialogResult savegame_on_save_click(Panel& panel, SaveDialogContext& context) {
    SaveDialogResult result;
    if (panel.selected == kNoSelection) {
        savegame_release_lists(context);
        return result;
    }
    if (panel_selected_is(panel, "CANCEL")) {
        play(context, kPreviousSound);
        result.action = SaveDialogAction::cancelled;
        return result;
    }
    if (panel_selected_is(panel, "DELETE")) {
        play(context, kSmallButtonMixedSound);
        const auto index = games_selection(panel);
        char path[kSaveFileBytes * 2];
        if (selection_valid(context, index) && context.files.remove != nullptr) {
            savegame_entry_path(context, static_cast<std::size_t>(index), path, sizeof path);
            context.files.remove(context.files.context, path);
        }
        const auto count = savegame_build_list(context);
        if (index >= static_cast<int32_t>(count))
            set_games_selection(panel, static_cast<int16_t>(count) - 1);
        panel_clear_selection(panel);
        savegame_fill_preview(panel, context);
        result.action = SaveDialogAction::refreshed;
        return result;
    }
    if (!panel_selected_is(panel, "LOAD") && !panel_selected_is(panel, "GAMES") &&
        !panel_selected_is(panel, "GAMENAME")) {
        panel_clear_selection(panel);
        return result;
    }
    play(context, kSmallButtonLowerSound);
    const auto* field = panel_control(panel, "GAMENAME");
    const auto name = field != nullptr ? control_text(*field) : std::string_view{};
    if (name.empty())
        return result;
    char path[kSaveFileBytes * 2];
    variant_path(context, name, kSaveExtension, path, sizeof path);
    result.action = SaveDialogAction::save;
    copy(result.path, path);
    return result;
}

SaveDialogResult savegame_on_save_press(Panel& panel, SaveDialogContext& context, int32_t control) {
    if (control < 1 || control > panel.count ||
        panel.controls[static_cast<std::size_t>(control)].type != ControlType::button)
        return {};
    panel.selected = control;
    return savegame_on_save_click(panel, context);
}

bool savegame_read_load_summary(const SaveSummaryReader& reader, void* bank, LoadSummary& summary) {
    summary = LoadSummary{};
    if (bank == nullptr || reader.get_int == nullptr || reader.get_string == nullptr)
        return false;
    const auto get_int = [&](const char* field, int32_t fallback) {
        return reader.get_int(reader.context, bank, field, fallback);
    };
    summary.game_type = get_int(save_key::game_type, 0);
    summary.has_campaign = reader.get_string(
        reader.context, bank, save_key::campaign, summary.campaign.data(), summary.campaign.size()
    );
    summary.side = get_int(save_key::side, 0);
    summary.difficulty = get_int(save_key::difficulty, 0);
    if (!reader.get_string(
            reader.context, bank, save_key::mission, summary.mission.data(), summary.mission.size()
        ) ||
        summary.mission[0] == '\0')
        return false;
    // "Thumbs" is copied with a 25-byte bound; a record that is not exactly
    // 25 results long is replaced by the 25 'U' bytes a skirmish start also
    // writes. A missing field counts as a record of the wrong length.
    const bool thumbs = reader.get_string(
        reader.context,
        bank,
        save_key::thumbs,
        summary.start_pattern.data(),
        summary.start_pattern.size()
    );
    if (!thumbs ||
        std::strlen(summary.start_pattern.data()) != ui::frontend_state::start_pattern_bytes) {
        std::fill_n(
            summary.start_pattern.begin(),
            ui::frontend_state::start_pattern_bytes,
            static_cast<char>(ui::frontend_state::start_pattern_value)
        );
        summary.start_pattern[ui::frontend_state::start_pattern_bytes] = '\0';
    }
    if (summary.game_type == data::persist::game_type_skirmish) {
        summary.players = get_int(save_key::players, 0);
        summary.commander_death = get_int(save_key::commander_death, 1);
        summary.location = get_int(save_key::location, 1);
        summary.mapping = get_int(save_key::mapping, 1);
        summary.line_of_sight = get_int(save_key::line_of_sight, 1);
        summary.line_of_sight_type = get_int(save_key::line_of_sight_type, 1);
    }
    summary.between_missions = summary.game_type == data::persist::game_type_campaign &&
                               reader.has_field != nullptr &&
                               reader.has_field(reader.context, bank, save_key::between_missions);
    return true;
}

// ---------------------------------------------------------------------------
// Restriction lists

std::vector<uint8_t>
restrict_list_encode(std::span<const int32_t> unit_ids, std::span<const RestrictRow> rows) {
    std::vector<uint8_t> out;
    const auto count = static_cast<int32_t>(unit_ids.size());
    write_u32(out, static_cast<uint32_t>(count - 1));
    for (int32_t unit = 1; unit < count; ++unit) {
        for (const auto& row : rows) {
            if (row.unit_index != unit)
                continue;
            write_u32(out, static_cast<uint32_t>(unit_ids[static_cast<std::size_t>(unit)]));
            write_u32(out, static_cast<uint32_t>(row.limit));
            break;
        }
    }
    return out;
}

void restrict_list_apply(
    std::span<const uint8_t> bytes, std::span<const int32_t> unit_ids, std::span<RestrictRow> rows
) {
    std::size_t at = 0;
    int32_t pairs = 0;
    if (!read_i32(bytes, at, pairs))
        return;
    const auto count = static_cast<int32_t>(unit_ids.size());
    for (int32_t pair = 0; pair < pairs; ++pair) {
        int32_t id = 0;
        int32_t limit = 0;
        if (!read_i32(bytes, at, id) || !read_i32(bytes, at, limit))
            return;
        for (int32_t unit = 1; unit < count; ++unit) {
            if (unit_ids[static_cast<std::size_t>(unit)] != id)
                continue;
            for (auto& row : rows) {
                if (row.unit_index == unit) {
                    row.limit = limit;
                    break;
                }
            }
            break;
        }
    }
}

int32_t restrict_limit_label(int32_t value, char* out, std::size_t capacity) noexcept {
    if (value < kRestrictLimitCeiling + 1) {
        std::snprintf(out, capacity, "%d", value);
        return value;
    }
    std::snprintf(out, capacity, "No Limit");
    return kRestrictNoLimit;
}

std::size_t restrict_build_list(SaveDialogContext& context) {
    list_directory(context, kRestrictListExtension);
    strip_extensions(context.list);
    return context.list.entries.size();
}

void restrict_sync_name(Panel& panel, const SaveDialogContext& context) {
    const auto index = games_selection(panel);
    std::string_view name;
    if (selection_valid(context, index))
        name = view(context.list.entries[static_cast<std::size_t>(index)].description);
    panel_set_text(panel, "GAMENAME", name);
}

void restrict_on_games_selected(Panel& panel, const SaveDialogContext& context) {
    restrict_sync_name(panel, context);
}

void restrict_enter_save(Panel& panel, SaveDialogContext& context) {
    make_directory(context);
    const auto count = restrict_build_list(context);
    panel_set_text(panel, "TITLE", "Save Game");
    if (count == 0)
        panel_set_active(panel, "DELETE", 0);
    const auto name = panel_find(panel, "GAMENAME");
    if (name >= 0)
        panel.controls[static_cast<std::size_t>(name)].attributes |= kEditableAttribute;
    set_games_selection(panel, count == 0 ? int16_t{-1} : int16_t{0});
    restrict_sync_name(panel, context);
    panel_set_active(panel, "LoadGame", 0);
    panel.dirty = true;
}

bool restrict_enter_load(Panel& panel, SaveDialogContext& context) {
    if (restrict_build_list(context) == 0) {
        message(context, "There are no saved lists to choose from", kNoSavesMessageWidth);
        return false;
    }
    set_games_selection(panel, 0);
    panel_set_active(panel, "DELETE", 0);
    panel_set_active(panel, "GAMENAME", 0);
    restrict_sync_name(panel, context);
    panel_set_active(panel, "SaveGame", 0);
    panel.dirty = true;
    context.hold_game = true;
    return true;
}

SaveDialogResult restrict_on_save_click(Panel& panel, SaveDialogContext& context) {
    SaveDialogResult result;
    if (panel.selected == kNoSelection) {
        savegame_release_lists(context);
        context.hold_game = false;
        return result;
    }
    if (panel_selected_is(panel, "CANCEL")) {
        play(context, kPreviousSound);
        result.action = SaveDialogAction::cancelled;
        return result;
    }
    if (panel_selected_is(panel, "DELETE")) {
        play(context, kSmallButtonSound);
        const auto index = games_selection(panel);
        if (selection_valid(context, index) && context.files.remove != nullptr) {
            char path[kSaveFileBytes * 2];
            savegame_entry_path(context, static_cast<std::size_t>(index), path, sizeof path);
            context.files.remove(context.files.context, path);
        }
        restrict_build_list(context);
        panel_clear_selection(panel);
        restrict_sync_name(panel, context);
        result.action = SaveDialogAction::refreshed;
        return result;
    }
    if (!panel_selected_is(panel, "GAMES") && !panel_selected_is(panel, "LOAD") &&
        !panel_selected_is(panel, "GAMENAME")) {
        panel_clear_selection(panel);
        return result;
    }
    play(context, kOptionsSound);
    const auto* field = panel_control(panel, "GAMENAME");
    const auto name = field != nullptr ? control_text(*field) : std::string_view{};
    if (name.empty())
        return result;
    char path[kSaveFileBytes * 2];
    variant_path(context, name, kRestrictListExtension, path, sizeof path);
    result.action = SaveDialogAction::save;
    copy(result.path, path);
    return result;
}

SaveDialogResult restrict_on_load_click(Panel& panel, SaveDialogContext& context) {
    SaveDialogResult result;
    if (panel.selected == kNoSelection)
        return result;
    if (panel_selected_is(panel, "CANCEL")) {
        play(context, kPreviousSound);
        result.action = SaveDialogAction::cancelled;
        return result;
    }
    if (!panel_selected_is(panel, "LOAD") && !panel_selected_is(panel, "GAMES")) {
        panel_clear_selection(panel);
        return result;
    }
    play(context, kOptionsSound);
    const auto index = games_selection(panel);
    char path[kSaveFileBytes * 2] = {};
    if (selection_valid(context, index))
        savegame_entry_path(context, static_cast<std::size_t>(index), path, sizeof path);
    result.action = SaveDialogAction::load;
    copy(result.path, path);
    savegame_release_lists(context);
    return result;
}

// ---------------------------------------------------------------------------
// Host file system and src/data/persist adapters

namespace {

std::filesystem::path host_path(const std::filesystem::path& root, const char* path) {
    std::string relative(path);
    std::replace(relative.begin(), relative.end(), '\\', '/');
    return root / relative;
}

std::string upper(std::string text) {
    for (auto& ch : text)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return text;
}

bool read_host_file(void* context, const char* path, data::persist::ByteImage* out) {
    std::ifstream file(
        host_path(*static_cast<const std::filesystem::path*>(context), path), std::ios::binary
    );
    if (!file)
        return false;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    return data::persist::byte_image_assign(out, bytes.data(), static_cast<uint32_t>(bytes.size()));
}

/// Reads a save's bank with only its "Summary" account, that account open.
///
/// @param context Root directory (a std::filesystem::path) the path is below.
/// @param path Save path with '\' separators.
/// @return A heap-allocated data::persist::Bank, or null when the file is missing or not a save.
void* open_summary_bank(void* context, const char* path) {
    auto* bank = new data::persist::Bank{};
    data::persist::bank_init(bank);
    const data::persist::FileSource source{context, read_host_file};
    data::persist::BankError error{};
    if (!data::persist::bank_read_file(
            bank, path, data::persist::savegame_description, save_key::summary, &source, &error
        )) {
        data::persist::bank_destroy(bank);
        delete bank;
        return nullptr;
    }
    data::persist::bank_open_account(bank, save_key::summary);
    return bank;
}

/// Reads the open blob of a bank as a stream from its start.
///
/// @param bank bank with an open account and blob; must outlive the stream
/// @return a read-only stream over the blob
present::ByteStream blob_stream(data::persist::Bank* bank) {
    present::ByteStream stream;
    stream.user = bank;
    stream.read = [](void* user, void* data, int32_t size) {
        if (size <= 0)
            return 0;
        return static_cast<int32_t>(data::persist::bank_blob_read(
            static_cast<data::persist::Bank*>(user), data, static_cast<uint32_t>(size)
        ));
    };
    stream.seek = [](void* user, int32_t position) {
        data::persist::bank_blob_seek(static_cast<data::persist::Bank*>(user), position);
        return 0;
    };
    stream.length = [](void* user) {
        return data::persist::bank_blob_size(static_cast<const data::persist::Bank*>(user));
    };
    return stream;
}

/// Reads the radar image a save's Summary holds.
///
/// An image narrower or shorter than kSaveRadarMinimumSize counts as none, as
/// the preview draws it from one pixel inside each of its edges.
///
/// @param bank Bank open_summary_bank returned.
/// @param[out] picture The image; empty when the blob holds no whole image
///     of at least the minimum size.
/// @return true when the image was read.
bool load_summary_radar(void* /*context*/, void* bank, present::SurfaceBuffer& picture) {
    auto* summary = static_cast<data::persist::Bank*>(bank);
    data::persist::bank_open_blob_name(summary, save_key::radar_image);
    auto stream = blob_stream(summary);
    if (!present::read_surface_rows(stream, picture) ||
        picture.surface.width < kSaveRadarMinimumSize ||
        picture.surface.height < kSaveRadarMinimumSize) {
        picture = {};
        return false;
    }
    return true;
}

/// Releases a bank open_summary_bank returned.
///
/// @param context Reader context; unused.
/// @param bank Bank to destroy and free; null is ignored.
void close_summary_bank(void* /*context*/, void* bank) {
    auto* owned = static_cast<data::persist::Bank*>(bank);
    if (owned == nullptr)
        return;
    data::persist::bank_destroy(owned);
    delete owned;
}

} // namespace

SaveFiles savegame_host_files(const std::filesystem::path* root) {
    SaveFiles files;
    files.context = const_cast<std::filesystem::path*>(root);
    files.find = [](void* context,
                    const char* pattern,
                    void (*visit)(void*, const data::campaign::FindRecord&),
                    void* user) {
        const std::string_view text(pattern);
        const auto slash = text.rfind('\\');
        const auto directory =
            std::string(text.substr(0, slash == std::string_view::npos ? 0 : slash));
        const auto spec = text.substr(slash == std::string_view::npos ? 0 : slash + 1);
        const auto dot = spec.rfind('.');
        const auto wanted = upper(
            dot == std::string_view::npos ? std::string() : std::string(spec.substr(dot + 1))
        );
        const auto folder =
            host_path(*static_cast<const std::filesystem::path*>(context), directory.c_str());
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
            const bool directory_entry = entry.is_directory(error);
            if (!directory_entry && !entry.is_regular_file(error))
                continue;
            auto ext = entry.path().extension().string();
            if (!ext.empty())
                ext.erase(0, 1);
            if (wanted != "*" && upper(ext) != wanted)
                continue;
            // The find record's write time: seconds on the system clock.
            const auto written =
                std::chrono::system_clock::now() +
                std::chrono::duration_cast<std::chrono::system_clock::duration>(
                    entry.last_write_time(error) - std::filesystem::file_time_type::clock::now()
                );
            const auto seconds =
                std::chrono::duration_cast<std::chrono::seconds>(written.time_since_epoch())
                    .count();
            const auto name = entry.path().filename().string();
            const auto size = directory_entry ? 0 : entry.file_size(error);
            visit(
                user,
                {directory_entry ? data::campaign::kFindDirectory : 0U,
                 static_cast<uint32_t>(seconds),
                 static_cast<uint32_t>(size),
                 name.c_str()}
            );
        }
    };
    files.remove = [](void* context, const char* path) {
        std::error_code error;
        return std::filesystem::remove(
            host_path(*static_cast<const std::filesystem::path*>(context), path), error
        );
    };
    files.make_directory = [](void* context, const char* path) {
        std::error_code error;
        std::filesystem::create_directories(
            host_path(*static_cast<const std::filesystem::path*>(context), path), error
        );
    };
    files.read_file = [](void* context, const char* path, std::vector<uint8_t>& bytes) {
        std::ifstream file(
            host_path(*static_cast<const std::filesystem::path*>(context), path), std::ios::binary
        );
        if (!file)
            return false;
        bytes.assign(std::istreambuf_iterator<char>(file), {});
        return true;
    };
    files.write_file = [](void* context, const char* path, std::span<const uint8_t> bytes) {
        std::ofstream file(
            host_path(*static_cast<const std::filesystem::path*>(context), path),
            std::ios::binary | std::ios::trunc
        );
        file.write(
            reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
        );
        return static_cast<bool>(file);
    };
    return files;
}

SaveSummaryReader savegame_persist_reader(const std::filesystem::path* root) {
    SaveSummaryReader reader;
    reader.context = const_cast<std::filesystem::path*>(root);
    reader.open = open_summary_bank;
    reader.get_int = [](void*, void* bank, const char* field, int32_t fallback) {
        return data::persist::bank_get_int(
            static_cast<data::persist::Bank*>(bank), field, fallback
        );
    };
    reader.get_string = [](void*, void* bank, const char* field, char* out, std::size_t capacity) {
        const char* text =
            data::persist::bank_get_text(static_cast<data::persist::Bank*>(bank), field, nullptr);
        if (text == nullptr || capacity == 0)
            return false;
        std::snprintf(out, capacity, "%s", text);
        return true;
    };
    reader.has_field = [](void*, void* bank, const char* field) {
        return data::persist::bank_has_field(static_cast<data::persist::Bank*>(bank), field);
    };
    reader.load_radar = load_summary_radar;
    reader.close = close_summary_bank;
    return reader;
}

} // namespace oa::ui::frontend
