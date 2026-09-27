// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/music_disc.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>

namespace oa::audio {
namespace {

constexpr const char* music_directory_name = "music";
constexpr const char* track_extensions[] = {".mp3", ".ogg", ".wav", ".flac"};
constexpr uint32_t fnv_offset = 2166136261U;
constexpr uint32_t fnv_prime = 16777619U;

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

// Parses "<n>.<ext>" with a supported extension; 0 when not a track file.
int32_t track_number(const std::filesystem::path& file) {
    const std::string extension = lower(file.extension().string());
    if (std::find(std::begin(track_extensions), std::end(track_extensions), extension) ==
        std::end(track_extensions))
        return 0;
    const std::string stem = file.stem().string();
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

} // namespace

std::filesystem::path music_disc_directory(const std::filesystem::path& game_dir) {
    std::error_code error;
    for (std::filesystem::directory_iterator it(game_dir, error), end; !error && it != end;
         it.increment(error)) {
        if (lower(it->path().filename().string()) == music_directory_name &&
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
        if (slot.empty() || lower(it->path().extension().string()) == track_extensions[0])
            slot = it->path();
    }
    int32_t last = music_disc_first_audio_track - 1;
    while (last < music_disc_max_tracks && !found[static_cast<std::size_t>(last) + 1].empty())
        ++last;
    if (last < music_disc_first_audio_track)
        return disc;
    disc.track_count = last;
    disc.tracks.assign(found.begin(), found.begin() + last + 1);
    uint32_t hash = fnv_offset;
    hash_word(hash, static_cast<uint64_t>(last));
    for (int32_t track = music_disc_first_audio_track; track <= last; ++track) {
        const auto size =
            std::filesystem::file_size(disc.tracks[static_cast<std::size_t>(track)], error);
        hash_word(hash, error ? 0 : static_cast<uint64_t>(size));
    }
    disc.disc_id = hash == 0 ? 1 : hash;
    return disc;
}

} // namespace oa::audio
