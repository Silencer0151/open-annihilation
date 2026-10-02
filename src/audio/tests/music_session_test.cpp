// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "audio_test_support.hpp"
#include "oa/audio/music_disc.hpp"
#include "oa/audio/music_session.hpp"
#include "oa/test/scratch_directory.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>

using namespace oa::audio;
using audio_test::require;

namespace {

using Play = std::pair<int32_t, int32_t>;

// Fires due engine timers until none remain; returns how many fired.
int run_timers(audio_test::FakeTimers& timers) {
    int fired = 0;
    for (bool any = true; any && fired < 1000;) {
        any = false;
        for (std::size_t i = 0; i < timers.timers.size(); ++i) {
            if (timers.timers[i].active) {
                timers.fire(static_cast<int32_t>(i));
                ++fired;
                any = true;
                break;
            }
        }
    }
    return fired;
}

struct Rig {
    audio_test::FakeSink sink;
    audio_test::FakeMusic music;
    audio_test::FakeTimers timers;
    audio_test::FakeFiles files;
    std::unique_ptr<Mixer> mixer;
    std::unique_ptr<oa::Game> game = std::make_unique<oa::Game>();
    MusicSession session;
    uint32_t tick = 1000;

    explicit Rig(uint8_t cd_mode) {
        // The game disc: a data track and sixteen audio tracks.
        music.tracks = 17;
        music.first = CdTrackKind::other;
        mixer = audio_test::make_mixer(sink, music, timers, files);
        game->music_flags = music_flag_enabled;
        game->cd_mode = cd_mode;
        game->music_volume = 32;
        game->local_player_index = 0;
        game->players[0].unit_count = 40;
        session.mixer = mixer.get();
        session.game = game.get();
    }

    Play last() const { return music.plays.back(); }

    void finish_track() {
        music.playing = false;
        cd_on_play_complete(*mixer, true);
    }

