// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/sdl_audio.hpp"

#include "oa/audio/spatial_gain.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace oa::audio::game_audio {

struct LoopingTrack {
    std::vector<uint8_t> pcm;
    std::size_t offset = 0;
};

void SDLCALL refill_loop(void* userdata, SDL_AudioStream* stream, int additional, int) {
    auto* loop = static_cast<LoopingTrack*>(userdata);
    if (loop == nullptr || loop->pcm.empty() || additional <= 0)
        return;
    while (additional > 0) {
        if (loop->offset >= loop->pcm.size())
            loop->offset = 0;
        const auto remain = loop->pcm.size() - loop->offset;
        const auto put = std::min(remain, static_cast<std::size_t>(additional));
        if (!SDL_PutAudioStreamData(stream, loop->pcm.data() + loop->offset, static_cast<int>(put)))
            return;
        loop->offset += put;
        additional -= static_cast<int>(put);
    }
}

struct PlayingSound {
    SDL_AudioStream* stream{};
    float level{1.0F}; // the voice volume relative to the near volume
};

struct SdlWavPlayer::Impl {
    const oa::AssetStore& assets;
    std::vector<PlayingSound> streams;
    SDL_AudioStream* looping = nullptr;
    LoopingTrack loop_track;
    SDL_AudioStream* stream = nullptr; // the one streamed sound, played once
    uint32_t wave_out_volume{full_wave_out_volume};
    uint32_t fx_volume{default_fx_volume};

    explicit Impl(const oa::AssetStore& value) : assets(value) {}

    ~Impl() {
        if (looping != nullptr)
            SDL_DestroyAudioStream(looping);
        if (stream != nullptr)
            SDL_DestroyAudioStream(stream);
        for (auto& sound : streams)
            SDL_DestroyAudioStream(sound.stream);
    }
};

namespace {

constexpr uint64_t milliseconds_per_second = 1000;

// Decodes a WAV the asset store holds; the caller frees `wav` with SDL_free.
bool load_wav(
    const oa::AssetStore& assets,
    std::string_view resource,
    SDL_AudioSpec& spec,
    Uint8*& wav,
    Uint32& length,
    std::string& error
) {
    AssetData data;
    try {
        data = assets.read(resource);
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
    SDL_IOStream* io = SDL_IOFromConstMem(data.bytes.data(), data.bytes.size());
    if (!io) {
        error = SDL_GetError();
        return false;
    }
    if (!SDL_LoadWAV_IO(io, true, &spec, &wav, &length)) {
        error = SDL_GetError();
        return false;
    }
    return true;
}

// Pans a placed voice's samples, first widening them to 16-bit stereo.
bool place_samples(
    SDL_AudioSpec& spec, Uint8*& wav, Uint32& length, const oa::audio::Spatial& spatial
) {
    const SDL_AudioSpec stereo{SDL_AUDIO_S16, 2, spec.freq};
    Uint8* converted = nullptr;
    int converted_length = 0;
    const bool ok = SDL_ConvertAudioSamples(
        &spec, wav, static_cast<int>(length), &stereo, &converted, &converted_length
    );
    SDL_free(wav);
    wav = converted;
    if (!ok)
        return false;
    const auto gain = oa::audio::spatial_stereo_gain(spatial);
    auto* samples = reinterpret_cast<Sint16*>(converted);
    const auto frames = static_cast<std::size_t>(converted_length) / (2 * sizeof(Sint16));
    for (std::size_t frame = 0; frame < frames; ++frame) {
        samples[2 * frame] =
            static_cast<Sint16>(std::lround(static_cast<float>(samples[2 * frame]) * gain.left));
        samples[2 * frame + 1] = static_cast<Sint16>(
            std::lround(static_cast<float>(samples[2 * frame + 1]) * gain.right)
        );
    }
    spec = stereo;
    length = static_cast<Uint32>(converted_length);
    return true;
}

} // namespace

SdlWavPlayer::SdlWavPlayer(const oa::AssetStore& assets) : impl_(std::make_unique<Impl>(assets)) {
}

SdlWavPlayer::~SdlWavPlayer() = default;
SdlWavPlayer::SdlWavPlayer(SdlWavPlayer&&) noexcept = default;
SdlWavPlayer& SdlWavPlayer::operator=(SdlWavPlayer&&) noexcept = default;

bool SdlWavPlayer::play(const Selection& selection, std::string& error) {
    if (selection.status != SelectionStatus::selected || selection.sound == nullptr) {
        error = "sound selection is not playable";
        return false;
    }
    return play_resource(selection.sound->resource, error);
}

bool SdlWavPlayer::play_resource(std::string_view resource, std::string& error) {
    return play_placed(resource, oa::audio::volume_near, oa::audio::Spatial{}, error);
}

bool SdlWavPlayer::play_placed(
    std::string_view resource, int32_t volume, const oa::audio::Spatial& spatial, std::string& error
) {
    collect_finished();
    SDL_AudioSpec spec{};
    Uint8* wav = nullptr;
    Uint32 length = 0;
    if (!load_wav(impl_->assets, resource, spec, wav, length, error))
        return false;
    if (spatial.mode == oa::audio::SpatialMode::normal &&
        !place_samples(spec, wav, length, spatial)) {
        error = SDL_GetError();
        return false;
    }
    const float level =
        std::pow(10.0F, static_cast<float>(volume - oa::audio::volume_near) / 2000.0F);
    SDL_AudioStream* stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream) {
        error = SDL_GetError();
        SDL_free(wav);
        return false;
    }
    if (!SDL_SetAudioStreamGain(
            stream, output_gain(impl_->wave_out_volume, impl_->fx_volume) * level
        )) {
        error = SDL_GetError();
        SDL_DestroyAudioStream(stream);
        SDL_free(wav);
        return false;
    }
    const bool queued = SDL_PutAudioStreamData(stream, wav, static_cast<int>(length));
    SDL_free(wav);
    if (!queued || !SDL_FlushAudioStream(stream) || !SDL_ResumeAudioStreamDevice(stream)) {
        error = SDL_GetError();
        SDL_DestroyAudioStream(stream);
        return false;
    }
    impl_->streams.push_back({stream, level});
    error.clear();
    return true;
}

