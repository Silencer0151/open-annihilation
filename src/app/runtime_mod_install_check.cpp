// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-mod-install: made-up mod packages installed through the game,
// each question answered through the prompt's overlay by keys and the
// pointer, and the player's Mods folder checked after each. Each run of
// the check is one turn. The first: the command line's missing package
// refused; a package the Finder made installed with no question from a
// dropped file; an update asked and cancelled, then replaced; a third
// revision replacing, keeping one .backup; a reinstall dropping a file
// added since; an older revision declined; another version installed
// alongside; a folder that holds another mod left as it is; a profile that
// does not resolve refused; ROLL BACK twice on the Mods page, none offered
// for a kept version that cannot be played, and one that became so refused
// in a prompt over the dialog; then PLAY NOW. The second plays
// the mod and replaces it, the change waiting for the run to end; the third
// plays the new revision, tells of the update and rolls the mod played
// back; the fourth plays the rolled back revision, tells of it, and leaves
// the folder as a stop after a replace's first rename would; the fifth
// finds the mod played all the same, put back before its folder was
// resolved.

#include "engine_settings_state.hpp"
#include "mod_install_state.hpp"
#include "user_folder_state.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/app/mod_install.hpp"
#include "oa/app/mod_install/inbox.hpp"
#include "oa/app/runtime.hpp"
#include "oa/app/user_folder.hpp"
#include "oa/formats/zip.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/engine_settings/prompt.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>

