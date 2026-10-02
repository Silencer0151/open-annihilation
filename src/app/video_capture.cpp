// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The video capture: frames read back from the window's renderer and piped
// to ffmpeg, paced by the sound device's clock, and the mix SDL's disk audio
// driver writes, joined to them at the end.
#include "oa/app/video_capture.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#endif

namespace oa::app {

namespace fs = std::filesystem;

namespace {

// The mix SDL's disk audio driver writes: 32-bit float samples.
constexpr const char* kMixFormat = "f32le";
constexpr const char* kMixFormatHint = "F32";
// The disk driver waits this share of a buffer's length before it mixes the
// next, so that it runs ahead of the wall clock and the capture's callback
// holds each buffer back to its time.
constexpr const char* kDiskTimescale = "0.75";
constexpr uint64_t kNanosecondsPerSecond = 1'000'000'000;
constexpr int kBytesPerSample = 4;
constexpr int kFrameBytesPerPixel = 3;
// The encoder's settings: frames held while it catches up, its constant
// quality (lower is better, 18 is close to lossless to the eye), B-frames
// between reference frames, and the mix's bit rate.
constexpr const char* kFrameQueue = "32";
constexpr const char* kQuality = "18";
constexpr const char* kBFrames = "2";
constexpr const char* kMixBitRate = "384k";
// Sound the mix is played on past the last frame before the two are joined,
// so that the mix file holds the last frame's sound even where the driver
// has some of it still to write: a quarter of a second.
constexpr uint64_t kMixTailFrames = capture_sample_rate / 4;
// Longest wait for that tail, in milliseconds: the device plays in real time.
constexpr uint64_t kMixTailWaitMs = 5000;
constexpr uint32_t kMixTailPollMs = 10;

// Throws the capture's error.
[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("video capture: " + what);
}

// The path's UTF-8 spelling, as ffmpeg and the messages take it.
std::string path_text(const fs::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

// `video` with its extension replaced by `suffix`.
fs::path beside(const fs::path& video, const char* suffix) {
    fs::path path = video;
    path.replace_extension();
    path += suffix;
    return path;
}

// Starts the encoder with `arguments`; standard input is piped from the game
// when `piped`, and its messages go to the game's standard error.
SDL_Process* start_encoder(const std::vector<std::string>& arguments, bool piped) {
    std::vector<const char*> argv;
    for (const auto& argument : arguments)
        argv.push_back(argument.c_str());
    argv.push_back(nullptr);
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetNumberProperty(
        properties,
        SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
        piped ? SDL_PROCESS_STDIO_APP : SDL_PROCESS_STDIO_NULL
    );
    SDL_SetNumberProperty(
        properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL
    );
    SDL_SetNumberProperty(
        properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_INHERITED
    );
    SDL_Process* process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (process == nullptr)
        fail(
            std::string("cannot start ") + capture_encoder +
            ", which capture needs on PATH: " + SDL_GetError()
        );
    return process;
}

// SDL opens a process's input without blocking; the capture hands each frame
// over whole, so the input is made to wait for the encoder instead.
void wait_on_input(SDL_IOStream* input) {
    const SDL_PropertiesID properties = SDL_GetIOProperties(input);
#ifdef _WIN32
    auto* pipe = static_cast<HANDLE>(
        SDL_GetPointerProperty(properties, SDL_PROP_IOSTREAM_WINDOWS_HANDLE_POINTER, nullptr)
    );
    DWORD mode = PIPE_WAIT;
    if (pipe == nullptr || !SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr))
        fail("cannot make the encoder's input wait");
#else
    const auto descriptor = static_cast<int>(
        SDL_GetNumberProperty(properties, SDL_PROP_IOSTREAM_FILE_DESCRIPTOR_NUMBER, -1)
    );
    const int flags = descriptor < 0 ? -1 : fcntl(descriptor, F_GETFL);
    if (flags < 0 || fcntl(descriptor, F_SETFL, flags & ~O_NONBLOCK) < 0)
        fail("cannot make the encoder's input wait");
#endif
}

// Waits for the encoder and fails unless it succeeded.
void finish_encoder(SDL_Process* process, const char* step) {
    int status = -1;
    const bool exited = SDL_WaitProcess(process, true, &status);
    SDL_DestroyProcess(process);
    if (!exited || status != 0)
        fail(
            std::string(capture_encoder) + " failed " + step + " (exit status " +
            std::to_string(status) + ")"
        );
}

// The renderer's whole target as RGB, whatever logical presentation the
// screen uses: the letterboxed frontend and the match alike fill the window.
void read_target(SDL_Renderer* renderer, int width, int height, std::vector<uint8_t>& rgb) {
    int logical_width = 0;
    int logical_height = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    if (!SDL_GetRenderLogicalPresentation(renderer, &logical_width, &logical_height, &mode))
        fail(std::string("cannot read the window's presentation: ") + SDL_GetError());
    if (mode != SDL_LOGICAL_PRESENTATION_DISABLED &&
        !SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED))
        fail(std::string("cannot read the whole window: ") + SDL_GetError());
    SDL_Surface* target = SDL_RenderReadPixels(renderer, nullptr);
    // The game goes on drawing in the presentation it chose, so one that
    // cannot be put back ends the capture.
    if (mode != SDL_LOGICAL_PRESENTATION_DISABLED &&
        !SDL_SetRenderLogicalPresentation(renderer, logical_width, logical_height, mode)) {
        SDL_DestroySurface(target);
        fail(std::string("cannot restore the window's presentation: ") + SDL_GetError());
    }
    SDL_Surface* converted =
        target != nullptr ? SDL_ConvertSurface(target, SDL_PIXELFORMAT_RGB24) : nullptr;
    SDL_DestroySurface(target);
    if (converted == nullptr)
        fail(std::string("cannot read the frame: ") + SDL_GetError());
    if (converted->w != width || converted->h != height) {
        const auto size = std::to_string(converted->w) + 'x' + std::to_string(converted->h);
        SDL_DestroySurface(converted);
        fail(
            "the window is " + size + ", not the " + std::to_string(width) + 'x' +
            std::to_string(height) + " the capture started at"
        );
    }
    const auto row_bytes = static_cast<std::size_t>(width) * kFrameBytesPerPixel;
    rgb.resize(row_bytes * static_cast<std::size_t>(height));
    for (int row = 0; row < height; ++row)
        std::memcpy(
            rgb.data() + static_cast<std::size_t>(row) * row_bytes,
            static_cast<const uint8_t*>(converted->pixels) +
                static_cast<std::ptrdiff_t>(row) * converted->pitch,
            row_bytes
        );
    SDL_DestroySurface(converted);
}

} // namespace

