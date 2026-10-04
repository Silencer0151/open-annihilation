// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The player's own folder, "Open Annihilation" in their Documents folder:
// where it is, the folders it holds (Saves, Screenshots, Films and Mods), the
// one-time move of saved games from where earlier versions kept them, beside
// the preferences file, and the main menu's notice of that move; and the
// warning, in the same look, that a mod's games cannot start until its files
// are in its folder. Nothing here reads the clock or SDL; the runtime does
// the rest.
#pragma once

#include "oa/platform/preferences.hpp"
#include "oa/ui/engine_settings/notice.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace oa::app {

/// The folder of the player's own folder that holds the saved games of 3.1c,
/// and a folder of its own for each mod's, named after the mod's id.
inline constexpr std::string_view saves_folder_name = "Saves";
/// The folder of the player's own folder screenshots and posters go in
/// while no Image Output Directory is set.
inline constexpr std::string_view screenshots_folder_name = "Screenshots";
/// The folder of the player's own folder film captures go in while no
/// Image Output Directory is set.
inline constexpr std::string_view films_folder_name = "Films";
/// The folder of the player's own folder whose mod folders the Mod setting
/// offers besides the game folder's mods folder.
inline constexpr std::string_view user_mods_folder_name = "Mods";

/// The folder below the preferences file's folder, and below each mods/<id>
/// folder there, that held saved games before they moved to Saves.
inline constexpr std::string_view earlier_saves_folder_name = "SAVEGAME";
/// The folder below the preferences file's folder that held each mod's
/// earlier save folder, in a folder named after the mod's id.
inline constexpr std::string_view earlier_mods_folder_name = "mods";
/// The extension of a saved game, matched without case.
inline constexpr std::string_view saved_game_extension = ".SAV";
/// What the name of a file being copied into Saves ends with until the copy
/// is whole; a copy cut short keeps it, and no dialog lists it.
inline constexpr std::string_view partial_copy_suffix = ".moving";

/// The preference that moves the player's own folder, as an absolute UTF-8
/// path; absent or not absolute for the default.
inline constexpr std::string_view user_folder_preference = "open-annihilation.user-folder";
/// The preference that records that the saved games were moved, so that the
/// move never runs again: the saved games moved and the ones left where
/// they were, as two decimal numbers, "12 0".
inline constexpr std::string_view saves_moved_preference = "open-annihilation.saves-moved";
/// The preference that records the main menu's notice of the move:
/// saves_notice_due until a run someone watches has shown it, then
/// saves_notice_told; absent when the move moved and left nothing.
inline constexpr std::string_view saves_notice_preference = "open-annihilation.saves-moved-notice";
/// saves_notice_preference's value while the notice waits to be shown.
inline constexpr std::string_view saves_notice_due = "due";
/// saves_notice_preference's value once the notice has shown.
inline constexpr std::string_view saves_notice_told = "told";

/// The folder a run with --preferences-file keeps as the player's own,
/// beside that file, unless --user-folder or the file's own key names one,
/// so that a check never reaches the player's Documents folder.
///
/// @param preferences_file the --preferences-file
/// @return the folder named user_folder_name in the file's folder
[[nodiscard]] std::filesystem::path
user_folder_beside(const std::filesystem::path& preferences_file);

/// Picks the player's own folder: --user-folder, else the preferences' key
/// while it holds an absolute path, else `fallback`. A relative
/// --user-folder is taken from the current directory.
///
/// @param option the --user-folder value, when given
/// @param values the loaded preferences
/// @param fallback the default folder (default_user_folder, or
///        user_folder_beside with --preferences-file)
/// @return the folder, normalised and without a separator at its end,
///     absolute unless `fallback` is not; it need not exist
[[nodiscard]] std::filesystem::path choose_user_folder(
    const std::optional<std::filesystem::path>& option,
    const platform::preferences::Values& values,
    const std::filesystem::path& fallback
);

/// Returns the folder the saved games of a game or a mod are kept in.
///
/// @param user_folder the player's own folder
/// @param mod_id the id of the mod's profile; empty for 3.1c
/// @return Saves, or Saves/<mod id> for a mod
[[nodiscard]] std::filesystem::path
saves_folder(const std::filesystem::path& user_folder, std::string_view mod_id);

/// Finds a folder's entry whose name matches, without case.
///
/// @param folder the folder
/// @param name the name
/// @return the entry's path; nullopt when there is none or the folder cannot be listed
[[nodiscard]] std::optional<std::filesystem::path>
entry_without_case(const std::filesystem::path& folder, std::string_view name);

