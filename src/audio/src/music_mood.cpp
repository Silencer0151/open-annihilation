// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/music_mood.hpp"

#include "oa/audio/cd_music.hpp"

#include <cstdint>

namespace oa::audio {
namespace {

constexpr uint8_t session_flag_music = 4;
constexpr uint16_t load_flag_active = 1;
constexpr uint16_t load_flag_music = 2;
constexpr uint32_t update_interval = 30;
constexpr int32_t warmup_updates = 10;
constexpr int32_t recent_slots = 5;
constexpr int32_t battle_total = 51;
constexpr int32_t battle_recent = 31;
constexpr uint16_t battle_min_units = 31;
constexpr int32_t calm_max_total = 9;
constexpr int32_t calm_min_updates = 61;
constexpr int32_t kind_calm = 0;
constexpr int32_t kind_battle = 1;

} // namespace

bool music_mood_update(MusicMood& mood, Mixer& mixer, const Game& game, uint32_t now) noexcept {
    if ((game.session_flags & session_flag_music) == 0)
        return false;
    if ((game.load_flags & load_flag_active) != 0 && (game.load_flags & load_flag_music) == 0)
        return false;
    if (now <= mood.last_update + update_interval)
        return false;

    const int32_t current = cd_music_kind(mixer);
    ++mood.ticks_since_switch;
    if (mood.ticks_since_switch > warmup_updates) {
        int32_t recent = 0;
        int32_t total = 0;
        int32_t remaining_recent = recent_slots;
        int32_t slot = mood.ring_index - 1;
        for (;;) {
            if (slot < 0)
                slot += static_cast<int32_t>(music_activity_slots);
            total += mood.activity[slot];
            if (remaining_recent != 0) {
                --remaining_recent;
                recent += mood.activity[slot];
            }
            if (slot == mood.ring_index)
                break;
            --slot;
        }
        int32_t wanted = -1;
        if (current == kind_calm) {
            const uint16_t units = game.players[game.local_player_index].unit_count;
            if ((total >= battle_total || recent >= battle_recent) && units >= battle_min_units)
                wanted = kind_battle;
        } else if (
            current == kind_battle && total <= calm_max_total && recent == 0 &&
            mood.ticks_since_switch >= calm_min_updates
        ) {
            wanted = kind_calm;
        }
        if (wanted >= 0 && wanted != mood.applied_kind) {
            cd_set_music_kind(mixer, wanted);
            mood.ticks_since_switch = 0;
            mood.applied_kind = wanted;
        }
    }
    if (++mood.ring_index > static_cast<int32_t>(music_activity_slots) - 1)
        mood.ring_index = 0;
    mood.activity[mood.ring_index] = 0;
    mood.last_update = now;
    return true;
}

int32_t music_mood_add_activity(MusicMood& mood, int32_t amount) noexcept {
    mood.activity[mood.ring_index] += amount;
    return mood.activity[mood.ring_index];
}

} // namespace oa::audio
