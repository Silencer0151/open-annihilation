// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/sdl_music.hpp"

#include "oa/audio/music_decoder.hpp"
#include "oa/audio/sound_output.hpp"

#include <cstdint>
#include <memory>
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
    std::unique_ptr<OutputStream> stream;
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

    explicit StreamLock(SdlMusicDevice& device) : stream_(device.stream.get()) {
        if (stream_ != nullptr)
            stream_->lock();
    }

    ~StreamLock() {
        if (stream_ != nullptr)
            stream_->unlock();
    }

    StreamLock(const StreamLock&) = delete;
    StreamLock& operator=(const StreamLock&) = delete;

  private:

    OutputStream* stream_;
};

bool track_in_disc(const SdlMusicDevice& device, int32_t track) {
    return track >= music_disc_first_audio_track && track <= device.disc.track_count;
}

void feed_stream(void* user, OutputStream& stream, int32_t additional) {
    auto& device = *static_cast<SdlMusicDevice*>(user);
    while (additional > 0 && device.active && !device.range_done) {
        device.scratch.clear();
        const bool more = device.decoder.decode(device.scratch);
        if (!device.scratch.empty()) {
            const auto bytes = static_cast<int>(device.scratch.size() * sizeof(float));
            (void)stream.put(device.scratch.data(), bytes);
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
        device.stream->set_gain(
            static_cast<float>(device.aux_volume & max_device_volume) /
            static_cast<float>(max_device_volume)
        );
}

bool device_open(void* context) {
    auto& device = *static_cast<SdlMusicDevice*>(context);
    if (device.stream != nullptr)
        return true;
    SoundOutput& output = sound_output();
    if (!output.start(device.error))
        return false;
    const StreamFormat format{
        SampleFormat::f32, static_cast<uint8_t>(output_channels), static_cast<uint32_t>(output_rate)
    };
    device.stream = output.open_stream(format, feed_stream, &device, device.error);
    if (device.stream == nullptr) {
        output.stop();
        return false;
    }
    apply_gain(device);
    return true;
}

bool device_stop(void* context) {
    auto& device = *static_cast<SdlMusicDevice*>(context);
    if (device.stream == nullptr)
        return true;
    device.stream->pause();
    StreamLock lock(device);
    device.decoder.close();
    device.active = false;
    device.paused = false;
    device.range_done = false;
    device.stream->clear();
    return true;
}

void device_close(void* context) {
    auto& device = *static_cast<SdlMusicDevice*>(context);
    if (device.stream == nullptr)
        return;
    device_stop(context);
    device.stream.reset();
    sound_output().stop();
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
           !(device.range_done && device.stream->queued_bytes() == 0);
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
        device.stream->clear();
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
    device.stream->resume();
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
    device.stream->resume();
    return true;
}

bool device_pause(void* context) {
    auto& device = *static_cast<SdlMusicDevice*>(context);
    if (device.stream == nullptr)
        return false;
    device.stream->pause();
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
        device->stream->queued_bytes() != 0)
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
