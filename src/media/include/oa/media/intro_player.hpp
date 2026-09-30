// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;

namespace oa::media {

inline constexpr std::size_t kDefaultMaxDecodedFrames = 100000;
inline constexpr std::size_t kDefaultMaxChunks = 4000000;
inline constexpr std::size_t kDefaultMaxAudioBytes = 128U * 1024U * 1024U;
inline constexpr std::size_t kDefaultMaxVideoPixels = 4096U * 4096U;

struct PlayerLimits {
    std::size_t max_decoded_frames = kDefaultMaxDecodedFrames;
    std::size_t max_chunks = kDefaultMaxChunks;
    std::size_t max_audio_bytes = kDefaultMaxAudioBytes;
    std::size_t max_video_pixels = kDefaultMaxVideoPixels;
};

/// What playback leaves to the application while a movie plays.
struct PlaybackHooks {
    void* context{};
    /// Receives each window event playback does not act on itself, such as
    /// the application's Alt+Enter; null discards them.
    void (*window_event)(void* context, const SDL_Event& event){};
};

struct PlaybackOptions {
    // Zero means decode until the stream ends, subject to PlayerLimits.
    std::size_t frame_limit = 0;
    bool headless_check = false;
    bool play_audio = true;
    // When set, write the first decoded RGB frame as a binary PPM (P6).
    std::filesystem::path snapshot_path;
    // When both are set, play into this already-created window instead of
    // creating and destroying a transient intro window.
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    PlaybackHooks hooks{};
};

struct VideoInfo {
    int width = 0;
    int height = 0;
    int64_t frame_count = 0;
    double frame_rate = 0.0;
    bool has_audio = false;
    int audio_sample_rate = 0;
    int audio_channels = 0;
};

struct PlaybackResult {
    bool skipped = false;
    std::size_t decoded_frames = 0;
    std::size_t decoded_audio_bytes = 0;
    std::string error;

    /// Reports whether playback finished without an error.
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

class IntroPlayer;
struct OpenPlayerResult;

class IntroPlayer {
  public:

    IntroPlayer(IntroPlayer&&) noexcept;
    IntroPlayer& operator=(IntroPlayer&&) noexcept;
    IntroPlayer(const IntroPlayer&) = delete;
    IntroPlayer& operator=(const IntroPlayer&) = delete;
    ~IntroPlayer();

    /// Opens a movie file and its decoders.
    ///
    /// Without FFmpeg (oa-media-intro-player-null) only the SMK2 header and tables
    /// are checked, and info() carries the header's size, frame count and rate.
    ///
    /// @param path Movie file path; must not be empty.
    /// @param limits Bounds on decoded frames, reads, audio bytes and video size.
    /// @return The player, or an error message when the file cannot be played.
    [[nodiscard]] static OpenPlayerResult
    open(const std::filesystem::path& path, const PlayerLimits& limits = {});

    /// Returns the movie's dimensions, frame count, rate and audio format.
    [[nodiscard]] const VideoInfo& info() const noexcept { return info_; }

    /// Plays the movie, or decodes it without a window for a headless check.
    ///
    /// Escape, a quit event or closing the window skips the rest. Full
    /// playback waits for the queued audio to drain. Without FFmpeg nothing
    /// is decoded: one line is logged and the movie is reported skipped.
    ///
    /// @param options Frame limit, headless mode, audio, snapshot path and an
    ///        optional window and renderer to play into.
    /// @return Whether it was skipped, what was decoded, and any error.
    [[nodiscard]] PlaybackResult play(const PlaybackOptions& options = {});

  private:

    struct Impl;
    explicit IntroPlayer(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> implementation_;
    VideoInfo info_;
};

struct OpenPlayerResult {
    std::optional<IntroPlayer> player;
    std::string error;

    /// Reports whether the player opened.
    explicit operator bool() const noexcept { return player.has_value(); }
};

// PCM S16LE used for looping frontend CD/music tracks (music/*.mp3).
struct DecodedPcm {
    std::vector<uint8_t> samples;
    int sample_rate = 0;
    int channels = 0;
};

/// Decodes a whole audio file to interleaved signed 16-bit PCM at its own rate and channel count.
///
/// Without FFmpeg it always fails.
///
/// @param path Audio file path.
/// @param[out] error Reason for a failure; cleared on success.
/// @return The samples, or nothing when the file cannot be decoded or is empty.
[[nodiscard]] std::optional<DecodedPcm>
decode_audio_file(const std::filesystem::path& path, std::string& error);

// Uniform 4:3 (or movie canvas) letterbox that fills the output. Scale is
// min(output/canvas) on both axes so 1920x1080 shows 1440x1080 with side bars,
// not an integer-shrunk 1280x960 crop.
struct LetterboxDest {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
};

/// Returns the letterboxed destination rectangle of a canvas on an output.
///
/// @param canvas_w Canvas width in pixels.
/// @param canvas_h Canvas height in pixels.
/// @param output_w Output width in pixels.
/// @param output_h Output height in pixels.
/// @return The centred, uniformly scaled rectangle; the whole output when any size is not positive.
[[nodiscard]] inline LetterboxDest
letterbox_dest(int canvas_w, int canvas_h, int output_w, int output_h) noexcept {
    if (canvas_w <= 0 || canvas_h <= 0 || output_w <= 0 || output_h <= 0)
        return {
            0,
            0,
            static_cast<float>(std::max(0, output_w)),
            static_cast<float>(std::max(0, output_h))
        };
    const auto fit = std::min(
        static_cast<float>(output_w) / static_cast<float>(canvas_w),
        static_cast<float>(output_h) / static_cast<float>(canvas_h)
    );
    const auto w = static_cast<float>(canvas_w) * fit;
    const auto h = static_cast<float>(canvas_h) * fit;
    return {
        (static_cast<float>(output_w) - w) * 0.5F, (static_cast<float>(output_h) - h) * 0.5F, w, h
    };
}

} // namespace oa::media
