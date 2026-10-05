// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/music_disc.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>
#include <vector>

namespace oa::audio {
namespace {

constexpr const char* music_directory_name = "music";
constexpr const char* track_extensions[] = {".mp3", ".ogg", ".wav", ".flac"};
constexpr uint32_t fnv_offset = 2166136261U;
constexpr uint32_t fnv_prime = 16777619U;

/// Returns a path's UTF-8 spelling: a name outside the system's code page
/// has no narrow spelling on Windows.
///
/// @param path the path
/// @return its UTF-8 spelling
std::string utf8_text(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

// Parses "<n>.<ext>" with a supported extension; 0 when not a track file.
int32_t track_number(const std::filesystem::path& file) {
    const std::string extension = lower(utf8_text(file.extension()));
    if (std::find(std::begin(track_extensions), std::end(track_extensions), extension) ==
        std::end(track_extensions))
        return 0;
    const std::string stem = utf8_text(file.stem());
    if (stem.empty() || stem.size() > 2)
        return 0;
    int32_t number = 0;
    for (const char c : stem) {
        if (c < '0' || c > '9')
            return 0;
        number = number * 10 + (c - '0');
    }
    return number;
}

void hash_word(uint32_t& hash, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        hash ^= static_cast<uint32_t>((value >> (8 * i)) & 0xffU);
        hash *= fnv_prime;
    }
}

// Names the disc by its track count and its audio tracks' sizes.
void identify_disc(MusicDisc& disc) {
    std::error_code error;
    uint32_t hash = fnv_offset;
    hash_word(hash, static_cast<uint64_t>(disc.track_count));
    for (int32_t track = music_disc_first_audio_track; track <= disc.track_count; ++track) {
        const auto size =
            std::filesystem::file_size(disc.tracks[static_cast<std::size_t>(track)], error);
        hash_word(hash, error ? 0 : static_cast<uint64_t>(size));
    }
    disc.disc_id = hash == 0 ? 1 : hash;
}

// The regular files of a directory whose extension is .mp3, matched
// without case.
std::vector<std::filesystem::path> mp3_files(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error))
        if (it->is_regular_file(error) && lower(utf8_text(it->path().extension())) == ".mp3")
            files.push_back(it->path());
    return files;
}

// Upper-cases ASCII letters, the order names sort in without case.
std::string upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return text;
}

} // namespace

std::filesystem::path music_disc_directory(const std::filesystem::path& game_dir) {
    std::error_code error;
    for (std::filesystem::directory_iterator it(game_dir, error), end; !error && it != end;
         it.increment(error)) {
        if (lower(utf8_text(it->path().filename())) == music_directory_name &&
            it->is_directory(error))
            return it->path();
    }
    return game_dir / music_directory_name;
}

MusicDisc music_disc_scan(const std::filesystem::path& directory) {
    MusicDisc disc;
    disc.directory = directory;
    std::vector<std::filesystem::path> found(static_cast<std::size_t>(music_disc_max_tracks) + 1);
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
        if (!it->is_regular_file(error))
            continue;
        const int32_t number = track_number(it->path().filename());
        if (number < music_disc_first_audio_track || number > music_disc_max_tracks)
            continue;
        auto& slot = found[static_cast<std::size_t>(number)];
        // Prefer the first extension in the list when several exist.
        if (slot.empty() || lower(utf8_text(it->path().extension())) == track_extensions[0])
            slot = it->path();
    }
    int32_t last = music_disc_first_audio_track - 1;
    while (last < music_disc_max_tracks && !found[static_cast<std::size_t>(last) + 1].empty())
        ++last;
    if (last < music_disc_first_audio_track)
        return disc;
    disc.track_count = last;
    disc.tracks.assign(found.begin(), found.begin() + last + 1);
    identify_disc(disc);
    return disc;
}

MusicDisc music_disc_scan_numbered(const std::filesystem::path& directory) {
    MusicDisc disc;
    disc.directory = directory;
    std::vector<std::filesystem::path> found(static_cast<std::size_t>(music_disc_max_tracks) + 1);
    for (const auto& file : mp3_files(directory)) {
        // Only the number's own spelling: 01.mp3 is not track 1.
        const int32_t number = track_number(file.filename());
        if (number >= 1 && number <= music_disc_max_tracks &&
            utf8_text(file.stem()) == std::to_string(number))
            found[static_cast<std::size_t>(number)] = file;
    }
    int32_t last = 0;
    while (last < music_disc_max_tracks && !found[static_cast<std::size_t>(last) + 1].empty())
        ++last;
    if (last < music_disc_first_audio_track)
        return disc;
    disc.track_count = last;
    disc.tracks.assign(found.begin(), found.begin() + last + 1);
    // Track 1 is the data track whatever its file holds.
    disc.tracks[1].clear();
    identify_disc(disc);
    return disc;
}

MusicDisc music_disc_scan_folder(const std::filesystem::path& directory) {
    MusicDisc disc;
    disc.directory = directory;
    auto files = mp3_files(directory);
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        const auto left = upper(utf8_text(a.filename()));
        const auto right = upper(utf8_text(b.filename()));
        return left != right ? left < right : utf8_text(a.filename()) < utf8_text(b.filename());
    });
    const auto playable =
        static_cast<std::size_t>(music_disc_max_tracks - music_disc_first_audio_track + 1);
    if (files.size() > playable)
        files.resize(playable);
    if (files.empty())
        return disc;
    disc.track_count = static_cast<int32_t>(files.size()) + music_disc_first_audio_track - 1;
    disc.tracks.assign(static_cast<std::size_t>(music_disc_first_audio_track), {});
    disc.tracks.insert(disc.tracks.end(), files.begin(), files.end());
    identify_disc(disc);
    return disc;
}

} // namespace oa::audio