/// Ways a move of files may fail, which the tests stand in for; each null
/// member uses std::filesystem.
struct FileMoveHooks {
    void* context{};
    /// Renames `from` to `to`; null uses std::filesystem::rename. Sets
    /// `error` when it fails.
    void (*rename)(
        void* context,
        const std::filesystem::path& from,
        const std::filesystem::path& to,
        std::error_code& error
    ){};
    /// Copies `from` to the new file `to`, overwriting nothing; null uses
    /// std::filesystem::copy_file. Sets `error` when it fails.
    void (*copy)(
        void* context,
        const std::filesystem::path& from,
        const std::filesystem::path& to,
        std::error_code& error
    ){};
};

/// What the move of the saved games did.
struct SavesMove {
    std::size_t moved{};       ///< saved games now in a Saves folder
    std::size_t renamed{};     ///< of those, the ones kept under another name, theirs taken
    std::size_t left{};        ///< saved games that could not move and stay where they were
    std::size_t other_files{}; ///< other files moved with them, such as restriction lists
    /// What happened, a line each, for standard error and the log.
    std::vector<std::string> lines;
};

/// Returns a name for a file that takes no name a folder holds, matched
/// without case: the name itself when it is free, else the name with " (2)",
/// " (3)" and so on before its extension.
///
/// @param name the file's name
/// @param taken the names the folder holds, and those already given, in
///        capitals (ASCII letters raised)
/// @return the free name
[[nodiscard]] std::string
free_file_name(const std::string& name, const std::vector<std::string>& taken);

/// Moves the files of an earlier saves folder into a Saves folder, never
/// overwriting one: a name the Saves folder holds already, matched without
/// case, is kept for the file there, and the file moved takes a free name
/// (free_file_name). A file is renamed into place; where that fails, as
/// across volumes, it is copied under its name and partial_copy_suffix,
/// renamed into place once the copy is whole, and the original removed;
/// where that fails too, it stays where it is, and the game keeps listing
/// that folder. The earlier folder is removed once it is empty. Folders
/// within it are left.
///
/// @param earlier the earlier folder, SAVEGAME
/// @param saves the Saves folder; made when missing
/// @param[in,out] move what the move did, added to
/// @param hooks stand-ins for the file system's rename and copy
void move_saves_folder(
    const std::filesystem::path& earlier,
    const std::filesystem::path& saves,
    SavesMove& move,
    const FileMoveHooks& hooks = {}
);

/// Moves the saved games from where earlier versions kept them, beside the
/// preferences file: <root>/SAVEGAME into saves_folder(user_folder, ""),
/// and <root>/mods/<id>/SAVEGAME into saves_folder(user_folder, id) for
/// each mod (move_saves_folder). The names are matched without case.
///
/// @param earlier_root the folder that held them: the preferences file's
/// @param user_folder the player's own folder
/// @param hooks stand-ins for the file system's rename and copy
/// @return what the move did
[[nodiscard]] SavesMove move_earlier_saves(
    const std::filesystem::path& earlier_root,
    const std::filesystem::path& user_folder,
    const FileMoveHooks& hooks = {}
);

/// Records a move in the preferences: saves_moved_preference, and the notice
/// due when it moved or left a saved game.
///
/// @param[in,out] values the preferences
/// @param move what the move did
void record_saves_move(platform::preferences::Values& values, const SavesMove& move);

/// The counts a recorded move keeps for its notice.
struct RecordedMove {
    std::size_t moved{}; ///< saved games moved
    std::size_t left{};  ///< saved games left where they were
};

/// Reads what a recorded move moved and left.
///
/// @param values the preferences
/// @return the counts; nullopt when no move is recorded, or its value
///         cannot be read
[[nodiscard]] std::optional<RecordedMove>
recorded_saves_move(const platform::preferences::Values& values);

/// Tells whether the main menu's notice of the move waits to be shown.
///
/// @param values the preferences
/// @return true while saves_notice_preference holds saves_notice_due
[[nodiscard]] bool saves_notice_due_in(const platform::preferences::Values& values);

/// What opening a folder in the system's file manager came to.
struct FolderOpening {
    bool opened{}; ///< the file manager was asked to show the folder
    /// Why not, in a few words of the engine's own that a dialog shows;
    /// empty when it was opened.
    std::string reason;
    /// What happened, a line for standard error and the log; empty when
    /// there is nothing to tell.
    std::string detail;
};

/// Why a folder was not shown: the file manager did not take it.
inline constexpr std::string_view file_manager_failed_text = "The file manager could not open it.";
/// Why a folder was not shown: the system has no way to show one.
inline constexpr std::string_view no_file_manager_text = "No file manager is there to open it.";
/// Why a folder was not shown: it was missing and could not be made.
inline constexpr std::string_view folder_not_made_text = "The folder cannot be made.";

/// Opens folders in the system's file manager.
struct FolderOpenerHooks {
    void* context{};
    /// Shows `folder`, which exists, in the system's file manager; null
    /// opens nothing and tells file_manager_failed_text.
    FolderOpening (*open)(void* context, const std::filesystem::path& folder){};
};

