// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The intro player without SDL3: it checks each movie, then skips it.

#include "oa/media/intro_player.hpp"
#include "oa/formats/smacker.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using oa::media::IntroPlayer;
using oa::formats::smacker::on_disk::FileHeader;

// A 4x2 SMK2 movie of two frames at -3333 (100000 / 3333 fps), one audio
// track, three bytes of trees and frame payloads of five and three bytes.
constexpr uint32_t kMovieWidth = 4;
constexpr uint32_t kMovieHeight = 2;
constexpr uint32_t kMovieFrames = 2;
constexpr int32_t kMovieRate = -3333;
constexpr uint32_t kMovieTreeBytes = 3;
constexpr uint32_t kFirstFrameBytes = 5;
constexpr uint32_t kSecondFrameBytes = 3;
constexpr uint32_t kAudioTrackBytes = 9;
constexpr uint32_t kAudioTrackSampleHz = 22050;
// The player reads no format bits, so only the present flag is set.
constexpr uint32_t kAudioTrackRate =
    oa::formats::smacker::kAudioTrackPresentFlag | kAudioTrackSampleHz;
constexpr size_t kMovieFileBytes = oa::formats::smacker::kSmackerFixedHeaderBytes +
                                   kMovieFrames * oa::formats::smacker::kFrameTableEntryBytes +
                                   kMovieTreeBytes + kFirstFrameBytes + kSecondFrameBytes;

int failures = 0;

/// Records a failure and prints what was expected when the condition is false.
///
/// @param condition The expectation.
/// @param what What was expected.
void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

/// Writes a little-endian 32-bit value.
///
/// @param[in,out] bytes The file image.
/// @param offset Byte offset of the value.
/// @param value The value.
void put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset + shift / 8] = static_cast<uint8_t>(value >> shift);
}

/// Builds the 4x2, two-frame SMK2 movie described above.
///
/// @return The file image.
std::vector<uint8_t> movie_bytes() {
    std::vector<uint8_t> bytes(kMovieFileBytes, 0);
    put32(bytes, offsetof(FileHeader, signature), oa::formats::smacker::kSmk2);
    put32(bytes, offsetof(FileHeader, width), kMovieWidth);
    put32(bytes, offsetof(FileHeader, height), kMovieHeight);
    put32(bytes, offsetof(FileHeader, frames), kMovieFrames);
    put32(bytes, offsetof(FileHeader, frame_rate), static_cast<uint32_t>(kMovieRate));
    put32(bytes, offsetof(FileHeader, audio_size), kAudioTrackBytes);
    put32(bytes, offsetof(FileHeader, trees_size), kMovieTreeBytes);
    put32(bytes, offsetof(FileHeader, audio_rate), kAudioTrackRate);
    const auto frame_sizes = oa::formats::smacker::kSmackerFixedHeaderBytes;
    put32(bytes, frame_sizes, kFirstFrameBytes);
    put32(bytes, frame_sizes + oa::formats::smacker::kFrameSizeEntryBytes, kSecondFrameBytes);
    return bytes;
}

/// Writes a file image to disk.
///
/// @param path Destination file.
/// @param bytes The file image.
/// @return Whether every byte was written.
bool write_file(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
    );
    return static_cast<bool>(output);
}

/// Checks that an empty path, a missing file and an SMK4 movie are refused.
///
/// @param scratch Directory for the test's files.
void test_refuses_what_it_cannot_check(const fs::path& scratch) {
    const auto empty = IntroPlayer::open({});
    check(!empty && empty.error == "intro movie path is empty", "an empty path is refused");

    const auto missing = IntroPlayer::open(scratch / "missing.zrb");
    check(
        !missing && missing.error.starts_with("SMK2 preflight: "),
        "a missing movie fails the SMK2 check"
    );

    auto bytes = movie_bytes();
    put32(bytes, 0, oa::formats::smacker::kSmk4);
    const auto smk4 = scratch / "smk4.zrb";
    check(write_file(smk4, bytes), "the SMK4 movie is written");
    const auto refused = IntroPlayer::open(smk4);
    check(!refused && refused.error.starts_with("SMK2 preflight: "), "an SMK4 movie is refused");
    fs::remove(smk4);
}

/// Checks that a valid movie opens with its header's facts and plays as skipped.
///
/// @param scratch Directory for the test's files.
void test_skips_a_valid_movie(const fs::path& scratch) {
    const auto path = scratch / "movie.zrb";
    check(write_file(path, movie_bytes()), "the movie is written");

    oa::media::PlayerLimits small;
    small.max_video_pixels = kMovieWidth * kMovieHeight - 1;
    const auto too_large = IntroPlayer::open(path, small);
    check(
        !too_large && too_large.error == "intro video dimensions exceed bounds",
        "a movie over the pixel bound is refused"
    );

    auto opened = IntroPlayer::open(path);
    check(static_cast<bool>(opened), "a valid SMK2 movie opens");
    if (opened) {
        const auto& info = opened.player->info();
        check(info.width == static_cast<int>(kMovieWidth), "width comes from the header");
        check(info.height == static_cast<int>(kMovieHeight), "height comes from the header");
        check(info.frame_count == kMovieFrames, "frame count comes from the header");
        check(info.frame_rate > 30.0 && info.frame_rate < 30.01, "rate is 100000 / 3333 fps");
        check(!info.has_audio, "nothing will be played, so no audio is reported");

        oa::media::PlaybackOptions options;
        options.headless_check = true;
        const auto result = opened.player->play(options);
        check(result.ok(), "playback reports no error");
        check(result.skipped, "the movie is reported skipped");
        check(result.decoded_frames == 0, "no frames are decoded");
        check(result.decoded_audio_bytes == 0, "no audio is decoded");
    }
    fs::remove(path);
}

} // namespace

int main(int argc, char** argv) {
    const auto scratch = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path();
    std::error_code created;
    fs::create_directories(scratch, created);
    test_refuses_what_it_cannot_check(scratch);
    test_skips_a_valid_movie(scratch);
    if (failures != 0)
        return 1;
    std::printf("intro player without SDL3 checks and skips movies\n");
    return 0;
}
