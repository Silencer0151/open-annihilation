// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Where oa-game finds the Total Annihilation installation: --game-dir, the
// platform's default folder, the folder remembered in the user preferences,
// the one folder found where Steam, Heroic, Lutris or Bottles put it, the
// in-engine folder chooser, or a native folder dialog. A folder that holds
// the installer of the Total Annihilation demo (1997) instead of game
// archives is played from the archive unpacked from it. A mod plays from a
// mod folder layered over the
// game folder (--mod-dir, or the one remembered), or from a copied install
// whose folder holds the mod's oamod.yaml; the profile is resolved before
// any archive is mounted, and one the engine cannot use makes the folder
// unusable.
#pragma once

#include "oa/app/demo_installer.hpp"
#include "oa/app/mod_profile_loader.hpp"
#include "oa/platform/game_installs.hpp"
#include "oa/platform/preferences.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {

namespace fs = std::filesystem;

struct Options;

// An engine-owned preference: without '|' or a backslash it cannot equal a
// game "<section>|<name>" key or a services "<application>\<name>" key.
inline constexpr std::string_view kGameDirectoryPreference = "open-annihilation.game-directory";
static_assert(
    kGameDirectoryPreference.find('|') == std::string_view::npos &&
    kGameDirectoryPreference.find('\\') == std::string_view::npos
);

