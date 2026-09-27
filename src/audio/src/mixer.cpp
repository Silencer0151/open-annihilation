// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/mixer.hpp"

#include "oa/audio/cd_music.hpp"

#include <cstdint>
#include <cstring>
#include <utility>

namespace oa::audio {
namespace {

// Largest stream buffer accepted (two seconds of 48 kHz 16-bit stereo is
// under 0.4 MiB); larger formats are malformed input.
constexpr uint32_t max_stream_bytes = 16U * 1024U * 1024U;
constexpr uint8_t silence_8bit = 0x80;
constexpr uint8_t silence_16bit = 0;

uint32_t pack_stereo(uint32_t level) noexcept {
    return level << 16 | level;
}

uint32_t clamp_level(int32_t level) noexcept {
    if (level < 0)
        level = 0;
    if (level > static_cast<int32_t>(max_device_volume))
        level = static_cast<int32_t>(max_device_volume);
    return static_cast<uint32_t>(level);
}

bool query_playing(Mixer& mixer, BufferHandle buffer, bool& playing) noexcept {
    playing = false;
    return mixer.sink.query_playing != nullptr &&
           mixer.sink.query_playing(mixer.sink.context, buffer, &playing);
}

void stop_buffer(Mixer& mixer, BufferHandle buffer) noexcept {
    if (mixer.sink.stop != nullptr)
        mixer.sink.stop(mixer.sink.context, buffer);
}

void release_buffer(Mixer& mixer, BufferHandle buffer) noexcept {
    if (mixer.sink.release_buffer != nullptr)
        mixer.sink.release_buffer(mixer.sink.context, buffer);
}

bool set_play_position(Mixer& mixer, BufferHandle buffer, uint32_t position) noexcept {
    return mixer.sink.set_play_position != nullptr &&
           mixer.sink.set_play_position(mixer.sink.context, buffer, position);
}

bool set_volume(Mixer& mixer, BufferHandle buffer, int32_t volume) noexcept {
    return mixer.sink.set_volume != nullptr &&
           mixer.sink.set_volume(mixer.sink.context, buffer, volume);
}

bool start_buffer(Mixer& mixer, BufferHandle buffer, bool looping) noexcept {
    return mixer.sink.play != nullptr && mixer.sink.play(mixer.sink.context, buffer, looping);
}

void remove_timer(Mixer& mixer, int32_t handle) noexcept {
    if (mixer.timers.remove != nullptr)
        mixer.timers.remove(mixer.timers.context, handle);
}

SampleId allocate_sample(Mixer& mixer) noexcept {
    for (std::size_t i = 0; i < sample_pool; ++i) {
        if (!mixer.samples[i].in_use) {
            mixer.samples[i] = Sample{};
            mixer.samples[i].in_use = true;
            return static_cast<SampleId>(i + 1);
        }
    }
    return no_sample;
}

bool attach_sample(
    Mixer& mixer, SampleId id, int32_t volume, const VoicePosition* position, bool looping
) noexcept {
    if (looping) {
        for (std::size_t i = 0; i < voice_slots; ++i)
            if (mixer.voices[i] != no_buffer && mixer.voice_looping[i] == 1)
                return false;
    }
    while (mixer.voice_limit <= mixer.voice_count) {
        // Every voice looping leaves nothing to evict.
        if (!mixer_evict_oldest_voice(mixer))
            return false;
    }
    Sample* sample = mixer_sample(mixer, id);
    if (sample == nullptr)
        return false;

    BufferHandle chosen = no_buffer;
    uint32_t furthest = 0;
    std::size_t furthest_index = 0;
    std::size_t empty_index = 0;
    for (std::size_t k = 0; k < sample_buffers; ++k) {
        const BufferHandle buffer = sample->buffers[k];
        if (buffer == no_buffer) {
            empty_index = k;
            continue;
        }
        bool playing = false;
        if (!query_playing(mixer, buffer, playing))
            return false;
        if (!playing) {
            chosen = buffer;
            break;
        }
        uint32_t position_bytes = 0;
        if (mixer.sink.play_position != nullptr)
            mixer.sink.play_position(mixer.sink.context, buffer, &position_bytes);
        if (furthest < position_bytes) {
            furthest = position_bytes;
            furthest_index = k;
        }
    }
    if (chosen == no_buffer) {
        if (empty_index < 1) {
            chosen = sample->buffers[furthest_index];
            set_play_position(mixer, chosen, 0);
        } else {
            if (mixer.sink.duplicate_buffer != nullptr)
                chosen = mixer.sink.duplicate_buffer(mixer.sink.context, sample->buffers[0]);
            if (chosen == no_buffer)
                return false;
            sample->buffers[empty_index] = chosen;
        }
    }

    const Spatial spatial =
        voice_spatial(mixer.spatial_enabled, mixer.min_distance, mixer.max_distance, position);
    if (mixer.sink.set_spatial != nullptr)
        mixer.sink.set_spatial(mixer.sink.context, chosen, &spatial);

    if (!set_play_position(mixer, chosen, 0) || !set_volume(mixer, chosen, volume) ||
        !start_buffer(mixer, chosen, looping))
        return false;
    for (std::size_t i = 0; i < voice_slots; ++i) {
        if (mixer.voices[i] == no_buffer) {
            mixer.voices[i] = chosen;
            ++mixer.voice_serial;
            mixer.voice_order[i] = mixer.voice_serial;
            mixer.voice_looping[i] = looping ? 1 : 0;
            ++mixer.voice_count;
            return true;
        }
    }
    return true;
}

} // namespace

Spatial voice_spatial(
    int32_t spatial_enabled, float min_distance, float max_distance, const VoicePosition* position
) noexcept {
    Spatial spatial{};
    if (spatial_enabled != 0 && position != nullptr) {
        spatial.mode = SpatialMode::normal;
        spatial.x = static_cast<float>(position->x);
        spatial.y = static_cast<float>(position->y);
        spatial.z = static_cast<float>(position->z);
        spatial.min_distance = min_distance;
        spatial.max_distance = max_distance;
    }
    return spatial;
}

Sample* mixer_sample(Mixer& mixer, SampleId id) noexcept {
    if (id == no_sample || id > sample_pool || !mixer.samples[id - 1].in_use)
        return nullptr;
    return &mixer.samples[id - 1];
}

void mixer_init(Mixer& mixer) noexcept {
    const AudioSink sink = mixer.sink;
    const MusicDevice music = mixer.music;
    const AudioTimers timers = mixer.timers;
    const AudioFiles files = mixer.files;
    const AudioRandom random = mixer.random;
    mixer.stream = StreamState{};
    mixer.cd = CdAudio{};
    for (auto& sample : mixer.samples)
        sample = Sample{};
    mixer.sink = sink;
    mixer.music = music;
    mixer.timers = timers;
    mixer.files = files;
    mixer.random = random;

    mixer.min_distance = default_min_distance;
    mixer.cd.device_open = 0;
    mixer.spatial_enabled = 0;
    mixer.no_driver = 0;
    mixer.max_distance = default_max_distance;
    mixer.device_open = 0;
    for (auto& slot : mixer.transients)
        slot = no_sample;
    mixer_init_volume_devices(mixer);
    mixer.voice_limit = default_voice_limit;
    mixer.voice_count = 0;
    mixer.voice_serial = 1;
    for (std::size_t i = 0; i < voice_slots; ++i) {
        mixer.voices[i] = no_buffer;
        mixer.voice_order[i] = 0;
        mixer.voice_looping[i] = 0;
    }
    mixer.stream.buffer = no_buffer;
    mixer.stream.timer = no_timer;
    mixer.primary = PcmFormat{};
    mixer.music_volume = mixer_music_line_volume(mixer);
}

void mixer_teardown(Mixer& mixer) noexcept {
    for (auto& slot : mixer.transients) {
        if (slot != no_sample) {
            mixer_release_sample(mixer, slot);
            slot = no_sample;
        }
    }
    mixer_stop_stream(mixer);
    cd_close(mixer);
    if (mixer.device_open != 0 && mixer.sink.close_device != nullptr)
        mixer.sink.close_device(mixer.sink.context);
    mixer.device_open = 0;
}

bool mixer_open_device(Mixer& mixer, PcmFormat format) noexcept {
    DeviceResult result = DeviceResult::failed;
    if (mixer.device_open == 0) {
        if (mixer.sink.open_device != nullptr)
            result = mixer.sink.open_device(mixer.sink.context);
        if (result == DeviceResult::ok)
            mixer.device_open = 1;
    } else {
        result = DeviceResult::ok;
    }
    if (result == DeviceResult::ok) {
        result = mixer.sink.set_primary_format != nullptr
                     ? mixer.sink.set_primary_format(mixer.sink.context, format)
                     : DeviceResult::failed;
        if (result == DeviceResult::ok) {
            mixer.primary = format;
            return true;
        }
    }
    if (result == DeviceResult::no_driver)
        mixer.no_driver = 1;
    mixer_teardown(mixer);
    return false;
}

void mixer_collect_finished(Mixer& mixer) noexcept {
    for (auto& slot : mixer.transients) {
        if (slot == no_sample)
            continue;
        const Sample* sample = mixer_sample(mixer, slot);
        bool playing = false;
        if (sample == nullptr || !query_playing(mixer, sample->buffers[0], playing) || !playing) {
            mixer_release_sample(mixer, slot);
            slot = no_sample;
        }
    }
    for (std::size_t i = 0; i < voice_slots; ++i) {
        if (mixer.voices[i] == no_buffer)
            continue;
        bool playing = false;
        if (!query_playing(mixer, mixer.voices[i], playing) || !playing) {
            mixer.voices[i] = no_buffer;
            --mixer.voice_count;
        }
    }
    if (mixer.stream.buffer != no_buffer)
        mixer_update_stream(mixer);
}

void mixer_stop_voices(Mixer& mixer) noexcept {
    for (std::size_t i = 0; i < voice_slots; ++i) {
        if (mixer.voices[i] != no_buffer) {
            stop_buffer(mixer, mixer.voices[i]);
            mixer.voices[i] = no_buffer;
            --mixer.voice_count;
        }
    }
}

bool mixer_evict_oldest_voice(Mixer& mixer) noexcept {
    std::size_t oldest = 0;
    while (oldest < voice_slots &&
           (mixer.voices[oldest] == no_buffer || (mixer.voice_looping[oldest] & 1) != 0))
        ++oldest;
    if (oldest == voice_slots)
        return false;
    for (std::size_t i = oldest + 1; i < voice_slots; ++i) {
        if (mixer.voices[i] != no_buffer && (mixer.voice_looping[i] & 1) == 0 &&
            mixer.voice_order[i] < mixer.voice_order[oldest])
            oldest = i;
    }
    stop_buffer(mixer, mixer.voices[oldest]);
    mixer.voices[oldest] = no_buffer;
    --mixer.voice_count;
    return true;
}

void mixer_set_voice_limit(Mixer& mixer, int32_t limit) noexcept {
    mixer.voice_limit = limit;
}

int32_t mixer_voice_limit(const Mixer& mixer) noexcept {
    return mixer.voice_limit;
}

SampleId
mixer_create_sample(Mixer& mixer, WaveCursor& cursor, uint32_t bytes, PcmFormat format) noexcept {
    // A short source is rejected before any device allocation, which an
    // untrusted header would otherwise size.
    if (wave_remaining(cursor) < bytes || mixer.sink.create_buffer == nullptr)
        return no_sample;
    const BufferHandle buffer =
        mixer.sink.create_buffer(mixer.sink.context, BufferKind::sample, format, bytes);
    if (buffer == no_buffer)
        return no_sample;
    const uint8_t* source = cursor.data + cursor.position;
    cursor.position += bytes;
    const bool written = mixer.sink.write_buffer != nullptr &&
                         mixer.sink.write_buffer(mixer.sink.context, buffer, 0, source, bytes);
    const SampleId id = written ? allocate_sample(mixer) : no_sample;
    if (id == no_sample) {
        release_buffer(mixer, buffer);
        return no_sample;
    }
    mixer.samples[id - 1].buffers[0] = buffer;
    return id;
}

void mixer_release_sample(Mixer& mixer, SampleId id) noexcept {
    Sample* sample = mixer_sample(mixer, id);
    if (sample == nullptr)
        return;
    for (std::size_t k = 0; k < sample_buffers; ++k) {
        const BufferHandle buffer = sample->buffers[k];
        if (buffer == no_buffer)
            continue;
        stop_buffer(mixer, buffer);
        release_buffer(mixer, buffer);
        for (std::size_t i = 0; i < voice_slots; ++i) {
            if (mixer.voices[i] == buffer) {
                mixer.voices[i] = no_buffer;
                mixer.voice_looping[i] = 0;
                --mixer.voice_count;
                break;
            }
        }
    }
    *sample = Sample{};
}

bool mixer_play_sample(
    Mixer& mixer, SampleId sample, int32_t volume, const VoicePosition* position
) noexcept {
    return attach_sample(mixer, sample, volume, position, false);
}

bool mixer_play_looping(Mixer& mixer, SampleId sample, int32_t volume) noexcept {
    return attach_sample(mixer, sample, volume, nullptr, true);
}

bool mixer_play_transient(
    Mixer& mixer,
    WaveCursor& cursor,
    uint32_t bytes,
    PcmFormat format,
    int32_t volume,
    const VoicePosition* position
) noexcept {
    mixer_collect_finished(mixer);
    std::size_t slot = 0;
    while (mixer.transients[slot] != no_sample) {
        if (++slot >= transient_slots)
            return false;
    }
    const SampleId id = mixer_create_sample(mixer, cursor, bytes, format);
    mixer.transients[slot] = id;
    if (id == no_sample)
        return false;
    if (!attach_sample(mixer, id, volume, position, false)) {
        mixer_release_sample(mixer, id);
        mixer.transients[slot] = no_sample;
        return false;
    }
    return true;
}

void mixer_start_stream(
    Mixer& mixer, std::vector<uint8_t> file, uint32_t data_offset, PcmFormat format, int32_t volume
) noexcept {
    mixer_stop_stream(mixer);
    if (mixer.stream.timer != no_timer) {
        remove_timer(mixer, mixer.stream.timer);
        mixer.stream.timer = no_timer;
    }
    const int64_t total =
        static_cast<int64_t>(format.bits / 8) * format.sample_rate * format.channels * 2;
    StreamState& stream = mixer.stream;
    stream.half_bytes = static_cast<uint32_t>(total / 2);
    if (total <= 0 || total > max_stream_bytes || mixer.sink.create_buffer == nullptr) {
        stream.buffer = no_buffer;
        return;
    }
    stream.buffer = mixer.sink.create_buffer(
        mixer.sink.context, BufferKind::stream, format, static_cast<uint32_t>(total)
    );
    if (stream.buffer == no_buffer)
        return;
    stream.end_offset = -1;
    stream.bits = format.bits;
    stream.write_offset = 0;
    stream.file = std::move(file);
    stream.cursor = wave_cursor(stream.file.data(), static_cast<uint32_t>(stream.file.size()));
    wave_seek(stream.cursor, data_offset);
    mixer_fill_stream(mixer);
    if (stream.buffer != no_buffer && set_volume(mixer, stream.buffer, volume) &&
        start_buffer(mixer, stream.buffer, true))
        return;
    mixer_stop_stream(mixer);
}

void mixer_stop_stream(Mixer& mixer) noexcept {
    StreamState& stream = mixer.stream;
    if (stream.timer != no_timer) {
        remove_timer(mixer, stream.timer);
        stream.timer = no_timer;
    }
    if (stream.buffer != no_buffer) {
        stop_buffer(mixer, stream.buffer);
        release_buffer(mixer, stream.buffer);
        stream.buffer = no_buffer;
        stream.file.clear();
        stream.cursor = WaveCursor{};
    }
}

bool mixer_stream_busy(const Mixer& mixer) noexcept {
    return mixer.stream.buffer != no_buffer || mixer.stream.timer != no_timer;
}

void mixer_update_stream(Mixer& mixer) noexcept {
    StreamState& stream = mixer.stream;
    uint32_t play = 0;
    if (mixer.sink.play_position != nullptr)
        mixer.sink.play_position(mixer.sink.context, stream.buffer, &play);
    const auto half = static_cast<int32_t>(stream.half_bytes);
    const int32_t end = stream.end_offset;
    if (end >= 0) {
        bool keep = false;
        if (stream.write_offset == 0)
            keep = half <= end || play < stream.half_bytes;
        else
            keep = end < half || stream.half_bytes <= play;
        if (!keep) {
            mixer_stop_stream(mixer);
            return;
        }
    }
    if (static_cast<int32_t>(stream.write_offset) < half) {
        if (play < stream.half_bytes)
            return;
    } else if (stream.half_bytes <= play) {
        return;
    }
    mixer_fill_stream(mixer);
}

void mixer_fill_stream(Mixer& mixer) noexcept {
    StreamState& stream = mixer.stream;
    if (stream.buffer == no_buffer)
        return;
    const uint32_t half = stream.half_bytes;
    // The device region's earlier contents are not preserved; a width other
    // than 8 or 16 bits leaves the unfilled tail zeroed rather than stale.
    std::vector<uint8_t> region(half);
    uint32_t pad_from = 0;
    bool pad = true;
    if (stream.end_offset < 0) {
        const uint32_t remaining = wave_remaining(stream.cursor);
        const uint32_t count = half <= remaining ? half : remaining;
        wave_read(stream.cursor, region.data(), count);
        if (half <= count) {
            pad = false;
        } else {
            stream.end_offset = static_cast<int32_t>(stream.write_offset + count);
            pad_from = count;
        }
    }
    if (pad) {
        if (stream.bits == 8)
            std::memset(region.data() + pad_from, silence_8bit, half - pad_from);
        else if (stream.bits == 16)
            std::memset(region.data() + pad_from, silence_16bit, half - pad_from);
    }
    if (mixer.sink.write_buffer == nullptr ||
        !mixer.sink.write_buffer(
            mixer.sink.context, stream.buffer, stream.write_offset, region.data(), half
        )) {
        mixer_stop_stream(mixer);
        return;
    }
    stream.write_offset = stream.write_offset == 0 ? half : 0;
}

void mixer_enable_spatial(Mixer& mixer) noexcept {
    mixer.spatial_enabled = 1;
}

void mixer_set_distance_range(Mixer& mixer, float min_distance, float max_distance) noexcept {
    mixer.min_distance = min_distance;
    mixer.max_distance = max_distance;
}

bool mixer_no_driver(const Mixer& mixer) noexcept {
    return mixer.no_driver != 0;
}

void mixer_init_volume_devices(Mixer& mixer) noexcept {
    mixer.wave_out_count =
        mixer.sink.wave_out_count != nullptr ? mixer.sink.wave_out_count(mixer.sink.context) : 0;
    mixer.aux_device = no_device;
    const int32_t aux_count =
        mixer.music.aux_count != nullptr ? mixer.music.aux_count(mixer.music.context) : 0;
    for (int32_t device = 0; device < aux_count; ++device) {
        if (mixer.music.aux_is_cd_audio != nullptr &&
            mixer.music.aux_is_cd_audio(mixer.music.context, device)) {
            mixer.aux_device = device;
            break;
        }
    }
    mixer.saved_wave_out_volume = mixer_wave_out_volume(mixer);
    mixer.saved_aux_volume = mixer_music_line_volume(mixer);
}

uint32_t mixer_wave_out_volume(const Mixer& mixer) noexcept {
    for (int32_t device = 0; device < mixer.wave_out_count; ++device) {
        uint32_t packed = 0;
        if (mixer.sink.get_wave_out_volume != nullptr &&
            mixer.sink.get_wave_out_volume(mixer.sink.context, device, &packed))
            return packed & max_device_volume;
    }
    return unknown_volume;
}

uint32_t mixer_music_line_volume(const Mixer& mixer) noexcept {
    uint32_t packed = 0;
    if (mixer.aux_device >= 0 && mixer.music.get_aux_volume != nullptr &&
        mixer.music.get_aux_volume(mixer.music.context, mixer.aux_device, &packed))
        return packed & max_device_volume;
    return unknown_volume;
}

bool mixer_set_wave_out_volume(Mixer& mixer, int32_t level) noexcept {
    const uint32_t packed = pack_stereo(clamp_level(level));
    bool rejected = false;
    for (int32_t device = 0; device < mixer.wave_out_count; ++device) {
        if (mixer.sink.set_wave_out_volume == nullptr ||
            !mixer.sink.set_wave_out_volume(mixer.sink.context, device, packed))
            rejected = true;
    }
    return rejected;
}

bool mixer_set_music_volume(Mixer& mixer, int32_t level, bool transient) noexcept {
    if (mixer.cd.fade_step != 0 && !transient)
        return true;
    const uint32_t clamped = clamp_level(level);
    if (!transient)
        mixer.music_volume = clamped;
    return mixer.music.set_aux_volume != nullptr &&
           mixer.music.set_aux_volume(mixer.music.context, mixer.aux_device, pack_stereo(clamped));
}

void mixer_restore_volumes(Mixer& mixer) noexcept {
    if (static_cast<int32_t>(mixer.saved_wave_out_volume) >= 0)
        mixer_set_wave_out_volume(mixer, static_cast<int32_t>(mixer.saved_wave_out_volume));
    if (static_cast<int32_t>(mixer.saved_aux_volume) >= 0)
        mixer_set_music_volume(mixer, static_cast<int32_t>(mixer.saved_aux_volume), false);
}

uint32_t mixer_load_wave(
    Mixer& mixer, const char* path, WaveLoadMode mode, int32_t volume, const VoicePosition* position
) noexcept {
    std::vector<uint8_t> bytes;
    if (path == nullptr || mixer.files.load == nullptr ||
        !mixer.files.load(mixer.files.context, path, &bytes) || bytes.size() > 0xffffffffU)
        return 0;
    WaveCursor cursor = wave_cursor(bytes.data(), static_cast<uint32_t>(bytes.size()));
    WaveLayout layout{};
    if (!describe_wave(cursor, layout))
        return 0;
    switch (mode) {
    case WaveLoadMode::sample:
        return mixer_create_sample(mixer, cursor, layout.data_bytes, layout.format);
    case WaveLoadMode::play:
        return mixer_play_transient(
                   mixer, cursor, layout.data_bytes, layout.format, volume, position
               )
                   ? 1U
                   : 0U;
    case WaveLoadMode::stream:
        mixer_start_stream(mixer, std::move(bytes), layout.data_offset, layout.format, volume);
        return 1;
    }
    return 0;
}

SampleId mixer_load_sample(Mixer& mixer, const char* path) noexcept {
    return mixer_load_wave(mixer, path, WaveLoadMode::sample, 0, nullptr);
}

bool mixer_play_wave_file(
    Mixer& mixer, const char* path, int32_t volume, const VoicePosition* position
) noexcept {
    return mixer_load_wave(mixer, path, WaveLoadMode::play, volume, position) != 0;
}

void mixer_on_stream_timer(void* user) noexcept {
    auto& mixer = *static_cast<Mixer*>(user);
    remove_timer(mixer, mixer.stream.timer);
    mixer.stream.timer = no_timer;
    char name[sizeof(mixer.stream.pending_name)];
    std::memcpy(name, mixer.stream.pending_name, sizeof(name));
    mixer_load_wave(mixer, name, WaveLoadMode::stream, mixer.stream.pending_volume, nullptr);
}

bool mixer_schedule_stream(
    Mixer& mixer, const char* path, int32_t volume, uint32_t delay
) noexcept {
    auto& pending = mixer.stream.pending_name;
    std::size_t length = 0;
    while (path != nullptr && path[length] != '\0' && length + 1 < sizeof(pending)) {
        pending[length] = path[length];
        ++length;
    }
    pending[length] = '\0';
    mixer.stream.pending_volume = volume;
    mixer.stream.timer =
        mixer.timers.add != nullptr
            ? mixer.timers.add(mixer.timers.context, delay, mixer_on_stream_timer, &mixer)
            : no_timer;
    return true;
}

} // namespace oa::audio
