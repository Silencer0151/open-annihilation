// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/sdl_music.hpp"

#include "oa/audio/music_decoder.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace oa::audio {
namespace {

constexpr int output_rate = static_cast<int>(music_output_rate);
constexpr int output_channels = static_cast<int>(music_output_channels);
constexpr int32_t music_aux_device = 0;
constexpr uint32_t full_aux_volume = 0xffffffffU;

} // namespace

struct SdlMusicDevice {
    MusicDisc disc;
    SDL_AudioStream* stream{};
    MusicDecoder decoder;
    std::vector<float> scratch;
    std::string error;
    int32_t current{};  // disc track being decoded
    int32_t to_track{}; // exclusive end of the range, 0 for the disc end
    uint64_t frames{};  // decoded frames of the current track
    uint32_t aux_volume{full_aux_volume};
    bool active{}; // a range is loaded
    bool paused{};
    bool range_done{}; // the decoder reached the end of the range
};

namespace {

// Holds the stream lock that the audio-thread callback runs under.
class StreamLock {
  public:

    explicit StreamLock(SdlMusicDevice& device) : stream_(device.stream) {
        if (stream_ != nullptr)
            SDL_LockAudioStream(stream_);
    }

    ~StreamLock() {
        if (stream_ != nullptr)
            SDL_UnlockAudioStream(stream_);
    }

    StreamLock(const StreamLock&) = delete;
    StreamLock& operator=(const StreamLock&) = delete;

  private:

    SDL_AudioStream* stream_;
};

bool track_in_disc(const SdlMusicDevice& device, int32_t track) {
    return track >= music_disc_first_audio_track && track <= device.disc.track_count;
}

void SDLCALL feed_stream(void* user, SDL_AudioStream* stream, int additional, int) {
    auto& device = *static_cast<SdlMusicDevice*>(user);
    while (additional > 0 && device.active && !device.range_done) {
        device.scratch.clear();
        const bool more = device.decoder.decode(device.scratch);
        if (!device.scratch.empty()) {
            const auto bytes = static_cast<int>(device.scratch.size() * sizeof(float));
            SDL_PutAudioStreamData(stream, device.scratch.data(), bytes);
            additional -= bytes;
            device.frames += device.scratch.size() / output_channels;
        }
        if (more)
            continue;
        device.decoder.close();
        const int32_t next = device.current + 1;
        const bool in_range =
            device.to_track == 0 ? next <= device.disc.track_count : next < device.to_track;
        if (in_range && track_in_disc(device, next) &&
            device.decoder.open(device.disc.tracks[static_cast<std::size_t>(next)], device.error)) {
            device.current = next;
            device.frames = 0;
            continue;
        }
        device.range_done = true;
    }
}

void apply_gain(SdlMusicDevice& device) {
    if (device.stream != nullptr)
        SDL_SetAudioStreamGain(
            device.stream,
            static_cast<float>(device.aux_volume & max_device_volume) /
                static_cast<float>(max_device_volume)
        );
}

bool device_open(void* context) {
    auto& device = *static_cast<SdlMusicDevice*>(context);
    if (device.stream != nullptr)
        return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        device.error = SDL_GetError();
        return false;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, output_channels, output_rate};
    device.stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed_stream, &device);
    if (device.stream == nullptr) {
        device.error = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    apply_gain(device);
    return true;
}

bool device_stop(void* context) {
    auto& device = *static_cast<SdlMusicDevice*>(context);
    if (device.stream == nullptr)
        return true;
    SDL_PauseAudioStreamDevice(device.stream);
    StreamLock lock(device);
    device.decoder.close();
    device.active = false;
    device.paused = false;
    device.range_done = false;
    SDL_ClearAudioStream(device.stream);
    return true;
}

