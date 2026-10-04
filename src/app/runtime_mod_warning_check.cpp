// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-mod-warning: a mod whose files are missing is switched to and
// plays its menus, and warns of them: its games start without a side's
// missing interface art and font, and cannot start without its units. Each
// run of the check is one turn. The first writes four made-up profiles into
// the player's own Mods folder, and two folders without a profile: one
// whose second side's intgaf and font name files no folder holds, and one
// whose second side lacks its LOGO section, whose start would end and whose
// switch is refused; it switches to the first of those folders. The second
// sees its warning of both files once over the main menu, then plays a
// skirmish on that side, its HUD without those panels and in COMIX; it
// switches to a profile whose SIDEDATA names the same files, and the third
// does the same. The fourth plays the profile whose unit files are missing:
// its warning shows once over the main menu, then over a refused Skirmish
// start that stays on the setup, and a start past the warning fails without
// ending the run, whose warning a finger's tap just under OK closes. The
// fifth plays a profile whose first side's commander has
// its file but the unit catalog drops it, by its Copyright line, and whose
// second side's intgaf names a GAF no folder holds: it finds that commander
// missing, and no other, and that GAF. The sixth plays the profile that
// plays whole: it sees no warning and starts a skirmish.

#include "engine_settings_state.hpp"
#include "touch_state.hpp"
#include "user_folder_state.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/app/runtime.hpp"
#include "oa/app/user_folder.hpp"
#include "oa/data/defs/layout.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/engine_settings/notice.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

/// The profile whose unit files are missing: it reads units from files of
/// an extension no folder holds.
constexpr std::string_view kHollowFolder = "warning-check-hollow";
/// Its oamod.yaml.
constexpr std::string_view kHollowProfile =
    "oamod: 1\n"
    "id: warning-check-hollow\n"
    "name: Warning Check Hollow\n"
    "version: \"1.0\"\n"
    "description: A made-up profile whose unit files are missing.\n"
    "requires: {base: ta-3.1c, catalogue: 1}\n"
    "author: {name: unknown}\n"
    "packaging: {revision: 1, date: 2026-10-04, packager: Open Annihilation}\n"
    "layout:\n"
    "  file-extensions: {unit-definition: HLW}\n";
/// The profile whose first side's commander the unit catalog drops: the
/// game's own units, with that commander's file in the profile's folder
/// carrying a Copyright line of its own, and the game's own SIDEDATA with
/// the second side's intgaf naming kMissingPanels.
constexpr std::string_view kDroppedFolder = "warning-check-dropped";
/// Its oamod.yaml.
constexpr std::string_view kDroppedProfile =
    "oamod: 1\n"
    "id: warning-check-dropped\n"
    "name: Warning Check Dropped\n"
    "version: \"1.0\"\n"
    "description: A made-up profile whose first side's commander is dropped.\n"
    "requires: {base: ta-3.1c, catalogue: 1}\n"
    "author: {name: unknown}\n"
    "packaging: {revision: 1, date: 2026-10-04, packager: Open Annihilation}\n";
/// The GAF the dropped profile's and the side art folders' second side
/// names for its panels, which no folder holds.
constexpr std::string_view kMissingPanels = "WARNCHECKINT";
/// The font the side art folders' second side names, which no folder holds.
constexpr std::string_view kMissingFont = "WARNCHECKFNT";
/// A folder without a profile: the game's own SIDEDATA with the second
/// side's intgaf naming kMissingPanels and its font kMissingFont.
constexpr std::string_view kSidelessFolder = "warning-check-sideless";
/// The profile whose SIDEDATA is the folder without a profile's: the game's
/// own units.
constexpr std::string_view kSideArtFolder = "warning-check-sideart";
/// Its oamod.yaml.
constexpr std::string_view kSideArtProfile =
    "oamod: 1\n"
    "id: warning-check-sideart\n"
    "name: Warning Check Side Art\n"
    "version: \"1.0\"\n"
    "description: A made-up profile whose second side's art and font are missing.\n"
    "requires: {base: ta-3.1c, catalogue: 1}\n"
    "author: {name: unknown}\n"
    "packaging: {revision: 1, date: 2026-10-04, packager: Open Annihilation}\n";
