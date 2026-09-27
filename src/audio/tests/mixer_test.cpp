// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "audio_test_support.hpp"
#include "oa/audio/mixer.hpp"
#include "oa/audio/spatial_gain.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

using namespace oa::audio;
using audio_test::require;

namespace {

struct Rig {
    audio_test::FakeSink sink;
    audio_test::FakeMusic music;
    audio_test::FakeTimers timers;
    audio_test::FakeFiles files;
    std::unique_ptr<Mixer> mixer = audio_test::make_mixer(sink, music, timers, files);
};

SampleId make_sample(Rig& rig, uint32_t bytes = 4) {
    std::vector<uint8_t> pcm(bytes, 0x42);
    auto cursor = wave_cursor(pcm.data(), static_cast<uint32_t>(pcm.size()));
    return mixer_create_sample(*rig.mixer, cursor, bytes, PcmFormat{11025, 8, 1});
}

void init_and_device() {
    Rig rig;
    Mixer& m = *rig.mixer;
    require(m.voice_limit == 8 && m.voice_serial == 1 && m.voice_count == 0, "voice defaults");
    require(m.min_distance == 1.0F && m.max_distance == 1.0e20F, "distance defaults");
    require(m.wave_out_count == 2 && m.aux_device == 1, "first cd-audio aux line chosen");
    require(m.saved_wave_out_volume == 0x1234, "low word of the first wave output volume");
    require(m.saved_aux_volume == 0x1000 && m.music_volume == 0x1000, "aux volume low word");
    require(m.stream.timer == no_timer, "no pending stream");

    require(mixer_open_device(m, PcmFormat{11025, 16, 2}), "device opens");
    require(m.primary.sample_rate == 11025 && m.primary.channels == 2, "primary format stored");

    Rig missing;
    missing.sink.open_result = DeviceResult::no_driver;
    require(!mixer_open_device(*missing.mixer, PcmFormat{11025, 16, 2}), "no driver fails");
    require(mixer_no_driver(*missing.mixer), "no driver recorded");

    Rig bad_format;
    bad_format.sink.format_result = DeviceResult::failed;
    require(!mixer_open_device(*bad_format.mixer, PcmFormat{11025, 16, 2}), "format fails");
    require(
        !mixer_no_driver(*bad_format.mixer) && bad_format.sink.closes == 1,
        "other failure tears down without no_driver"
    );
}

void voices() {
    Rig rig;
    Mixer& m = *rig.mixer;
    const SampleId a = make_sample(rig);
    require(a != no_sample, "sample created");
    require(rig.sink.at(m.samples[a - 1].buffers[0]).bytes[0] == 0x42, "sample bytes written");

    std::vector<uint8_t> short_pcm(2);
    auto short_cursor = wave_cursor(short_pcm.data(), 2);
    require(
        mixer_create_sample(m, short_cursor, 4, PcmFormat{11025, 8, 1}) == no_sample,
        "short source rejected"
    );

    // The first play reuses the sample's own idle buffer.
    VoicePosition where{1, 2, 3};
    require(mixer_play_sample(m, a, volume_near, &where), "play");
    const BufferHandle first = m.samples[a - 1].buffers[0];
    require(m.voices[0] == first && m.voice_order[0] == 2 && m.voice_count == 1, "voice 0");
    require(rig.sink.at(first).volume == volume_near, "volume applied");
    require(rig.sink.at(first).spatial.mode == SpatialMode::disabled, "3D off by default");

    mixer_enable_spatial(m);
    mixer_set_distance_range(m, 5.0F, 50.0F);
    // Busy buffer: duplicates are made for the next three plays.
    for (int i = 0; i < 3; ++i)
        require(mixer_play_sample(m, a, volume_far, &where), "duplicate play");
    require(m.samples[a - 1].buffers[3] != no_buffer, "four buffers exist");
    const auto& dup = rig.sink.at(m.samples[a - 1].buffers[1]);
    require(
        dup.spatial.mode == SpatialMode::normal && dup.spatial.x == 1.0F &&
            dup.spatial.min_distance == 5.0F && dup.spatial.max_distance == 50.0F,
        "3D placement"
    );

    // With four busy buffers the furthest-played one restarts.
    rig.sink.at(m.samples[a - 1].buffers[2]).position = 99;
    rig.sink.at(m.samples[a - 1].buffers[1]).position = 50;
    require(mixer_play_sample(m, a, volume_near, nullptr), "restart play");
    require(rig.sink.at(m.samples[a - 1].buffers[2]).position == 0, "furthest restarted");
    require(m.voice_count == 5, "voice count");

    // Reaching the limit evicts the oldest non-looping voice.
    mixer_set_voice_limit(m, 5);
    const SampleId b = make_sample(rig);
    require(mixer_play_sample(m, b, volume_near, nullptr), "evicting play");
    require(!rig.sink.at(first).playing && m.voice_count == 5, "oldest voice stopped");
    require(m.voices[0] == m.samples[b - 1].buffers[0], "freed slot reused");

    mixer_release_sample(m, b);
    require(m.voices[0] == no_buffer && m.voice_count == 4, "release forgets voices");
    require(mixer_sample(m, b) == nullptr, "sample freed");
}

void looping_and_collect() {
    Rig rig;
    Mixer& m = *rig.mixer;
    const SampleId a = make_sample(rig);
    const SampleId b = make_sample(rig);
    require(mixer_play_looping(m, a, 0), "looping voice");
    require(
        rig.sink.at(m.samples[a - 1].buffers[0]).looping && m.voice_looping[0] == 1,
        "looping marked"
    );
    require(!mixer_play_looping(m, b, 0), "second looping voice refused");

    mixer_set_voice_limit(m, 1);
    require(!mixer_play_sample(m, b, 0, nullptr), "only looping voices cannot be evicted");
    mixer_set_voice_limit(m, 8);

    require(mixer_play_sample(m, b, 0, nullptr), "plain voice");
    rig.sink.at(m.samples[b - 1].buffers[0]).playing = false;
    mixer_collect_finished(m);
    require(m.voices[1] == no_buffer && m.voice_count == 1, "finished voice collected");

    mixer_stop_voices(m);
    require(m.voice_count == 0 && m.voice_looping[0] == 1, "stop keeps looping marks");
}

void transients_and_files() {
    Rig rig;
    Mixer& m = *rig.mixer;
    rig.files.files["sounds/beep.wav"] = audio_test::make_riff(22050, 8, 1, {1, 2, 3, 4});
    VoicePosition where{};
    require(mixer_play_wave_file(m, "sounds/beep.wav", volume_near, &where), "transient play");
    require(m.transients[0] != no_sample && m.voice_count == 1, "transient slot and voice");
    const auto& buffer = rig.sink.at(m.samples[m.transients[0] - 1].buffers[0]);
    require(buffer.format.sample_rate == 22050 && buffer.bytes.size() == 4, "riff data loaded");

    const SampleId loaded = mixer_load_sample(m, "sounds/beep.wav");
    require(loaded != no_sample && m.transients[1] == no_sample, "sample mode");
    require(!mixer_play_wave_file(m, "sounds/missing.wav", 0, nullptr), "missing file");

    const SampleId transient = m.transients[0];
    rig.sink.at(m.samples[transient - 1].buffers[0]).playing = false;
    mixer_collect_finished(m);
    require(
        m.transients[0] == no_sample && mixer_sample(m, transient) == nullptr,
        "finished transient freed"
    );
}

void stream() {
    Rig rig;
    Mixer& m = *rig.mixer;
    // 8-bit mono at 4 Hz: an 8-byte buffer in two 4-byte halves.
    rig.files.files["talk.wav"] = audio_test::make_riff(4, 8, 1, {1, 2, 3, 4, 5, 6});
    require(mixer_schedule_stream(m, "talk.wav", -100, 45), "scheduled");
    require(mixer_stream_busy(m) && rig.timers.timers[0].interval == 45, "pending start");
    rig.timers.fire(m.stream.timer);
    require(m.stream.timer == no_timer && m.stream.buffer != no_buffer, "stream started");
    auto& buffer = rig.sink.at(m.stream.buffer);
    require(buffer.looping && buffer.volume == -100 && m.stream.half_bytes == 4, "stream buffer");
    require(
        std::memcmp(buffer.bytes.data(), "\1\2\3\4", 4) == 0 && m.stream.write_offset == 4,
        "first half filled"
    );

    // Playing the first half: the second half is refilled with the tail.
    buffer.position = 1;
    mixer_update_stream(m);
    const uint8_t second[4] = {5, 6, 0x80, 0x80};
    require(std::memcmp(buffer.bytes.data() + 4, second, 4) == 0, "tail padded with silence");
    require(m.stream.end_offset == 6 && m.stream.write_offset == 0, "end marker recorded");
    mixer_update_stream(m);
    require(m.stream.write_offset == 0, "no refill while the written half is ahead");

    buffer.position = 5;
    mixer_update_stream(m);
    const uint8_t silence[4] = {0x80, 0x80, 0x80, 0x80};
    require(
        std::memcmp(buffer.bytes.data(), silence, 4) == 0 && m.stream.write_offset == 4,
        "silence half written after the end"
    );
    mixer_update_stream(m);
    require(m.stream.buffer != no_buffer, "still playing the tail");

    buffer.position = 1;
    mixer_update_stream(m);
    require(m.stream.buffer == no_buffer && !mixer_stream_busy(m), "stream ends after its data");
}

void volumes() {
    Rig rig;
    Mixer& m = *rig.mixer;
    require(!mixer_set_wave_out_volume(m, 0x12345), "all devices accept");
    require(rig.sink.wave_out_volumes[0] == 0xffffffffU, "clamped and duplicated per channel");
    rig.sink.wave_out_accepts[1] = false;
    require(mixer_set_wave_out_volume(m, -5), "rejection reported");
    require(rig.sink.wave_out_volumes[1] == 0, "negative clamps to zero");

    require(mixer_set_music_volume(m, 0x200, false) && m.music_volume == 0x200, "music stored");
    m.cd.fade_step = -3;
    require(
        mixer_set_music_volume(m, 0x300, false) && m.music_volume == 0x200, "ignored during fade"
    );
    require(
        mixer_set_music_volume(m, 0x100, true) && m.music_volume == 0x200 &&
            rig.music.aux_sets.back() == 0x01000100U,
        "transient level applied, not stored"
    );
    m.cd.fade_step = 0;
    mixer_restore_volumes(m);
    require(
        rig.sink.wave_out_volumes[0] == 0x12341234U && m.music_volume == 0x1000,
        "saved volumes restored"
    );
}

} // namespace