bool SdlWavPlayer::start_loop_resource(std::string_view resource, std::string& error) {
    stop_loop();
    SDL_AudioSpec spec{};
    Uint8* wav = nullptr;
    Uint32 length = 0;
    if (!load_wav(impl_->assets, resource, spec, wav, length, error))
        return false;
    impl_->loop_track.pcm.assign(wav, wav + length);
    impl_->loop_track.offset = 0;
    SDL_free(wav);
    impl_->looping = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, refill_loop, &impl_->loop_track
    );
    if (impl_->looping == nullptr) {
        error = SDL_GetError();
        impl_->loop_track.pcm.clear();
        return false;
    }
    if (!SDL_SetAudioStreamGain(
            impl_->looping, output_gain(impl_->wave_out_volume, impl_->fx_volume)
        ) ||
        !SDL_ResumeAudioStreamDevice(impl_->looping)) {
        error = SDL_GetError();
        stop_loop();
        return false;
    }
    error.clear();
    return true;
}

void SdlWavPlayer::stop_loop() noexcept {
    if (impl_->looping != nullptr) {
        SDL_DestroyAudioStream(impl_->looping);
        impl_->looping = nullptr;
    }
    impl_->loop_track = {};
}

bool SdlWavPlayer::play_stream(std::string_view resource, uint32_t delay_ms, std::string& error) {
    stop_stream();
    SDL_AudioSpec spec{};
    Uint8* wav = nullptr;
    Uint32 length = 0;
    if (!load_wav(impl_->assets, resource, spec, wav, length, error))
        return false;
    SDL_AudioStream* stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream == nullptr) {
        error = SDL_GetError();
        SDL_free(wav);
        return false;
    }
    // The delay plays as silence ahead of the sound.
    const auto delay_frames = static_cast<std::size_t>(
        static_cast<uint64_t>(spec.freq) * delay_ms / milliseconds_per_second
    );
    const std::vector<Uint8> silence(
        delay_frames * static_cast<std::size_t>(SDL_AUDIO_FRAMESIZE(spec)),
        static_cast<Uint8>(SDL_GetSilenceValueForFormat(spec.format))
    );
    const bool queued =
        SDL_SetAudioStreamGain(stream, output_gain(impl_->wave_out_volume, impl_->fx_volume)) &&
        (silence.empty() ||
         SDL_PutAudioStreamData(stream, silence.data(), static_cast<int>(silence.size()))) &&
        SDL_PutAudioStreamData(stream, wav, static_cast<int>(length)) &&
        SDL_FlushAudioStream(stream) && SDL_ResumeAudioStreamDevice(stream);
    SDL_free(wav);
    if (!queued) {
        error = SDL_GetError();
        SDL_DestroyAudioStream(stream);
        return false;
    }
    impl_->stream = stream;
    error.clear();
    return true;
}

void SdlWavPlayer::stop_stream() noexcept {
    if (impl_->stream != nullptr) {
        SDL_DestroyAudioStream(impl_->stream);
        impl_->stream = nullptr;
    }
}