/// Match ticks a skirmish over the side art folders plays.
constexpr uint32_t kSideArtTicks = 300;
/// Source pixels Mods is scrolled by at a time to bring a row into view.
constexpr int32_t kScrollStep = 4;
/// The most times it is scrolled.
constexpr int32_t kScrollSteps = 256;
/// A folder without a profile: the game's own SIDEDATA without the second
/// side's LOGO section.
constexpr std::string_view kLogolessFolder = "warning-check-logoless";
/// The profile that plays whole: the game's own units.
constexpr std::string_view kWholeFolder = "warning-check-whole";
/// Its oamod.yaml.
constexpr std::string_view kWholeProfile =
    "oamod: 1\n"
    "id: warning-check-whole\n"
    "name: Warning Check Whole\n"
    "version: \"1.0\"\n"
    "description: A made-up profile that plays whole.\n"
    "requires: {base: ta-3.1c, catalogue: 1}\n"
    "author: {name: unknown}\n"
    "packaging: {revision: 1, date: 2026-10-04, packager: Open Annihilation}\n";

/// The touch device the check's finger comes from, which no system registers.
constexpr SDL_TouchID kCheckTouchDevice = 0x70c5;

/// Where the pointer rests while nothing is clicked: off every button.
constexpr oa::ui::display_layout::Point kRestingPointer{4, 240};
/// The window's size for the snapshots: a 16:9 window.
constexpr int kSnapshotWidth = 1280;
/// The window's height for the snapshots.
constexpr int kSnapshotHeight = 720;

/// Throws when a step of the check fails.
///
/// @param ok the step passed
/// @param what what failed
void require(bool ok, std::string_view what) {
    if (!ok)
        throw std::runtime_error("mod warning check: " + std::string(what));
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

/// Returns SIDEDATA's text with the value of a key of SIDE1's section
/// replaced: the first key of that name after the section's head, before
/// any of its own sections.
///
/// @param sidedata the text
/// @param key the key, in lower case
/// @param value the new value
/// @return the text
std::string
name_second_side_file(std::string sidedata, std::string_view key, std::string_view value) {
    std::string folded = sidedata;
    for (char& c : folded)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto section = folded.find("[side1]");
    auto at = section;
    std::size_t equals = std::string::npos;
    // A whole key: after a space, a line's end, a brace or a semicolon, and
    // followed by its '=', so that font does not match fontgui.
    while (at != std::string::npos && equals == std::string::npos) {
        at = folded.find(key, at + 1);
        if (at == std::string::npos)
            break;
        const char before = folded[at - 1];
        const auto next = folded.find_first_not_of(" \t", at + key.size());
        if ((std::isspace(static_cast<unsigned char>(before)) != 0 || before == '{' ||
             before == ';') &&
            next != std::string::npos && folded[next] == '=')
            equals = next;
    }
    const auto end = equals == std::string::npos ? equals : folded.find(';', equals);
    require(
        end != std::string::npos, "the game's SIDEDATA names no " + std::string(key) + " for SIDE1"
    );
    return sidedata.replace(equals + 1, end - equals - 1, value);
}

/// Returns SIDEDATA's text without SIDE1's LOGO section.
///
/// @param sidedata the text
/// @return the text
std::string drop_second_side_logo(std::string sidedata) {
    std::string folded = sidedata;
    for (char& c : folded)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto section = folded.find("[side1]");
    const auto logo = section == std::string::npos ? section : folded.find("[logo]", section);
    const auto end = logo == std::string::npos ? logo : folded.find('}', logo);
    require(end != std::string::npos, "the game's SIDEDATA has no LOGO for SIDE1");
    return sidedata.erase(logo, end + 1 - logo);
}

/// Writes a profile into its folder, made when missing.
///
/// @param folder the folder
/// @param profile the oamod.yaml's text
void write_profile(const fs::path& folder, std::string_view profile) {
    fs::create_directories(folder);
    std::ofstream file(folder / "oamod.yaml", std::ios::binary | std::ios::trunc);
    file << profile;
    require(static_cast<bool>(file), "a test profile could not be written");
}

} // namespace

