// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/audio/mixer.hpp"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace audio_test {

inline void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

// Recording double for the sound device.
struct FakeSink {
    struct Buffer {
        oa::audio::BufferKind kind{};
        oa::audio::PcmFormat format{};
        std::vector<uint8_t> bytes;
        bool playing{};
        bool looping{};
        uint32_t position{};
        int32_t volume{};
        oa::audio::Spatial spatial{};
        bool released{};
    };

    oa::audio::DeviceResult open_result{oa::audio::DeviceResult::ok};
    oa::audio::DeviceResult format_result{oa::audio::DeviceResult::ok};
    bool open{};
    int closes{};
    std::map<oa::audio::BufferHandle, Buffer> buffers;
    oa::audio::BufferHandle next{1};
    std::vector<uint32_t> wave_out_volumes{0x1234, 0x5678};
    std::vector<bool> wave_out_accepts{true, true};
    std::vector<std::string> log;

    Buffer& at(oa::audio::BufferHandle handle) { return buffers.at(handle); }

    oa::audio::AudioSink sink() {
        oa::audio::AudioSink s{};
        s.context = this;
        s.open_device = [](void* c) {
            auto* self = static_cast<FakeSink*>(c);
            self->open = self->open_result == oa::audio::DeviceResult::ok;
            return self->open_result;
        };
        s.set_primary_format = [](void* c, oa::audio::PcmFormat) {
            return static_cast<FakeSink*>(c)->format_result;
        };
        s.close_device = [](void* c) {
            auto* self = static_cast<FakeSink*>(c);
            self->open = false;
            ++self->closes;
        };
        s.create_buffer = [](void* c,
                             oa::audio::BufferKind kind,
                             oa::audio::PcmFormat format,
                             uint32_t bytes) -> oa::audio::BufferHandle {
            auto* self = static_cast<FakeSink*>(c);
            const auto handle = self->next++;
            Buffer b{};
            b.kind = kind;
            b.format = format;
            b.bytes.assign(bytes, 0xee);
            self->buffers[handle] = b;
            return handle;
        };
        s.duplicate_buffer = [](void* c,
                                oa::audio::BufferHandle source) -> oa::audio::BufferHandle {
            auto* self = static_cast<FakeSink*>(c);
            const auto handle = self->next++;
            Buffer b = self->buffers.at(source);
            b.playing = false;
            b.position = 0;
            self->buffers[handle] = b;
            self->log.push_back("duplicate");
            return handle;
        };
        s.release_buffer = [](void* c, oa::audio::BufferHandle h) {
            static_cast<FakeSink*>(c)->buffers.at(h).released = true;
        };
        s.write_buffer = [](void* c,
                            oa::audio::BufferHandle h,
                            uint32_t offset,
                            const uint8_t* bytes,
                            uint32_t count) {
            auto& b = static_cast<FakeSink*>(c)->buffers.at(h);
            if (offset + count > b.bytes.size())
                return false;
            std::copy(bytes, bytes + count, b.bytes.begin() + offset);
            return true;
        };
        s.query_playing = [](void* c, oa::audio::BufferHandle h, bool* playing) {
            *playing = static_cast<FakeSink*>(c)->buffers.at(h).playing;
            return true;
        };
        s.play_position = [](void* c, oa::audio::BufferHandle h, uint32_t* position) {
            *position = static_cast<FakeSink*>(c)->buffers.at(h).position;
            return true;
        };
        s.set_play_position = [](void* c, oa::audio::BufferHandle h, uint32_t position) {
            static_cast<FakeSink*>(c)->buffers.at(h).position = position;
            return true;
        };
        s.set_volume = [](void* c, oa::audio::BufferHandle h, int32_t volume) {
            static_cast<FakeSink*>(c)->buffers.at(h).volume = volume;
            return true;
        };
        s.set_spatial = [](void* c, oa::audio::BufferHandle h, const oa::audio::Spatial* spatial) {
            static_cast<FakeSink*>(c)->buffers.at(h).spatial = *spatial;
            return true;
        };
        s.play = [](void* c, oa::audio::BufferHandle h, bool looping) {
            auto& b = static_cast<FakeSink*>(c)->buffers.at(h);
            b.playing = true;
            b.looping = looping;
            return true;
        };
        s.stop = [](void* c, oa::audio::BufferHandle h) {
            static_cast<FakeSink*>(c)->buffers.at(h).playing = false;
        };
        s.wave_out_count = [](void* c) {
            return static_cast<int32_t>(static_cast<FakeSink*>(c)->wave_out_volumes.size());
        };
        s.get_wave_out_volume = [](void* c, int32_t d, uint32_t* packed) {
            *packed = static_cast<FakeSink*>(c)->wave_out_volumes.at(d) | 0xabcd0000U;
            return true;
        };
        s.set_wave_out_volume = [](void* c, int32_t d, uint32_t packed) {
            auto* self = static_cast<FakeSink*>(c);
            self->wave_out_volumes.at(d) = packed;
            return static_cast<bool>(self->wave_out_accepts.at(d));
        };
        return s;
    }
};

