// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Finding, reading and resolving a mod profile (oamod.yaml) for oa-game:
// the file a folder holds, matched without case; a mod folder layered over
// the game folder; the settings the profile binds, read from the mod's INI
// and from the preferences that stand in for its registry; the --mod and
// --print-profile options.
#pragma once

#include "oa/data/defs/layout.hpp"
#include "oa/data/mod_profile.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/platform/preferences.hpp"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {

/// The name of a mod profile in the root of a mod's folder, matched without case.
inline constexpr std::string_view mod_profile_name = "oamod.yaml";

/// The folder of a game folder that holds mod folders, matched without case.
inline constexpr std::string_view mods_folder_name = "mods";

/// The preference that remembers the chosen mod folder, as UTF-8; absent or
/// empty for the base game. Without '|' or a backslash it cannot equal a
/// game key.
inline constexpr std::string_view mod_directory_preference = "open-annihilation.mod-directory";

/// The registry section, below a registry root, that holds the game's own
/// settings.
inline constexpr std::string_view registry_game_section = "Total Annihilation";

/// What a game folder is played with besides its own files.
struct ModChoice {
    /// A mod folder layered over the game folder; empty for none.
    std::filesystem::path folder{};
    /// A profile read instead of the folder's own (--mod); empty for none.
    std::filesystem::path profile_file{};
    /// Accept hacks this build does not implement yet, with a warning each.
    bool accept_unimplemented_hacks{};
    /// The player's preferences, whose registry section the profile's
    /// registry bindings read; null for none.
    const platform::preferences::Values* preferences{};
};

/// A game folder's profile, read with the settings it binds and resolved.
struct FolderProfile {
    /// The resolved profile; null for a folder that plays base 3.1c.
    std::shared_ptr<const data::mod_profile::ModProfile> profile{};
    /// Each error, one a line; any error makes the folder unusable.
    std::vector<std::string> errors{};
    /// Each warning, one a line.
    std::vector<std::string> warnings{};
    /// The profile file the diagnostics name; empty when the folders play base 3.1c.
    std::filesystem::path file{};
};

/// What a game folder's profile is resolved from, so that it can be resolved
/// again with a player's overrides of its standard hacks (Developer Mode).
struct ProfileSource {
    std::vector<uint8_t> text{}; ///< the profile's bytes
    std::string name{};          ///< its file as UTF-8, which its diagnostics name
    /// The settings it binds and whether hacks this build does not
    /// implement yet are accepted, as the folder's profile was resolved
    /// with them; no overrides.
    data::mod_profile::ResolveOptions options{};
};

/// Reads what the profile a game folder plays with is resolved from, as
/// resolve_folder_profile reads it: the --mod file or the first folder's
/// oamod.yaml, and the settings it binds.
///
/// @param folders the folders loose files come from, the mod folder first
/// @param choice the --mod file, the unimplemented-hack choice and the
///        player's preferences; its mod folder is not looked at
/// @return the source; nothing when the folders hold no profile, or it
///         cannot be read or resolved
[[nodiscard]] std::optional<ProfileSource>
folder_profile_source(const std::vector<std::filesystem::path>& folders, const ModChoice& choice);

/// Finds, reads and resolves the profile a game folder plays with.
///
/// The profile is the --mod file when one is chosen, else the mod folder's
/// own oamod.yaml, else the game folder's (a copied install). A game folder
/// that holds its own oamod.yaml cannot carry a mod folder. A mod folder
/// without an oamod.yaml is no error: its files still layer over the game
/// folder's, and with no profile the game plays by 3.1c's own rules. The profile is
/// resolved once to learn its settings file and registry root, then again
/// with the settings it binds: the INI of that name from the first folder
/// that holds it, read only, and the preferences' registry section, which
/// the profile's registry seeds fill.
///
/// @param folders the folders loose files come from, the mod folder first
/// @param choice the mod folder, the --mod file and the player's preferences
/// @return the profile, or the errors that make the folders unusable
[[nodiscard]] FolderProfile
resolve_folder_profile(const std::vector<std::filesystem::path>& folders, const ModChoice& choice);

/// The largest INI file a profile's settings are read from, in bytes; a
/// larger one is not read.
inline constexpr uintmax_t mod_ini_most_bytes = uintmax_t{1024} * 1024;

