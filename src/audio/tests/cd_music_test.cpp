// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "audio_test_support.hpp"
#include "oa/audio/cd_music.hpp"

#include <cstddef>
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
    int32_t next_random{};
    std::unique_ptr<Mixer> mixer = audio_test::make_mixer(sink, music, timers, files);

    Rig() {
        mixer->random.context = this;
        mixer->random.next = [](void* c) { return static_cast<Rig*>(c)->next_random; };
    }
};

int callback_calls = 0;

void count_callback(void*) {
    ++callback_calls;
}

void open_and_tracks() {
    Rig rig;
    Mixer& m = *rig.mixer;
    rig.music.opens = false;
    rig.music.foreign = 77;
    require(!cd_open(m), "open fails twice");
    require(rig.music.open_calls == 2 && cd_foreign_player_present(m), "foreign player probed");
    cd_close_foreign_player(m);
    require(rig.music.closed_foreign == 77 && !cd_foreign_player_present(m), "foreign closed");

    rig.music.opens = true;
    rig.music.first = CdTrackKind::other;
    rig.music.tracks = 17;
    require(cd_open(m), "open succeeds");
    require(m.cd.device_open == 1 && m.cd.enabled == 1 && m.cd.play_mode == 1, "open state");
    require(cd_track_count(m) == 16 && m.cd.first_track_offset == 1, "data track excluded");
    require(
        cd_current_track(m) == 0 && cd_disc_id(m) == 0xcafe, "disc identity; open rewinds to 0"
    );
    require(
        cd_track_type(m, 0) == 1 && cd_track_type(m, 1) == 2 && cd_track_type(m, 5) == 2,
        "default kinds cycle 1..4"
    );
    require(cd_open(m), "second open is a no-op");

    require(
        cd_set_disc_change_callback(m, count_callback, nullptr) && callback_calls == 1,
        "callback invoked on registration"
    );
    cd_on_device_change(m, true);
    require(callback_calls == 2, "arrival reruns the callback");
    cd_on_device_change(m, false);
    require(callback_calls == 2, "removal only stops");

    cd_close(m);
    require(m.cd.device_open == 0 && rig.music.close_calls == 1, "closed");
}

void playing() {
    Rig rig;
    Mixer& m = *rig.mixer;
    rig.music.first = CdTrackKind::other;
    rig.music.tracks = 5; // four audio tracks after the data track
    require(cd_open(m), "open");

    require(cd_play_track(m, 2), "play");
    require(rig.music.plays.back() == std::make_pair(3, 4), "offset track, stops at next");
    require(m.cd.current_track == 2 && m.cd.playback == 1, "current");
    require(cd_play_track(m, 2) && rig.music.plays.size() == 1, "already playing is ignored");
    require(
        cd_play_track(m, 4) && rig.music.plays.back() == std::make_pair(5, 0),
        "last track plays to the end"
    );

    // Sequential advance after the track ends.
    rig.music.playing = false;
    cd_on_play_complete(m, true);
    require(
        m.cd.current_track == 1 && rig.music.plays.back() == std::make_pair(6, 0),
        "sequential wraps after the last track"
    );
    require(m.cd.playback == 1, "playback marked active");

    rig.music.playing = false;
    cd_set_play_mode(m, static_cast<int32_t>(CdPlayMode::random));
    rig.next_random = 6;
    cd_advance(m);
    require(m.cd.current_track == 3, "random track is rand % count + 1");

    cd_set_play_mode(m, static_cast<int32_t>(CdPlayMode::selected));
    cd_select_playlist_track(m, 9);
    require(cd_playlist_track(m) == 1, "playlist track above the count ignored");
    cd_select_playlist_track(m, 4);
    cd_advance(m);
    require(m.cd.current_track == 4, "selected track");

    require(cd_set_paused(m, true) && m.cd.playback == 2 && rig.music.pauses == 1, "paused");
    cd_advance(m);
    require(m.cd.current_track == 4, "paused player does not advance");
    rig.music.device_track = 2;
    require(
        cd_set_paused(m, false) && rig.music.resumes.back() == 3 && m.cd.playback == 1,
        "resume to the next track start"
    );

    require(cd_select_track(m, 7) == 3, "select wraps modulo count while playing");

    cd_set_play_mode(m, static_cast<int32_t>(CdPlayMode::off));
    rig.music.playing = true;
    cd_advance(m);
    require(m.cd.playback == 0 && !rig.music.playing, "off mode stops playback");

    cd_set_enabled(m, 0);
    require(cd_play_track(m, 2) && rig.music.plays.back().first != 3, "disabled ignores play");
}

