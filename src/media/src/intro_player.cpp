// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/media/intro_player.hpp"
#include "oa/audio/sound_output.hpp"
#include "oa/formats/smacker.hpp"
#include "oa/formats/smacker/decoder.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace oa::media {
namespace {

/// Returns a path's UTF-8 spelling: a name outside the system's code page
/// has no narrow spelling on Windows.
///
/// @param path the path
/// @return its UTF-8 spelling
std::string utf8_text(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

namespace smacker = formats::smacker;

// Movies are drawn at x=0 and y=(480-height)/2 of the frontend's 640x480
// presentation canvas below. The frame itself keeps its decoded dimensions.
inline constexpr int kGamePresentationWidth = 640;
inline constexpr int kGamePresentationHeight = 480;
// The longest full playback waits for its queued samples to play out, 5 s:
// a sound device that stops taking samples ends the movie with an error
// after it instead of holding the game. Playback waits at most 5 ms at a
// time, between looks at the window's events and the queue.
inline constexpr uint64_t kAudioDrainTimeoutNs = 5000000000ULL;
inline constexpr uint64_t kAudioDrainPollNs = 5000000ULL;
// A negative frame-rate field counts 1/100000 s per frame and a positive one
// milliseconds; these turn either into nanoseconds.
inline constexpr uint64_t kNanosecondsPerRateUnit = 10000ULL;
inline constexpr uint64_t kRateUnitsPerMillisecond = 100ULL;
inline constexpr std::size_t kRgbBytesPerPixel = 3;

/// Returns a frame's presentation time after the first frame's.
///
/// @param frame_rate the header's frame-rate field
/// @param frame the frame's index
/// @return nanoseconds; 0 for a zero rate, saturated when too large
uint64_t frame_time_ns(int32_t frame_rate, uint64_t frame) noexcept {
    const uint64_t units = frame_rate < 0
                               ? static_cast<uint64_t>(-static_cast<int64_t>(frame_rate))
                               : static_cast<uint64_t>(frame_rate) * kRateUnitsPerMillisecond;
    const auto per_frame = units * kNanosecondsPerRateUnit;
    if (per_frame != 0 && frame > std::numeric_limits<uint64_t>::max() / per_frame)
        return std::numeric_limits<uint64_t>::max();
    return frame * per_frame;
}

/// Returns a failed open with its reason.
///
/// @param message why the movie cannot be played
/// @return the result
OpenPlayerResult failure(std::string message) {
    return OpenPlayerResult{std::nullopt, std::move(message)};
}

/// Writes RGB rows as a binary PPM (P6).
///
/// @param path destination; nothing is written when empty
/// @param pixels the first row
/// @param pitch bytes from one row to the next
/// @param width pixels per row
/// @param height rows
/// @param[out] error why the file was not written
/// @return true when written, or when `path` is empty
bool write_snapshot(
    const std::filesystem::path& path,
    const uint8_t* pixels,
    int pitch,
    int width,
    int height,
    std::string& error
) {
    if (path.empty())
        return true;
    if (pixels == nullptr || pitch < width * 3 || width <= 0 || height <= 0) {
        error = "snapshot dimensions are invalid";
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot create snapshot: " + utf8_text(path);
        return false;
    }
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (int row = 0; row < height; ++row)
        output.write(reinterpret_cast<const char*>(pixels + row * pitch), width * 3);
    if (!output) {
        error = "cannot write snapshot: " + utf8_text(path);
        return false;
    }
    return true;
}

/// Starts the sound output for a movie and opens its playing stream.
///
/// @param track the movie's played track
/// @param[out] started set once the output has started; the caller stops it
///        once, after the stream is gone
/// @param[out] reason why there is no stream
/// @return the stream, or null when the movie plays without sound
std::unique_ptr<oa::audio::OutputStream>
open_movie_sound(const smacker::AudioFormat& track, bool& started, std::string& reason) {
    auto& output = oa::audio::sound_output();
    if (!output.start(reason))
        return nullptr;
    started = true;
    oa::audio::StreamFormat format{};
    format.sample = oa::audio::SampleFormat::s16;
    format.channels = static_cast<uint8_t>(track.channels);
    format.rate = track.sample_rate;
    auto stream = output.open_stream(format, nullptr, nullptr, reason);
    if (stream == nullptr || !stream->resume()) {
        if (reason.empty())
            reason = output.last_error();
        return nullptr;
    }
    return stream;
}

} // namespace

// One movie's decoding state: its tables, the open file, the decoder with
// its one frame of palette indices, the palette and the reused buffers.
struct IntroPlayer::Impl {
    PlayerLimits limits;
    std::filesystem::path path;
    std::optional<smacker::SmackerReader> reader;
    std::optional<smacker::VideoDecoder> video;
    std::ifstream file;
    smacker::Palette palette{};
    // The track played: the first with a sample rate, when it is playable.
    std::size_t audio_track = 0;
    smacker::AudioFormat audio_format{};
    int width = 0;
    int height = 0;
    int presentation_height = 0;
    int presentation_texture_height = 0;
    int presentation_pitch = 0;
    bool mode_two_scanline_doubling = false;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> rgb;
    std::vector<uint8_t> presentation_rgb;