    // One mood update with `hits` local hits recorded beforehand.
    void second(int hits) {
        for (int i = 0; i < hits; ++i)
            music_note_hit(session, 0, 3);
        tick += 31;
        require(music_session_tick(session, tick), "mood update runs every 31 ticks");
    }
};

void by_kind_follows_combat() {
    Rig rig(static_cast<uint8_t>(CdPlayMode::by_kind));
    require(music_session_start(rig.session), "start opens the player");
    require(rig.music.plays.empty(), "no music before a game");
    require(cd_track_count(*rig.mixer) == 16, "data track excluded");
    for (int32_t track = 1; track <= 16; ++track)
        require(
            cd_track_type(*rig.mixer, track) == (track <= 7 ? 1 : 0),
            "unknown game disc: tracks 1-7 battle, 8-16 calm"
        );
    require(rig.music.aux_sets.back() == 0x80008000U, "musicvol 32 is half scale");

    music_session_main_menu(rig.session);
    require(
        cd_music_kind(*rig.mixer) == music_kind_stop && rig.music.plays.empty(), "menus stay silent"
    );

    music_session_begin_match(rig.session);
    require(rig.last() == Play(9, 10), "first calm track (disc track 9) plays alone");
    rig.finish_track();
    require(rig.last() == Play(10, 11), "next calm track follows on completion");

    // Quiet updates never leave calm music.
    for (int i = 0; i < 20; ++i)
        rig.second(0);
    require(cd_music_kind(*rig.mixer) == music_kind_calm, "peace stays calm");

    // Sustained local combat: battle after the warmup, faded out first.
    const std::size_t before = rig.music.plays.size();
    // A slot joins the five-slot recent window only once the ring moves on.
    rig.second(40);
    require(cd_music_kind(*rig.mixer) == music_kind_calm, "current slot is not yet recent");
    rig.second(0);
    require(cd_music_kind(*rig.mixer) == music_kind_battle, "combat selects battle music");
    require(rig.music.plays.size() == before, "old track fades before switching");
    const std::size_t volume_steps = rig.music.aux_sets.size();
    require(run_timers(rig.timers) == 19, "fade takes 19 steps of 2 ticks");
    require(rig.music.aux_sets.size() > volume_steps + 17, "fade lowers the line volume");
    require(rig.last() == Play(2, 3), "battle track (disc track 2) starts at silence");
    rig.finish_track();
    require(rig.last() == Play(3, 4), "battle tracks continue while fighting");

    // Activity decays: calm only after 61 quiet-enough updates.
    for (int i = 0; i < 60; ++i)
        rig.second(0);
    require(cd_music_kind(*rig.mixer) == music_kind_battle, "battle holds for 60 updates");
    rig.second(0);
    require(cd_music_kind(*rig.mixer) == music_kind_calm, "calm after 61 quiet updates");
    // Fade to silence, then a 120-tick pause, then the next calm track.
    require(run_timers(rig.timers) == 20, "fade steps plus the calm pause");
    require(rig.last() == Play(9, 10), "calm track after the pause");

    music_session_end_match(rig.session);
    require(
        !rig.music.playing && cd_music_kind(*rig.mixer) == music_kind_stop,
        "leaving the game stops the music"
    );
}

void consecutive_matches_are_independent() {
    Rig rig(static_cast<uint8_t>(CdPlayMode::by_kind));
    music_session_start(rig.session);
    music_session_begin_match(rig.session);
    for (int i = 0; i < 12; ++i)
        rig.second(40);
    require(cd_music_kind(*rig.mixer) == music_kind_battle, "first match ends in battle");
    music_session_end_match(rig.session);
    require(
        rig.session.mood.applied_kind == -1 && rig.session.mood.last_update == 0,
        "leaving a match resets the mood"
    );

    // The second match's clock restarts below the first match's last update.
    rig.tick = 0;
    music_session_begin_match(rig.session);
    require(
        cd_music_kind(*rig.mixer) == music_kind_calm && rig.last() == Play(9, 10),
        "second match starts calm"
    );
    for (int i = 0; i < 12; ++i)
        rig.second(40);
    require(cd_music_kind(*rig.mixer) == music_kind_battle, "second match reaches battle");
}

void small_forces_stay_calm() {
    Rig rig(static_cast<uint8_t>(CdPlayMode::by_kind));
    rig.game->players[0].unit_count = 30;
    music_session_start(rig.session);
    music_session_begin_match(rig.session);
    for (int i = 0; i < 30; ++i)
        rig.second(40);
    require(cd_music_kind(*rig.mixer) == music_kind_calm, "fewer than 31 units never fight");
}

void activity_is_local() {
    Rig rig(static_cast<uint8_t>(CdPlayMode::by_kind));
    music_session_start(rig.session);
    music_session_begin_match(rig.session);
    music_note_hit(rig.session, 2, 3);
    music_note_kill(rig.session, 2);
    require(
        rig.session.mood.activity[rig.session.mood.ring_index] == 0, "foreign combat adds nothing"
    );
    music_note_hit(rig.session, 0, 3);
    music_note_hit(rig.session, 3, 0);
    music_note_kill(rig.session, 0);
    require(
        rig.session.mood.activity[rig.session.mood.ring_index] == 7, "hits add 1, local kills add 5"
    );
}

void sequential_and_controls() {
    Rig rig(static_cast<uint8_t>(CdPlayMode::sequential));
    music_session_start(rig.session);
    music_session_begin_match(rig.session);
    require(rig.last() == Play(3, 4), "sequential mode starts after the rewound track");
    rig.finish_track();
    require(rig.last() == Play(4, 5), "and plays in disc order");

    require(music_set_paused(rig.session, true) && rig.music.pauses == 1, "options pause");
    rig.music.device_track = 4;
    require(
        music_set_paused(rig.session, false) && rig.music.resumes.back() == 5,
        "resume to the end of the paused track"
    );

    require(music_command_cd_play(rig.session, 5) && rig.last() == Play(6, 7), "CDPlay 5");
    require(music_command_cd_stop(rig.session) && !rig.music.playing, "CDStop");
    music_command_music_mode(rig.session, music_kind_battle);
    require(cd_music_kind(*rig.mixer) == music_kind_battle, "MusicMode 1");

    rig.session.panel_track = 16;
    music_panel_step(rig.session, true);
    require(rig.session.panel_track == 1, "CDNEXT wraps to track 1");
    music_panel_step(rig.session, false);
    require(rig.session.panel_track == 16, "CDPREV wraps to the last track");
    music_panel_play(rig.session);
    require(rig.last() == Play(17, 0), "CDPLAY plays the shown track to the disc end");
    music_panel_set_enabled(rig.session, false);
    require(!rig.music.playing && rig.game->music_flags == 0, "NOTRAK off stops the player");
    music_panel_leave(rig.session);
}

void selected_mode_panel() {
    Rig rig(static_cast<uint8_t>(CdPlayMode::by_kind));
    music_session_start(rig.session);
    rig.session.panel_track = 5;
    const uint8_t kind = music_panel_set_mode(rig.session, 3, 0);
    require(rig.game->cd_mode == 4 && kind == 1, "by-kind mode shows the track's kind");
    music_panel_set_track_type(rig.session, 0);
    require(cd_track_type(*rig.mixer, 5) == 0, "TRACKTYPE edits the shown track");
    music_panel_set_mode(rig.session, 2, 0);
    require(
        rig.game->cd_mode == 3 && rig.session.panel_track == 1, "selected mode adopts the playlist"
    );
    rig.session.panel_track = 6;
    music_panel_sync_playlist(rig.session);
    require(cd_playlist_track(*rig.mixer) == 6, "shown track becomes the playlist track");

    // The edited kinds persist per disc through CDLISTS.
    uint8_t saved[sizeof(DiscCache)]{};
    rig.session.settings.context = saved;
    rig.session.settings.write_blob = [](void* c, const char*, const uint8_t* b, uint32_t n) {
        std::copy(b, b + n, static_cast<uint8_t*>(c));
    };
    music_session_shutdown(rig.session);
    const auto& record = reinterpret_cast<const DiscCache*>(saved)->records[0];
    require(record.disc_id[0] == 0xfe && record.disc_id[1] == 0xca, "front record is this disc");
    require(record.track_types[4] == 0, "edited kind saved");
}

void disc_scan() {
    const auto root = oa::test::make_scratch_directory("oa-music-disc-test");
    std::filesystem::create_directories(root / "Music");
    for (const char* name : {"0.mp3", "1.mp3", "2.mp3", "3.OGG", "4.wav", "6.mp3", "readme.txt"})
        std::ofstream(root / "Music" / name) << name;
    const auto directory = music_disc_directory(root);
    require(directory.filename() == "Music", "music directory found case-insensitively");
    const MusicDisc disc = music_disc_scan(directory);
    require(disc.track_count == 4, "tracks 2..4 are contiguous; 6 is unreachable");
    require(disc.tracks[3].filename() == "3.OGG", "any supported extension");
    require(disc.disc_id != 0 && music_disc_present(disc), "identity");
    require(!music_disc_present(music_disc_scan(root / "missing")), "no directory, no disc");
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    by_kind_follows_combat();
    consecutive_matches_are_independent();
    small_forces_stay_calm();
    activity_is_local();
    sequential_and_controls();
    selected_mode_panel();
    disc_scan();
    return 0;
}
