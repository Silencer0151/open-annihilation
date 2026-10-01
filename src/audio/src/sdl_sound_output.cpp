// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The sound output over SDL: each stream is an SDL audio stream bound to a
// logical playback device of its own, which SDL converts and mixes.

#include "oa/audio/sound_output_backends.hpp"

#include <SDL3/SDL.h>

namespace oa::audio {
namespace {

SDL_AudioFormat sdl_format(SampleFormat sample) {
    switch (sample) {
    case SampleFormat::u8:
        return SDL_AUDIO_U8;
    case SampleFormat::s16:
        return SDL_AUDIO_S16LE;
    case SampleFormat::s32:
        return SDL_AUDIO_S32LE;
    case SampleFormat::f32:
        return SDL_AUDIO_F32LE;
    }
    return SDL_AUDIO_S16LE;
}

class SdlStream final : public OutputStream {
  public:

    SdlStream(StreamFeed feed, void* context) : feed_(feed), context_(context) {}

    ~SdlStream() override {
        if (stream_ != nullptr)
            SDL_DestroyAudioStream(stream_);
    }

    // Opens the stream on its own logical device, paused.
    bool open(const StreamFormat& format, std::string& error) {
        SDL_AudioSpec spec{};
        spec.format = sdl_format(format.sample);
        spec.channels = format.channels;
        spec.freq = static_cast<int>(format.rate);
        stream_ = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed_ != nullptr ? feed_thunk : nullptr, this
        );
        if (stream_ == nullptr) {
            error = SDL_GetError();
            return false;
        }
        return true;
    }

    bool put(const void* bytes, int32_t count) override {
        return SDL_PutAudioStreamData(stream_, bytes, count);
    }

    bool flush() override { return SDL_FlushAudioStream(stream_); }

    int32_t queued_bytes() override { return SDL_GetAudioStreamQueued(stream_); }

    int32_t available_bytes() override { return SDL_GetAudioStreamAvailable(stream_); }

    void clear() override { SDL_ClearAudioStream(stream_); }

    bool set_gain(float gain) override { return SDL_SetAudioStreamGain(stream_, gain); }

    bool pause() override { return SDL_PauseAudioStreamDevice(stream_); }

    bool resume() override { return SDL_ResumeAudioStreamDevice(stream_); }

    void lock() override { SDL_LockAudioStream(stream_); }

    void unlock() override { SDL_UnlockAudioStream(stream_); }

  private:

    static void SDLCALL feed_thunk(void* user, SDL_AudioStream*, int additional, int) {
        auto* stream = static_cast<SdlStream*>(user);
        stream->feed_(stream->context_, *stream, additional);
    }

    SDL_AudioStream* stream_{};
    StreamFeed feed_{};
    void* context_{};
};

class SdlOutput final : public SoundOutput {
  public:

    bool start(std::string& error) override {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO))
            return true;
        error = SDL_GetError();
        return false;
    }

    void stop() override { SDL_QuitSubSystem(SDL_INIT_AUDIO); }

    bool started() const override { return SDL_WasInit(SDL_INIT_AUDIO) != 0; }

    std::unique_ptr<OutputStream> open_stream(
        const StreamFormat& format, StreamFeed feed, void* context, std::string& error
    ) override {
        auto stream = std::make_unique<SdlStream>(feed, context);
        if (!stream->open(format, error))
            return nullptr;
        return stream;
    }

    std::string driver_name() const override {
        const char* name = SDL_GetCurrentAudioDriver();
        return name != nullptr ? name : "";
    }

    std::string last_error() const override { return SDL_GetError(); }
};

} // namespace

std::unique_ptr<SoundOutput> sdl_sound_output_create() {
    return std::make_unique<SdlOutput>();
}

} // namespace oa::audio