CaptureFiles capture_files(const fs::path& video) {
    return {video, beside(video, ".capture-frames.mp4"), beside(video, ".capture-mix.f32")};
}

uint64_t capture_frames_due(uint64_t played) {
    return played * capture_frame_rate / capture_sample_rate + 1;
}

std::vector<std::string> frame_encoder_arguments(const CaptureFiles& files, int width, int height) {
    return {
        capture_encoder,
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        // Raw frames on standard input.
        "-f",
        "rawvideo",
        "-pix_fmt",
        "rgb24",
        "-video_size",
        std::to_string(width) + 'x' + std::to_string(height),
        "-framerate",
        std::to_string(capture_frame_rate),
        "-thread_queue_size",
        kFrameQueue,
        "-i",
        "-",
        // 4:2:0 in BT.709's limited range, and the frames tagged so.
        "-vf",
        "scale=out_color_matrix=bt709:out_range=tv,format=yuv420p,"
        "setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709:range=tv",
        "-c:v",
        "libx264",
        "-preset",
        "medium",
        "-profile:v",
        "high",
        "-crf",
        kQuality,
        "-bf",
        kBFrames,
        "-g",
        std::to_string(capture_frame_rate / 2),
        "-flags",
        "+cgop",
        path_text(files.frames),
    };
}

