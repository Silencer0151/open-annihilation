// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What outlives each run of the game within one process: the mod packages
// opened in the game and waiting for the main menu, the change that waits
// for the run playing its target to end, its outcome for the next run, and
// the discard folders the start's recovery found. Every function may be
// called from any thread.
#pragma once

#include "oa/app/mod_install.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace oa::app::mod_install {

/// Queues a package to be installed once the main menu shows. A path already
/// queued, or the one being installed now, is not queued again: paths are
/// compared as std::filesystem::weakly_canonical gives them.
///
/// @param file the package
void post_mod_file(const std::filesystem::path& file);

/// Takes the next package, oldest first, and notes it as the one being
/// installed until finish_mod_file.
///
/// @return the package; nothing when none waits
[[nodiscard]] std::optional<std::filesystem::path> take_mod_file();

/// Puts a package back at the front, as when its question was set aside.
///
/// @param file the package
void return_mod_file(const std::filesystem::path& file);

/// Notes that the package take_mod_file gave is done with.
void finish_mod_file();

/// Tells whether any package waits.
///
/// @return true while one does
[[nodiscard]] bool mod_files_waiting();

/// A change whose target is the mod played: it waits for the run to end and
/// its archives to close, then main() puts it in place before the next run.
struct PendingChange {
    std::filesystem::path mods{}; ///< the Mods folder
    std::string target{};         ///< the folder in Mods
    Change change{Change::install};
    Incoming incoming{};     ///< what it installs; for a roll back, the kept version
    InstalledMod replaced{}; ///< what the target holds now, which it must still hold
    std::string file_name{}; ///< the package's name; empty for a roll back
    /// The hold on the Mods folder's lock the unpacking took, kept until the
    /// change is in place; null for a roll back, which takes its own.
    ModsHold hold{};
};

/// Sets the change that waits for the run to end, replacing any.
///
/// @param change the change
void set_pending_change(PendingChange change);

/// Tells whether a change waits.
///
/// @return true while one does
[[nodiscard]] bool pending_change_waiting();

/// What a change that waited for its run did, for the next run to tell.
struct ChangeOutcome {
    PendingChange change{};
    ChangeResult result{};
};

/// Puts the waiting change in place, if any, keeps its outcome for the next
/// run, releases the hold on the Mods folder, then settles the folder again
/// (recover_changes) and keeps the discard folders found.
///
/// @param options the hooks and the waits
void finish_pending_change(const ChangeOptions& options = {});

/// Takes the outcome of the change that waited, once.
///
/// @return the outcome; nothing when no change waited
[[nodiscard]] std::optional<ChangeOutcome> take_change_outcome();

/// Keeps discard folders for the next run to delete.
///
/// @param folders the folders
void keep_discards(const std::vector<std::filesystem::path>& folders);

/// Takes the discard folders kept.
///
/// @return the folders
[[nodiscard]] std::vector<std::filesystem::path> take_discards();

/// Keeps the hand-off folder this process takes a second start's packages
/// from, while it holds the instance lock (handoff.hpp).
///
/// @param folder the folder
void set_handoff_folder(const std::filesystem::path& folder);

/// Returns the hand-off folder this process takes packages from.
///
/// @return the folder; nothing when the process holds no instance lock
[[nodiscard]] std::optional<std::filesystem::path> handoff_folder();

} // namespace oa::app::mod_install
