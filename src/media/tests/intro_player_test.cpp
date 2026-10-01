// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The intro player on the small movie of smacker_test_movie.hpp: what it
// reports when opened, the first frame's snapshot in RGB, the samples it
// decodes, a frame limit, its bounds, a damaged frame, and playback through
// SDL with the dummy video and audio drivers (ctest sets them), which opens
// no window and plays no sound. On sound outputs of the test's own it plays
// without a sound device, without a stream, and on a device that stops
// taking samples, where it waits a bounded time; each time it stops the
// output it started. Every file the test writes is removed once the players
// that read it are gone, which Windows allows only when they closed it.
#include "oa/audio/sound_output.hpp"
#include "oa/media/intro_player.hpp"
#include "smacker_test_movie.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <source_location>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace movie = oa::formats::smacker::test_movie;
using oa::media::IntroPlayer;

// Longer than the player's 5 s wait for samples to play out, with room for a
// slow machine; a wait past it is one the player did not bound.
constexpr auto kStalledSoundBound = std::chrono::seconds(30);
// The bytes of the movie's six 16-bit samples, all in its first frame.
constexpr std::size_t kMovieSampleBytes = movie::kFirstAudioSamples.size() * sizeof(int16_t);

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

/// A stream on a device that never takes its samples: all it is given stays
/// queued.
class StalledStream final : public oa::audio::OutputStream {
  public:

    /// @param bytes_put counts the bytes put, for the test
    explicit StalledStream(int32_t& bytes_put) : bytes_put_(bytes_put) {}

    bool put(const void*, int32_t count) override {
        bytes_put_ += count;
        queued_ += count;
        return true;
    }

    bool flush() override { return true; }

    int32_t queued_bytes() override { return queued_; }

    int32_t available_bytes() override { return 0; }

    void clear() override { queued_ = 0; }

    bool set_gain(float) override { return true; }

    bool pause() override { return true; }

    bool resume() override { return true; }

    void lock() override {}

    void unlock() override {}

  private:

    int32_t& bytes_put_;
    int32_t queued_ = 0;
};

/// A sound output with one fault, counting what the player asks of it.
class TestOutput final : public oa::audio::SoundOutput {
  public:

    enum class Fault {
        cannot_start, ///< no sound device: start() fails
        cannot_open,  ///< the device starts and refuses every stream
        stalls,       ///< streams open and never play what they are given
    };

    explicit TestOutput(Fault fault) : fault_(fault) {}

    int starts = 0;        ///< start() calls that succeeded
    int stops = 0;         ///< stop() calls
    int streams = 0;       ///< streams opened
    int32_t bytes_put = 0; ///< bytes put to the streams

    bool start(std::string& error) override {
        if (fault_ == Fault::cannot_start) {
            error = "no sound device";
            return false;
        }
        ++starts;
        return true;
    }

    void stop() override { ++stops; }

    bool started() const override { return starts > stops; }

    std::unique_ptr<oa::audio::OutputStream> open_stream(
        const oa::audio::StreamFormat&, oa::audio::StreamFeed, void*, std::string& error
    ) override {
        if (fault_ == Fault::cannot_open) {
            error = "the device refuses streams";
            return nullptr;
        }
        ++streams;
        return std::make_unique<StalledStream>(bytes_put);
    }

    std::string driver_name() const override { return "test"; }

    std::string last_error() const override { return {}; }

  private:

    Fault fault_;
};

