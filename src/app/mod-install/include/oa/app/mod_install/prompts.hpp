// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the player is shown and asked while a mod package installs: each
// prompt's title, text and buttons, and what each button answers. Every
// text is an English template looked up in the interface catalogue
// (oa/data/languages/interface_text.hpp) and then filled with its values,
// which are never looked up: mod names, versions, file names, paths and
// sizes. The captions shared with the settings' own dialogs are the same
// English words, so one catalogue entry serves both.
#pragma once

#include "oa/app/mod_install.hpp"
#include "oa/ui/engine_settings/prompt.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app::mod_install {

/// What a prompt's button answers.
enum class Answer : uint8_t {
    cancel,      ///< CANCEL: nothing is done
    ok,          ///< OK: the prompt is done with
    replace,     ///< REPLACE: the folder's version is replaced and kept as its .backup
    alongside,   ///< INSTALL ALONGSIDE: the package goes in a folder of its own
    reinstall,   ///< REINSTALL: the folder's files are installed again
    open_folder, ///< OPEN FOLDER: the folder is shown; the prompt stays
    play_now,    ///< PLAY NOW: the game switches to the mod
};

/// A prompt and what its buttons answer.
struct ModPrompt {
    oa::ui::engine_settings::Prompt prompt{};
    std::vector<Answer> answers{}; ///< one for each button, left to right
};

/// Returns a version as a prompt names it: "{version}", or "{version}
/// revision {n}" where the versions it is shown beside are the same.
///
/// @param version the version
/// @param revision the revision; 0 names an unknown one
/// @param with_revision the revision is named
/// @return the label
[[nodiscard]] std::string
version_label(std::string_view version, int64_t revision, bool with_revision);

/// Returns the prompt shown while a package unpacks (INSTALLING MOD), with
/// its progress, or while its files are put in place.
///
/// @param incoming the mod
/// @param done_bytes the bytes unpacked so far
/// @param total_bytes the bytes it unpacks to
/// @param placing its files are being put in place
/// @return the prompt: CANCEL
[[nodiscard]] ModPrompt installing_prompt(
    const Incoming& incoming, uint64_t done_bytes, uint64_t total_bytes, bool placing
);

/// Returns the question a plan asks: UPDATE MOD, ANOTHER VERSION, ALREADY
/// INSTALLED or FOLDER IN USE. Where the answer removes something that
/// cannot be brought back (a .backup that exists, or the files a reinstall
/// replaces), CANCEL is marked first.
///
/// @param plan the plan, one of the ask kinds
/// @param incoming the mod
/// @param file_name the package's name
/// @param mods the Mods folder, whose alongside folder the text shows
/// @param played the target is the mod the game plays now
/// @return the prompt
[[nodiscard]] ModPrompt question_prompt(
    const InstallPlan& plan,
    const Incoming& incoming,
    std::string_view file_name,
    const std::filesystem::path& mods,
    bool played
);

/// Returns the prompt that says a mod was installed in a folder of its own
/// (MOD INSTALLED).
///
/// @param incoming the mod
/// @param folder its folder
/// @param play_now PLAY NOW is offered; else the text says where to choose it
/// @return the prompt: OPEN FOLDER, PLAY NOW when offered, OK
[[nodiscard]] ModPrompt
installed_prompt(const Incoming& incoming, const std::filesystem::path& folder, bool play_now);

/// Returns the prompt that says a mod's folder was updated, installed
/// again or rolled back (MOD UPDATED).
///
/// @param change what was done
/// @param now what the folder holds now
/// @param before what it held before
/// @param folder the folder
/// @param result what the change did: whether the earlier version was kept,
///        and where it is when it could not be
/// @param play_now PLAY NOW is offered
/// @return the prompt: OPEN FOLDER, PLAY NOW when offered, OK
[[nodiscard]] ModPrompt updated_prompt(
    Change change,
    const Incoming& now,
    const InstalledMod& before,
    const std::filesystem::path& folder,
    const ChangeResult& result,
    bool play_now
);

/// Returns the sentence that says why a package was not installed.
///
/// @param problem the problem
/// @return the sentence
[[nodiscard]] std::string refusal_text(const Problem& problem);

/// Returns the prompt that says a package was not installed (MOD NOT
/// INSTALLED): the reason, at most three of its profile's diagnostics, and
/// whether a change failed.
///
/// @param file_name the package's name
/// @param problem why
/// @param change_failed a change was tried and undone
/// @return the prompt: OK
[[nodiscard]] ModPrompt
refused_prompt(std::string_view file_name, const Problem& problem, bool change_failed);

/// Returns the prompt, shown over the settings dialog, that says the Mods
/// page's ROLL BACK changed nothing (MOD NOT ROLLED BACK).
///
/// @param title the mod's name
/// @return the prompt: OK
[[nodiscard]] ModPrompt roll_back_failed_prompt(std::string_view title);

/// Returns the prompt, shown over the settings dialog, that says the kept
/// version cannot be played, so it was not rolled back to (MOD NOT ROLLED
/// BACK).
///
/// @param title the mod's name
/// @param to the kept version's label
/// @param reason why it cannot be played, a sentence in the language shown
/// @return the prompt: OK
[[nodiscard]] ModPrompt
roll_back_refused_prompt(std::string_view title, std::string_view to, std::string_view reason);

} // namespace oa::app::mod_install