bool SdlWavPlayer::stream_busy() const noexcept {
    return impl_->stream != nullptr && (SDL_GetAudioStreamQueued(impl_->stream) > 0 ||
                                        SDL_GetAudioStreamAvailable(impl_->stream) > 0);
}

void SdlWavPlayer::set_volume(uint32_t wave_out_volume, uint32_t fx_volume) noexcept {
    impl_->wave_out_volume = wave_out_volume;
    impl_->fx_volume = fx_volume;
    const float gain = output_gain(wave_out_volume, fx_volume);
    for (auto& sound : impl_->streams)
        SDL_SetAudioStreamGain(sound.stream, gain * sound.level);
    if (impl_->looping != nullptr)
        SDL_SetAudioStreamGain(impl_->looping, gain);
    if (impl_->stream != nullptr)
        SDL_SetAudioStreamGain(impl_->stream, gain);
}

void SdlWavPlayer::collect_finished() noexcept {
    std::erase_if(impl_->streams, [](const PlayingSound& sound) {
        if (SDL_GetAudioStreamQueued(sound.stream) > 0 ||
            SDL_GetAudioStreamAvailable(sound.stream) > 0)
            return false;
        SDL_DestroyAudioStream(sound.stream);
        return true;
    });
    if (impl_->stream != nullptr && !stream_busy())
        stop_stream();
}

} // namespace oa::audio::game_audio

namespace oa::audio {

struct SdlVoice {
    SDL_AudioStream* stream{};
    std::vector<uint8_t> pcm;
    PcmFormat format{};
    uint32_t read{};
    bool looping{};
    bool playing{};
    float gain{1.0F};
};

struct SdlAudioDevice {
    bool open{};
    std::map<BufferHandle, std::unique_ptr<SdlVoice>> voices;
    BufferHandle next{1};
    uint32_t wave_out_packed{0xffffffffU};

    SdlVoice* find(BufferHandle handle) {
        const auto found = voices.find(handle);
        return found == voices.end() ? nullptr : found->second.get();
    }

    float master_gain() const {
        return static_cast<float>(wave_out_packed & max_device_volume) /
               static_cast<float>(max_device_volume);
    }
};

namespace {

// Runs on the SDL audio thread with the stream locked.
void SDLCALL feed_voice(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* voice = static_cast<SdlVoice*>(user);
    while (voice->playing && additional > 0 && !voice->pcm.empty()) {
        if (voice->read >= voice->pcm.size()) {
            if (!voice->looping) {
                voice->playing = SDL_GetAudioStreamQueued(stream) > 0;
                return;
            }
            voice->read = 0;
        }
        const auto count = std::min<std::size_t>(
            voice->pcm.size() - voice->read, static_cast<std::size_t>(additional)
        );
        const auto* bytes = voice->pcm.data() + voice->read;
        if (!SDL_PutAudioStreamData(stream, bytes, static_cast<int>(count)))
            return;
        voice->read += static_cast<uint32_t>(count);
        additional -= static_cast<int>(count);
    }
}

SDL_AudioFormat sdl_format(uint16_t bits) {
    return bits == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
}

BufferHandle open_voice(SdlAudioDevice& device, std::unique_ptr<SdlVoice> voice) {
    if (!device.open || (voice->format.bits != 8 && voice->format.bits != 16) ||
        voice->format.channels == 0 || voice->format.sample_rate == 0)
        return no_buffer;
    SDL_AudioSpec spec{};
    spec.format = sdl_format(voice->format.bits);
    spec.channels = voice->format.channels;
    spec.freq = static_cast<int>(voice->format.sample_rate);
    voice->stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed_voice, voice.get()
    );
    if (voice->stream == nullptr)
        return no_buffer;
    SDL_SetAudioStreamGain(voice->stream, device.master_gain());
    const BufferHandle handle = device.next++;
    device.voices.emplace(handle, std::move(voice));
    return handle;
}

template <typename Function>
bool with_voice(void* context, BufferHandle handle, Function&& function) {
    auto* voice = static_cast<SdlAudioDevice*>(context)->find(handle);
    if (voice == nullptr)
        return false;
    SDL_LockAudioStream(voice->stream);
    const bool result = function(*voice);
    SDL_UnlockAudioStream(voice->stream);
    return result;
}

} // namespace

SdlAudioDevice* sdl_audio_device_create() {
    return new SdlAudioDevice{};
}

void sdl_audio_device_destroy(SdlAudioDevice* device) noexcept {
    if (device == nullptr)
        return;
    for (auto& [handle, voice] : device->voices)
        SDL_DestroyAudioStream(voice->stream);
    delete device;
}