    /// Reports whether the movie has a track to play.
    [[nodiscard]] bool has_audio() const noexcept {
        return audio_format.coding != smacker::AudioCoding::none;
    }
};

IntroPlayer::IntroPlayer(std::unique_ptr<Impl> implementation)
    : implementation_(std::move(implementation)) {
    const auto& header = implementation_->reader->header();
    info_.width = implementation_->width;
    info_.height = implementation_->height;
    info_.frame_count = header.frame_count;
    info_.frame_rate = header.frame_rate_hz();
    info_.has_audio = implementation_->has_audio();
    if (info_.has_audio) {
        info_.audio_sample_rate = static_cast<int>(implementation_->audio_format.sample_rate);
        info_.audio_channels = implementation_->audio_format.channels;
    }
}

IntroPlayer::IntroPlayer(IntroPlayer&&) noexcept = default;
IntroPlayer& IntroPlayer::operator=(IntroPlayer&&) noexcept = default;
IntroPlayer::~IntroPlayer() = default;

OpenPlayerResult IntroPlayer::open(const std::filesystem::path& path, const PlayerLimits& limits) {
    if (path.empty())
        return failure("intro movie path is empty");
    auto implementation = std::make_unique<Impl>();
    implementation->limits = limits;
    implementation->path = path;
    auto container = smacker::SmackerReader::open(path);
    if (!container)
        return failure("SMK2 preflight: " + container.error);
    const auto& header = container.reader->header();
    if (static_cast<uint64_t>(header.width) * static_cast<uint64_t>(header.height) >
            limits.max_video_pixels ||
        header.width > static_cast<uint32_t>(std::numeric_limits<int>::max() / 3) ||
        header.height > static_cast<uint32_t>(std::numeric_limits<int>::max()))
        return failure("intro video dimensions exceed bounds");
    std::string error;
    std::vector<uint8_t> trees;
    if (!container.reader->read_huffman_trees(trees, error))
        return failure("Smacker trees: " + error);
    implementation->video = smacker::VideoDecoder::create(header, trees, error);
    if (!implementation->video)
        return failure("Smacker video: " + error);
    implementation->width = static_cast<int>(header.width);
    implementation->height = static_cast<int>(header.height);
    const auto display_height =
        static_cast<uint64_t>(header.height) * (header.uses_doubled_height() ? 2U : 1U);
    if (display_height > static_cast<uint64_t>(std::numeric_limits<int>::max()))
        return failure("intro presentation height exceeds bounds");
    implementation->presentation_height = static_cast<int>(display_height);
    implementation->mode_two_scanline_doubling =
        (header.flags & smacker::kHeightModeMask) == smacker::kDoubleHeightModeTwo;
    implementation->presentation_texture_height = implementation->mode_two_scanline_doubling
                                                      ? implementation->presentation_height
                                                      : implementation->height;
    const auto presentation_bytes =
        static_cast<uint64_t>(header.width) *
        static_cast<uint64_t>(implementation->presentation_texture_height) * kRgbBytesPerPixel;
    if (presentation_bytes > std::numeric_limits<std::size_t>::max())
        return failure("intro presentation buffer exceeds bounds");
    implementation->presentation_pitch = implementation->width * 3;
    implementation->rgb.resize(
        static_cast<std::size_t>(header.width) * header.height * kRgbBytesPerPixel
    );
    if (implementation->mode_two_scanline_doubling)
        implementation->presentation_rgb.resize(static_cast<std::size_t>(presentation_bytes));
    for (std::size_t track = 0; track < header.audio.size(); ++track) {
        if (header.audio[track].sample_rate() == 0)
            continue;
        implementation->audio_track = track;
        implementation->audio_format = smacker::audio_format(header.audio[track]);
        break;
    }
    implementation->file.open(path, std::ios::binary);
    if (!implementation->file)
        return failure("cannot open intro movie: " + utf8_text(path));
    implementation->reader = std::move(container.reader);
    IntroPlayer player(std::move(implementation));
    return OpenPlayerResult{std::optional<IntroPlayer>(std::move(player)), {}};
}

bool skips_movie(const SDL_Event& event) noexcept {
    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_FINGER_DOWN:
        return true;
    case SDL_EVENT_KEY_DOWN:
        return event.key.key == SDLK_ESCAPE;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        return event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ||
               event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST ||
               event.gbutton.button == SDL_GAMEPAD_BUTTON_START;
    default:
        return false;
    }
}

