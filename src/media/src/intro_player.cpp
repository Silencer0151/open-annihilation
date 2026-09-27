// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/media/intro_player.hpp"
#include "oa/formats/smacker.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace oa::media {
namespace {

// Movies are drawn at x=0 and y=(480-height)/2 of the frontend's 640x480
// presentation canvas below. The frame itself keeps its decoded dimensions.
inline constexpr int kGamePresentationWidth = 640;
inline constexpr int kGamePresentationHeight = 480;
inline constexpr uint64_t kNanosecondsPerSecond = 1000000000ULL;
inline constexpr uint64_t kAudioDrainTimeoutNs = 5000000000ULL;
inline constexpr uint64_t kAudioDrainPollNs = 5000000ULL;
inline constexpr int kMaxDecoderBackpressureRetries = 8;

// PAL8 carries its 256-entry RGB32 palette in AVFrame::data[1]. On the
// little-endian targets supported here RGB32 is stored as BGRA, while the
// decoded RGB24 surface is ordered RGB. A height-mode-2 movie shows its odd
// rows in palette index 0, so use the actual palette entry instead of
// assuming a black background.
std::array<uint8_t, 3> palette_zero_rgb(const AVFrame* frame) noexcept {
    if (frame == nullptr || frame->format != AV_PIX_FMT_PAL8 || frame->data[1] == nullptr)
        return {};
    const auto* palette = frame->data[1];
    return {palette[2], palette[1], palette[0]};
}

std::string ffmpeg_error(const char* operation, int code) {
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, text, sizeof(text));
    return std::string(operation) + ": " + text;
}

OpenPlayerResult failure(std::string message) {
    return OpenPlayerResult{std::nullopt, std::move(message)};
}

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
        error = "cannot create snapshot: " + path.string();
        return false;
    }
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (int row = 0; row < height; ++row)
        output.write(reinterpret_cast<const char*>(pixels + row * pitch), width * 3);
    if (!output) {
        error = "cannot write snapshot: " + path.string();
        return false;
    }
    return true;
}

} // namespace

struct IntroPlayer::Impl {
    PlayerLimits limits;
    std::filesystem::path path;
    AVFormatContext* format = nullptr;
    AVCodecContext* video = nullptr;
    AVCodecContext* audio = nullptr;
    AVPacket* chunk = nullptr;
    AVFrame* video_frame = nullptr;
    AVFrame* audio_frame = nullptr;
    SwsContext* scaler = nullptr;
    SwrContext* resampler = nullptr;
    int video_stream = -1;
    int audio_stream = -1;
    int output_audio_rate = 0;
    int output_audio_channels = 0;
    int presentation_height = 0;
    int presentation_texture_height = 0;
    int presentation_pitch = 0;
    bool mode_two_scanline_doubling = false;
    uint8_t* rgb_data[4]{};
    int rgb_linesize[4]{};
    std::vector<uint8_t> presentation_rgb;

    ~Impl() {
        if (resampler != nullptr)
            swr_free(&resampler);
        if (scaler != nullptr)
            sws_freeContext(scaler);
        if (rgb_data[0] != nullptr)
            av_freep(&rgb_data[0]);
        av_frame_free(&audio_frame);
        av_frame_free(&video_frame);
        av_packet_free(&chunk);
        avcodec_free_context(&audio);
        avcodec_free_context(&video);
        avformat_close_input(&format);
    }
};

