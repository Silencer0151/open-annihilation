// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A developer's capture of the game as an MP4 video: the frames the window
// presents, 30 a second of the sound device's clock, and the mix SDL plays,
// encoded by the ffmpeg program (docs/capture.md).
#pragma once

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace oa::app {

/// Frames a second of video the capture writes.
inline constexpr uint64_t capture_frame_rate = 30;
/// Sample frames a second of the captured mix.
inline constexpr int capture_sample_rate = 48000;
/// Channels of the captured mix.
inline constexpr int capture_channels = 2;
/// The program that encodes the capture, looked up on PATH.
inline constexpr const char* capture_encoder = "ffmpeg";

/// The files a capture of one video writes.
struct CaptureFiles {
    std::filesystem::path video;  ///< the MP4 the capture makes
    std::filesystem::path frames; ///< the encoded frames, until the mix joins them
    std::filesystem::path mix;    ///< the mix SDL played: 32-bit float, stereo, 48 kHz
};

/// Names the files a capture of `video` writes: the frames and the mix go
/// beside it until they are joined into it.
///
/// @param video the MP4 to make
/// @return the capture's files
[[nodiscard]] CaptureFiles capture_files(const std::filesystem::path& video);

/// Returns how many video frames are due once the sound device has played
/// `played` sample frames since the capture started: frame n shows the screen
/// from n / 30 seconds on, so frame 0 is due at once.
///
/// @param played sample frames played, at capture_sample_rate
/// @return frames due
[[nodiscard]] uint64_t capture_frames_due(uint64_t played);

/// Returns the encoder's arguments that make `files.frames` from raw RGB
/// frames on its standard input: H.264 (High profile, 4:2:0, BT.709,
/// constant quality 18, closed groups of half a second's frames), as
/// YouTube recommends.
///
/// @param files the capture's files
/// @param width frame width in pixels, even
/// @param height frame height in pixels, even
/// @return the arguments, the program's name first
[[nodiscard]] std::vector<std::string>
frame_encoder_arguments(const CaptureFiles& files, int width, int height);

/// Returns the encoder's arguments that join the encoded frames and the mix
/// into `files.video`: the frames as they are, the mix as AAC at 48 kHz and
/// 384 kbit/s cut to the frames' length, and the index at the file's start.
///
/// @param files the capture's files
/// @param frames video frames written
/// @return the arguments, the program's name first
[[nodiscard]] std::vector<std::string> join_arguments(const CaptureFiles& files, uint64_t frames);

/// Sends SDL's sound to the capture of `video` instead of the speakers: the
/// disk audio driver writes the mix to its mix file, running a little ahead
/// of the wall clock for the capture to hold back. Call it before SDL starts
/// its audio.
///
/// Throws std::runtime_error when SDL refuses one of the settings.
///
/// @param video the MP4 the capture makes
void prepare_capture_audio(const std::filesystem::path& video);

/// A running capture. Each frame the window presents may be written; the
/// sound device's clock decides how many frames are due, and a frame that
/// is late repeats the one before it, so the video keeps pace with the mix.
class VideoCapture {
  public:

    /// Starts capturing: opens the sound device whose clock paces the
    /// frames, and starts the encoder.
    ///
    /// Throws std::runtime_error when SDL's audio is not the disk driver
    /// prepare_capture_audio() chose, when the device plays another format,
    /// when a size is odd or when the encoder does not start.
    ///
    /// @param video the MP4 to make
    /// @param width window width in pixels, even
    /// @param height window height in pixels, even
    VideoCapture(const std::filesystem::path& video, int width, int height);

    /// Stops an unfinished capture: the encoder is ended and the files it
    /// left stay for a look.
    ~VideoCapture();

    VideoCapture(const VideoCapture&) = delete;
    VideoCapture& operator=(const VideoCapture&) = delete;

    /// Writes the frames due since the last call: the earlier ones repeat
    /// the frame before, and the last is read from the renderer's target.
    /// Call it with the frame drawn, before it is presented.
    ///
    /// Throws std::runtime_error when the frame cannot be read, is not the
    /// capture's size, or the encoder stops taking frames.
    ///
    /// @param renderer the window's renderer
    void add_frame(SDL_Renderer* renderer);

    /// Ends the capture and makes the video: the encoder finishes the
    /// frames, the mix is played on past the last frame so its file holds
    /// all of it, and the two are joined. The frame and mix files are then
    /// removed.
    ///
    /// Throws std::runtime_error when the encoder fails.
    void finish();

  private:

    /// Holds each buffer the sound device plays back to its time on the wall
    /// clock, then counts its sample frames; runs on SDL's audio thread.
    ///
    /// The disk driver runs ahead of the wall clock by itself (its timescale
    /// is set below 1), so the callback waits a little for each buffer and the
    /// mix plays in real time. A machine too busy to keep up plays it slower;
    /// the frames follow it all the same.
    ///
    /// @param capture the VideoCapture
    /// @param spec the format of the buffer
    /// @param bytes bytes in the buffer, the capture's own device's silence
    static void SDLCALL keep_time(void* capture, const SDL_AudioSpec* spec, float*, int bytes);

    /// Writes one frame to the encoder, whole.
    ///
    /// @param rgb the frame, 3 bytes a pixel
    void write_frame(const std::vector<uint8_t>& rgb);

    /// Closes the sound device and lets the encoder end the frames it has.
    void release() noexcept;

    CaptureFiles files_;
    int width_{};
    int height_{};
    SDL_AudioDeviceID clock_device_{}; ///< the capture's own sound device, silent
    std::atomic<uint64_t> played_{};   ///< sample frames played since the start
    uint64_t clock_start_ns_{};        ///< when the first buffer played, ns; audio thread only
    SDL_Process* encoder_{};
    SDL_IOStream* encoder_input_{};
    std::vector<uint8_t> frame_; ///< the frame last written, RGB
    uint64_t frames_written_{};  ///< frames written, repeats included
    uint64_t frames_repeated_{}; ///< frames written again while the game was late
    bool finished_{};
};

} // namespace oa::app