/// Converts UTF-8 text to a path.
///
/// SDL, argv on Windows and the preferences file carry UTF-8; a narrow
/// fs::path would decode it in the Windows ANSI code page.
///
/// @param text UTF-8 path
/// @return the path
[[nodiscard]] inline fs::path path_from_utf8(std::string_view text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

/// Converts a path to UTF-8 text.
///
/// @param path path to convert
/// @return its UTF-8 spelling
[[nodiscard]] inline std::string path_to_utf8(const fs::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

/// An archive the discovery found but could not mount, and why.
struct SkippedArchive {
    fs::path path{};     ///< the archive
    std::string error{}; ///< what the mount reported
};

struct GameInstall {
    bool folder = false;
    // Archives in the discovery mount order main() mounts.
    std::vector<fs::path> archives;
    // Resources every installation provides that neither the archives nor
    // loose files hold.
    std::vector<std::string> missing;
    /// The folders loose files come from, highest precedence first: the
    /// overlay, the mod folder, then the inspected folder; empty when the
    /// folder does not exist.
    std::vector<fs::path> folders;
    /// The mod profile the folders play with; null for base 3.1c.
    std::shared_ptr<const data::mod_profile::ModProfile> profile;
    /// Why the mod profile cannot be used, one line each; any makes the
    /// folder unusable.
    std::vector<std::string> profile_errors;
    /// The mod profile's warnings, one line each.
    std::vector<std::string> profile_warnings;
    // Why reading the folder failed; a folder that could not be read is unusable.
    std::string problem;
    /// The folder the archives lie in: the inspected folder, or the folder the
    /// Total Annihilation demo (1997) was unpacked to from an installer in it.
    /// Empty when the folder does not exist.
    fs::path installation;
    /// What the search for the demo's installer found; it runs only when the
    /// folder holds no archives.
    DemoSetup demo;
    /// The archives discovery skipped because they could not be mounted, in the order met;
    /// the error output names each too, as before.
    std::vector<SkippedArchive> skipped{};
};

/// Runs the archive discovery on a folder and checks the resources the frontend opens first.
///
/// The folder's mod profile is resolved first (resolve_folder_profile()):
/// with a mod folder the two are layered, the mod folder first, and the
/// profile's layout names the archives discovered and the directories of
/// the required resources. A profile that cannot be used stops the
/// inspection with its errors. A folder that holds no archives and plays
/// no mod is searched for the installer of the
/// Total Annihilation demo (1997), whose archive is unpacked to `data_folder`
/// or reused from there (set_up_demo()); that checked archive is then the
/// only one taken, from the folder it lies in, whatever other archives the
/// folder holds. An archive that fails to mount is reported on stderr and
/// skipped and recorded in GameInstall::skipped. A named installation archive
/// that is missing or cannot be opened is that, and also a profile error, so
/// the folder is unusable. A non-empty `overlay` is
/// laid over the folder first, as a mod folder is, ahead of the mod folder.
///
/// @param root candidate game folder
/// @param data_folder the per-user data folder; empty when none is known
/// @param release the release of the demo to recognise
/// @param mod the mod folder, the --mod file and the player's preferences
/// @param overlay files laid over the folder, first, as a mod folder is; empty: none
/// @return whether it is a folder, its archives in mount order, the
///     required resources none of them holds, where they lie and the
///     profile they play with
[[nodiscard]] GameInstall inspect_game_install(
    const fs::path& root,
    const fs::path& data_folder = {},
    const DemoRelease& release = demo_1997,
    const ModChoice& mod = {},
    const fs::path& overlay = {}
);

/// Says why a folder cannot be played, in words for the player (the text resolution shows).
///
/// @param install inspect_game_install() result of a folder that is not usable
/// @return one or more sentences naming what is wrong
[[nodiscard]] std::string describe_install_problem(const GameInstall& install);

/// Says why a folder without archives cannot be played, from the search for the demo's installer.
///
/// @param demo what the search for the demo's installer found
/// @return one sentence naming what the folder lacks or why the demo could not be used
[[nodiscard]] std::string describe_archive_problem(const DemoSetup& demo);

/// The characters a file's name adds to the path of the folder it lies in:
/// a separator and a name of up to eight characters, a dot and three more,
/// as the game's archives are named.
inline constexpr std::size_t file_name_room = 13;

/// Says why a folder, or the files in it, may be out of the system's reach:
/// its path is longer than the system opens, or leaves too little room within
/// that for the names of the files in it.
///
/// @param folder the folder, made absolute to be measured
/// @param names the characters the names of the files in it add to its path
///        (file_name_room); 0 measures the folder alone
/// @param longest the most characters a path the system opens may have
///        (oa::platform::longest_path())
/// @param long_paths_turned_off whether the system opens longer paths once
///        they are turned on in it (oa::platform::long_paths_turned_off())
/// @return the reason, as a sentence that names the length, the limit and
///         what to do; empty when the path and the names fit within the limit
[[nodiscard]] std::string path_length_problem(
    const fs::path& folder, std::size_t names, std::size_t longest, bool long_paths_turned_off
);

/// Says why a folder, or the files in it, may be out of this system's reach,
/// as path_length_problem() does with this system's limits.
///
/// @param folder the folder, made absolute to be measured
/// @param names the characters the names of the files in it add to its path;
///        0 measures the folder alone
/// @return the reason, or empty when the path and the names fit within the
///         limit
[[nodiscard]] std::string path_length_problem(const fs::path& folder, std::size_t names = 0);

/// Tests whether an inspected folder can run the game.
///
/// @param install inspect_game_install() result
/// @return true when the folder was read, exists, has archives, lacks no
///     required resource and its mod profile can be used
[[nodiscard]] bool usable(const GameInstall& install);

/// A Total Annihilation folder found where Steam, Heroic, Lutris or Bottles put it.
using FoundInstall = oa::platform::game_installs::Candidate;

enum class FolderPick : uint8_t { chosen, cancelled, unavailable };
enum class Notice : uint8_t { information, warning };

// The dialogs resolution needs, as a platform boundary so the resolution
// order is tested with a scripted host.
struct GameDirectoryHost {
    void* context{};
    // Opens the folder dialog at `start` (empty: the platform's choice).
    // `error` explains unavailable. Null where the build offers no folder
    // dialog (the OA_NATIVE_FOLDER_DIALOG build option): resolution then
    // never asks, and says where the folder goes instead.
    FolderPick (*pick_folder)(
        void* context, const fs::path& start, fs::path* chosen, std::string* error
    ){};
    void (*tell_user)(void* context, Notice kind, std::string_view text){};
    GameInstall (*inspect)(void* context, const fs::path& folder){};
    /// Shows `text` with one button labelled `button` and returns once it is pressed; false
    /// when the notice could not be shown. Null: no such notice (resolution tells and ends).
    bool (*ask)(void* context, Notice kind, std::string_view text, std::string_view button){};
    /// Looks for the platform's default folder again, writing it when there is one. Null:
    /// request.platform_default is the only one.
    bool (*find_platform_default)(void* context, fs::path* folder){};
};

struct GameDirectoryRequest {
    fs::path argument;
    // The preference value, UTF-8.
    std::optional<std::string> stored;
    bool choose = false;
    bool unattended = false;
    /// --archive names the archives: the argument is taken as given, unread.
    bool archives_named = false;
    /// The folder the platform keeps the game in (PlatformHooks's
    /// default_game_folder); empty where there is none, as on the desktop.
    /// It ranks above the stored folder, and is never stored itself.
    fs::path platform_default{};
    /// What the platform tells the player when no folder is usable and no
    /// dialog can ask (PlatformHooks's missing_game_folder_advice), UTF-8;
    /// empty for the --game-dir advice.
    std::string platform_advice{};
    /// The Game files screen is offered (GameFilesHooks installed, not --no-game-files-screen,
    /// not unattended unless --check-game-files): with no usable folder, resolution shows no
    /// notice and reports it in GameFilesNeeded instead.
    bool import_offered = false;
    /// The label of the missing-folder notice's look-again button
    /// (PlatformHooks::game_folder_check_again); empty: none.
    std::string check_again_label{};
    /// Folders found on this machine (find_candidates), in the order found; resolution
    /// inspects each and offers only usable ones. Unattended runs leave it empty.
    std::vector<FoundInstall> found{};
    /// The in-engine folder chooser can be shown (folder_chooser_offered).
    bool chooser_offered = false;
    /// The chooser comes before the system's folder dialog (Steam's Game Mode, where the
    /// dialog may not show).
    bool chooser_first = false;
};

/// Why resolution found no folder to play when the Game files screen is offered.
struct GameFilesNeeded {
    bool needed = false; ///< no usable folder; the screen should open
    fs::path
        folder{}; ///< the folder looked at first (the platform default), empty when none existed
    std::string problem{}; ///< describe_install_problem of that folder; empty when none existed
    /// Open the in-engine folder chooser, not the Game files screen.
    bool chooser = false;
    /// The usable folders found, for the chooser's list.
    std::vector<FoundInstall> found{};
    /// A remembered folder that can no longer be used; empty when none.
    fs::path stored_folder{};
    /// Why the remembered folder can no longer be used; empty when none.
    std::string stored_problem{};
    /// The chooser may offer the system's folder dialog: the build has one, this is not Steam's
    /// Game Mode and it has not failed.
    bool dialog_offered{};
    /// Why the system's folder dialog gave no folder, for the chooser's notice; empty when it
    /// did not fail.
    std::string dialog_problem{};
};

/// How the game folder was found.
enum class GameDirectorySource : uint8_t {
    argument, ///< --game-dir named it
    stored,   ///< the preferences remembered it
    chosen,   ///< the player chose it in the folder dialog; it is remembered
    platform, ///< the platform's default folder, which is not remembered
    found,    ///< the one usable folder found on this machine; it is remembered
};

struct GameDirectory {
    /// The folder named or chosen; the one the preferences remember.
    fs::path path;
    // Empty when --archive names the archives.
    std::vector<fs::path> archives;
    GameDirectorySource source{};
    /// The folder mounted as the installation: `path`, or the folder the
    /// Total Annihilation demo (1997) was unpacked to from an installer in it.
    fs::path installation;
    /// The demo's installer and archive, when `path` held the installer.
    DemoSetup demo;
    /// The folders loose files come from, highest precedence first; empty
    /// when --archive names the archives, which then come from `installation`.
    std::vector<fs::path> folders;
    /// The mod profile the folders play with; null for base 3.1c.
    std::shared_ptr<const data::mod_profile::ModProfile> profile;
    /// The mod profile's warnings, one line each.
    std::vector<std::string> profile_warnings;
    /// Where the folder was found, for GameDirectorySource::found.
    std::optional<FoundInstall> found_from{};
};

/// Resolves the game folder.
///
/// --game-dir, else the platform's default folder while it is usable, else
/// the stored folder while it is still usable (neither when `choose`), else
/// the one usable folder found on this machine (request.found) when no stored
/// folder has gone, else the folder dialog until the user picks a usable
/// folder. Where the in-engine chooser is offered (request.chooser_offered,
/// with `needed`), it opens instead (`needed` says so, with the usable
/// folders found) for several usable folders found, for a stored folder that
/// has gone while a usable one was found, for a start with no usable folder
/// in Steam's Game Mode (request.chooser_first, which --choose-game-dir
/// follows too) or with no dialog in the build, and when the dialog cannot
/// open (with its reason); otherwise a desktop whose dialog works resolves
/// as without the chooser. An unattended run
/// never opens the dialog: without --game-dir it takes a usable platform
/// default, else a usable stored folder. --game-dir is inspected too, unless
/// --archive names the archives, and refused when it names no folder or one
/// without game archives or the demo's installer; a folder whose archives
/// lack a required resource is still taken. It throws std::runtime_error
/// naming --game-dir when an unattended run knows no usable folder, and
/// naming --choose-game-dir when `choose` asks for a dialog nobody can
/// answer or the build does not offer. Where no dialog can ask (the host
/// has none, or it is unavailable), the user is told why no folder can be
/// played and given the platform's advice, else the --game-dir advice; with
/// the platform's look-again button (check_again_label) and a host that can
/// show it (ask), that notice stays up instead: each press looks for the
/// platform's default folder again (find_platform_default), then inspects it
/// and the stored folder, takes the first usable one, and otherwise asks
/// again with the text written from what it found. Where the Game files
/// screen is offered (import_offered, with `needed`), no notice is shown and
/// nothing is thrown: with neither the platform's default nor the stored
/// folder usable, `needed` says so and names the platform's folder and why
/// it cannot be played.
///
/// @param request the argument, the platform's default and advice, the
///     stored folder and the run's mode
/// @param host dialogs, notices and folder inspection
/// @param[out] needed filled when request.import_offered and no folder is usable, or when the
///     in-engine chooser should open; may be null
/// @return the folder and how it was found; nullopt after the user was told
///     why the game cannot start (cancelled, or no dialog on this platform),
///     when the look-again notice could not be shown, or when `needed` was
///     filled
[[nodiscard]] std::optional<GameDirectory> resolve_game_directory(
    const GameDirectoryRequest& request,
    const GameDirectoryHost& host,
    GameFilesNeeded* needed = nullptr
);

/// Tests whether nobody can answer a dialog.
///
/// SDL's dialogs ignore the video driver.
///
/// @param ci value of the CI environment variable
/// @param video_drivers SDL video driver hint, a comma-separated list
/// @return true for CI and for a driver list headed by dummy or offscreen
[[nodiscard]] bool unattended_environment(std::string_view ci, std::string_view video_drivers);

/// Picks the folder the dialog opens in.
///
/// A missing folder would send Windows to its legacy dialog, and without the
/// separator macOS and Windows open the parent.
///
/// @param start folder to start from
/// @return `start` or its nearest existing ancestor, UTF-8 with a trailing
///     separator; empty when none exists
[[nodiscard]] std::string dialog_location(const fs::path& start);

/// Picks the preferences file.
///
/// @param explicit_file --preferences-file value, when given
/// @return that file, else the platform's default preferences file
[[nodiscard]] fs::path preference_file(const std::optional<fs::path>& explicit_file);

/// Reads the remembered game folder from the preferences.
///
/// @param values loaded preferences
/// @return the folder as UTF-8, or nullopt before the first choice
[[nodiscard]] std::optional<std::string>
stored_game_directory(const oa::platform::preferences::Values& values);

/// Stores a game folder in the preferences.
///
/// @param[in,out] values preferences to update
/// @param folder chosen folder; stored absolute and normalised, as UTF-8
void remember_game_directory(oa::platform::preferences::Values& values, const fs::path& folder);

/// Picks the mod folder a run plays: --mod-dir, else none with --base-game,
/// else the one the preferences remember.
///
/// @param mod_dir the --mod-dir folder; empty when not given
/// @param base_game whether --base-game was given
/// @param values loaded preferences
/// @return the mod folder; empty for the base game
[[nodiscard]] fs::path chosen_mod_directory(
    const fs::path& mod_dir, bool base_game, const oa::platform::preferences::Values& values
);

/// Stores the chosen mod folder in the preferences.
///
/// @param[in,out] values preferences to update
/// @param folder the mod folder, stored absolute and normalised as UTF-8;
///        empty forgets the choice, for the base game
void remember_mod_directory(oa::platform::preferences::Values& values, const fs::path& folder);

/// Resolves the game folder with the native dialogs (game_directory_dialog.cpp).
///
/// The request takes --game-dir and --choose-game-dir, and is unattended for
/// unattended runs, CI and a dummy or offscreen video driver. It carries the
/// platform's default folder and advice from the platform's hooks
/// (platform_hooks()). Without --game-dir it carries the folder stored in
/// the preferences file, which an unattended run reads only when
/// --preferences-file names it, so a scripted run never depends on the
/// player's own settings. A build without the native folder dialog
/// (OA_NATIVE_FOLDER_DIALOG off) offers none. The mod folder is
/// chosen_mod_directory()'s, and the profile is --mod's or the folders' own.
/// The demo's archive is unpacked to --data-dir, or else to the platform's
/// per-user data folder. The missing-folder notice takes the platform's
/// look-again button (PlatformHooks's game_folder_check_again), shown as a
/// message box with that one button. The Game files screen is offered when
/// `needed` is given, the platform brings game files in
/// (game_files_import_offered), --no-game-files-screen was not given, and
/// the run is not unattended unless it is --check-game-files. A run that is
/// not unattended and names no folder carries the folders found on this
/// machine (oa::platform::game_installs::find_candidates); the in-engine
/// chooser is offered when `needed` is given and folder_chooser_offered()
/// says so, and comes before the dialog in Steam's Game Mode
/// (oa::platform::running_in_steam_game_mode).
///
/// @param options parsed command line
/// @param[out] needed non-null: the Game files screen may be offered, and is
///        filled as resolve_game_directory() fills it
/// @return as resolve_game_directory()
[[nodiscard]] std::optional<GameDirectory>
find_game_directory(const Options& options, GameFilesNeeded* needed = nullptr);

/// Inspects a folder the in-engine chooser picked with a host's inspection and returns it as
/// chosen (it is remembered) when it can be played.
///
/// @param host the inspection
/// @param folder the folder
/// @param[out] problem why it cannot be played, in words for the player; may be null
/// @return the folder, or none
[[nodiscard]] std::optional<GameDirectory>
take_picked_folder(const GameDirectoryHost& host, const fs::path& folder, std::string* problem);

/// Opens the system's folder dialog for the in-engine chooser, a second time when the first
/// fails, as resolution does (game_directory_dialog.cpp).
///
/// @param start the folder it opens at; empty: the system's choice
/// @param[out] chosen the folder picked
/// @param[out] error why no folder came back, for FolderPick::unavailable
/// @return what the dialog gave; unavailable where the build offers none
[[nodiscard]] FolderPick
pick_game_folder_with_dialog(const fs::path& start, fs::path* chosen, std::string* error);

/// Inspects a folder the chooser picked and returns it as chosen (it is remembered) when it
/// can be played (game_directory_dialog.cpp), with the mod folder and the data folder
/// find_game_directory() inspects with.
///
/// @param options the parsed command line (mod, data folder)
/// @param folder the folder
/// @param[out] problem why it cannot be played, in words for the player; may be null
/// @return the folder, or none
[[nodiscard]] std::optional<GameDirectory>
take_chosen_folder(const Options& options, const fs::path& folder, std::string* problem);

/// Returns the main menu's words for a found folder: "Playing Total Annihilation from your
/// Steam library:" and the folder on the next line.
///
/// @param install where the folder was found
/// @return the notice's text, UTF-8
[[nodiscard]] std::string found_install_notice(const FoundInstall& install);

} // namespace oa::app