void device_close(void* context) {
    auto& device = *static_cast<SdlMusicDevice*>(context);
    if (device.stream == nullptr)
        return;
    device_stop(context);
    SDL_DestroyAudioStream(device.stream);
    device.stream = nullptr;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool device_track_count(void* context, int32_t* count) {
    const auto& device = *static_cast<SdlMusicDevice*>(context);
    if (!music_disc_present(device.disc))
        return false;
    *count = device.disc.track_count;
    return true;
}

CdTrackKind device_first_track_kind(void* context) {
    const auto& device = *static_cast<SdlMusicDevice*>(context);
    return music_disc_present(device.disc) ? CdTrackKind::other : CdTrackKind::unavailable;
}

bool device_is_playing(void* context) {
    auto& device = *static_cast<SdlMusicDevice*>(context);
    if (device.stream == nullptr)
        return false;
    StreamLock lock(device);
    return device.active && !device.paused &&
           !(device.range_done && SDL_GetAudioStreamQueued(device.stream) == 0);
}

bool device_current_track(void* context, int32_t* track) {
    auto& device = *static_cast<SdlMusicDevice*>(context);
    StreamLock lock(device);
    *track = device.current;
    return music_disc_present(device.disc);
}

bool device_play(void* context, int32_t from, int32_t to) {
    auto& device = *static_cast<SdlMusicDevice*>(context);
    if (device.stream == nullptr || !track_in_disc(device, from))
        return false;
    {
        StreamLock lock(device);
        device.decoder.close();
        SDL_ClearAudioStream(device.stream);
        device.active =
            device.decoder.open(device.disc.tracks[static_cast<std::size_t>(from)], device.error);
        device.current = from;
        device.to_track = to;
        device.frames = 0;
        device.paused = false;
        device.range_done = false;
        if (!device.active)
            return false;
    }
    SDL_ResumeAudioStreamDevice(device.stream);
    return true;
}

bool device_resume(void* context, int32_t to) {
    auto& device = *static_cast<SdlMusicDevice*>(context);
    if (device.stream == nullptr)
        return false;
    {
        StreamLock lock(device);
        if (!device.active)
            return false;
        device.to_track = to;
        device.paused = false;
    }
    SDL_ResumeAudioStreamDevice(device.stream);
    return true;
}

bool device_pause(void* context) {
    auto& device = *static_cast<SdlMusicDevice*>(context);
    if (device.stream == nullptr)
        return false;
    SDL_PauseAudioStreamDevice(device.stream);
    StreamLock lock(device);
    device.paused = true;
    return true;
}

bool device_disc_id(void* context, uint32_t* id) {
    const auto& device = *static_cast<SdlMusicDevice*>(context);
    if (!music_disc_present(device.disc))
        return false;
    *id = device.disc.disc_id;
    return true;
}

uint32_t device_find_foreign_player(void*) {
    return 0;
}

void device_close_foreign_player(void*, uint32_t) {
}

int32_t device_aux_count(void*) {
    return 1;
}

bool device_aux_is_cd_audio(void*, int32_t aux) {
    return aux == music_aux_device;
}

bool device_get_aux_volume(void* context, int32_t aux, uint32_t* packed) {
    if (aux != music_aux_device)
        return false;
    *packed = static_cast<SdlMusicDevice*>(context)->aux_volume;
    return true;
}

bool device_set_aux_volume(void* context, int32_t aux, uint32_t packed) {
    if (aux != music_aux_device)
        return false;
    auto& device = *static_cast<SdlMusicDevice*>(context);
    device.aux_volume = packed;
    apply_gain(device);
    return true;
}

} // namespace

SdlMusicDevice* sdl_music_device_create(MusicDisc disc) {
    auto* device = new (std::nothrow) SdlMusicDevice{};
    if (device != nullptr)
        device->disc = std::move(disc);
    return device;
}

void sdl_music_device_destroy(SdlMusicDevice* device) noexcept {
    if (device == nullptr)
        return;
    device_close(device);
    delete device;
}

MusicDevice sdl_music_device(SdlMusicDevice* device) noexcept {
    MusicDevice music{};
    music.context = device;
    music.open = device_open;
    music.close = device_close;
    music.stop = device_stop;
    music.track_count = device_track_count;
    music.first_track_kind = device_first_track_kind;
    music.is_playing = device_is_playing;
    music.current_track = device_current_track;
    music.play = device_play;
    music.resume = device_resume;
    music.pause = device_pause;
    music.disc_id = device_disc_id;
    music.find_foreign_player = device_find_foreign_player;
    music.close_foreign_player = device_close_foreign_player;
    music.aux_count = device_aux_count;
    music.aux_is_cd_audio = device_aux_is_cd_audio;
    music.get_aux_volume = device_get_aux_volume;
    music.set_aux_volume = device_set_aux_volume;
    return music;
}

bool sdl_music_device_poll_complete(SdlMusicDevice* device) noexcept {
    if (device == nullptr || device->stream == nullptr)
        return false;
    StreamLock lock(*device);
    if (!device->active || device->paused || !device->range_done ||
        SDL_GetAudioStreamQueued(device->stream) != 0)
        return false;
    device->active = false;
    return true;
}

bool sdl_music_decoder_available() noexcept {
    return true;
}

const char* sdl_music_device_error(const SdlMusicDevice* device) noexcept {
    return device != nullptr ? device->error.c_str() : "";
}

double sdl_music_device_position(SdlMusicDevice* device) noexcept {
    if (device == nullptr)
        return 0.0;
    StreamLock lock(*device);
    return static_cast<double>(device->frames) / output_rate;
}

} // namespace oa::audio