std::vector<std::string> join_arguments(const CaptureFiles& files, uint64_t frames) {
    // The frames' length in seconds, to the microsecond.
    char seconds[32];
    std::snprintf(
        seconds,
        sizeof seconds,
        "%.6f",
        static_cast<double>(frames) / static_cast<double>(capture_frame_rate)
    );
    return {
        capture_encoder,
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-i",
        path_text(files.frames),
        "-f",
        kMixFormat,
        "-ar",
        std::to_string(capture_sample_rate),
        "-ac",
        std::to_string(capture_channels),
        "-i",
        path_text(files.mix),
        "-map",
        "0:v:0",
        "-map",
        "1:a:0",
        "-c:v",
        "copy",
        "-c:a",
        "aac",
        "-b:a",
        kMixBitRate,
        "-ar",
        std::to_string(capture_sample_rate),
        "-t",
        seconds,
        "-movflags",
        "+faststart",
        path_text(files.video),
    };
}

void prepare_capture_audio(const fs::path& video) {
    const auto mix = path_text(capture_files(video).mix);
    const auto set = [](const char* hint, const std::string& value) {
        if (!SDL_SetHintWithPriority(hint, value.c_str(), SDL_HINT_OVERRIDE))
            fail(std::string("SDL refused the sound setting ") + hint + ": " + SDL_GetError());
    };
    set(SDL_HINT_AUDIO_DRIVER, "disk");
    set(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, mix);
    set(SDL_HINT_AUDIO_DISK_TIMESCALE, kDiskTimescale);
    set(SDL_HINT_AUDIO_FORMAT, kMixFormatHint);
    set(SDL_HINT_AUDIO_CHANNELS, std::to_string(capture_channels));
    set(SDL_HINT_AUDIO_FREQUENCY, std::to_string(capture_sample_rate));
}

VideoCapture::VideoCapture(const fs::path& video, int width, int height)
    : files_(capture_files(video)), width_(width), height_(height) {
    if (width <= 0 || height <= 0 || width % 2 != 0 || height % 2 != 0)
        fail(
            "the window is " + std::to_string(width) + 'x' + std::to_string(height) +
            "; H.264 at 4:2:0 needs an even width and height"
        );
    const char* driver = SDL_GetCurrentAudioDriver();
    if (driver == nullptr || std::strcmp(driver, "disk") != 0)
        fail(
            std::string("the mix is taken from SDL's disk audio driver, but SDL plays through ") +
            (driver != nullptr ? driver : "no driver")
        );
    try {
        // The capture's own device opens first, so the device plays this
        // format and stays open, writing one mix file, until the capture
        // ends. Its postmix callback keeps the device to the wall clock and
        // counts what it plays: the capture's clock.
        const SDL_AudioSpec wanted{SDL_AUDIO_F32, capture_channels, capture_sample_rate};
        clock_device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &wanted);
        if (clock_device_ == 0)
            fail(std::string("cannot open the sound device: ") + SDL_GetError());
        SDL_AudioSpec played{};
        if (!SDL_GetAudioDeviceFormat(clock_device_, &played, nullptr) ||
            played.format != wanted.format || played.channels != wanted.channels ||
            played.freq != wanted.freq)
            fail("the sound device was already open in another format");
        if (!SDL_SetAudioPostmixCallback(clock_device_, keep_time, this))
            fail(std::string("cannot follow the sound device: ") + SDL_GetError());
        encoder_ = start_encoder(frame_encoder_arguments(files_, width, height), true);
        encoder_input_ = SDL_GetProcessInput(encoder_);
        if (encoder_input_ == nullptr)
            fail(std::string("cannot write to ") + capture_encoder + ": " + SDL_GetError());
        wait_on_input(encoder_input_);
    } catch (...) {
        release();
        throw;
    }
    std::cout << "video capture: " << width << 'x' << height << " at " << capture_frame_rate
              << " frames a second to " << path_text(files_.video) << '\n';
}

VideoCapture::~VideoCapture() {
    release();
}

