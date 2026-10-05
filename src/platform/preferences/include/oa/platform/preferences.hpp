// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace oa::platform::preferences {
using Values = std::map<std::string, std::string>;
/// Returns the preferences file in the platform's user configuration location.
///
/// Never the installation, executable, asset directory or current working
/// directory. Throws std::runtime_error when the user directory cannot be
/// resolved.
///
/// @return the file path; the file need not exist
std::filesystem::path default_file();
/// Returns the platform's per-user folder for data the engine keeps, such as game data it unpacks.
///
/// On macOS and Windows it is the folder that holds the preferences file:
/// Application Support/net.coreprime.open-annihilation, as
/// apple_data_directory() settles it, and Local AppData/CorePrime/Open
/// Annihilation. On Linux it is $XDG_DATA_HOME/open-annihilation, or
/// $HOME/.local/share/open-annihilation when XDG_DATA_HOME is unset or
/// relative. Never the installation, executable, asset directory or current
/// working directory. Throws std::runtime_error when the user directory
/// cannot be resolved.
///
/// @return the folder; it need not exist
std::filesystem::path data_directory();
/// Returns the engine's per-user folder inside a macOS Application Support folder.
///
/// The folder is net.coreprime.open-annihilation. Earlier versions named it
/// com.coreprime.open-annihilation: when only that folder exists, it is
/// renamed to the new name first, so the preferences file and the unpacked
/// game data move with it. When the rename fails, the result is the folder
/// under its earlier name, and a line on standard error says so. While the
/// new folder exists, the earlier one is never touched. Creates, copies and
/// merges nothing, and never throws.
///
/// @param application_support the Application Support folder
/// @return the folder to use; it need not exist
std::filesystem::path apple_data_directory(const std::filesystem::path& application_support);
/// The name of the player's own folder, which holds their saved games,
/// screenshots, films, recordings and mods, inside their Documents folder.
inline constexpr std::string_view user_folder_name = "Open Annihilation";
/// Returns the player's Documents folder, as the system names it.
///
/// On macOS it is the user's Documents directory as Foundation resolves it.
/// On Windows it is the Documents folder (My Documents on Windows XP),
/// wherever it has been redirected to. On Linux it is the folder
/// XDG_DOCUMENTS_DIR names in user-dirs.dirs, read from $XDG_CONFIG_HOME, or
/// from $HOME/.config when XDG_CONFIG_HOME is unset or relative
/// (xdg_documents_directory), else $HOME/Documents. Never the installation,
/// executable, asset directory or current working directory. Throws
/// std::runtime_error when the folder cannot be resolved.
///
/// @return the folder; it need not exist
std::filesystem::path documents_directory();
/// Returns the folder XDG_DOCUMENTS_DIR names in the text of a user-dirs.dirs
/// file.
///
/// A line reads XDG_DOCUMENTS_DIR="$HOME/name" for a folder in the home
/// folder, or XDG_DOCUMENTS_DIR="/path" for an absolute one; a backslash
/// keeps the character after it, and no other variable is expanded. Spaces
/// may stand before the name and round the '='. The last such line counts;
/// a line of another form, or a comment, is passed over.
///
/// @param text the file's text
/// @param home the home folder that $HOME stands for
/// @return the folder; nullopt when no line names one
std::optional<std::filesystem::path>
xdg_documents_directory(std::string_view text, const std::filesystem::path& home);
/// Returns the player's own folder: user_folder_name inside
/// documents_directory(). Throws std::runtime_error when the Documents
/// folder cannot be resolved.
///
/// @return the folder; it need not exist
std::filesystem::path default_user_folder();
/// Reads a preferences file.
///
/// Its lines may end in LF or CR LF. Throws std::runtime_error for an unreadable, oversized, corrupt or
/// duplicate-key file.
///
/// @param file preferences file
/// @return the key/value map; empty when the file is missing
Values load(const std::filesystem::path& file);
/// Whether save() also makes the replacement of the file last through a
/// system crash.
enum class SyncFolder : uint8_t {
    no,  ///< the file's bytes are flushed before the replace; the folder is left to the system
    yes, ///< the folder is synced after the replace as well, so a system crash leaves the old file or the new one
};
/// Writes a preferences file, replacing it from a temporary file in the same directory.
///
/// The file's folder is made when it is missing; a file named without a
/// folder is written in the current directory. The temporary file is flushed
/// to the disk before it replaces the file, and a failed write preserves the
/// last complete file. On Windows the replace itself is written through to
/// the disk either way; elsewhere SyncFolder::yes also syncs the folder after
/// the rename, and a folder that cannot be synced is left as the system keeps
/// it, the file replaced. Throws std::runtime_error on failure or when the
/// values exceed the size limits.
///
/// @param file preferences file
/// @param values key/value map to store
/// @param sync whether the folder is synced after the replace
void save(
    const std::filesystem::path& file, const Values& values, SyncFolder sync = SyncFolder::no
);
/// Rewrites a preferences file in place, for a small file that is rewritten often.
///
/// Writes the same format as save(), but straight into the file: no
/// temporary file, no rename and no flush, so the system writes it to the
/// disk when it chooses. A crash or a full disk during the write can leave
/// the file cut short, which load() then rejects. The file's folder is made
/// when it is missing; a file named without a folder is written in the
/// current directory. Throws std::runtime_error on failure or when the
/// values exceed the size limits.
///
/// @param file preferences file
/// @param values key/value map to store
void overwrite(const std::filesystem::path& file, const Values& values);

inline constexpr uint32_t default_music_volume = 0x20; // musicvol after RESTORE
inline constexpr uint8_t default_cd_mode = 4;          // cdmode after RESTORE

// Bit 0 of Game.music_flags, stored under the musicmode preference key.
namespace music_flag {
inline constexpr uint16_t mode = 1;
} // namespace music_flag

struct MusicOptions {
    uint32_t music_volume{}; // Game.music_volume
    uint16_t music_flags{};  // Game.music_flags
    uint8_t cd_mode{};       // Game.cd_mode
};

/// Applies the music screen's RESTORE button: default volume and CD mode, music mode on.
///
/// Changes only `options`: no preference is written, no GUI is touched and
/// the CD player's volume is left alone, where 3.1c also updates the CD
/// player and reapplies the saved volumes. The music screen's UNDO button is
/// handled separately.
///
/// @param[in,out] options music settings; musicvol becomes 0x20 and cdmode 4,
///        and bit 0 of the flag word is set when clear, every other bit kept
void restore_music_defaults(MusicOptions& options) noexcept;
} // namespace oa::platform::preferences
