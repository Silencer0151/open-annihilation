// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

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
/// Reads a preferences file.
///
/// Throws std::runtime_error for an unreadable, oversized, corrupt or
/// duplicate-key file.
///
/// @param file preferences file
/// @return the key/value map; empty when the file is missing
Values load(const std::filesystem::path& file);
/// Writes a preferences file, replacing it from a temporary file in the same directory.
///
/// The file's folder is made when it is missing; a file named without a
/// folder is written in the current directory. A failed write preserves the
/// last complete file. Throws std::runtime_error on failure or when the
/// values exceed the size limits.
///
/// @param file preferences file
/// @param values key/value map to store
void save(const std::filesystem::path& file, const Values& values);

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