/// Reads the settings a profile binds: the INI file it names
/// (identity.settings-file) from the first folder that holds one, its name
/// matched without case, read only, and the preferences' registry section,
/// which stands in for the mod's registry.
///
/// @param profile the profile, resolved without settings
/// @param folders the folders, highest precedence first
/// @param preferences the player's preferences; null for none
/// @return the settings
[[nodiscard]] data::mod_profile::Settings mod_settings_of(
    const data::mod_profile::ModProfile& profile,
    const std::vector<std::filesystem::path>& folders,
    const platform::preferences::Values* preferences
);

/// Builds the settings a profile binds from its INI file's text, wherever it
/// was read from (a folder, or a mod package), and the preferences' registry
/// section, which stands in for the mod's registry.
///
/// @param profile the profile, resolved without settings
/// @param ini_text the INI file's text, at most mod_ini_most_bytes; nothing
///        when there is none
/// @param preferences the player's preferences; null for none
/// @return the settings
[[nodiscard]] data::mod_profile::Settings mod_settings_from(
    const data::mod_profile::ModProfile& profile,
    std::optional<std::string_view> ini_text,
    const platform::preferences::Values* preferences
);

/// Lists the mod folders a game folder offers: every folder below its mods
/// folder, with an oamod.yaml or without one, in name order, but those whose
/// names start with a dot (list_mods_in).
///
/// @param game_folder the game folder
/// @return each mod folder's path
[[nodiscard]] std::vector<std::filesystem::path>
list_mod_folders(const std::filesystem::path& game_folder);

/// Lists the mod folders a folder of mods holds, such as the player's own
/// Mods folder: every folder in it, with an oamod.yaml or without one, in
/// name order, matched without case. A folder whose name starts with a dot
/// is hidden, as file managers hide it, and not listed: the folders an
/// install of a mod package keeps in Mods while it works are named so.
///
/// @param mods the folder of mods; one that is missing holds none
/// @return each mod folder's path
[[nodiscard]] std::vector<std::filesystem::path> list_mods_in(const std::filesystem::path& mods);

/// Why a folder the player picked cannot be played as a mod folder.
struct PickedFolderCheck {
    /// Why not, in a few words the settings dialog shows; empty when it can be.
    std::string refusal{};
    /// Each error the profile's resolution gave, one a line, for the log.
    std::vector<std::string> errors{};
    /// The folder holds no oamod.yaml: it can be played only without a
    /// profile, by 3.1c's own rules, which the player is asked to agree to.
    bool without_profile{};
    /// The id Developer Mode's overrides are kept under: the profile's, or
    /// for a folder without one folder_overrides_id's; empty when it is
    /// refused.
    std::string profile_id{};
};

/// What folder_overrides_id's ids start with. No profile's id holds a ':'.
inline constexpr std::string_view folder_overrides_prefix = "folder:";

/// Returns the id a mod folder without a profile keeps Developer Mode's
/// overrides under in place of a profile's: folder_overrides_prefix, then
/// the folder's absolute path in UTF-8, each '%' written "%25" and each '|'
/// "%7C", so that no two folders share an id and no preference key holds a
/// '|'. A profile's id is kebab-case, so no profile, and not the plain 3.1c
/// baseline, takes one.
///
/// @param folder the mod folder
/// @return its id
[[nodiscard]] std::string folder_overrides_id(const std::filesystem::path& folder);

/// Checks a folder the player picked as a mod folder over a game folder,
/// as the next start will play it: it is a folder, and its oamod.yaml's
/// profile resolves over the game folder with the settings it binds
/// (resolve_folder_profile). Its profile's id, which no profile shares
/// with the plain 3.1c baseline's (a profile's id is kebab-case, and
/// ta-3.1c is not), keeps its overrides of the standard hacks apart from
/// the game's without a mod. A folder without an oamod.yaml is not refused
/// unless the game folder cannot carry a mod folder: it is marked to be
/// played without a profile once the player agrees, its overrides kept
/// under folder_overrides_id.
///
/// @param folder the folder picked
/// @param game_folder the game folder it lies over
/// @param choice whether hacks not implemented yet are accepted, and the
///        player's preferences; its folder and profile file are not looked at
/// @return why it cannot be played, or its profile's id
[[nodiscard]] PickedFolderCheck check_picked_mod_folder(
    const std::filesystem::path& folder,
    const std::filesystem::path& game_folder,
    const ModChoice& choice
);

/// Reads the values of an INI file as Section/Key settings.
///
/// Lines are "[Section]" or "Key = Value"; a ';' starts a comment, on a line
/// of its own or after a value, which ends there as the mod's own readers
/// take only its leading number or word; a line before any section is left
/// out; names keep their case and are matched without it later.
///
/// @param text the file's bytes
/// @return each value, named "Section/Key", in file order
[[nodiscard]] std::vector<data::mod_profile::SettingValue> read_ini_settings(std::string_view text);

