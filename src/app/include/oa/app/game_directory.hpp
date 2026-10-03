// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Where oa-game finds the Total Annihilation installation: --game-dir, the
// folder remembered in the user preferences, or a native folder dialog. A
// folder that holds the installer of the Total Annihilation demo (1997)
// instead of game archives is played from the archive unpacked from it. A
// mod plays from a mod folder layered over the game folder (--mod-dir, or
// the one remembered), or from a copied install whose folder holds the
// mod's oamod.yaml; the profile is resolved before any archive is mounted,
// and one the engine cannot use makes the folder unusable.
#pragma once

#include "oa/app/demo_installer.hpp"
#include "oa/app/mod_profile_loader.hpp"
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

struct GameInstall {
    bool folder = false;
    // Archives in the discovery mount order main() mounts.
    std::vector<fs::path> archives;
    // Resources every installation provides that neither the archives nor
    // loose files hold.
    std::vector<std::string> missing;
    /// The folders loose files come from, highest precedence first: the mod
    /// folder, then the inspected folder; empty when the folder does not exist.
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
/// skipped.
///
/// @param root candidate game folder
/// @param data_folder the per-user data folder; empty when none is known
/// @param release the release of the demo to recognise
/// @param mod the mod folder, the --mod file and the player's preferences
/// @return whether it is a folder, its archives in mount order, the
///     required resources none of them holds, where they lie and the
///     profile they play with
[[nodiscard]] GameInstall inspect_game_install(
    const fs::path& root,
    const fs::path& data_folder = {},
    const DemoRelease& release = demo_1997,
    const ModChoice& mod = {}
);

/// Tests whether an inspected folder can run the game.
///
/// @param install inspect_game_install() result
/// @return true when the folder was read, exists, has archives, lacks no
///     required resource and its mod profile can be used
[[nodiscard]] bool usable(const GameInstall& install);

enum class FolderPick : uint8_t { chosen, cancelled, unavailable };
enum class Notice : uint8_t { information, warning };

// The dialogs resolution needs, as a platform boundary so the resolution
// order is tested with a scripted host.
struct GameDirectoryHost {
    void* context{};
    // Opens the folder dialog at `start` (empty: the platform's choice).
    // `error` explains unavailable.
    FolderPick (*pick_folder)(
        void* context, const fs::path& start, fs::path* chosen, std::string* error
    ){};
    void (*tell_user)(void* context, Notice kind, std::string_view text){};
    GameInstall (*inspect)(void* context, const fs::path& folder){};
};

struct GameDirectoryRequest {
    fs::path argument;
    // The preference value, UTF-8.
    std::optional<std::string> stored;
    bool choose = false;
    bool unattended = false;
    /// --archive names the archives: the argument is taken as given, unread.
    bool archives_named = false;
};

enum class GameDirectorySource : uint8_t { argument, stored, chosen };

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
};

/// Resolves the game folder.
///
/// --game-dir, else the stored folder while it is still usable (unless
/// `choose`), else the folder dialog until the user picks a usable folder.
/// An unattended run never opens the dialog: without --game-dir it takes a
/// usable stored folder. --game-dir is inspected too, unless --archive names
/// the archives, and refused when it names no folder or one without game
/// archives or the demo's installer; a folder whose archives lack a required
/// resource is still taken. It throws std::runtime_error naming --game-dir
/// when no usable folder is known, and naming --choose-game-dir when
/// `choose` asks for the dialog.
///
/// @param request the argument, the stored folder and the run's mode
/// @param host dialogs, notices and folder inspection
/// @return the folder and how it was found; nullopt after the user was told
///     why the game cannot start (cancelled, or no dialog on this platform)
[[nodiscard]] std::optional<GameDirectory>
resolve_game_directory(const GameDirectoryRequest& request, const GameDirectoryHost& host);

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
/// unattended runs, CI and a dummy or offscreen video driver. Without
/// --game-dir it carries the folder stored in the preferences file, which an
/// unattended run reads only when --preferences-file names it, so a scripted
/// run never depends on the player's own settings. The mod folder is
/// chosen_mod_directory()'s, and the profile is --mod's or the folders' own.
/// The demo's archive is unpacked to --data-dir, or else to the platform's
/// per-user data folder.
///
/// @param options parsed command line
/// @return as resolve_game_directory()
[[nodiscard]] std::optional<GameDirectory> find_game_directory(const Options& options);

} // namespace oa::app