// Recording double for the CD device and its mixer line.
struct FakeMusic {
    bool opens{true};
    int open_calls{};
    int close_calls{};
    int stop_calls{};
    int32_t tracks{10};
    oa::audio::CdTrackKind first{oa::audio::CdTrackKind::audio};
    bool playing{};
    int32_t device_track{1};
    uint32_t disc{0xcafe};
    uint32_t foreign{};
    uint32_t closed_foreign{};
    std::vector<std::pair<int32_t, int32_t>> plays;
    std::vector<int32_t> resumes;
    int pauses{};
    std::vector<bool> aux_cd{false, true};
    uint32_t aux_volume{0x00ff1000};
    std::vector<uint32_t> aux_sets;

    oa::audio::MusicDevice device() {
        oa::audio::MusicDevice m{};
        m.context = this;
        m.open = [](void* c) {
            auto* self = static_cast<FakeMusic*>(c);
            ++self->open_calls;
            return self->opens;
        };
        m.close = [](void* c) { ++static_cast<FakeMusic*>(c)->close_calls; };
        m.stop = [](void* c) {
            auto* self = static_cast<FakeMusic*>(c);
            ++self->stop_calls;
            self->playing = false;
            return true;
        };
        m.track_count = [](void* c, int32_t* count) {
            *count = static_cast<FakeMusic*>(c)->tracks;
            return true;
        };
        m.first_track_kind = [](void* c) { return static_cast<FakeMusic*>(c)->first; };
        m.is_playing = [](void* c) { return static_cast<FakeMusic*>(c)->playing; };
        m.current_track = [](void* c, int32_t* track) {
            *track = static_cast<FakeMusic*>(c)->device_track;
            return true;
        };
        m.play = [](void* c, int32_t from, int32_t to) {
            auto* self = static_cast<FakeMusic*>(c);
            self->plays.emplace_back(from, to);
            self->playing = true;
            return true;
        };
        m.resume = [](void* c, int32_t to) {
            static_cast<FakeMusic*>(c)->resumes.push_back(to);
            return true;
        };
        m.pause = [](void* c) {
            ++static_cast<FakeMusic*>(c)->pauses;
            return true;
        };
        m.disc_id = [](void* c, uint32_t* id) {
            *id = static_cast<FakeMusic*>(c)->disc;
            return true;
        };
        m.find_foreign_player = [](void* c) { return static_cast<FakeMusic*>(c)->foreign; };
        m.close_foreign_player = [](void* c, uint32_t h) {
            static_cast<FakeMusic*>(c)->closed_foreign = h;
        };
        m.aux_count = [](void* c) {
            return static_cast<int32_t>(static_cast<FakeMusic*>(c)->aux_cd.size());
        };
        m.aux_is_cd_audio = [](void* c, int32_t d) {
            return static_cast<bool>(static_cast<FakeMusic*>(c)->aux_cd.at(d));
        };
        m.get_aux_volume = [](void* c, int32_t, uint32_t* packed) {
            *packed = static_cast<FakeMusic*>(c)->aux_volume;
            return true;
        };
        m.set_aux_volume = [](void* c, int32_t, uint32_t packed) {
            static_cast<FakeMusic*>(c)->aux_sets.push_back(packed);
            return true;
        };
        return m;
    }
};