namespace oa::app {

namespace settings = oa::ui::engine_settings;
namespace install = oa::app::mod_install;

namespace {

/// The made-up mod the check installs, and its folder.
constexpr std::string_view kModId = "example-mod";
/// The made-up mod whose folder name another folder holds.
constexpr std::string_view kOtherId = "other-mod";
/// A made-up mod whose kept version names a hack no build knows.
constexpr std::string_view kBrokenId = "broken-mod";
/// The most frames a step of the check runs before it gives up.
constexpr int kMostFrames = 400;
/// The window's size for the snapshots.
constexpr int kSnapshotWidth = 1280;
/// The window's height for the snapshots.
constexpr int kSnapshotHeight = 720;
/// Where the pointer rests while nothing is clicked: off every button.
constexpr oa::ui::display_layout::Point kRestingPointer{4, 240};

/// Throws when a step of the check fails.
///
/// @param ok the step passed
/// @param what what failed
void require(bool ok, std::string_view what) {
    if (!ok)
        throw std::runtime_error("mod install check: " + std::string(what));
}

/// Returns a made-up profile.
///
/// @param id its id
/// @param name its name
/// @param version its version
/// @param revision its packaging revision
/// @param more lines after it
/// @return its text
std::string profile_text(
    std::string_view id,
    std::string_view name,
    std::string_view version,
    int revision,
    std::string_view more = {}
) {
    return "oamod: 1\nid: " + std::string(id) + "\nname: " + std::string(name) + "\nversion: \"" +
           std::string(version) +
           "\"\ndescription: A made-up mod the install check installs.\n"
           "requires: {base: ta-3.1c, catalogue: 1}\n"
           "author: {name: unknown}\n"
           "packaging: {revision: " +
           std::to_string(revision) + ", date: 2026-10-04, packager: Open Annihilation}\n" +
           std::string(more);
}

/// Writes a package of stored entries.
///
/// @param file the package
/// @param entries each entry's name and text
void write_package(
    const fs::path& file, const std::vector<std::pair<std::string, std::string>>& entries
) {
    std::vector<std::vector<uint8_t>> data;
    data.reserve(entries.size());
    std::vector<oa::formats::zip::NewEntry> listed;
    for (const auto& [name, text] : entries) {
        data.emplace_back(text.begin(), text.end());
        listed.push_back({name, data.back()});
    }
    std::vector<uint8_t> archive;
    oa::formats::zip::ZipError error{};
    require(oa::formats::zip::write_archive(listed, archive, error), "a package could not be made");
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(
        reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size())
    );
    require(static_cast<bool>(out), "a package could not be written");
}

/// Writes a package of the made-up mod with its profile at its top.
///
/// @param folder where packages go
/// @param version its version
/// @param revision its revision
/// @return the package
fs::path mod_package(const fs::path& folder, std::string_view version, int revision) {
    const fs::path file = folder / ("example-mod-" + std::string(version) + "-r" +
                                    std::to_string(revision) + ".oamod");
    write_package(
        file,
        {{"oamod.yaml", profile_text(kModId, "Example Mod", version, revision)},
         {"units/", ""},
         {"units/readme.txt", "revision " + std::to_string(revision)}}
    );
    return file;
}

/// Returns the revision a folder's profile gives; -1 when it holds no mod.
///
/// @param folder the folder
/// @return the revision
int64_t revision_in(const fs::path& folder) {
    const auto held = install::read_installed_mod(folder);
    return held.kind == install::FolderKind::mod ? held.revision : -1;
}

/// Returns the version a folder's profile gives; empty when it holds no mod.
///
/// @param folder the folder
/// @return the version
std::string version_in(const fs::path& folder) {
    return install::read_installed_mod(folder).version;
}

/// Tells whether the Mods folder holds a folder an install keeps while it works.
///
/// @param mods the Mods folder
/// @return true when one is there
bool reserved_left(const fs::path& mods) {
    std::error_code error;
    for (fs::directory_iterator entry{mods, error}, end; !error && entry != end;
         entry.increment(error)) {
        const std::string name = path_to_utf8(entry->path().filename());
        if (name.starts_with(install::reserved_prefix) && name != install::lock_file_name)
            return true;
    }
    return false;
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

void Runtime::check_mod_install() {
    require(!user_folder_.empty(), "the run has no folder of the player's own");
    require(sdl_.window != nullptr, "the check needs the SDL presenter");
    const fs::path mods = user_folder_ / std::string(user_mods_folder_name);
    const fs::path packages = user_folder_.parent_path() / "files";
    const fs::path folder = mods / std::string(kModId);
    const fs::path kept = folder / std::string(install::backup_folder_name);
    auto& state = mod_install_state();
    state.check_shows_prompts = true;
    std::ignore = user_folder_state();
    fake_frontend_tick_ = 1000U;
    load(Screen::main_menu);
    require(engine_settings_fonts() != nullptr, "the dialog's fonts did not load");
    send_check_pointer(SDL_EVENT_MOUSE_MOTION, kRestingPointer, 0);

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
    // Frames over the main menu until a prompt that asks or tells shows.
    const auto until_prompt = [&](std::string_view what) {
        for (int frame = 0; frame < kMostFrames; ++frame) {
            tell_mod_installs();
            if (state.shown && (state.stage == ModInstallState::Stage::asking ||
                                state.stage == ModInstallState::Stage::telling))
                return;
        }
        require(false, std::string("no prompt showed: ") + std::string(what));
    };
    // Frames until the folders a change dropped are deleted.
    const auto until_deleted = [&] {
        for (int frame = 0; frame < kMostFrames && state.discarder.busy(); ++frame)
            tell_mod_installs();
        require(!state.discarder.busy(), "the folders a change dropped were not deleted");
    };
    const auto says = [&](std::string_view part) {
        if (!state.shown)
            return false;
        for (const auto& paragraph : state.shown->prompt.paragraphs)
            if (paragraph.text.find(part) != std::string::npos)
                return true;
        return false;
    };
    const auto titled = [&](std::string_view title) {
        return state.shown && state.shown->prompt.title == title;
    };
    // A key's press and release, through the prompt's overlay.
    const auto key = [this](SDL_Keycode code) {
        for (const SDL_EventType type : {SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP}) {
            SDL_Event event{};
            event.type = type;
            event.key.windowID = SDL_GetWindowID(sdl_.window);
            event.key.key = code;
            event.key.scancode = SDL_GetScancodeFromKey(code, nullptr);
            event.key.down = type == SDL_EVENT_KEY_DOWN;
            bool running = true;
            dispatch_event(event, running);
            require(running, "a key on a prompt ended the run");
        }
    };
    // A click on the button that answers, through the prompt's overlay.
    const auto click = [&](install::Answer answer) {
        require(state.shown.has_value(), "no prompt to answer");
        const auto& prompt = state.shown->prompt;
        const auto& answers = state.shown->answers;
        const auto found = std::find(answers.begin(), answers.end(), answer);
        require(found != answers.end(), "the prompt has no such button");
        const auto button = static_cast<int32_t>(found - answers.begin());
        const auto* fonts = engine_settings_fonts();
        const auto placement =
            ModInstallState::prompt_placement(settings::prompt_height(prompt, fonts));
        for (const auto& part : settings::prompt_layout(prompt, fonts))
            if (part.control == button) {
                const oa::ui::display_layout::Point point{
                    placement.x + part.rect.x + part.rect.width / 2,
                    placement.y + part.rect.y + part.rect.height / 2
                };
                send_check_pointer(SDL_EVENT_MOUSE_MOTION, point, 0);
                send_check_pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, point, SDL_BUTTON_LEFT);
                send_check_pointer(SDL_EVENT_MOUSE_BUTTON_UP, point, SDL_BUTTON_LEFT);
                send_check_pointer(SDL_EVENT_MOUSE_MOTION, kRestingPointer, 0);
                return;
            }
        require(false, "the prompt's button is not laid out");
    };
    // ROLL BACK on a folder's row of the Mods page, answered ROLL BACK;
    // `before_answer` runs while the question shows.
    const auto roll_back = [&](const fs::path& rolled, const auto& before_answer) {
        list_offered_mods();
        auto& dialog = open_engine_settings_dialog(settings::DialogKind::engine);
        dialog.page = settings::Page::mods;
        const std::string path = kept_path(rolled);
        const auto rows = settings::mod_rows(dialog);
        int32_t row_control = settings::no_control;
        for (std::size_t index = 0; index < rows.size(); ++index)
            if (rows[index].offered >= 0 &&
                dialog.mod_folders[static_cast<std::size_t>(rows[index].offered)] == path)
                row_control = settings::first_row_control + static_cast<int32_t>(index);
        require(row_control != settings::no_control, "Mods does not list the folder");
        const auto parts = settings::dialog_layout(dialog);
        const auto row = std::find_if(parts.begin(), parts.end(), [&](const auto& part) {
            return part.control == row_control;
        });
        require(row != parts.end(), "the folder's row is not shown");
        bool clicked = false;
        for (const auto& part : parts) {
            const auto& r = part.rect;
            if (part.control < 0 || part.text != "ROLL BACK" || r.x < row->rect.x ||
                r.y < row->rect.y || r.x + r.width > row->rect.x + row->rect.width ||
                r.y + r.height > row->rect.y + row->rect.height)
                continue;
            const int32_t x = r.x + r.width / 2;
            const int32_t y = r.y + r.height / 2;
            std::ignore = settings::dialog_pointer_move(dialog, x, y);
            std::ignore = settings::dialog_pointer_down(dialog, x, y);
            std::ignore = settings::dialog_pointer_up(dialog, x, y);
            clicked = true;
        }
        require(clicked, "the folder's row shows no ROLL BACK");
        require(
            dialog.mod_question == settings::ModQuestion::roll_back, "ROLL BACK asked no question"
        );
        before_answer();
        const auto action = settings::dialog_key(dialog, settings::DialogKey::enter);
        require(action == settings::DialogAction::roll_back_mod, "ROLL BACK did not ask for it");
        return take_engine_settings_action(action);
    };
    const auto play_example = [&] {
        return options_.game_folders.size() > 1 &&
               kept_path(options_.game_folders.front()) == kept_path(folder) &&
               mod_profile() != nullptr && mod_profile()->id == kModId;
    };

    switch (options_.restarts) {
    case 0: {
        // The check starts afresh: an earlier run's packages and mods go.
        require(
            !play_example(),
            "an earlier run of the check that stopped left its mod chosen; start it again"
        );
        if (options_.game_folders.size() > 1) {
            preference_values_.erase(std::string(settings::key::mod_directory));
            preferences_dirty_ = true;
        }
        std::error_code error;
        fs::remove_all(mods, error);
        fs::remove_all(packages, error);
        // The command line's package that is not there.
        until_prompt("the missing package");
        require(
            titled("MOD NOT INSTALLED") && says("It cannot be read."),
            "a missing package is not refused"
        );
        key(SDLK_RETURN);
        require(!state.shown, "Enter did not close the refusal");

        // A package the Finder made, dropped on the window: installed with
        // no question.
        const fs::path finder = packages / "Example Mod.oamod";
        write_package(
            finder,
            {{"Example Mod/", ""},
             {"Example Mod/oamod.yaml", profile_text(kModId, "Example Mod", "1.0", 1)},
             {"Example Mod/units/readme.txt", "revision 1"},
             {"Example Mod/.DS_Store", "finder"},
             {"__MACOSX/Example Mod/._oamod.yaml", "attributes"}}
        );
        static std::string dropped;
        dropped = path_to_utf8(finder);
        SDL_Event drop{};
        drop.type = SDL_EVENT_DROP_FILE;
        drop.drop.windowID = SDL_GetWindowID(sdl_.window);
        drop.drop.data = dropped.c_str();
        require(SDL_PushEvent(&drop), "the drop could not be sent");
        until_prompt("the dropped package");
        require(titled("MOD INSTALLED"), "a new mod was not installed with no question");
        require(revision_in(folder) == 1 && !fs::exists(kept), "the new mod is not in its folder");
        require(
            !fs::exists(folder / ".DS_Store") && !fs::exists(mods / "__MACOSX"),
            "macOS's own files were unpacked"
        );
        require(!reserved_left(mods), "the install left a folder of its own");
        snapshot("installed");
        click(install::Answer::open_folder);
        const auto& opened = user_folder_state().opened;
        require(
            !opened.empty() && kept_path(opened.back()) == kept_path(folder),
            "OPEN FOLDER did not show the mod's folder"
        );
        key(SDLK_ESCAPE);
        require(!state.shown, "Escape did not close the notice");

        // An update asked, cancelled, then replaced.
        install::post_mod_file(mod_package(packages, "1.0", 2));
        until_prompt("the second revision");
        require(
            titled("UPDATE MOD") && says("revision 1, is installed. Replace it with revision 2"),
            "an update was not asked"
        );
        key(SDLK_ESCAPE);
        require(!state.shown && revision_in(folder) == 1, "CANCEL changed the folder");
        install::post_mod_file(mod_package(packages, "1.0", 2));
        until_prompt("the second revision again");
        key(SDLK_RETURN);
        until_prompt("the update's outcome");
        require(titled("MOD UPDATED"), "the update was not told");
        require(
            revision_in(folder) == 2 && revision_in(kept) == 1,
            "REPLACE did not keep the first revision"
        );
        key(SDLK_RETURN);

        // A third revision keeps one version back: the first goes.
        install::post_mod_file(mod_package(packages, "1.0", 3));
        until_prompt("the third revision");
        require(
            says("The folder kept for ROLL BACK (1.0 revision 1) is removed."),
            "the kept version's removal is not said"
        );
        require(
            state.shown->answers[static_cast<std::size_t>(state.shown->prompt.marked)] ==
                install::Answer::cancel,
            "CANCEL is not marked first"
        );
        click(install::Answer::replace);
        until_prompt("the third revision's outcome");
        key(SDLK_RETURN);
        until_deleted();
        require(
            revision_in(folder) == 3 && revision_in(kept) == 2,
            "the third revision did not keep the second"
        );
        require(
            !fs::exists(kept / std::string(install::backup_folder_name)) && !reserved_left(mods),
            "the first revision is still kept"
        );

        // The same revision again: reinstalled, a file added since dropped.
        {
            std::ofstream(folder / "added.txt") << "added";
        }
        install::post_mod_file(mod_package(packages, "1.0", 3));
        until_prompt("the same revision");
        require(titled("ALREADY INSTALLED"), "a reinstall was not asked");
        click(install::Answer::reinstall);
        until_prompt("the reinstall's outcome");
        key(SDLK_RETURN);
        until_deleted();
        require(
            !fs::exists(folder / "added.txt") && revision_in(kept) == 2,
            "the reinstall did not drop the added file and keep the version"
        );

        // An older revision is said to be older, and declined.
        install::post_mod_file(mod_package(packages, "1.0", 1));
        until_prompt("the older revision");
        require(
            says("Revision 1 is older than the one installed."),
            "an older revision was not said to be older"
        );
        key(SDLK_N);
        require(revision_in(folder) == 3, "declining changed the folder");

        // Another version, installed alongside.
        install::post_mod_file(mod_package(packages, "2.0", 1));
        until_prompt("another version");
        require(titled("ANOTHER VERSION"), "another version was not asked about");
        snapshot("another-version");
        click(install::Answer::alongside);
        until_prompt("the version alongside");
        key(SDLK_RETURN);
        require(
            version_in(mods / "example-mod-2.0") == "2.0" && revision_in(folder) == 3,
            "the version was not installed alongside"
        );

        // A folder of the mod's id that holds something else is left as it is.
        const fs::path other = mods / std::string(kOtherId);
        fs::create_directories(other);
        {
            std::ofstream(other / "notes.txt") << "not a mod";
        }
        const fs::path other_package = packages / "other-mod-1.0.oamod";
        write_package(
            other_package, {{"oamod.yaml", profile_text(kOtherId, "Other Mod", "1.0", 1)}}
        );
        install::post_mod_file(other_package);
        until_prompt("a folder in use");
        require(titled("FOLDER IN USE"), "a folder in use was not asked about");
        key(SDLK_Y);
        until_prompt("the other mod alongside");
        key(SDLK_RETURN);
        require(
            fs::exists(other / "notes.txt") && !fs::exists(other / "oamod.yaml"),
            "the folder in use was changed"
        );
        require(
            revision_in(mods / "other-mod-1.0") == 1, "the other mod was not installed alongside"
        );

        // A profile that does not resolve is refused with its diagnostic.
        const fs::path broken_package = packages / "broken.oamod";
        write_package(
            broken_package,
            {{"oamod.yaml",
              profile_text(
                  "broken-package", "Broken", "1.0", 1, "hacks:\n  example.no-such-hack: true\n"
              )}}
        );
        install::post_mod_file(broken_package);
        until_prompt("a profile that does not resolve");
        require(
            titled("MOD NOT INSTALLED") && says("Its oamod.yaml has errors:") &&
                says("no-such-hack"),
            "a broken profile was not refused with its diagnostic"
        );
        key(SDLK_RETURN);
        require(!fs::exists(mods / "broken-package"), "a refused package was installed");

        // ROLL BACK on the Mods page, twice: back and forth.
        std::ignore = roll_back(folder, [] {});
        require(
            revision_in(folder) == 2 && revision_in(kept) == 3,
            "ROLL BACK did not swap the versions"
        );
        auto* dialog = engine_settings_dialog();
        require(
            dialog != nullptr && dialog->folder_notice.empty(),
            "ROLL BACK closed the dialog or failed"
        );
        std::ignore = roll_back(folder, [] {});
        require(
            revision_in(folder) == 3 && revision_in(kept) == 2,
            "a second ROLL BACK did not swap them back"
        );
        // A kept version this build cannot play offers no ROLL BACK; one
        // that became so while the question showed is refused in a prompt
        // over the dialog, nothing changed.
        const fs::path broken = mods / std::string(kBrokenId);
        const fs::path broken_kept = broken / std::string(install::backup_folder_name);
        fs::create_directories(broken_kept);
        const auto keep_broken = [&](bool playable) {
            std::ofstream(broken_kept / "oamod.yaml", std::ios::trunc) << profile_text(
                kBrokenId,
                "Broken Mod",
                "1.0",
                1,
                playable ? "" : "hacks:\n  example.no-such-hack: true\n"
            );
        };
        std::ofstream(broken / "oamod.yaml") << profile_text(kBrokenId, "Broken Mod", "1.0", 2);
        keep_broken(false);
        list_offered_mods();
        {
            const auto& listed = engine_settings_state();
            const auto at =
                std::find(listed.mod_folders.begin(), listed.mod_folders.end(), kept_path(broken));
            require(
                at != listed.mod_folders.end() &&
                    listed.mod_details[static_cast<std::size_t>(at - listed.mod_folders.begin())]
                        .roll_back_from.empty(),
                "ROLL BACK is offered for a kept version that cannot be played"
            );
        }
        keep_broken(true);
        std::ignore = roll_back(broken, [&] { keep_broken(false); });
        require(
            titled("MOD NOT ROLLED BACK") &&
                says(
                    "Broken Mod 1.0 revision 1, the version kept for ROLL BACK, cannot be "
                    "played."
                ) &&
                says("Nothing was changed."),
            "an unplayable kept version was not refused in a prompt"
        );
        require(revision_in(broken) == 2, "an unplayable kept version was rolled back to");
        dialog = engine_settings_dialog();
        require(
            dialog != nullptr && dialog->folder_notice.empty(),
            "the refusal closed the dialog or went to the page's note"
        );
        key(SDLK_RETURN);
        require(!state.shown, "Enter did not close the roll back's refusal");
        require(engine_settings_dialog() != nullptr, "the refusal's OK closed the dialog");
        std::ignore = take_engine_settings_action(settings::DialogAction::cancelled);
        until_deleted();

        // PLAY NOW plays the mod: the run ends for it.
        install::post_mod_file(mod_package(packages, "1.0", 3));
        until_prompt("the last reinstall");
        click(install::Answer::reinstall);
        until_prompt("the last reinstall's outcome");
        // PLAY NOW ends the run, which a pointer event the check sends may
        // not do: the answer goes to the prompt as its click would.
        const auto& answers = state.shown->answers;
        const auto play = std::find(answers.begin(), answers.end(), install::Answer::play_now);
        require(play != answers.end(), "the update offers no PLAY NOW");
        answer_mod_install_prompt(static_cast<int32_t>(play - answers.begin()));
        require(soft_restart_requested(), "PLAY NOW did not end the run");
        const auto stored = preference_values_.find(std::string(settings::key::mod_directory));
        require(
            stored != preference_values_.end() && stored->second == kept_path(folder),
            "PLAY NOW did not store the mod"
        );
        std::cout
            << "mod install check: run 0 installed, updated, rolled back and played the mod\n";
        return;
    }
    case 1: {
        // The mod played is replaced as the run ends.
        require(
            play_example() && mod_profile()->packaging.revision == 3,
            "the run does not play the third revision"
        );
        install::post_mod_file(mod_package(packages, "1.0", 4));
        until_prompt("an update of the mod played");
        require(
            says("The game reloads its data for the mod"),
            "the update does not say the game reloads"
        );
        click(install::Answer::replace);
        for (int frame = 0; frame < kMostFrames && !soft_restart_requested(); ++frame)
            tell_mod_installs();
        require(soft_restart_requested(), "the update of the mod played did not end the run");
        require(revision_in(folder) == 3, "the mod played changed before its run ended");
        require(
            revision_in(mods / (std::string(install::staging_prefix) + std::string(kModId))) == 4,
            "the new revision is not staged whole"
        );
        require(install::pending_change_waiting(), "no change waits for the run's end");
        std::cout << "mod install check: run 1 staged the update of the mod played\n";
        return;
    }
    case 2: {
        // The update was put in place between the runs, and is told.
        require(
            play_example() && mod_profile()->packaging.revision == 4,
            "the run does not play the fourth revision"
        );
        require(revision_in(kept) == 3, "the update between runs did not keep the third revision");
        until_prompt("the update's outcome");
        require(
            titled("MOD UPDATED") && says("is now 1.0 revision 4"),
            "the update between runs was not told"
        );
        key(SDLK_RETURN);
        until_deleted();
        require(!reserved_left(mods), "the update between runs left a folder of its own");
        // ROLL BACK of the mod played ends the run too.
        require(roll_back(folder, [] {}), "ROLL BACK of the mod played did not close the dialog");
        require(
            soft_restart_requested() && install::pending_change_waiting(),
            "ROLL BACK of the mod played did not end the run"
        );
        std::cout << "mod install check: run 2 rolled back the mod played\n";
        return;
    }
    case 3: {
        require(
            play_example() && mod_profile()->packaging.revision == 3,
            "the run does not play the rolled back revision"
        );
        require(
            revision_in(kept) == 4, "the roll back between runs did not keep the fourth revision"
        );
        until_prompt("the roll back's outcome");
        require(
            titled("MOD UPDATED") && says("is back to 1.0 revision 3"), "the roll back was not told"
        );
        key(SDLK_RETURN);
        until_deleted();
        require(!reserved_left(mods), "the roll back between runs left a folder of its own");
        // A stop just after a replace moved the folder aside.
        fs::rename(folder, mods / (std::string(install::old_prefix) + std::string(kModId)));
        request_soft_restart();
        std::cout << "mod install check: run 3 left the mod's folder as a stop mid-replace would\n";
        return;
    }
    default: {
        // The folder was put back before it was looked for: the mod plays.
        require(
            play_example() && mod_profile()->packaging.revision == 3,
            "the mod played was dropped after a stop mid-replace"
        );
        require(engine_settings_state().dropped_mod_folder.empty(), "the mod setting was dropped");
        require(
            revision_in(kept) == 4 && !reserved_left(mods), "the recovery did not settle the folder"
        );
        // The next start of the check plays No Mod again.
        preference_values_.erase(std::string(settings::key::mod_directory));
        preferences_dirty_ = true;
        std::cout << "mod install check: passed\n";
        return;
    }
    }
}

} // namespace oa::app