AudioSink sdl_audio_sink(SdlAudioDevice* device) noexcept {
    AudioSink sink{};
    sink.context = device;
    sink.open_device = [](void* context) {
        auto& self = *static_cast<SdlAudioDevice*>(context);
        if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
            return DeviceResult::no_driver;
        self.open = true;
        return DeviceResult::ok;
    };
    sink.set_primary_format = [](void*, PcmFormat) { return DeviceResult::ok; };
    sink.close_device = [](void* context) {
        auto& self = *static_cast<SdlAudioDevice*>(context);
        for (auto& [handle, voice] : self.voices)
            SDL_DestroyAudioStream(voice->stream);
        self.voices.clear();
        self.open = false;
    };
    sink.create_buffer = [](void* context, BufferKind, PcmFormat format, uint32_t bytes) {
        auto voice = std::make_unique<SdlVoice>();
        voice->format = format;
        voice->pcm.assign(bytes, format.bits == 8 ? 0x80 : 0);
        return open_voice(*static_cast<SdlAudioDevice*>(context), std::move(voice));
    };
    sink.duplicate_buffer = [](void* context, BufferHandle source) {
        auto& self = *static_cast<SdlAudioDevice*>(context);
        const auto* original = self.find(source);
        if (original == nullptr)
            return no_buffer;
        auto voice = std::make_unique<SdlVoice>();
        voice->format = original->format;
        voice->pcm = original->pcm;
        return open_voice(self, std::move(voice));
    };
    sink.release_buffer = [](void* context, BufferHandle handle) {
        auto& self = *static_cast<SdlAudioDevice*>(context);
        const auto found = self.voices.find(handle);
        if (found == self.voices.end())
            return;
        SDL_DestroyAudioStream(found->second->stream);
        self.voices.erase(found);
    };
    sink.write_buffer = [](void* context,
                           BufferHandle handle,
                           uint32_t offset,
                           const uint8_t* bytes,
                           uint32_t count) {
        return with_voice(context, handle, [&](SdlVoice& voice) {
            if (offset > voice.pcm.size() || count > voice.pcm.size() - offset)
                return false;
            std::memcpy(voice.pcm.data() + offset, bytes, count);
            return true;
        });
    };
    sink.query_playing = [](void* context, BufferHandle handle, bool* playing) {
        return with_voice(context, handle, [&](SdlVoice& voice) {
            *playing = voice.playing || SDL_GetAudioStreamQueued(voice.stream) > 0;
            return true;
        });
    };
    sink.play_position = [](void* context, BufferHandle handle, uint32_t* position) {
        return with_voice(context, handle, [&](SdlVoice& voice) {
            *position = voice.read;
            return true;
        });
    };
    sink.set_play_position = [](void* context, BufferHandle handle, uint32_t position) {
        return with_voice(context, handle, [&](SdlVoice& voice) {
            const auto size = static_cast<uint32_t>(voice.pcm.size());
            voice.read = std::min(position, size);
            SDL_ClearAudioStream(voice.stream);
            return true;
        });
    };
    sink.set_volume = [](void* context, BufferHandle handle, int32_t centibels) {
        const float master = static_cast<SdlAudioDevice*>(context)->master_gain();
        return with_voice(context, handle, [&](SdlVoice& voice) {
            voice.gain = std::pow(10.0F, static_cast<float>(centibels) / 2000.0F);
            return SDL_SetAudioStreamGain(voice.stream, voice.gain * master);
        });
    };
    sink.set_spatial = [](void*, BufferHandle, const Spatial*) { return false; };
    sink.play = [](void* context, BufferHandle handle, bool looping) {
        const bool started = with_voice(context, handle, [&](SdlVoice& voice) {
            voice.looping = looping;
            voice.playing = true;
            return true;
        });
        auto* voice = static_cast<SdlAudioDevice*>(context)->find(handle);
        return started && SDL_ResumeAudioStreamDevice(voice->stream);
    };
    sink.stop = [](void* context, BufferHandle handle) {
        with_voice(context, handle, [](SdlVoice& voice) {
            voice.playing = false;
            SDL_ClearAudioStream(voice.stream);
            return true;
        });
    };
    sink.wave_out_count = [](void*) { return 1; };
    sink.get_wave_out_volume = [](void* context, int32_t, uint32_t* packed) {
        *packed = static_cast<SdlAudioDevice*>(context)->wave_out_packed;
        return true;
    };
    sink.set_wave_out_volume = [](void* context, int32_t, uint32_t packed) {
        auto& self = *static_cast<SdlAudioDevice*>(context);
        self.wave_out_packed = packed;
        for (auto& [handle, voice] : self.voices)
            SDL_SetAudioStreamGain(voice->stream, voice->gain * self.master_gain());
        return true;
    };
    return sink;
}

} // namespace oa::audio