/// Checks that a voice is placed only while 3D sound is on (spatial_enabled
/// nonzero) and the caller gives a position, and that the spatial gain keeps
/// full level within the minimum distance, falls off as min/d, holds from the
/// maximum distance and fades the far channel with the bearing's sine.
void placement() {
    const VoicePosition where{300, 0, 400};
    require(voice_spatial(0, 464.0F, 4096.0F, &where).mode == SpatialMode::disabled, "3D off");
    require(
        voice_spatial(1, 464.0F, 4096.0F, nullptr).mode == SpatialMode::disabled, "no position"
    );
    const Spatial placed = voice_spatial(1, 464.0F, 4096.0F, &where);
    require(
        placed.mode == SpatialMode::normal && placed.x == 300.0F && placed.y == 0.0F &&
            placed.z == 400.0F && placed.min_distance == 464.0F && placed.max_distance == 4096.0F,
        "placed at the position within the range"
    );

    const StereoGain off = spatial_stereo_gain(Spatial{});
    require(off.left == 1.0F && off.right == 1.0F, "unplaced voice at full level");
    // 500 away, 3/5 to the right, inside the minimum distance.
    const StereoGain near = spatial_stereo_gain(voice_spatial(1, 600.0F, 4096.0F, &where));
    require(near.right == 1.0F && std::fabs(near.left - 0.4F) < 1e-6F, "near voice panned right");
    // Twice the minimum distance straight ahead: half level on both sides.
    const VoicePosition ahead{0, 0, 1000};
    const StereoGain far = spatial_stereo_gain(voice_spatial(1, 500.0F, 4096.0F, &ahead));
    require(far.left == 0.5F && far.right == 0.5F, "rolloff min/d");
    // Beyond the maximum distance the level holds at min/max.
    const VoicePosition behind{-8000, 0, 0};
    const StereoGain held = spatial_stereo_gain(voice_spatial(1, 500.0F, 2000.0F, &behind));
    require(held.right == 0.0F && held.left == 0.25F, "held at the maximum distance");
}

int main() {
    init_and_device();
    placement();
    voices();
    looping_and_collect();
    transients_and_files();
    stream();
    volumes();
    return 0;
}