void by_kind_and_fades() {
    Rig rig;
    Mixer& m = *rig.mixer;
    rig.music.tracks = 6;
    require(cd_open(m), "open");
    const uint8_t kinds[6] = {0, 1, 0, 1, 0, 1};
    cd_copy_track_types(m, kinds);
    require(cd_track_type(m, 2) == 1 && cd_track_type(m, 6) == 1, "kinds copied from track 1");

    cd_set_play_mode(m, static_cast<int32_t>(CdPlayMode::by_kind));
    rig.next_random = 2; // second matching track after the current one
    cd_advance(m);
    require(m.cd.current_track == 3, "second calm track after track 1");

    // Switching kind starts a fade; its steps lower the line volume.
    m.music_volume = 36;
    cd_set_music_kind(m, 1);
    require(m.cd.fade_step == -2 && m.cd.fade_timer >= 0, "fade scheduled");
    require(
        rig.timers.timers[static_cast<std::size_t>(m.cd.fade_timer)].interval == 2, "fade interval"
    );
    require(m.cd.kind_positions[0] == 3, "track remembered for the old kind");
    rig.timers.fire(m.cd.fade_timer);
    require(m.cd.fade_level == 34 && rig.music.aux_sets.back() == 0x00220022U, "fade step");
    m.cd.fade_level = 1;
    rig.next_random = 0;
    rig.music.playing = false;
    rig.timers.fire(m.cd.fade_timer);
    require(m.cd.fade_timer == no_timer && m.cd.fade_step == 0, "fade finished");
    require(cd_track_type(m, m.cd.current_track) == 1, "battle track after the fade");

    // A fade back to calm pauses before advancing.
    rig.music.playing = false;
    cd_set_music_kind(m, 0);
    m.cd.fade_level = 1;
    rig.timers.fire(m.cd.fade_timer);
    require(m.cd.fade_end_timer >= 0, "calm pause scheduled");
    require(
        rig.timers.timers[static_cast<std::size_t>(m.cd.fade_end_timer)].interval == 0x78,
        "calm pause interval"
    );
    rig.timers.fire(m.cd.fade_end_timer);
    require(
        m.cd.fade_end_timer == no_timer && cd_track_type(m, m.cd.current_track) == 0,
        "calm track after the pause"
    );

    // No track of the wanted kind: stop, then still marked playing.
    cd_set_music_kind(m, 9);
    require(m.cd.music_kind == 9, "kind stored");
    rig.music.playing = false;
    const auto stops = rig.music.stop_calls;
    cd_advance(m);
    require(rig.music.stop_calls == stops + 1 && m.cd.playback == 1, "unmatched kind stops");

    cd_set_music_kind(m, music_kind_stop);
    cd_advance(m);
    require(m.cd.playback == 0, "stop kind stops music");
}

/// Checks that the CDLISTS cache clears when the setting is missing, keeps discs
/// most recent first and persists their track kinds.
void disc_cache() {
    Rig rig;
    Mixer& m = *rig.mixer;
    rig.music.first = CdTrackKind::other;
    rig.music.tracks = 17;
    require(cd_open(m), "open");

    std::vector<uint8_t> persisted;
    AudioSettings settings{};
    settings.context = &persisted;
    settings.read_blob = [](void* c, const char*, uint8_t* bytes, uint32_t* size) {
        auto* stored = static_cast<std::vector<uint8_t>*>(c);
        if (stored->size() != *size)
            return false;
        std::memcpy(bytes, stored->data(), stored->size());
        return true;
    };
    settings.write_blob = [](void* c, const char* name, const uint8_t* bytes, uint32_t size) {
        require(std::strcmp(name, "CDLISTS") == 0, "setting name");
        static_cast<std::vector<uint8_t>*>(c)->assign(bytes, bytes + size);
    };

    auto cache = std::make_unique<DiscCache>();
    std::memset(static_cast<void*>(cache.get()), 0xaa, sizeof(DiscCache));
    cd_load_disc_cache(*cache, settings);
    require(cache->records[3].track_types[0] == 0, "missing setting clears the cache");

    oa::Game game{};
    game.music_flags = music_flag_enabled;
    game.cd_mode = static_cast<uint8_t>(CdPlayMode::by_kind);
    cd_handle_disc_change(m, *cache, game);
    require(
        cd_track_type(m, 1) == 1 && cd_track_type(m, 7) == 1 && cd_track_type(m, 8) == 0,
        "unknown 16-track data disc gets the default kinds"
    );
    require(
        cache->records[0].disc_id[0] == 0xfe && cache->records[0].disc_id[1] == 0xca,
        "disc recorded in the front record"
    );
    require(
        m.cd.play_mode == 4 && m.cd.enabled == 1 && m.cd.playback == 0,
        "options applied; stopped outside a match"
    );

    cd_set_track_type(m, 8, 3);
    cd_save_disc_cache(*cache, m, settings);
    require(
        persisted.size() == 0xaa0 && persisted[offsetof(DiscCacheRecord, track_types) + 7] == 3,
        "track kinds persisted"
    );

    // A second disc pushes the first down; reinserting moves it back.
    rig.music.disc = 0xbeef;
    cd_refresh_tracks(m);
    cd_handle_disc_change(m, *cache, game);
    require(
        cache->records[1].disc_id[0] == 0xfe && cache->records[0].disc_id[0] == 0xef,
        "most recent first"
    );
    rig.music.disc = 0xcafe;
    cd_refresh_tracks(m);
    game.session_flags = 4;
    game.mode = 6;
    rig.music.playing = false;
    cd_handle_disc_change(m, *cache, game);
    require(
        cache->records[0].disc_id[0] == 0xfe && cache->records[1].disc_id[0] == 0xef,
        "known disc moved to the front"
    );
    require(cd_track_type(m, 8) == 3, "known disc restores its kinds");
    require(m.cd.playback == 1, "music resumes during a match");
}

} // namespace

int main() {
    open_and_tracks();
    playing();
    by_kind_and_fades();
    disc_cache();
    return 0;
}