IntroPlayer::IntroPlayer(std::unique_ptr<Impl> implementation)
    : implementation_(std::move(implementation)) {
    info_.width = implementation_->video->width;
    info_.height = implementation_->video->height;
    info_.frame_count = implementation_->format->streams[implementation_->video_stream]->nb_frames;
    const auto frame_rate = av_guess_frame_rate(
        implementation_->format,
        implementation_->format->streams[implementation_->video_stream],
        nullptr
    );
    if (frame_rate.num > 0 && frame_rate.den > 0)
        info_.frame_rate = av_q2d(frame_rate);
    info_.has_audio = implementation_->audio != nullptr;
    if (info_.has_audio) {
        info_.audio_sample_rate = implementation_->output_audio_rate;
        info_.audio_channels = implementation_->output_audio_channels;
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
    // Keep the player attached to the game's resource boundary and expose
    // the exact table frame count even when FFmpeg leaves nb_frames unknown.
    const auto container = formats::smacker::SmackerReader::open(path);
    if (!container)
        return failure("SMK2 preflight: " + container.error);
    const auto& smacker_header = container.reader->header();
    int result =
        avformat_open_input(&implementation->format, path.string().c_str(), nullptr, nullptr);
    if (result < 0)
        return failure(ffmpeg_error("avformat_open_input", result));
    result = avformat_find_stream_info(implementation->format, nullptr);
    if (result < 0)
        return failure(ffmpeg_error("avformat_find_stream_info", result));
    for (unsigned i = 0; i < implementation->format->nb_streams; ++i) {
        const auto* stream = implementation->format->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            implementation->video_stream < 0) {
            implementation->video_stream = static_cast<int>(i);
        } else if (
            stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && implementation->audio_stream < 0
        ) {
            implementation->audio_stream = static_cast<int>(i);
        }
    }
    if (implementation->video_stream < 0)
        return failure("intro movie has no video stream");
    const auto* video_parameters =
        implementation->format->streams[implementation->video_stream]->codecpar;
    const auto* video_codec = avcodec_find_decoder(video_parameters->codec_id);
    if (video_codec == nullptr)
        return failure("no FFmpeg decoder for intro video");
    implementation->video = avcodec_alloc_context3(video_codec);
    if (implementation->video == nullptr)
        return failure("cannot allocate video decoder");
    result = avcodec_parameters_to_context(implementation->video, video_parameters);
    if (result < 0)
        return failure(ffmpeg_error("video codec parameters", result));
    if (implementation->video->width <= 0 || implementation->video->height <= 0 ||
        static_cast<uint64_t>(implementation->video->width) *
                static_cast<uint64_t>(implementation->video->height) >
            limits.max_video_pixels)
        return failure("intro video dimensions exceed bounds");
    if (implementation->video->width != static_cast<int>(smacker_header.width) ||
        implementation->video->height != static_cast<int>(smacker_header.height))
        return failure("decoded video dimensions differ from SMK2 header");
    const auto display_height = static_cast<uint64_t>(implementation->video->height) *
                                (smacker_header.uses_doubled_height() ? 2U : 1U);
    if (display_height > static_cast<uint64_t>(std::numeric_limits<int>::max()))
        return failure("intro presentation height exceeds bounds");
    implementation->presentation_height = static_cast<int>(display_height);
    implementation->mode_two_scanline_doubling =
        (smacker_header.flags & formats::smacker::kHeightModeMask) ==
        formats::smacker::kDoubleHeightModeTwo;
    implementation->presentation_texture_height = implementation->mode_two_scanline_doubling
                                                      ? implementation->presentation_height
                                                      : implementation->video->height;
    const auto presentation_bytes =
        static_cast<uint64_t>(implementation->video->width) *
        static_cast<uint64_t>(implementation->presentation_texture_height) * 3U;
    if (presentation_bytes > std::numeric_limits<std::size_t>::max())
        return failure("intro presentation buffer exceeds bounds");
    implementation->presentation_pitch = implementation->video->width * 3;
    implementation->presentation_rgb.resize(static_cast<std::size_t>(presentation_bytes));
    result = avcodec_open2(implementation->video, video_codec, nullptr);
    if (result < 0)
        return failure(ffmpeg_error("avcodec_open2 video", result));
    implementation->video_frame = av_frame_alloc();
    if (implementation->video_frame == nullptr)
        return failure("cannot allocate video frame");
    implementation->scaler = sws_getContext(
        implementation->video->width,
        implementation->video->height,
        implementation->video->pix_fmt,
        implementation->video->width,
        implementation->video->height,
        AV_PIX_FMT_RGB24,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr
    );
    if (implementation->scaler == nullptr)
        return failure("cannot allocate video scaler");
    result = av_image_alloc(
        implementation->rgb_data,
        implementation->rgb_linesize,
        implementation->video->width,
        implementation->video->height,
        AV_PIX_FMT_RGB24,
        1
    );
    if (result < 0)
        return failure(ffmpeg_error("av_image_alloc", result));

    if (implementation->audio_stream >= 0) {
        const auto* audio_parameters =
            implementation->format->streams[implementation->audio_stream]->codecpar;
        const auto* audio_codec = avcodec_find_decoder(audio_parameters->codec_id);
        if (audio_codec != nullptr) {
            implementation->audio = avcodec_alloc_context3(audio_codec);
            if (implementation->audio == nullptr)
                return failure("cannot allocate audio decoder");
            result = avcodec_parameters_to_context(implementation->audio, audio_parameters);
            if (result < 0)
                return failure(ffmpeg_error("audio codec parameters", result));
            result = avcodec_open2(implementation->audio, audio_codec, nullptr);
            if (result < 0)
                return failure(ffmpeg_error("avcodec_open2 audio", result));
            implementation->output_audio_rate = implementation->audio->sample_rate;
            implementation->output_audio_channels = implementation->audio->ch_layout.nb_channels;
            if (implementation->output_audio_rate <= 0 ||
                implementation->output_audio_channels <= 0 ||
                implementation->output_audio_channels > 8)
                return failure("intro audio layout is outside bounds");
            AVChannelLayout output_layout{};
            av_channel_layout_default(&output_layout, implementation->output_audio_channels);
            result = swr_alloc_set_opts2(
                &implementation->resampler,
                &output_layout,
                AV_SAMPLE_FMT_S16,
                implementation->output_audio_rate,
                &implementation->audio->ch_layout,
                implementation->audio->sample_fmt,
                implementation->audio->sample_rate,
                0,
                nullptr
            );
            av_channel_layout_uninit(&output_layout);
            if (result < 0 || implementation->resampler == nullptr)
                return failure(ffmpeg_error("swr_alloc_set_opts2", result));
            result = swr_init(implementation->resampler);
            if (result < 0)
                return failure(ffmpeg_error("swr_init", result));
            implementation->audio_frame = av_frame_alloc();
            if (implementation->audio_frame == nullptr)
                return failure("cannot allocate audio frame");
        } else {
            implementation->audio_stream = -1;
        }
    }
    implementation->chunk = av_packet_alloc();
    if (implementation->chunk == nullptr)
        return failure("cannot allocate FFmpeg chunk");
    IntroPlayer player(std::move(implementation));
    player.info_.frame_count = container.reader->header().frame_count;
    return OpenPlayerResult{std::optional<IntroPlayer>(std::move(player)), {}};
}