void VideoCapture::release() noexcept {
    // The device goes first: its callback counts into this capture.
    if (clock_device_ != 0)
        SDL_CloseAudioDevice(clock_device_);
    clock_device_ = 0;
    // The encoder ends the frames it has, so what was captured can be played.
    if (encoder_input_ != nullptr)
        SDL_CloseIO(encoder_input_);
    encoder_input_ = nullptr;
    // An encoder still here was abandoned: finish() reads the status of one
    // that ends the capture. It is waited for only so that it is gone.
    if (encoder_ != nullptr) {
        std::ignore = SDL_WaitProcess(encoder_, true, nullptr);
        SDL_DestroyProcess(encoder_);
    }
    encoder_ = nullptr;
}

void SDLCALL VideoCapture::keep_time(void* capture, const SDL_AudioSpec* spec, float*, int bytes) {
    if (spec == nullptr || spec->channels <= 0)
        return;
    auto& self = *static_cast<VideoCapture*>(capture);
    // The buffer about to be played starts `played` sample frames after the
    // first; it waits for that moment on the wall clock.
    const uint64_t now = SDL_GetTicksNS();
    if (self.clock_start_ns_ == 0)
        self.clock_start_ns_ = now;
    const uint64_t played = self.played_.load();
    const uint64_t due =
        self.clock_start_ns_ + played * kNanosecondsPerSecond / capture_sample_rate;
    if (due > now)
        SDL_DelayNS(due - now);
    const auto frame_bytes = static_cast<uint64_t>(spec->channels) * kBytesPerSample;
    self.played_ += static_cast<uint64_t>(bytes) / frame_bytes;
}

void VideoCapture::write_frame(const std::vector<uint8_t>& rgb) {
    const uint8_t* data = rgb.data();
    std::size_t left = rgb.size();
    while (left > 0) {
        const std::size_t written = SDL_WriteIO(encoder_input_, data, left);
        if (written == 0)
            fail(std::string(capture_encoder) + " stopped taking frames: " + SDL_GetError());
        data += written;
        left -= written;
    }
    ++frames_written_;
}

void VideoCapture::add_frame(SDL_Renderer* renderer) {
    if (finished_ || encoder_ == nullptr)
        return;
    const uint64_t due = capture_frames_due(played_.load());
    if (due <= frames_written_)
        return;
    // The screen showed the last frame until now.
    if (!frame_.empty())
        for (; frames_written_ + 1 < due; ++frames_repeated_)
            write_frame(frame_);
    read_target(renderer, width_, height_, frame_);
    while (frames_written_ < due) {
        if (frames_written_ + 1 < due)
            ++frames_repeated_;
        write_frame(frame_);
    }
}

void VideoCapture::finish() {
    if (finished_ || encoder_ == nullptr)
        return;
    finished_ = true;
    SDL_CloseIO(encoder_input_);
    encoder_input_ = nullptr;
    SDL_Process* encoder = encoder_;
    encoder_ = nullptr;
    finish_encoder(encoder, "encoding the frames");
    const uint64_t needed =
        frames_written_ * capture_sample_rate / capture_frame_rate + kMixTailFrames;
    const uint64_t deadline = SDL_GetTicks() + kMixTailWaitMs;
    while (played_.load() < needed && SDL_GetTicks() < deadline)
        SDL_Delay(kMixTailPollMs);
    SDL_CloseAudioDevice(clock_device_);
    clock_device_ = 0;
    finish_encoder(
        start_encoder(join_arguments(files_, frames_written_), false), "joining the frames and mix"
    );
    std::error_code ignored;
    fs::remove(files_.frames, ignored);
    // SDL may still be writing the mix, for the game's sound after the
    // capture; where the file cannot be removed while open, it stays.
    fs::remove(files_.mix, ignored);
    std::cout << "video capture: wrote " << frames_written_ << " frames ("
              << static_cast<double>(frames_written_) / static_cast<double>(capture_frame_rate)
              << " s), " << frames_repeated_ << " of them repeated while the game was late, to "
              << path_text(files_.video) << '\n';
}

} // namespace oa::app