PlaybackResult IntroPlayer::play(const PlaybackOptions& options) {
    PlaybackResult playback;
    if (implementation_ == nullptr) {
        playback.error = "intro player is not initialized";
        return playback;
    }
    auto& impl = *implementation_;
    const auto& header = impl.reader->header();
    const auto frame_limit = options.frame_limit == 0
                                 ? impl.limits.max_decoded_frames
                                 : std::min(options.frame_limit, impl.limits.max_decoded_frames);
    SDL_Window* window = options.window;
    SDL_Renderer* renderer = options.renderer;
    const bool host_display = window != nullptr && renderer != nullptr;
    SDL_Texture* texture = nullptr;
    std::unique_ptr<oa::audio::OutputStream> audio_stream;
    bool sound_started = false;
    bool sdl_initialized = false;
    // The application may have no gamepad open yet, as before the game
    // starts, and SDL sends a gamepad's buttons only while it is open: the
    // movie opens every gamepad for its length. SDL counts the opens, so
    // the application's own stay open after.
    std::vector<SDL_Gamepad*> gamepads;
    const auto open_gamepad = [&](SDL_JoystickID id) {
        if (SDL_Gamepad* gamepad = SDL_OpenGamepad(id); gamepad != nullptr)
            gamepads.push_back(gamepad);
    };
    const auto canvas_width = std::max(kGamePresentationWidth, impl.width);
    const auto canvas_height = std::max(kGamePresentationHeight, impl.presentation_height);
    const SDL_FRect presentation_rect{
        static_cast<float>((canvas_width - impl.width) / 2),
        static_cast<float>((canvas_height - impl.presentation_height) / 2),
        static_cast<float>(impl.width),
        static_cast<float>(impl.presentation_height)
    };
    auto cleanup = [&] {
        for (SDL_Gamepad* gamepad : gamepads)
            SDL_CloseGamepad(gamepad);
        gamepads.clear();
        audio_stream.reset();
        if (sound_started)
            oa::audio::sound_output().stop();
        sound_started = false;
        if (texture != nullptr)
            SDL_DestroyTexture(texture);
        if (!host_display) {
            if (renderer != nullptr)
                SDL_DestroyRenderer(renderer);
            if (window != nullptr)
                SDL_DestroyWindow(window);
            if (sdl_initialized)
                SDL_Quit();
        }
        texture = nullptr;
        if (!host_display) {
            renderer = nullptr;
            window = nullptr;
        }
    };
    if (!options.headless_check) {
        if (!host_display) {
            if (!SDL_Init(SDL_INIT_VIDEO)) {
                playback.error = std::string("SDL_Init: ") + SDL_GetError();
                return playback;
            }
            sdl_initialized = true;
            window = SDL_CreateWindow(
                "Open Annihilation", canvas_width * 2, canvas_height * 2, SDL_WINDOW_RESIZABLE
            );
            renderer = window == nullptr ? nullptr : SDL_CreateRenderer(window, nullptr);
        }
        texture = renderer == nullptr ? nullptr
                                      : SDL_CreateTexture(
                                            renderer,
                                            SDL_PIXELFORMAT_RGB24,
                                            SDL_TEXTUREACCESS_STREAMING,
                                            impl.width,
                                            impl.presentation_texture_height
                                        );
        // Fill the window with a uniform 4:3 letterbox and linear upsample.
        // Nearest + fractional scale (or integer-shrunk 1280x960 at 1080p)
        // either crops the movie or makes mode-2 scanlines 2px then 3px.
        if (window == nullptr || renderer == nullptr || texture == nullptr ||
            (texture != nullptr && !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR)) ||
            !SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED) ||
            !SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255)) {
            playback.error = std::string("SDL video setup: ") + SDL_GetError();
            cleanup();
            return playback;
        }
        if (SDL_WasInit(SDL_INIT_GAMEPAD) == SDL_INIT_GAMEPAD) {
            int count = 0;
            if (SDL_JoystickID* ids = SDL_GetGamepads(&count); ids != nullptr) {
                for (int index = 0; index < count; ++index)
                    open_gamepad(ids[index]);
                SDL_free(ids);
            }
        }
        // A movie whose sound cannot play, as on a computer with no sound
        // device, is shown without it.
        if (options.play_audio && impl.has_audio()) {
            std::string reason;
            audio_stream = open_movie_sound(impl.audio_format, sound_started, reason);
            if (audio_stream == nullptr)
                std::fprintf(stderr, "intro plays without sound: %s\n", reason.c_str());
        }
        const auto* video_backend = SDL_GetCurrentVideoDriver();
        const auto* renderer_backend = SDL_GetRendererName(renderer);
        const std::string audio_driver =
            audio_stream != nullptr ? oa::audio::sound_output().driver_name() : std::string();
        const char* audio_backend = audio_driver.empty() ? nullptr : audio_driver.c_str();
        std::fprintf(
            stderr,
            "intro backends video=%s renderer=%s audio=%s\n",
            video_backend != nullptr ? video_backend : "unknown",
            renderer_backend != nullptr ? renderer_backend : "unknown",
            audio_backend != nullptr ? audio_backend : "disabled"
        );
    }

    std::vector<int16_t> audio_samples;
    bool stop = false;
    bool timing_started = false;
    uint64_t presentation_start_ns = 0;
    bool snapshot_written = false;
    const auto poll_window_events = [&]() -> bool {
        if (options.headless_check)
            return true;
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (skips_movie(event)) {
                playback.skipped = true;
                stop = true;
                return false;
            }
            if (event.type == SDL_EVENT_GAMEPAD_ADDED)
                open_gamepad(event.gdevice.which);
            if (options.hooks.window_event != nullptr)
                options.hooks.window_event(options.hooks.context, event);
        }
        return true;
    };
    const auto wait_until = [&](uint64_t deadline_ns) -> bool {
        if (options.headless_check)
            return true;
        for (;;) {
            if (!poll_window_events())
                return false;
            const auto now = SDL_GetTicksNS();
            if (now >= deadline_ns)
                return true;
            SDL_DelayNS(std::min(deadline_ns - now, kAudioDrainPollNs));
        }
    };
    const auto process_audio = [&](std::span<const uint8_t> chunk) -> bool {
        std::string error;
        if (!smacker::decode_audio(chunk, impl.audio_format, audio_samples, error)) {
            playback.error = "intro audio: " + error;
            return false;
        }
        const auto bytes = audio_samples.size() * sizeof(int16_t);
        if (bytes > impl.limits.max_audio_bytes ||
            playback.decoded_audio_bytes > impl.limits.max_audio_bytes - bytes) {
            playback.error = "decoded audio exceeds configured bound";
            return false;
        }
        playback.decoded_audio_bytes += bytes;
        if (audio_stream != nullptr && bytes > 0 && bytes <= INT_MAX &&
            !audio_stream->put(audio_samples.data(), static_cast<int32_t>(bytes))) {
            playback.error = "sound output put: " + oa::audio::sound_output().last_error();
            return false;
        }
        return true;
    };
    const auto process_video = [&](uint32_t frame_index) -> bool {
        // Palette indices to RGB24 through the frame's palette.
        const auto indices = impl.video->pixels();
        for (std::size_t pixel = 0; pixel < indices.size(); ++pixel)
            std::memcpy(
                &impl.rgb[pixel * kRgbBytesPerPixel],
                &impl.palette[static_cast<std::size_t>(indices[pixel]) * kRgbBytesPerPixel],
                kRgbBytesPerPixel
            );
        ++playback.decoded_frames;
        const auto* render_pixels = impl.rgb.data();
        auto render_pitch = impl.presentation_pitch;
        // A height-mode-2 movie shows its odd rows in palette entry 0.
        const std::array<uint8_t, 3> palette_zero{
            impl.palette[0], impl.palette[1], impl.palette[2]
        };
        if (impl.mode_two_scanline_doubling) {
            for (int row = 0; row < impl.presentation_height; ++row) {
                auto* destination = impl.presentation_rgb.data() +
                                    static_cast<std::size_t>(row) * impl.presentation_pitch;
                const auto* source =
                    impl.rgb.data() + static_cast<std::size_t>(row / 2) * impl.presentation_pitch;
                if ((row & 1) == 0 || !options.headless_check) {
                    // Display doubles each decoded row so linear upscale is a
                    // full frame. Snapshots keep the blank odd rows.
                    std::copy(source, source + impl.presentation_pitch, destination);
                } else {
                    for (int pixel = 0; pixel < impl.width; ++pixel)
                        std::memcpy(
                            destination + pixel * 3, palette_zero.data(), palette_zero.size()
                        );
                }
            }
            render_pixels = impl.presentation_rgb.data();
            render_pitch = impl.presentation_pitch;
        }
        if (!snapshot_written && !options.snapshot_path.empty()) {
            const auto* snap_pixels = render_pixels;
            auto snap_pitch = render_pitch;
            if (impl.mode_two_scanline_doubling && !options.headless_check) {
                for (int row = 1; row < impl.presentation_height; row += 2) {
                    auto* destination = impl.presentation_rgb.data() +
                                        static_cast<std::size_t>(row) * impl.presentation_pitch;
                    for (int pixel = 0; pixel < impl.width; ++pixel)
                        std::memcpy(
                            destination + pixel * 3, palette_zero.data(), palette_zero.size()
                        );
                }
                snap_pixels = impl.presentation_rgb.data();
                snap_pitch = impl.presentation_pitch;
            }
            if (!write_snapshot(
                    options.snapshot_path,
                    snap_pixels,
                    snap_pitch,
                    impl.width,
                    impl.presentation_texture_height,
                    playback.error
                ))
                return false;
            snapshot_written = true;
            if (impl.mode_two_scanline_doubling && !options.headless_check) {
                for (int row = 1; row < impl.presentation_height; row += 2) {
                    auto* destination = impl.presentation_rgb.data() +
                                        static_cast<std::size_t>(row) * impl.presentation_pitch;
                    const auto* source =
                        impl.presentation_rgb.data() +
                        static_cast<std::size_t>(row - 1) * impl.presentation_pitch;
                    std::copy(source, source + impl.presentation_pitch, destination);
                }
            }
        }
        if (!options.headless_check) {
            // Each frame is due at its index times the header's frame
            // period after the first frame was shown.
            if (!timing_started) {
                timing_started = true;
                presentation_start_ns = SDL_GetTicksNS();
            }
            const auto offset_ns = frame_time_ns(header.frame_rate, frame_index);
            const auto deadline_ns =
                offset_ns > std::numeric_limits<uint64_t>::max() - presentation_start_ns
                    ? presentation_start_ns
                    : presentation_start_ns + offset_ns;
            if (!wait_until(deadline_ns))
                return true;
            if (!poll_window_events())
                return true;
            int output_w = 0, output_h = 0;
            if (!SDL_GetRenderOutputSize(renderer, &output_w, &output_h)) {
                playback.error = std::string("SDL_GetRenderOutputSize: ") + SDL_GetError();
                return false;
            }
            const auto canvas = letterbox_dest(canvas_width, canvas_height, output_w, output_h);
            const auto sx = canvas_width > 0 ? canvas.w / static_cast<float>(canvas_width) : 1.0F;
            const auto sy = canvas_height > 0 ? canvas.h / static_cast<float>(canvas_height) : 1.0F;
            const SDL_FRect dest{
                canvas.x + presentation_rect.x * sx,
                canvas.y + presentation_rect.y * sy,
                presentation_rect.w * sx,
                presentation_rect.h * sy
            };
            if (!SDL_UpdateTexture(texture, nullptr, render_pixels, render_pitch) ||
                !SDL_RenderClear(renderer) ||
                !SDL_RenderTexture(renderer, texture, nullptr, &dest) ||
                !SDL_RenderPresent(renderer)) {
                playback.error = std::string("SDL render: ") + SDL_GetError();
                return false;
            }
        }
        if (playback.decoded_frames >= frame_limit)
            stop = true;
        return true;
    };
    // Each frame is read whole, split into its chunks, and played in file
    // order: palette, then the played track's audio, then the video.
    std::size_t chunks = 0;
    std::string error;
    for (uint32_t frame_index = 0; !stop && frame_index < header.frame_count; ++frame_index) {
        const auto frame = impl.reader->frame(frame_index);
        if (!frame) {
            playback.error = "intro frame index is outside the table";
            break;
        }
        impl.payload.resize(frame->compressed_size);
        impl.file.clear();
        impl.file.seekg(static_cast<std::streamoff>(frame->file_offset), std::ios::beg);
        if (!impl.file.read(
                reinterpret_cast<char*>(impl.payload.data()),
                static_cast<std::streamsize>(impl.payload.size())
            )) {
            playback.error = "intro frame payload is truncated";
            break;
        }
        smacker::FrameChunks parts;
        if (!smacker::split_frame(impl.payload, frame->type, header, parts, error) ||
            (!parts.palette.empty() &&
             !smacker::update_palette(parts.palette, impl.palette, error))) {
            playback.error = "intro frame: " + error;
            break;
        }
        bool failed = false;
        for (std::size_t track = 0; track < parts.audio.size() && !failed; ++track) {
            if (parts.audio[track].empty())
                continue;
            if (++chunks > impl.limits.max_chunks) {
                failed = true;
            } else if (track == impl.audio_track && impl.has_audio()) {
                failed = !process_audio(parts.audio[track]);
            }
        }
        if (failed || ++chunks > impl.limits.max_chunks)
            break;
        if (!impl.video->decode(parts.video, error)) {
            playback.error = "intro video: " + error;
            break;
        }
        if (!process_video(frame_index))
            break;
    }
    if (playback.error.empty() && chunks > impl.limits.max_chunks && !stop)
        playback.error = "intro chunk limit exceeded";
    if (audio_stream != nullptr && playback.error.empty()) {
        if (!audio_stream->flush()) {
            playback.error = "sound output flush: " + oa::audio::sound_output().last_error();
        } else if (!playback.skipped && options.frame_limit == 0) {
            // Full playback should not discard the final queued samples. A
            // finite smoke run intentionally returns after its requested
            // frames so validation remains bounded.
            const auto drain_start_ns = SDL_GetTicksNS();
            for (;;) {
                const auto queued = audio_stream->queued_bytes();
                const auto available = audio_stream->available_bytes();
                if (queued == 0 && available == 0)
                    break;
                if (queued < 0 || available < 0) {
                    playback.error =
                        "sound output drain query: " + oa::audio::sound_output().last_error();
                    break;
                }
                if (SDL_GetTicksNS() - drain_start_ns >= kAudioDrainTimeoutNs) {
                    playback.error = "audio stream did not drain before timeout";
                    break;
                }
                if (!poll_window_events())
                    break;
                SDL_DelayNS(kAudioDrainPollNs);
            }
        }
    }
    cleanup();
    return playback;
}

} // namespace oa::media