struct FakeTimers {
    struct Timer {
        uint32_t interval{};
        oa::audio::TimerCallback callback{};
        void* user{};
        bool active{};
    };

    std::vector<Timer> timers;
    std::vector<int32_t> removed;

    oa::audio::AudioTimers table() {
        oa::audio::AudioTimers t{};
        t.context = this;
        t.add = [](void* c, uint32_t interval, oa::audio::TimerCallback cb, void* user) {
            auto* self = static_cast<FakeTimers*>(c);
            self->timers.push_back({interval, cb, user, true});
            return static_cast<int32_t>(self->timers.size() - 1);
        };
        t.remove = [](void* c, int32_t handle) {
            auto* self = static_cast<FakeTimers*>(c);
            self->removed.push_back(handle);
            if (handle >= 0 && handle < static_cast<int32_t>(self->timers.size()))
                self->timers[static_cast<std::size_t>(handle)].active = false;
        };
        return t;
    }

    void fire(int32_t handle) {
        const auto& t = timers.at(static_cast<std::size_t>(handle));
        require(t.active, "fired timer is active");
        t.callback(t.user);
    }
};

struct FakeFiles {
    std::map<std::string, std::vector<uint8_t>> files;

    oa::audio::AudioFiles table() {
        oa::audio::AudioFiles f{};
        f.context = this;
        f.load = [](void* c, const char* path, std::vector<uint8_t>* bytes) {
            auto* self = static_cast<FakeFiles*>(c);
            const auto found = self->files.find(path);
            if (found == self->files.end())
                return false;
            *bytes = found->second;
            return true;
        };
        return f;
    }
};

inline void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

inline void put16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

inline void put_tag(std::vector<uint8_t>& out, const char* tag) {
    for (int index = 0; index < 4; ++index)
        out.push_back(static_cast<uint8_t>(tag[index]));
}

// A canonical RIFF/WAVE file, optionally with an extra chunk before "data".
inline std::vector<uint8_t> make_riff(
    uint32_t rate,
    uint16_t bits,
    uint16_t channels,
    const std::vector<uint8_t>& samples,
    const std::vector<uint8_t>& extra = {}
) {
    std::vector<uint8_t> out;
    put_tag(out, "RIFF");
    put32(out, 0);
    put_tag(out, "WAVE");
    if (!extra.empty()) {
        put_tag(out, "LIST");
        put32(out, static_cast<uint32_t>(extra.size()));
        out.insert(out.end(), extra.begin(), extra.end());
    }
    put_tag(out, "fmt ");
    put32(out, 16);
    put16(out, 1);
    put16(out, channels);
    put32(out, rate);
    put32(out, rate * channels * (bits / 8U));
    put16(out, static_cast<uint16_t>(channels * (bits / 8U)));
    put16(out, bits);
    put_tag(out, "data");
    put32(out, static_cast<uint32_t>(samples.size()));
    out.insert(out.end(), samples.begin(), samples.end());
    const auto riff_size = static_cast<uint32_t>(out.size() - 8);
    for (int i = 0; i < 4; ++i)
        out[4 + static_cast<std::size_t>(i)] = static_cast<uint8_t>(riff_size >> (8 * i));
    return out;
}

inline std::unique_ptr<oa::audio::Mixer>
make_mixer(FakeSink& sink, FakeMusic& music, FakeTimers& timers, FakeFiles& files) {
    auto mixer = std::make_unique<oa::audio::Mixer>();
    mixer->sink = sink.sink();
    mixer->music = music.device();
    mixer->timers = timers.table();
    mixer->files = files.table();
    oa::audio::mixer_init(*mixer);
    return mixer;
}

} // namespace audio_test
