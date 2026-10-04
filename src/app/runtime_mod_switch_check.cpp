// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-mod-switch: the settings' Mods page switches the mod with a soft
// restart. Each run of the check is one turn: it checks that the run plays
// the mod the last turn chose, listed first on the Mods page, then chooses
// the next of No Mod and two test profiles there and answers SWITCH, which
// ends the run; main() builds the next run on the same window and measures
// the working set as each starts.

#include "engine_settings_state.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/app/runtime.hpp"
#include "oa/app/user_folder.hpp"
#include "oa/platform/preferences.hpp"
#include "oa/ui/engine_settings/dialog.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>

namespace oa::app {

namespace settings = oa::ui::engine_settings;

namespace {

/// The switches the check makes before its last run.
constexpr uint32_t kSwitches = 10;

/// One test profile of the check: its folder's name in the player's Mods
/// folder, and its oamod.yaml.
struct TestMod {
    std::string_view folder;  ///< the folder's name
    std::string_view id;      ///< the profile's id
    std::string_view profile; ///< the oamod.yaml's text
};

/// The two test profiles, which change nothing of the game's rules.
constexpr std::array<TestMod, 2> kTestMods{{
    {"switch-check-alpha",
     "switch-check-alpha",
     "oamod: 1\n"
     "id: switch-check-alpha\n"
     "name: Switch Check Alpha\n"
     "version: \"1.0\"\n"
     "description: The first profile the mod switch check plays.\n"
     "requires: {base: ta-3.1c, catalogue: 1}\n"
     "author: {name: unknown}\n"
     "packaging: {revision: 1, date: 2026-10-04, packager: Open Annihilation}\n"},
    {"switch-check-beta",
     "switch-check-beta",
     "oamod: 1\n"
     "id: switch-check-beta\n"
     "name: Switch Check Beta\n"
     "version: \"2.0\"\n"
     "description: The second profile the mod switch check plays.\n"
     "requires: {base: ta-3.1c, catalogue: 1}\n"
     "author: {name: unknown}\n"
     "packaging: {revision: 1, date: 2026-10-04, packager: Open Annihilation}\n"},
}};

/// Throws when a step of the check fails.
///
/// @param ok the step passed
/// @param what what failed
void require(bool ok, std::string_view what) {
    if (!ok)
        throw std::runtime_error("mod switch check: " + std::string(what));
}

/// Returns a folder's path as the settings keep it: absolute, normal, in UTF-8.
///
/// @param folder the folder
/// @return its path
std::string kept_path(const fs::path& folder) {
    std::error_code error;
    const fs::path absolute = fs::absolute(folder, error);
    return path_to_utf8((error ? folder : absolute).lexically_normal());
}

} // namespace

void Runtime::check_mod_switch() {
    require(!user_folder_.empty(), "the run has no folder of the player's own");
    const fs::path mods = user_folder_ / std::string(user_mods_folder_name);
    // The first run writes the test profiles afresh and lists them.
    if (options_.restarts == 0) {
        for (const auto& mod : kTestMods) {
            const fs::path folder = mods / std::string(mod.folder);
            fs::create_directories(folder);
            std::ofstream profile(folder / "oamod.yaml", std::ios::binary | std::ios::trunc);
            profile << mod.profile;
            require(static_cast<bool>(profile), "a test profile could not be written");
        }
        list_offered_mods();
    }
    // The mods take turns in this order: No Mod, then the test profiles.
    std::vector<std::string> turns{std::string()};
    for (const auto& mod : kTestMods)
        turns.push_back(kept_path(mods / std::string(mod.folder)));
    auto& state = engine_settings_state();
    std::size_t playing_turn = turns.size();
    for (std::size_t turn = 0; turn < turns.size(); ++turn)
        if (turns[turn] == state.playing_mod_folder)
            playing_turn = turn;
    require(playing_turn < turns.size(), "the run plays a mod the check did not choose");
    // The run plays the profile of the mod chosen, read afresh at its start.
    const auto* profile = mod_profile();
    if (playing_turn == 0)
        require(
            profile == nullptr ||
                (profile->id != kTestMods[0].id && profile->id != kTestMods[1].id),
            "No Mod plays a test profile"
        );
    else
        require(
            profile != nullptr && profile->id == kTestMods[playing_turn - 1].id,
            "the mod chosen does not play its own profile"
        );
    std::cout << "mod switch check: run " << options_.restarts << " plays "
              << (playing_turn == 0 ? std::string("No Mod") : std::string(profile->id)) << '\n';

    auto& dialog = open_engine_settings_dialog(settings::DialogKind::engine);
    dialog.page = settings::Page::mods;
    const auto rows = settings::mod_rows(dialog);
    require(!rows.empty() && rows.front().playing, "Mods does not list the mod played first");
    const auto offered_of = [&dialog](const std::string& folder) {
        if (folder.empty())
            return settings::no_mod_row;
        for (std::size_t index = 0; index < dialog.mod_folders.size(); ++index)
            if (dialog.mod_folders[index] == folder)
                return static_cast<int32_t>(index);
        return settings::no_question;
    };
    require(
        rows.front().offered == offered_of(turns[playing_turn]),
        "Mods lists another mod first than the one played"
    );

    if (options_.restarts >= kSwitches) {
        // The last run: the next start of the check plays No Mod again.
        std::ignore = take_engine_settings_action(settings::DialogAction::cancelled);
        preference_values_.erase(std::string(settings::key::mod_directory));
        preferences_dirty_ = true;
        std::cout << "mod switch check: " << kSwitches << " switches made\n";
        return;
    }

    // A click on the next mod's row asks the Switch Mod question; on the
    // first run CANCEL leaves everything as it was, and a second click asks
    // again. SWITCH stores the mod and ends the run.
    const std::string& next = turns[(playing_turn + 1) % turns.size()];
    const int32_t wanted = offered_of(next);
    require(wanted != settings::no_question, "Mods does not list the next mod");
    std::size_t row_index = rows.size();
    for (std::size_t index = 0; index < rows.size(); ++index)
        if (rows[index].offered == wanted)
            row_index = index;
    require(row_index < rows.size(), "the next mod has no row");
    const int32_t control = settings::first_row_control + static_cast<int32_t>(row_index);
    const auto click_row = [&] {
        for (const auto& part : settings::dialog_layout(dialog)) {
            if (part.control != control)
                continue;
            const int32_t x = part.rect.x + part.rect.width / 2;
            const int32_t y = part.rect.y + part.rect.height / 2;
            std::ignore = settings::dialog_pointer_move(dialog, x, y);
            std::ignore = settings::dialog_pointer_down(dialog, x, y);
            std::ignore = settings::dialog_pointer_up(dialog, x, y);
            return;
        }
        require(false, "the next mod's row is not shown");
    };
    click_row();
    require(dialog.switch_question == wanted, "a click on a row did not ask to switch to it");
    if (options_.restarts == 0) {
        const std::string before = dialog.chosen.mod_folder;
        require(
            settings::dialog_key(dialog, settings::DialogKey::escape) ==
                    settings::DialogAction::redraw &&
                dialog.switch_question == settings::no_question &&
                dialog.chosen.mod_folder == before,
            "CANCEL changed the Mod setting or left the question"
        );
        click_row();
        require(dialog.switch_question == wanted, "a second click did not ask again");
    }
    const auto action = settings::dialog_key(dialog, settings::DialogKey::enter);
    require(action == settings::DialogAction::switch_mod, "SWITCH did not ask for the switch");
    require(dialog.chosen.mod_folder == next, "SWITCH did not choose the mod");
    std::ignore = take_engine_settings_action(action);
    require(
        soft_restart_requested() && engine_settings_dialog() == nullptr,
        "SWITCH did not close the dialog and end the run"
    );
    const auto stored = preference_values_.find(std::string(settings::key::mod_directory));
    require(
        next.empty() ? stored == preference_values_.end() || stored->second.empty()
                     : stored != preference_values_.end() && stored->second == next,
        "SWITCH did not store the mod"
    );
}

} // namespace oa::app