PlaybackResult IntroPlayer::play(const PlaybackOptions& options) {
    PlaybackResult playback;
    if (implementation_ == nullptr) {
        playback.error = "intro player is not initialized";
        return playback;
    }
    auto& impl = *implementation_;
    const auto frame_limit = options.frame_limit == 0
                                 ? impl.limits.max_decoded_frames
                                 : std::min(options.frame_limit, impl.limits.max_decoded_frames);
    SDL_Window* window = options.window;
    SDL_Renderer* renderer = options.renderer;
    const bool host_display = window != nullptr && renderer != nullptr;
    SDL_Texture* texture = nullptr;
    SDL_AudioStream* audio_stream = nullptr;
    bool sdl_initialized = false;
    const auto canvas_width = std::max(kGamePresentationWidth, impl.video->width);
    const auto canvas_height = std::max(kGamePresentationHeight, impl.presentation_height);
    const SDL_FRect presentation_rect{
        static_cast<float>((canvas_width - impl.video->width) / 2),
        static_cast<float>((canvas_height - impl.presentation_height) / 2),
        static_cast<float>(impl.video->width),
        static_cast<float>(impl.presentation_height)
    };
    auto cleanup = [&] {
        if (audio_stream != nullptr)
            SDL_DestroyAudioStream(audio_stream);
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
        audio_stream = nullptr;
        if (!host_display) {
            renderer = nullptr;
            window = nullptr;
        }
    };
    if (!options.headless_check) {
        if (!host_display) {
            SDL_InitFlags flags = SDL_INIT_VIDEO;
            if (options.play_audio && impl.audio != nullptr)
                flags |= SDL_INIT_AUDIO;
            if (!SDL_Init(flags)) {
                playback.error = std::string("SDL_Init: ") + SDL_GetError();
                return playback;
            }
            sdl_initialized = true;
            window = SDL_CreateWindow(
                "Open Annihilation", canvas_width * 2, canvas_height * 2, SDL_WINDOW_RESIZABLE
            );
            renderer = window == nullptr ? nullptr : SDL_CreateRenderer(window, nullptr);
        } else if (options.play_audio && impl.audio != nullptr) {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                playback.error = std::string("SDL_InitSubSystem audio: ") + SDL_GetError();
                return playback;
            }
        }
        texture = renderer == nullptr ? nullptr
                                      : SDL_CreateTexture(
                                            renderer,
                                            SDL_PIXELFORMAT_RGB24,
                                            SDL_TEXTUREACCESS_STREAMING,
                                            impl.video->width,
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
        if (options.play_audio && impl.audio != nullptr) {
            // SDL3's order is format, channels, frequency. Use named writes
            // here because the old SDL2 order was format, frequency, channels.
            SDL_AudioSpec spec{};
            spec.format = SDL_AUDIO_S16;
            spec.channels = impl.output_audio_channels;
            spec.freq = impl.output_audio_rate;
            audio_stream = SDL_OpenAudioDeviceStream(
                SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr
            );
            if (audio_stream == nullptr || !SDL_ResumeAudioStreamDevice(audio_stream)) {
                playback.error = std::string("SDL audio setup: ") + SDL_GetError();
                cleanup();
                return playback;
            }
        }
        const auto* video_backend = SDL_GetCurrentVideoDriver();
        const auto* renderer_backend = SDL_GetRendererName(renderer);
        const auto* audio_backend =
            options.play_audio && impl.audio != nullptr ? SDL_GetCurrentAudioDriver() : nullptr;
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
    bool have_first_pts = false;
    int64_t first_pts = AV_NOPTS_VALUE;
    uint64_t presentation_start_ns = 0;
    const auto video_time_base = impl.format->streams[impl.video_stream]->time_base;
    const auto fallback_frame_interval_ns =
        info_.frame_rate > 0.0
            ? std::max<uint64_t>(
                  1,
                  static_cast<uint64_t>(
                      static_cast<long double>(kNanosecondsPerSecond) / info_.frame_rate
                  )
              )
            : 0;
    bool snapshot_written = false;
    const auto poll_window_events = [&]() -> bool {
        if (options.headless_check)
            return true;
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                playback.skipped = true;
                stop = true;
                return false;
            }
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
    const auto process_audio = [&](AVFrame* frame) -> bool {
        if (impl.resampler == nullptr)
            return true;
        const auto delay = swr_get_delay(impl.resampler, impl.audio->sample_rate);
        const auto out_samples64 = av_rescale_rnd(
            delay + frame->nb_samples, impl.output_audio_rate, impl.audio->sample_rate, AV_ROUND_UP
        );
        if (out_samples64 <= 0 || out_samples64 > std::numeric_limits<int>::max()) {
            playback.error = "decoded audio sample count exceeds bounds";
            return false;
        }
        const auto sample_count = static_cast<int>(out_samples64);
        const auto sample_total =
            static_cast<uint64_t>(sample_count) * static_cast<uint64_t>(impl.output_audio_channels);
        if (sample_total > std::numeric_limits<std::size_t>::max() / sizeof(int16_t)) {
            playback.error = "decoded audio exceeds configured bound";
            return false;
        }
        const auto sample_bytes = static_cast<std::size_t>(sample_total * sizeof(int16_t));
        if (sample_bytes > impl.limits.max_audio_bytes ||
            playback.decoded_audio_bytes > impl.limits.max_audio_bytes - sample_bytes) {
            playback.error = "decoded audio exceeds configured bound";
            return false;
        }
        audio_samples.resize(static_cast<std::size_t>(sample_total));
        auto* output = reinterpret_cast<uint8_t*>(audio_samples.data());
        const auto converted = swr_convert(
            impl.resampler,
            &output,
            sample_count,
            const_cast<const uint8_t**>(frame->extended_data),
            frame->nb_samples
        );
        if (converted < 0) {
            playback.error = ffmpeg_error("swr_convert", converted);
            return false;
        }
        const auto bytes = static_cast<std::size_t>(converted) *
                           static_cast<std::size_t>(impl.output_audio_channels) * sizeof(int16_t);
        playback.decoded_audio_bytes += bytes;
        if (audio_stream != nullptr && bytes > 0 && bytes <= INT_MAX &&
            !SDL_PutAudioStreamData(audio_stream, audio_samples.data(), static_cast<int>(bytes))) {
            playback.error = std::string("SDL_PutAudioStreamData: ") + SDL_GetError();
            return false;
        }
        return true;
    };
    const auto process_video = [&](AVFrame* frame) -> bool {
        if (frame->width != impl.video->width || frame->height != impl.video->height ||
            frame->format != impl.video->pix_fmt) {
            playback.error = "decoded video frame changed dimensions or pixel format";
            return false;
        }
        sws_scale(
            impl.scaler,
            frame->data,
            frame->linesize,
            0,
            impl.video->height,
            impl.rgb_data,
            impl.rgb_linesize
        );
        ++playback.decoded_frames;
        const auto* render_pixels = impl.rgb_data[0];
        auto render_pitch = impl.rgb_linesize[0];
        if (impl.mode_two_scanline_doubling) {
            const auto palette_zero = palette_zero_rgb(frame);
            for (int row = 0; row < impl.presentation_height; ++row) {
                auto* destination = impl.presentation_rgb.data() +
                                    static_cast<std::size_t>(row) * impl.presentation_pitch;
                const auto* source =
                    impl.rgb_data[0] + static_cast<std::size_t>(row / 2) * impl.rgb_linesize[0];
                if ((row & 1) == 0 || !options.headless_check) {
                    // Display doubles each decoded row so linear upscale is a
                    // full frame. Snapshots keep the blank odd rows.
                    std::copy(source, source + impl.presentation_pitch, destination);
                } else {
                    for (int pixel = 0; pixel < impl.video->width; ++pixel)
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
                const auto palette_zero = palette_zero_rgb(frame);
                for (int row = 1; row < impl.presentation_height; row += 2) {
                    auto* destination = impl.presentation_rgb.data() +
                                        static_cast<std::size_t>(row) * impl.presentation_pitch;
                    for (int pixel = 0; pixel < impl.video->width; ++pixel)
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
                    impl.video->width,
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
            if (!timing_started) {
                timing_started = true;
                presentation_start_ns = SDL_GetTicksNS();
                if (frame->pts != AV_NOPTS_VALUE) {
                    first_pts = frame->pts;
                    have_first_pts = true;
                }
            }
            uint64_t deadline_ns = presentation_start_ns;
            if (playback.decoded_frames > 1) {
                bool used_pts = false;
                if (have_first_pts && frame->pts != AV_NOPTS_VALUE && video_time_base.num > 0 &&
                    video_time_base.den > 0) {
                    const auto relative_ns = av_rescale_q(
                        frame->pts - first_pts,
                        video_time_base,
                        AVRational{1, static_cast<int>(kNanosecondsPerSecond)}
                    );
                    if (relative_ns >= 0 &&
                        static_cast<uint64_t>(relative_ns) <=
                            std::numeric_limits<uint64_t>::max() - presentation_start_ns) {
                        deadline_ns = presentation_start_ns + static_cast<uint64_t>(relative_ns);
                        used_pts = true;
                    }
                }
                if (!used_pts && fallback_frame_interval_ns > 0 &&
                    playback.decoded_frames - 1 <=
                        std::numeric_limits<uint64_t>::max() / fallback_frame_interval_ns) {
                    deadline_ns = presentation_start_ns +
                                  (playback.decoded_frames - 1) * fallback_frame_interval_ns;
                }
            }
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
    const auto drain = [&](AVCodecContext* codec, AVFrame* frame, bool video) -> bool {
        while (!stop) {
            const auto result = avcodec_receive_frame(codec, frame);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
                return true;
            if (result < 0) {
                playback.error = ffmpeg_error(
                    video ? "avcodec_receive_frame video" : "avcodec_receive_frame audio", result
                );
                return false;
            }
            if (video ? !process_video(frame) : !process_audio(frame))
                return false;
        }
        return true;
    };
    const auto send_chunk = [&](AVCodecContext* codec, AVFrame* frame, bool video) -> bool {
        for (int attempt = 0; attempt < kMaxDecoderBackpressureRetries; ++attempt) {
            const auto result = avcodec_send_packet(codec, impl.chunk);
            if (result == AVERROR(EAGAIN)) {
                if (!drain(codec, frame, video))
                    return false;
                if (stop)
                    return true;
                continue; // retry the same still-owned compressed data after draining.
            }
            if (result == AVERROR_EOF)
                return true;
            if (result < 0) {
                playback.error = ffmpeg_error(
                    video ? "avcodec_send_packet video" : "avcodec_send_packet audio", result
                );
                return false;
            }
            return drain(codec, frame, video);
        }
        playback.error = "decoder remained backpressured after bounded retries";
        return false;
    };
    std::size_t chunks = 0;
    int read_result = AVERROR_EOF;
    while (!stop && chunks < impl.limits.max_chunks &&
           (read_result = av_read_frame(impl.format, impl.chunk)) >= 0) {
        ++chunks;
        if (impl.chunk->stream_index == impl.video_stream) {
            if (!send_chunk(impl.video, impl.video_frame, true))
                break;
        } else if (impl.chunk->stream_index == impl.audio_stream) {
            if (!send_chunk(impl.audio, impl.audio_frame, false))
                break;
        }
        av_packet_unref(impl.chunk);
    }
    av_packet_unref(impl.chunk);
    if (playback.error.empty() && read_result < 0 && read_result != AVERROR_EOF &&
        chunks < impl.limits.max_chunks && !stop)
        playback.error = ffmpeg_error("av_read_frame", read_result);
    if (playback.error.empty() && !stop) {
        const auto video_flush = avcodec_send_packet(impl.video, nullptr);
        if (video_flush >= 0 && !drain(impl.video, impl.video_frame, true)) {
            // drain() has already recorded the decoder error.
        } else if (video_flush < 0 && video_flush != AVERROR_EOF) {
            playback.error = ffmpeg_error("avcodec_flush video", video_flush);
        }
        if (playback.error.empty() && impl.audio != nullptr) {
            const auto audio_flush = avcodec_send_packet(impl.audio, nullptr);
            if (audio_flush >= 0 && !drain(impl.audio, impl.audio_frame, false)) {
                // drain() has already recorded the decoder error.
            } else if (audio_flush < 0 && audio_flush != AVERROR_EOF) {
                playback.error = ffmpeg_error("avcodec_flush audio", audio_flush);
            }
        }
    }
    if (playback.error.empty() && chunks >= impl.limits.max_chunks && !stop)
        playback.error = "intro chunk limit exceeded";
    if (audio_stream != nullptr && playback.error.empty()) {
        if (!SDL_FlushAudioStream(audio_stream)) {
            playback.error = std::string("SDL_FlushAudioStream: ") + SDL_GetError();
        } else if (!playback.skipped && options.frame_limit == 0) {
            // Full playback should not discard the final queued samples. A
            // finite smoke run intentionally returns after its requested
            // frames so validation remains bounded.
            const auto drain_start_ns = SDL_GetTicksNS();
            for (;;) {
                const auto queued = SDL_GetAudioStreamQueued(audio_stream);
                const auto available = SDL_GetAudioStreamAvailable(audio_stream);
                if (queued == 0 && available == 0)
                    break;
                if (queued < 0 || available < 0) {
                    playback.error = std::string("SDL audio stream drain query: ") + SDL_GetError();
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

std::optional<DecodedPcm> decode_audio_file(const std::filesystem::path& path, std::string& error) {
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, path.string().c_str(), nullptr, nullptr) < 0) {
        error = "cannot open audio file";
        return std::nullopt;
    }
    if (avformat_find_stream_info(format, nullptr) < 0) {
        avformat_close_input(&format);
        error = "cannot read audio stream info";
        return std::nullopt;
    }
    int stream_index = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_index < 0) {
        avformat_close_input(&format);
        error = "audio file has no audio stream";
        return std::nullopt;
    }
    const auto* parameters = format->streams[stream_index]->codecpar;
    const auto* codec = avcodec_find_decoder(parameters->codec_id);
    AVCodecContext* codec_ctx = codec != nullptr ? avcodec_alloc_context3(codec) : nullptr;
    if (codec_ctx == nullptr || avcodec_parameters_to_context(codec_ctx, parameters) < 0 ||
        avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format);
        error = "cannot open audio decoder";
        return std::nullopt;
    }
    const int channels = codec_ctx->ch_layout.nb_channels;
    const int rate = codec_ctx->sample_rate;
    if (channels <= 0 || channels > 8 || rate <= 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format);
        error = "audio layout is outside bounds";
        return std::nullopt;
    }
    SwrContext* resampler = nullptr;
    AVChannelLayout output_layout{};
    av_channel_layout_default(&output_layout, channels);
    if (swr_alloc_set_opts2(
            &resampler,
            &output_layout,
            AV_SAMPLE_FMT_S16,
            rate,
            &codec_ctx->ch_layout,
            codec_ctx->sample_fmt,
            codec_ctx->sample_rate,
            0,
            nullptr
        ) < 0 ||
        resampler == nullptr || swr_init(resampler) < 0) {
        av_channel_layout_uninit(&output_layout);
        swr_free(&resampler);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format);
        error = "cannot create audio resampler";
        return std::nullopt;
    }
    av_channel_layout_uninit(&output_layout);
    AVPacket* chunk = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    DecodedPcm pcm;
    pcm.sample_rate = rate;
    pcm.channels = channels;
    auto fail = [&](const char* message) {
        error = message;
        av_frame_free(&frame);
        av_packet_free(&chunk);
        swr_free(&resampler);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format);
        return std::optional<DecodedPcm>{};
    };
    if (chunk == nullptr || frame == nullptr)
        return fail("cannot allocate audio decode buffers");
    while (av_read_frame(format, chunk) >= 0) {
        if (chunk->stream_index != stream_index) {
            av_packet_unref(chunk);
            continue;
        }
        if (avcodec_send_packet(codec_ctx, chunk) < 0) {
            av_packet_unref(chunk);
            return fail("audio chunk decode failed");
        }
        av_packet_unref(chunk);
        while (avcodec_receive_frame(codec_ctx, frame) == 0) {
            const int out_samples = swr_get_out_samples(resampler, frame->nb_samples);
            std::vector<uint8_t> converted(
                static_cast<std::size_t>(std::max(out_samples, 0) * channels * 2)
            );
            uint8_t* dest = converted.data();
            const int written = swr_convert(
                resampler,
                &dest,
                out_samples,
                const_cast<const uint8_t**>(frame->extended_data),
                frame->nb_samples
            );
            av_frame_unref(frame);
            if (written < 0)
                return fail("audio resample failed");
            const auto bytes = static_cast<std::size_t>(written * channels * 2);
            pcm.samples.insert(
                pcm.samples.end(),
                converted.begin(),
                converted.begin() + static_cast<std::ptrdiff_t>(bytes)
            );
        }
    }
    av_frame_free(&frame);
    av_packet_free(&chunk);
    swr_free(&resampler);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format);
    if (pcm.samples.empty()) {
        error = "decoded audio is empty";
        return std::nullopt;
    }
    error.clear();
    return pcm;
}

} // namespace oa::media