/// Returns the prefix of the preference keys that stand in for a profile's
/// registry root: empty for the base game's root, whose keys are the base
/// game's own, else "registry:<root>\\".
///
/// @param profile the profile
/// @return the prefix
[[nodiscard]] std::string registry_key_prefix(const data::mod_profile::ModProfile& profile);

/// Writes the profile's registry seeds into the preferences that stand in
/// for its registry, each only where the value is absent, as a first run of
/// the mod would find them.
///
/// @param profile the profile
/// @param[in,out] values the preferences
/// @return whether a value was written
bool seed_registry(
    const data::mod_profile::ModProfile& profile, platform::preferences::Values& values
);

/// Returns the data layout a profile sets: its directory names, unit file
/// extension, map units section and build version.
///
/// @param profile the profile; null for the base game's layout
/// @return the layout
[[nodiscard]] data::defs::DataLayout data_layout_of(const data::mod_profile::ModProfile* profile);

/// Returns the archive discovery a profile sets: its revision archive,
/// installation archives and group patterns, the folders serving as the disc.
///
/// @param profile the profile; null for the base game's
/// @return the plan
[[nodiscard]] DiscoveryPlan discovery_plan_of(const data::mod_profile::ModProfile* profile);

/// Returns the file of the Data folder a profile plays one of the game's
/// movies from (`media.movies`).
///
/// @param profile the profile; null for the base game's movies
/// @param movie the movie, by the file 3.1c plays it from: 1.zrb the logo,
///        2.zrb the intro, 3.zrb and 4.zrb the campaign endings, 5.zrb the
///        credits
/// @return the profile's file for that movie, empty where it plays none;
///         `movie` itself without a profile, or for any other name
[[nodiscard]] std::string_view
movie_file_of(const data::mod_profile::ModProfile* profile, std::string_view movie);

/// Finds the profile in the root of a folder.
///
/// @param folder the folder
/// @param[out] error why the folder cannot be used: two names that differ
///        only in case, or a folder that cannot be listed; empty otherwise
/// @return the profile's path; nullopt when the folder holds none, or on error
[[nodiscard]] std::optional<std::filesystem::path>
find_mod_profile(const std::filesystem::path& folder, std::string& error);

/// Reads and resolves a profile file. A file that cannot be read gives one
/// error naming it.
///
/// @param file the profile
/// @param accept_unimplemented_hacks whether hacks this build does not
///        implement yet are accepted with a warning
/// @param settings the player's settings the profile's bindings read
/// @return the resolution, or the errors; warnings either way
[[nodiscard]] data::mod_profile::ResolveResult load_mod_profile(
    const std::filesystem::path& file,
    bool accept_unimplemented_hacks,
    const data::mod_profile::Settings& settings = {}
);

/// Runs --print-profile: resolves the --mod file, or the profile of the
/// game folder, and prints it with its hashes.
///
/// @param mod_file the --mod file; empty to use the game folder's profile
/// @param game_dir the game folder, used when `mod_file` is empty
/// @param accept_unimplemented_hacks whether hacks not implemented yet are accepted
/// @param[out] out receives the profile, or the note that the folder plays 3.1c
/// @param[out] err receives warnings and errors, one a line
/// @return the process's exit status: 0 when printed, 1 when the profile is refused
[[nodiscard]] int print_mod_profile(
    const std::filesystem::path& mod_file,
    const std::filesystem::path& game_dir,
    bool accept_unimplemented_hacks,
    std::ostream& out,
    std::ostream& err
);

/// Resolves the --mod file for a run of the game, so that a profile it
/// cannot use stops the run.
///
/// @param mod_file the --mod file
/// @param accept_unimplemented_hacks whether hacks not implemented yet are accepted
/// @param[out] out receives the line naming the profile and its sim hash, and the warnings
/// @return the resolved profile
/// @throws std::runtime_error naming every error when the profile is refused
data::mod_profile::ModProfile check_mod_profile(
    const std::filesystem::path& mod_file, bool accept_unimplemented_hacks, std::ostream& out
);

/// Tells a run which mod profile it plays: each warning, then a line naming
/// the profile and its sim hash.
///
/// @param profile the profile
/// @param warnings its warnings, one line each
/// @param[out] out receives the lines
void report_mod_profile(
    const data::mod_profile::ModProfile& profile,
    const std::vector<std::string>& warnings,
    std::ostream& out
);

} // namespace oa::app