/// Returns hooks that show a folder in the system's file manager: through the
/// platform's own show_folder hook where it has one (PlatformHooks, as the
/// Files app on iOS); else on macOS through open, on Windows through the
/// shell's open verb, and on Linux through xdg-open, or, where xdg-open is
/// missing, by asking the desktop portal over the session bus with gdbus. A
/// build that starts no other programs (OA_PROCESS_SPAWNING off) and has no
/// such hook shows none and tells no_file_manager_text.
///
/// @return the hooks; they need no context
[[nodiscard]] FolderOpenerHooks system_folder_opener() noexcept;

/// Returns hooks that show nothing and append each folder asked for to a
/// list instead, for runs nobody watches.
///
/// @param[in,out] requests receives every folder asked for, in order; it
///     must outlive the hooks
/// @return the hooks
[[nodiscard]] FolderOpenerHooks
recorded_folder_opener(std::vector<std::filesystem::path>& requests) noexcept;

/// Makes a folder when it is missing, then shows it through the hooks.
///
/// @param hooks how folders are shown
/// @param folder the folder
/// @return what came of it; folder_not_made_text when it cannot be made
[[nodiscard]] FolderOpening
open_folder(const FolderOpenerHooks& hooks, const std::filesystem::path& folder);

/// Returns a folder's file URI: "file://" and its absolute path in UTF-8,
/// '/' between its parts and before a drive, each byte other than an ASCII
/// letter, a digit, '-', '.', '_', '~', '/' and ':' written as '%' and two
/// capital hexadecimal digits.
///
/// @param folder the folder, absolute
/// @return the URI
[[nodiscard]] std::string file_uri(const std::filesystem::path& folder);

/// Returns the notice of the move, in the engine's own words, in English:
/// how many saved games moved and to which Saves folder, or with none moved
/// that new ones go there, its path whole for the notice to wrap; how many
/// could not move and stay listed where they were; and that screenshots,
/// films and mods now go in the same Open Annihilation folder.
///
/// @param move what the move moved and left
/// @param saves the Saves folder of 3.1c's saved games
/// @return the notice, with "OPEN FOLDER" for the folder's button
[[nodiscard]] oa::ui::engine_settings::Notice
saves_moved_notice(const RecordedMove& move, const std::filesystem::path& saves);

/// A side and the unit that is its commander, as SIDEDATA names them.
struct SideCommander {
    std::string side;      ///< the side's name
    std::string commander; ///< its commander's unit name; empty when it names none
};

/// A file a side's SIDEDATA section names, its interface art or font, that
/// the mod's files lack.
struct SideFileGap {
    std::string side; ///< the side's name
    std::string path; ///< the file, such as "anims/NAME.GAF"
};

/// What a mod's files lack: none of its units found, or a side's commander
/// missing from them, either of which keeps its games from starting; and the
/// files its sides name that are missing, without which its games start and
/// show what they can.
struct ModStartGaps {
    bool no_units{};                                 ///< no unit definition was found
    std::vector<SideCommander> missing_commanders{}; ///< the sides whose commander is missing
    std::vector<SideFileGap> missing_side_files{};   ///< the side files missing, by side

    /// Tells whether anything is missing.
    ///
    /// @return true when the mod warns
    [[nodiscard]] bool any() const noexcept {
        return games_cannot_start() || !missing_side_files.empty();
    }

    /// Tells whether what is missing keeps the mod's games from starting:
    /// its units, or a commander. Missing side files alone do not.
    ///
    /// @return true when a game cannot start
    [[nodiscard]] bool games_cannot_start() const noexcept {
        return no_units || !missing_commanders.empty();
    }
};

/// Finds what keeps a mod's games from starting: no units at all, and each
/// side whose commander is not among them, its name matched without case. A
/// side that names no commander is passed over.
///
/// @param unit_names the names of the mod's units
/// @param sides each side and its commander, in the sides' order
/// @return the gaps; none when every side's commander is among the units
[[nodiscard]] ModStartGaps find_mod_start_gaps(
    const std::vector<std::string>& unit_names, const std::vector<SideCommander>& sides
);

/// Returns the warning that a mod's files are missing, in the engine's own
/// words, in English: the mod's name, that no units were found, each side
/// whose commander is not among its units, each file a side names that its
/// files lack, which games show without, and either that its games cannot
/// start until its files are added to its folder or, when only side files
/// are missing, that they still start; then the folder's path whole for the
/// notice to wrap.
///
/// @param mod_name the mod's name, as its profile gives it; empty for a
///     folder without a profile
/// @param gaps what its files lack
/// @param folder the mod's folder
/// @return the notice, with "OPEN MOD FOLDER" for the folder's button
[[nodiscard]] oa::ui::engine_settings::Notice mod_files_missing_notice(
    std::string_view mod_name, const ModStartGaps& gaps, const std::filesystem::path& folder
);

} // namespace oa::app
