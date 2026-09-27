// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace oa::audio {

// A music directory played as the game CD. Files are named by disc
// track number (2.mp3, 3.ogg, ...). Track 1 of the game disc is the data
// track, so it is always reported as data and never played; stray 0.* and
// 1.* files are ignored. Playable tracks run from 2 to the highest number
// for which every lower track from 2 exists.
inline constexpr int32_t music_disc_first_audio_track = 2;
inline constexpr int32_t music_disc_max_tracks = 99;

struct MusicDisc {
    std::filesystem::path directory;
    // Indexed by disc track number; entries 0 and 1 are empty.
    std::vector<std::filesystem::path> tracks;
    int32_t track_count{}; // disc tracks, including the data track
    uint32_t disc_id{};    // stable identity keyed into CDLISTS
};

/// Resolves the "music" directory case-insensitively beneath the game directory.
///
/// @param game_dir Game installation directory.
/// @return The matching subdirectory, or game_dir/"music" when none exists.
[[nodiscard]] std::filesystem::path music_disc_directory(const std::filesystem::path& game_dir);

/// Scans a directory for numbered .mp3/.ogg/.wav/.flac files.
///
/// An unreadable or empty directory yields a disc with no tracks. The disc
/// identity hashes the track count and file sizes.
///
/// @param directory Music directory to scan.
/// @return The disc's track files, count and identity.
[[nodiscard]] MusicDisc music_disc_scan(const std::filesystem::path& directory);

/// Reports whether the disc has at least one playable track.
///
/// @param disc Scanned music disc.
/// @return True when track 2 exists.
[[nodiscard]] inline bool music_disc_present(const MusicDisc& disc) noexcept {
    return disc.track_count >= music_disc_first_audio_track;
}

} // namespace oa::audio
