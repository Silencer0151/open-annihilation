// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "audio_test_support.hpp"
#include "oa/audio/cd_music.hpp"
#include "oa/audio/music_mood.hpp"

#include <cstdint>

using namespace oa::audio;
using audio_test::require;

int main() {
    audio_test::FakeSink sink;
    audio_test::FakeMusic music;
    audio_test::FakeTimers timers;
    audio_test::FakeFiles files;
    auto mixer = audio_test::make_mixer(sink, music, timers, files);
    require(cd_open(*mixer), "open");

    auto game = std::make_unique<oa::Game>();
    MusicMood mood{};
    require(!music_mood_update(mood, *mixer, *game, 100), "outside a live game");
    game->session_flags = 4;
    game->load_flags = 1;
    require(!music_mood_update(mood, *mixer, *game, 100), "suppressed while loading");
    game->load_flags = 3;
    require(!music_mood_update(mood, *mixer, *game, 30), "rate limited to 30 ticks");
    uint32_t now = 31;
    require(music_mood_update(mood, *mixer, *game, now), "update runs");
    require(mood.ring_index == 1 && mood.ticks_since_switch == 1, "ring advanced");

    // Heavy activity without a sizeable force keeps calm music.
    game->players[game->local_player_index].unit_count = 30;
    for (int i = 0; i < 12; ++i) {
        music_mood_add_activity(mood, 10);
        now += 31;
        music_mood_update(mood, *mixer, *game, now);
    }
    require(cd_music_kind(*mixer) == 0, "small forces stay calm");

    game->players[game->local_player_index].unit_count = 31;
    require(music_mood_add_activity(mood, 60) == 60, "activity accumulates in the slot");
    now += 31;
    music_mood_update(mood, *mixer, *game, now);
    require(
        cd_music_kind(*mixer) == 1 && mood.applied_kind == 1 && mood.ticks_since_switch == 0,
        "battle music"
    );

    // Calm returns only after a quiet ring and 61 updates.
    for (int i = 0; i < 60; ++i) {
        now += 31;
        music_mood_update(mood, *mixer, *game, now);
    }
    require(cd_music_kind(*mixer) == 1, "still battle before 61 updates");
    now += 31;
    music_mood_update(mood, *mixer, *game, now);
    require(cd_music_kind(*mixer) == 0 && mood.applied_kind == 0, "calm music");

    for (int i = 0; i < 40; ++i) {
        now += 31;
        music_mood_update(mood, *mixer, *game, now);
    }
    require(mood.ring_index >= 0 && mood.ring_index < 30, "ring wraps");
    return 0;
}