/// Removes a file the test wrote, and checks that nothing holds it open:
/// Windows refuses to remove a file that is open without delete sharing, as
/// a player's movie is until the player is destroyed.
///
/// @param path the file
/// @param where the call's place in the source
void remove_file(
    const fs::path& path, std::source_location where = std::source_location::current()
) {
    std::error_code error;
    fs::remove(path, error);
    check(!error, "the test's file is free to remove", where);
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

/// Checks a headless run of a movie: the information, frames, samples and
/// snapshot. The players it opens are destroyed when it returns.
///
/// @param path the movie
/// @param snapshot_path where the first frame's snapshot is written
void check_headless(const fs::path& path, const fs::path& snapshot_path) {
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
    options.snapshot_path = snapshot_path;
    const auto played = opened.player->play(options);
    check(played.ok() && !played.skipped, "the movie plays through");
    check(played.decoded_frames == 3, "three frames");
    check(played.decoded_audio_bytes == kMovieSampleBytes, "six 16-bit samples");

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
}

/// Checks a headless run: the information, frames, samples and snapshot.
///
/// @param scratch directory for the test's files
void test_headless(const fs::path& scratch) {
    const auto path = scratch / "movie.smk";
    write_file(path, movie::movie_file());
    check_headless(path, scratch / "first.ppm");
    // Each player closed the movie when it was destroyed.
    remove_file(scratch / "first.ppm");
    remove_file(path);
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
    {
        auto opened = IntroPlayer::open(path);
        if (check(static_cast<bool>(opened), "the damaged movie opens")) {
            oa::media::PlaybackOptions options;
            options.headless_check = true;
            const auto played = opened.player->play(options);
            check(
                !played.ok() && played.decoded_frames == 2, "playback stops at the damaged frame"
            );
        }
    }
    remove_file(path);
}

/// Plays the movie through SDL's dummy drivers, as the game would.
///
/// @param scratch directory for the test's files
void test_through_sdl(const fs::path& scratch) {
    const auto path = scratch / "sdl.smk";
    write_file(path, movie::movie_file());
    {
        auto opened = IntroPlayer::open(path);
        if (check(static_cast<bool>(opened), "the movie opens for SDL")) {
            const auto played = opened.player->play({});
            check(played.ok(), played.error.empty() ? "plays through SDL" : played.error.c_str());
            check(
                played.decoded_frames == 3 && played.decoded_audio_bytes == kMovieSampleBytes,
                "every frame and sample"
            );
        }
    }
    remove_file(path);
}

/// Plays the movie once through SDL's dummy video on a sound output of the
/// test's, the player destroyed before it returns.
///
/// @param output the sound output
/// @param path the movie
/// @return what playback reported
oa::media::PlaybackResult play_on(TestOutput& output, const fs::path& path) {
    oa::audio::set_sound_output(&output);
    oa::media::PlaybackResult played;
    {
        auto opened = IntroPlayer::open(path);
        if (check(static_cast<bool>(opened), "the movie opens for a test output"))
            played = opened.player->play({});
    }
    oa::audio::set_sound_output(nullptr);
    return played;
}

/// Checks that a movie whose sound cannot play is shown without it, and that
/// the output is stopped as often as it started.
///
/// @param scratch directory for the test's files
void test_without_sound(const fs::path& scratch) {
    const auto path = scratch / "silent.smk";
    write_file(path, movie::movie_file());
    TestOutput no_device(TestOutput::Fault::cannot_start);
    const auto silent = play_on(no_device, path);
    check(
        silent.ok() && silent.decoded_frames == 3 && no_device.streams == 0,
        "a movie plays without a sound device"
    );
    check(no_device.stops == 0, "an output that did not start is not stopped");
    TestOutput refusing(TestOutput::Fault::cannot_open);
    const auto refused = play_on(refusing, path);
    check(refused.ok() && refused.decoded_frames == 3, "a movie plays when its stream cannot open");
    check(refusing.starts == 1 && refusing.stops == 1, "the output it started is stopped");
    remove_file(path);
}

/// Checks that a sound device that stops taking samples holds full playback
/// only a bounded time, which ends with an error.
///
/// @param scratch directory for the test's files
void test_stalled_sound(const fs::path& scratch) {
    const auto path = scratch / "stalled.smk";
    write_file(path, movie::movie_file());
    TestOutput stalled(TestOutput::Fault::stalls);
    const auto begun = std::chrono::steady_clock::now();
    const auto played = play_on(stalled, path);
    const auto waited = std::chrono::steady_clock::now() - begun;
    check(
        played.decoded_frames == 3 &&
            static_cast<std::size_t>(stalled.bytes_put) == kMovieSampleBytes,
        "every frame is shown and every sample put"
    );
    check(
        played.error == "audio stream did not drain before timeout",
        "samples that never play end the movie with an error"
    );
    check(waited < kStalledSoundBound, "the wait for a stalled device is bounded");
    check(stalled.starts == 1 && stalled.stops == 1, "the stalled output is stopped");
    remove_file(path);
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
    test_without_sound(scratch);
    test_stalled_sound(scratch);
    if (failures != 0)
        return 1;
    std::printf("intro player plays Smacker movies\n");
    return 0;
}