void Runtime::check_mod_warning() {
    require(!user_folder_.empty(), "the run has no folder of the player's own");
    require(sdl_.window != nullptr, "the check needs the SDL presenter");
    const fs::path mods = user_folder_ / std::string(user_mods_folder_name);
    const std::string hollow = kept_path(mods / std::string(kHollowFolder));
    const std::string dropped = kept_path(mods / std::string(kDroppedFolder));
    const std::string whole = kept_path(mods / std::string(kWholeFolder));
    const std::string sideless = kept_path(mods / std::string(kSidelessFolder));
    const std::string sideart = kept_path(mods / std::string(kSideArtFolder));
    // The first side and its commander, which every profile here takes from
    // the game's own SIDEDATA.
    const std::string commander(
        side_table_.sides[0].commander,
        ::strnlen(side_table_.sides[0].commander, sizeof side_table_.sides[0].commander)
    );
    const std::string side(
        side_table_.sides[0].name,
        ::strnlen(side_table_.sides[0].name, sizeof side_table_.sides[0].name)
    );
    const auto commander_line = [&side, &commander] {
        return "The " + side + " commander (" + commander + ") isn't in this mod's units.";
    };
    auto& settings_state = engine_settings_state();
    auto& state = user_folder_state();
    // The notices show although nobody watches the check.
    state.check_shows_notice = true;

    // The window's frame of a step, when --snapshot names a file.
    if (!options_.snapshot.empty() &&
        (!SDL_SetWindowSize(sdl_.window, kSnapshotWidth, kSnapshotHeight) ||
         !SDL_SyncWindow(sdl_.window)))
        throw std::runtime_error(std::string("SDL_SetWindowSize: ") + SDL_GetError());
    const auto snapshot = [this](std::string_view step) {
        if (options_.snapshot.empty())
            return;
        send_check_pointer(SDL_EVENT_MOUSE_MOTION, kRestingPointer, 0);
        renderer::Surface presented;
        capture_frame_ = &presented;
        render();
        capture_frame_ = nullptr;
        auto stem = options_.snapshot;
        stem.replace_extension();
        write_ppm(fs::path(stem.string() + "-" + std::string(step) + ".ppm"), presented);
    };
    const auto key = [this](SDL_EventType type, SDL_Keycode code) {
        SDL_Event event{};
        event.type = type;
        event.key.windowID = SDL_GetWindowID(sdl_.window);
        event.key.key = code;
        event.key.scancode = SDL_GetScancodeFromKey(code, nullptr);
        event.key.down = type == SDL_EVENT_KEY_DOWN;
        bool running = true;
        dispatch_event(event, running);
        return running;
    };
    // A press and a release of a key that closes the warning, which stays on
    // the screen it showed over.
    const auto close_with = [&](SDL_Keycode code, Screen screen, std::string_view where) {
        require(key(SDL_EVENT_KEY_DOWN, code), "a key on the warning ended the run");
        require(!saves_notice_shown(), "a key did not close the warning");
        require(key(SDL_EVENT_KEY_UP, code), "a key's release ended the run");
        require(screen_ == screen, std::string("closing the warning left ") + std::string(where));
    };
    // A finger's tap through the touch dispatcher just under the warning's
    // OK, within a finger's reach of it, which closes the warning.
    const auto tap_under_ok = [&](Screen screen, std::string_view where) {
        const auto& shown = *state.notice;
        const auto* fonts = engine_settings_fonts();
        const int32_t height = settings::notice_height(shown, fonts);
        const auto placement = UserFolderState::notice_placement(height);
        const int32_t reach = EngineSettingsState::finger_reach(*this, 1.0);
        float x = 0.0F;
        float y = 0.0F;
        for (const auto& part : settings::notice_layout(shown, fonts))
            if (part.control == settings::notice_ok_control) {
                x = static_cast<float>(placement.x + part.rect.x + part.rect.width / 2);
                y = static_cast<float>(
                    placement.y + part.rect.y + part.rect.height - 1 + std::max(1, reach * 2 / 3)
                );
            }
        require(
            SDL_RenderCoordinatesToWindow(sdl_.renderer, x, y, &x, &y),
            "a finger could not be placed on the window"
        );
        int width = 0;
        int window_height = 0;
        require(
            SDL_GetWindowSize(sdl_.window, &width, &window_height) && width > 0 &&
                window_height > 0,
            "the window has no size"
        );
        touch_state().dispatch.accept_unregistered_touch = true;
        for (const SDL_EventType type : {SDL_EVENT_FINGER_DOWN, SDL_EVENT_FINGER_UP}) {
            SDL_Event event{};
            event.type = type;
            event.tfinger.touchID = kCheckTouchDevice;
            event.tfinger.fingerID = 1;
            event.tfinger.x = x / static_cast<float>(width);
            event.tfinger.y = y / static_cast<float>(window_height);
            event.tfinger.pressure = type == SDL_EVENT_FINGER_UP ? 0.0F : 1.0F;
            event.tfinger.windowID = SDL_GetWindowID(sdl_.window);
            bool running = true;
            dispatch_event(event, running);
            require(running, "a finger on the warning ended the run");
        }
        require(!saves_notice_shown(), "a finger just under OK did not close the warning");
        require(screen_ == screen, std::string("closing the warning left ") + std::string(where));
    };
    // SWITCH on a mod's row of the Mods page.
    const auto press_switch = [&](const std::string& folder) {
        list_offered_mods();
        auto& dialog = open_engine_settings_dialog(settings::DialogKind::engine);
        dialog.page = settings::Page::mods;
        int32_t wanted = settings::no_question;
        for (std::size_t index = 0; index < dialog.mod_folders.size(); ++index)
            if (dialog.mod_folders[index] == folder)
                wanted = static_cast<int32_t>(index);
        require(wanted != settings::no_question, "Mods does not list the test profile");
        const auto rows = settings::mod_rows(dialog);
        int32_t control = settings::no_question;
        for (std::size_t index = 0; index < rows.size(); ++index)
            if (rows[index].offered == wanted)
                control = settings::first_row_control + static_cast<int32_t>(index);
        // A row below the section's view is scrolled to, a few pixels at a
        // time, until it lies wholly in the view.
        bool clicked = false;
        auto& scrolled = dialog.scroll[static_cast<std::size_t>(dialog.page)];
        for (int32_t step = 0; step < kScrollSteps && !clicked; ++step, scrolled += kScrollStep)
            for (const auto& part : settings::dialog_layout(dialog)) {
                if (part.control != control)
                    continue;
                const int32_t x = part.rect.x + part.rect.width / 2;
                const int32_t y = part.rect.y + part.rect.height / 2;
                std::ignore = settings::dialog_pointer_move(dialog, x, y);
                std::ignore = settings::dialog_pointer_down(dialog, x, y);
                std::ignore = settings::dialog_pointer_up(dialog, x, y);
                clicked = true;
            }
        require(clicked && dialog.switch_question == wanted, "a click did not ask to switch");
        const auto action = settings::dialog_key(dialog, settings::DialogKey::enter);
        require(action == settings::DialogAction::switch_mod, "SWITCH did not ask for the switch");
        std::ignore = take_engine_settings_action(action);
    };
    // SWITCH, which ends the run.
    const auto switch_to = [&](const std::string& folder) {
        press_switch(folder);
        require(soft_restart_requested(), "SWITCH did not end the run");
    };

    // The second side's name and the files the side art folders' second side
    // names.
    const std::string second(
        side_table_.sides[1].name,
        ::strnlen(side_table_.sides[1].name, sizeof side_table_.sides[1].name)
    );
    const std::string panels = "anims/" + std::string(kMissingPanels) + ".GAF";
    const std::string font = "fonts/" + std::string(kMissingFont) + ".FNT";
    const auto side_file_line = [&second](const std::string& path) {
        return "The " + second + " side's " + path +
               " isn't in this mod's files; games show without it.";
    };
    // A side art folder's turn: its warning names both files once over the
    // main menu and says its games still start; a skirmish on the second
    // side starts from Skirmish's Start and plays, its HUD without the
    // side's panels and its numbers in COMIX, unmeasured.
    const auto play_side_art = [&](std::string_view name,
                                   const std::string& folder,
                                   std::string_view step) {
        const ModStartGaps gaps = mod_start_gaps();
        require(
            !gaps.games_cannot_start() && gaps.missing_side_files.size() == 2 &&
                gaps.missing_side_files[0].side == second &&
                gaps.missing_side_files[0].path == panels &&
                gaps.missing_side_files[1].side == second &&
                gaps.missing_side_files[1].path == font,
            "the second side's missing panels and font, and they alone, are not found missing"
        );
        const auto previous_tick = fake_frontend_tick_;
        fake_frontend_tick_ = 1000U;
        load(Screen::main_menu);
        require(engine_settings_fonts() != nullptr, "the dialog's fonts did not load");
        send_check_pointer(SDL_EVENT_MOUSE_MOTION, kRestingPointer, 0);
        for (int pass = 0; pass < 4; ++pass)
            tell_incomplete_mod();
        require(
            saves_notice_shown() && state.mod_warnings_shown == 1 &&
                state.notice_screen == Screen::main_menu,
            "the side files' warning did not show over the main menu"
        );
        const auto& notice = *state.notice;
        require(
            notice.title == "MOD FILES MISSING" && notice.open_caption == "OPEN MOD FOLDER" &&
                notice.paragraphs.size() == 5 &&
                notice.paragraphs[0].text == std::string(name) + " is missing files." &&
                notice.paragraphs[1].text == side_file_line(panels) &&
                notice.paragraphs[2].text == side_file_line(font) &&
                notice.paragraphs[3].text ==
                    "Its games still start. Add the missing files to its folder to show them:" &&
                notice.paragraphs[4].path && notice.paragraphs[4].text == folder,
            "the warning does not name the second side's panels and font, say that games still "
            "start and show the mod's folder"
        );
        snapshot(std::string(step) + "-warning");
        close_with(SDLK_RETURN, Screen::main_menu, "the main menu");
        for (int pass = 0; pass < 4; ++pass)
            tell_incomplete_mod();
        require(!saves_notice_shown() && state.mod_warnings_shown == 1, "the warning showed again");
        exercise_click(menu::resource_name(menu::Button::single_player));
        exercise_click(entry::resource_name(entry::Button::skirmish));
        require(screen_ == Screen::skirmish, "Skirmish did not open its setup");
        // The player plays the second side, the computer the first.
        skirmish_settings_.slots[0].side = 1;
        skirmish_settings_.slots[1].side = 0;
        exercise_click(skirmish::resource_name(skirmish::Button::start));
        require(
            screen_ == Screen::match && match_ && !saves_notice_shown() &&
                state.mod_warnings_shown == 1,
            "the skirmish did not start, or warned"
        );
        require(
            match_view_side() == 1 && match_hud_.has_value() && match_side_panel_gaf().empty() &&
                match_side_font().empty() && !match_side_names_font_,
            "the second side's HUD is not drawn without its panels and in COMIX, unmeasured"
        );
        for (uint32_t tick = 0; tick < kSideArtTicks; ++tick)
            step_match_simulation();
        render();
        snapshot(std::string(step) + "-match");
        require(
            screen_ == Screen::match && match_ && match_->simulation().tick >= kSideArtTicks,
            "the skirmish did not play its ticks"
        );
        leave_match();
        fake_frontend_tick_ = previous_tick;
        std::cout << "mod warning check: " << name
                  << " warned once of the second side's panels and font, and its skirmish on "
                     "that side played "
                  << kSideArtTicks << " ticks\n";
    };

    if (options_.restarts == 0) {
        // No mod plays and nothing warns.
        write_profile(mods / std::string(kHollowFolder), kHollowProfile);
        write_profile(mods / std::string(kDroppedFolder), kDroppedProfile);
        write_profile(mods / std::string(kSideArtFolder), kSideArtProfile);
        write_profile(mods / std::string(kWholeFolder), kWholeProfile);
        // The dropped profile's commander file, whose Copyright line is not
        // the one the catalog keeps a unit for.
        const fs::path units = mods / std::string(kDroppedFolder) /
                               oa::data::defs::directory_name(oa::data::defs::DataDirectory::units);
        fs::create_directories(units);
        std::ofstream unit(
            units / (commander + "." + oa::data::defs::unit_extension()),
            std::ios::binary | std::ios::trunc
        );
        unit << "[UNITINFO]\r\n{\r\n\tUnitName=" << commander
             << ";\r\n\tVersion=1.0;\r\n\tCopyright=A made-up line;\r\n}\r\n";
        require(static_cast<bool>(unit), "the dropped profile's unit file could not be written");
        unit.close();
        // Its SIDEDATA, whose second side's panels are missing, and the side
        // art folders', whose second side's panels and font are missing.
        const auto read = assets_.read(
            oa::data::defs::data_path(oa::data::defs::DataDirectory::gamedata, "sidedata.tdf")
        );
        const std::string sidedata(read.bytes.begin(), read.bytes.end());
        const std::string named = name_second_side_file(sidedata, "intgaf", kMissingPanels);
        const std::string named_both = name_second_side_file(named, "font", kMissingFont);
        const auto write_sidedata = [&mods](std::string_view folder, const std::string& text) {
            const fs::path gamedata =
                mods / std::string(folder) /
                oa::data::defs::directory_name(oa::data::defs::DataDirectory::gamedata);
            fs::create_directories(gamedata);
            std::ofstream sides(gamedata / "SIDEDATA.TDF", std::ios::binary | std::ios::trunc);
            sides << text;
            require(static_cast<bool>(sides), "a test folder's SIDEDATA could not be written");
        };
        write_sidedata(kDroppedFolder, named);
        write_sidedata(kSideArtFolder, named_both);
        write_sidedata(kSidelessFolder, named_both);
        require(mod_profile() == nullptr && !plays_mod(), "the first run plays a mod");
        require(!mod_start_gaps().any(), "the game played without a mod warns");
        // The folder without a profile whose start would end on its second
        // side's missing LOGO is refused: the page says why and the dialog
        // stays open on the mod played. It is listed alone, and goes once
        // refused, so that the page shows its row.
        write_sidedata(kLogolessFolder, drop_second_side_logo(sidedata));
        press_switch(kept_path(mods / std::string(kLogolessFolder)));
        require(
            !soft_restart_requested() && settings_state.dialog &&
                settings_state.dialog->chosen.mod_folder == settings_state.current.mod_folder &&
                settings_state.dialog->folder_notice ==
                    oa::data::languages::interface_text(
                        "That folder cannot be played; the log says why."
                    ),
            "the switch to " + std::string(kLogolessFolder) + " was not refused"
        );
        std::ignore = take_engine_settings_action(
            settings::dialog_key(*settings_state.dialog, settings::DialogKey::escape)
        );
        require(!settings_state.dialog, "Escape did not close the refused switch's dialog");
        fs::remove_all(mods / std::string(kLogolessFolder));
        // The one whose side files alone are missing is not.
        switch_to(sideless);
        std::cout << "mod warning check: refused the folder whose second side's LOGO is "
                     "missing, and switched to the one whose second side's panels and font are "
                     "missing\n";
        return;
    }

    if (options_.restarts == 1) {
        require(
            settings_state.playing_mod_folder == sideless && mod_profile() == nullptr &&
                plays_mod(),
            "the switch to the folder without a profile did not play it"
        );
        play_side_art("This mod", sideless, "sideless");
        switch_to(sideart);
        return;
    }

    if (options_.restarts == 2) {
        const auto* profile = mod_profile();
        require(
            settings_state.playing_mod_folder == sideart && profile != nullptr &&
                profile->id == kSideArtFolder,
            "the switch to the side art profile did not play it"
        );
        play_side_art("Warning Check Side Art", sideart, "sideart");
        switch_to(hollow);
        return;
    }

    if (options_.restarts == 3) {
        const auto* profile = mod_profile();
        require(
            settings_state.playing_mod_folder == hollow && profile != nullptr &&
                profile->id == kHollowFolder,
            "the switch to the hollow profile did not play it"
        );
        const ModStartGaps gaps = mod_start_gaps();
        require(
            gaps.no_units && gaps.missing_commanders.size() == side_table_.count,
            "the hollow profile's missing units and commanders are not found"
        );
        // The main menu shows; the warning shows once over it.
        const auto previous_tick = fake_frontend_tick_;
        fake_frontend_tick_ = 1000U;
        load(Screen::main_menu);
        require(screen_ == Screen::main_menu, "the main menu did not open");
        require(engine_settings_fonts() != nullptr, "the dialog's fonts did not load");
        send_check_pointer(SDL_EVENT_MOUSE_MOTION, kRestingPointer, 0);
        for (int pass = 0; pass < 4; ++pass)
            tell_incomplete_mod();
        require(
            saves_notice_shown() && state.mod_warnings_shown == 1 &&
                state.notice_screen == Screen::main_menu,
            "the warning did not show over the main menu"
        );
        const auto& notice = *state.notice;
        require(
            notice.title == "MOD FILES MISSING" && notice.open_caption == "OPEN MOD FOLDER" &&
                notice.paragraphs.front().text == "Warning Check Hollow is missing files." &&
                notice.paragraphs[2].text == commander_line() && notice.paragraphs.back().path &&
                notice.paragraphs.back().text == hollow,
            "the warning does not name the mod, its first side's commander and its folder"
        );
        snapshot("main-menu");
        // OPEN MOD FOLDER shows the mod's folder and the warning stays.
        const auto* fonts = engine_settings_fonts();
        const int32_t height = settings::notice_height(notice, fonts);
        const auto placement = UserFolderState::notice_placement(height);
        oa::ui::display_layout::Point open_point{};
        for (const auto& part : settings::notice_layout(notice, fonts))
            if (part.control == settings::notice_open_control)
                open_point = {
                    placement.x + part.rect.x + part.rect.width / 2,
                    placement.y + part.rect.y + part.rect.height / 2
                };
        state.opened.clear();
        send_check_pointer(SDL_EVENT_MOUSE_MOTION, open_point, 0);
        send_check_pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, open_point, SDL_BUTTON_LEFT);
        send_check_pointer(SDL_EVENT_MOUSE_BUTTON_UP, open_point, SDL_BUTTON_LEFT);
        require(
            saves_notice_shown() && state.opened == std::vector<fs::path>{path_from_utf8(hollow)},
            "OPEN MOD FOLDER did not show the mod's folder, or closed the warning"
        );
        // Escape closes it, and its release never reaches the main menu,
        // whose Escape ends the program.
        close_with(SDLK_ESCAPE, Screen::main_menu, "the main menu");
        // Once from each start: it shows no more over the main menu.
        for (int pass = 0; pass < 4; ++pass)
            tell_incomplete_mod();
        require(!saves_notice_shown() && state.mod_warnings_shown == 1, "the warning showed again");
        std::cout << "mod warning check: the warning showed once over the main menu, showed the "
                     "mod's folder and closed on Escape\n";

        // Skirmish's Start shows it again and the setup stays.
        exercise_click(menu::resource_name(menu::Button::single_player));
        exercise_click(entry::resource_name(entry::Button::skirmish));
        require(screen_ == Screen::skirmish, "Skirmish did not open its setup");
        exercise_click(skirmish::resource_name(skirmish::Button::start));
        require(
            screen_ == Screen::skirmish && !match_ && saves_notice_shown() &&
                state.notice_screen == Screen::skirmish && state.mod_warnings_shown == 2,
            "Start did not warn and stay on the skirmish setup"
        );
        snapshot("skirmish");
        close_with(SDLK_RETURN, Screen::skirmish, "the skirmish setup");
        // A start past the warning fails without ending the run: the
        // missing commander stops it, and the setup comes back, warning again.
        const auto* start = widget(skirmish::resource_name(skirmish::Button::start));
        require(start != nullptr, "the skirmish setup has no Start");
        const auto origin = panel_origin();
        const float x = static_cast<float>(origin.x + start->common.x) +
                        static_cast<float>(start->common.width) / 2.0F;
        const float y = static_cast<float>(origin.y + start->common.y) +
                        static_cast<float>(start->common.height) / 2.0F;
        update_pointer(x, y);
        selected_ = hovered_ ? static_cast<int32_t>(*hovered_) : -1;
        update_pointer(x, y);
        require(
            !start_skirmish_from_setup(entry::Event{{kFrontendMenuHandle}, 0}) && !match_ &&
                screen_ == Screen::skirmish && saves_notice_shown() &&
                state.mod_warnings_shown == 3,
            "a failed start did not come back to the skirmish setup with the warning"
        );
        selected_ = -1;
        tap_under_ok(Screen::skirmish, "the skirmish setup");
        std::cout << "mod warning check: Skirmish's Start warned and stayed on the setup; a start "
                     "past the warning came back to it, and a finger just under OK closed it\n";
        fake_frontend_tick_ = previous_tick;
        switch_to(dropped);
        return;
    }

    if (options_.restarts == 4) {
        // A commander whose file is there but whose unit the catalog drops
        // is missing all the same; the other side's, which it keeps, is not.
        const auto* profile = mod_profile();
        require(
            settings_state.playing_mod_folder == dropped && profile != nullptr &&
                profile->id == kDroppedFolder,
            "the switch to the dropped profile did not play it"
        );
        const ModStartGaps gaps = mod_start_gaps();
        require(
            !gaps.no_units && gaps.missing_commanders.size() == 1 &&
                gaps.missing_commanders.front().commander == commander,
            "the dropped commander, and it alone, is not found missing"
        );
        // The second side's panels are missing, and nothing else a side names.
        require(
            gaps.missing_side_files.size() == 1 && gaps.missing_side_files.front().side == second &&
                gaps.missing_side_files.front().path == panels,
            "the second side's missing panels, and they alone, are not found missing"
        );
        const auto previous_tick = fake_frontend_tick_;
        fake_frontend_tick_ = 1000U;
        load(Screen::main_menu);
        send_check_pointer(SDL_EVENT_MOUSE_MOTION, kRestingPointer, 0);
        for (int pass = 0; pass < 4; ++pass)
            tell_incomplete_mod();
        require(
            saves_notice_shown() && state.mod_warnings_shown == 1 &&
                state.notice->paragraphs.size() > 2 &&
                state.notice->paragraphs[1].text == commander_line() &&
                state.notice->paragraphs[2].text == side_file_line(panels) &&
                state.notice->paragraphs[3].text ==
                    "Its games can't start until the mod's files are added to its folder:",
            "the warning of the dropped commander and the missing panels did not show over the "
            "main menu"
        );
        close_with(SDLK_RETURN, Screen::main_menu, "the main menu");
        std::cout << "mod warning check: a commander whose unit the catalog drops and a side's "
                     "missing panels warned as missing, and nothing else\n";
        fake_frontend_tick_ = previous_tick;
        switch_to(whole);
        return;
    }

    // The profile that plays whole warns of nothing, and its skirmish starts.
    const auto* profile = mod_profile();
    require(
        settings_state.playing_mod_folder == whole && profile != nullptr &&
            profile->id == kWholeFolder,
        "the switch to the whole profile did not play it"
    );
    require(!mod_start_gaps().any(), "the whole profile has gaps");
    load(Screen::main_menu);
    for (int pass = 0; pass < 4; ++pass)
        tell_incomplete_mod();
    require(!saves_notice_shown() && state.mod_warnings_shown == 0, "the whole profile warned");
    exercise_click(menu::resource_name(menu::Button::single_player));
    exercise_click(entry::resource_name(entry::Button::skirmish));
    exercise_click(skirmish::resource_name(skirmish::Button::start));
    require(screen_ == Screen::match && match_, "the whole profile's skirmish did not start");
    require(!saves_notice_shown(), "the whole profile's skirmish warned");
    leave_match();
    // The next start of the check plays No Mod again.
    preference_values_.erase(std::string(settings::key::mod_directory));
    preferences_dirty_ = true;
    std::cout << "mod warning check: the whole profile warned of nothing and started its "
                 "skirmish\n";
}

} // namespace oa::app
