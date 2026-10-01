// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The intro player on the small movie of smacker_test_movie.hpp: what it
// reports when opened, the first frame's snapshot in RGB, the samples it
// decodes, a frame limit, its bounds, a damaged frame, and playback through
// SDL with the dummy video and audio drivers (ctest sets them), which opens
// no window and plays no sound.
#include "oa/media/intro_player.hpp"
#include "smacker_test_movie.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <source_location>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace movie = oa::formats::smacker::test_movie;
using oa::media::IntroPlayer;

int failures = 0;

/// Reports a failed check with its line and counts it.
///
/// @param condition the check
/// @param what what was expected
/// @param where the check's place in the source
/// @return `condition`
bool check(
    bool condition, const char* what, std::source_location where = std::source_location::current()
) {
    if (!condition) {
        std::fprintf(stderr, "%s:%u: FAIL: %s\n", where.file_name(), where.line(), what);
        ++failures;
    }
    return condition;
}

/// Writes bytes to a file.
///
/// @param path the file
/// @param bytes its contents
void write_file(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
    );
}

/// Returns a file's bytes.
///
/// @param path the file
std::vector<uint8_t> read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

/// Returns the RGB frame 0's palette gives an index.
///
/// @param index the palette index
std::array<uint8_t, 3> first_palette_rgb(uint8_t index) {
    const auto components = movie::first_palette_components(index);
    std::array<uint8_t, 3> rgb{};
    for (std::size_t channel = 0; channel < rgb.size(); ++channel)
        rgb[channel] = static_cast<uint8_t>(components[channel] << 2 | components[channel] >> 4);
    return rgb;
}

/// Checks a headless run: the information, frames, samples and snapshot.
///
/// @param scratch directory for the test's files
void test_headless(const fs::path& scratch) {
    const auto path = scratch / "movie.smk";
    write_file(path, movie::movie_file());
    auto opened = IntroPlayer::open(path);
    if (!check(static_cast<bool>(opened), "the movie opens"))
        return;
    const auto& info = opened.player->info();
    check(info.width == 8 && info.height == 8 && info.frame_count == 3, "size and frames");
    check(info.frame_rate > 30.0 && info.frame_rate < 30.01, "100000 / 3333 frames a second");
    check(
        info.has_audio && info.audio_sample_rate == 22050 && info.audio_channels == 2, "the track"
    );

    oa::media::PlaybackOptions options;
    options.headless_check = true;
    options.snapshot_path = scratch / "first.ppm";
    const auto played = opened.player->play(options);
    check(played.ok() && !played.skipped, "the movie plays through");
    check(played.decoded_frames == 3, "three frames");
    check(played.decoded_audio_bytes == movie::kFirstAudioSamples.size() * 2, "six 16-bit samples");

    // The snapshot is frame 0 in RGB through frame 0's palette.
    const auto snapshot = read_file(options.snapshot_path);
    const std::string head = "P6\n8 8\n255\n";
    if (check(snapshot.size() == head.size() + 8 * 8 * 3, "the snapshot is an 8x8 PPM")) {
        check(std::equal(head.begin(), head.end(), snapshot.begin()), "the PPM header");
        const auto indices = movie::expected_indices(0);
        bool same = true;
        for (std::size_t pixel = 0; pixel < indices.size(); ++pixel) {
            const auto rgb = first_palette_rgb(indices[pixel]);
            for (std::size_t channel = 0; channel < 3; ++channel)
                same = same && snapshot[head.size() + pixel * 3 + channel] == rgb[channel];
        }
        check(same, "every pixel is its index's colour");
        check(
            snapshot[head.size()] == 195 && snapshot[head.size() + 1] == 48 &&
                snapshot[head.size() + 2] == 60,
            "pixel (0, 0) is colour 0x30: (195, 48, 60)"
        );
    }

    auto limited = IntroPlayer::open(path);
    options.snapshot_path.clear();
    options.frame_limit = 2;
    check(
        limited && limited.player->play(options).decoded_frames == 2, "a frame limit stops early"
    );

    oa::media::PlayerLimits small_audio;
    small_audio.max_audio_bytes = 4;
    auto bounded = IntroPlayer::open(path, small_audio);
    options.frame_limit = 0;
    check(
        bounded && !bounded.player->play(options).ok(), "audio over the bound stops with an error"
    );

    oa::media::PlayerLimits small_video;
    small_video.max_video_pixels = 63;
    check(!IntroPlayer::open(path, small_video), "a movie over the pixel bound is refused");
    fs::remove(options.snapshot_path);
    fs::remove(scratch / "first.ppm");
    fs::remove(path);
}

/// Checks that a damaged frame stops playback with an error.
///
/// @param scratch directory for the test's files
void test_damaged(const fs::path& scratch) {
    auto bytes = movie::movie_file();
    // Frame 2's palette chunk claims more bytes than the frame holds.
    const auto frame_two = bytes.size() - movie::frame_payload(2).size();
    bytes[frame_two] = 0xFF;
    const auto path = scratch / "damaged.smk";
    write_file(path, bytes);
    auto opened = IntroPlayer::open(path);
    if (check(static_cast<bool>(opened), "the damaged movie opens")) {
        oa::media::PlaybackOptions options;
        options.headless_check = true;
        const auto played = opened.player->play(options);
        check(!played.ok() && played.decoded_frames == 2, "playback stops at the damaged frame");
    }
    fs::remove(path);
}

/// Plays the movie through SDL's dummy drivers, as the game would.
///
/// @param scratch directory for the test's files
void test_through_sdl(const fs::path& scratch) {
    const auto path = scratch / "sdl.smk";
    write_file(path, movie::movie_file());
    auto opened = IntroPlayer::open(path);
    if (check(static_cast<bool>(opened), "the movie opens for SDL")) {
        const auto played = opened.player->play({});
        check(played.ok(), played.error.empty() ? "plays through SDL" : played.error.c_str());
        check(
            played.decoded_frames == 3 && played.decoded_audio_bytes == 12, "every frame and sample"
        );
    }
    fs::remove(path);
}

} // namespace

int main(int argc, char** argv) {
    const auto scratch =
        argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "oa-intro-player";
    std::error_code created;
    fs::create_directories(scratch, created);
    test_headless(scratch);
    test_damaged(scratch);
    test_through_sdl(scratch);
    if (failures != 0)
        return 1;
    std::printf("intro player plays Smacker movies\n");
    return 0;
}
