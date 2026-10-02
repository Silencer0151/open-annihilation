// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/cd_music.hpp"
#include "oa/base/bytes.hpp"

#include <cstdint>
#include <cstring>

namespace oa::audio {
namespace {
using base::bytes::load_le32;

constexpr int32_t default_track_kinds = 4;
constexpr uint32_t fade_step_interval = 2;
constexpr uint32_t calm_pause_interval = 0x78;
constexpr int32_t fade_divisor = -18;
constexpr int32_t random_kind_skip_mask = 0xf;
constexpr int32_t disc_cache_default_track_count = 16;
constexpr uint8_t session_flag_music = 4;
constexpr int32_t game_mode_music = 6;

// Kinds for a 16-track disc whose first track is data: seven of kind 1.
constexpr uint8_t data_disc_track_kinds[disc_cache_default_track_count] = {
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
};

int32_t next_random(Mixer& mixer) noexcept {
    return mixer.random.next != nullptr ? mixer.random.next(mixer.random.context) : 0;
}

void remove_timer(Mixer& mixer, int32_t handle) noexcept {
    if (mixer.timers.remove != nullptr)
        mixer.timers.remove(mixer.timers.context, handle);
}

int32_t add_timer(Mixer& mixer, uint32_t interval, TimerCallback callback) noexcept {
    return mixer.timers.add != nullptr
               ? mixer.timers.add(mixer.timers.context, interval, callback, &mixer)
               : no_timer;
}

void write_le32(uint8_t* bytes, uint32_t value) noexcept {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
    bytes[2] = static_cast<uint8_t>(value >> 16);
    bytes[3] = static_cast<uint8_t>(value >> 24);
}

} // namespace

uint32_t cd_disc_id(const Mixer& mixer) noexcept {
    return mixer.cd.disc_id;
}

bool cd_set_disc_change_callback(Mixer& mixer, TimerCallback callback, void* user) noexcept {
    mixer.cd.disc_change_callback = callback;
    mixer.cd.disc_change_user = user;
    cd_refresh_tracks(mixer);
    if (mixer.cd.disc_change_callback != nullptr)
        mixer.cd.disc_change_callback(mixer.cd.disc_change_user);
    return true;
}

int32_t cd_refresh_tracks(Mixer& mixer) noexcept {
    CdAudio& cd = mixer.cd;
    cd.current_track = 0;
    cd.playback = static_cast<int32_t>(CdPlayback::stopped);
    uint32_t id = 0;
    if (mixer.music.disc_id != nullptr && mixer.music.disc_id(mixer.music.context, &id))
        cd.disc_id = id;
    int32_t count = 0;
    if (mixer.music.track_count == nullptr || !mixer.music.track_count(mixer.music.context, &count))
        return 0;
    cd.track_count = count;
    if (!cd_first_track_is_data(mixer)) {
        cd.first_track_offset = 0;
    } else {
        --cd.track_count;
        cd.first_track_offset = 1;
        if (cd.track_count < 0)
            cd.track_count = 0;
    }
    if (cd.track_count != 0)
        cd.current_track = 1;
    return cd.track_count;
}

void cd_advance(Mixer& mixer) noexcept {
    CdAudio& cd = mixer.cd;
    if (cd.track_count == 0)
        return;
    const int32_t kind = cd.music_kind;
    if (kind == music_kind_stop) {
        cd_stop(mixer);
        return;
    }
    if (cd.playback == static_cast<int32_t>(CdPlayback::paused))
        return;

    const bool by_kind =
        kind == 2 || kind == 3 || cd.play_mode == static_cast<int32_t>(CdPlayMode::by_kind);
    if (!by_kind) {
        switch (static_cast<CdPlayMode>(cd.play_mode)) {
        case CdPlayMode::off:
            if (cd.playback == static_cast<int32_t>(CdPlayback::stopped))
                return;
            cd.playback = static_cast<int32_t>(CdPlayback::stopped);
            if (cd_is_playing(mixer))
                cd_stop(mixer);
            return;
        case CdPlayMode::sequential:
            if (!cd_is_playing(mixer)) {
                cd.current_track = cd.current_track < 1 ? 1 : cd.current_track + 1;
                cd_play_track(mixer, cd.current_track);
                if (cd.track_count < cd.current_track)
                    cd.current_track = 1;
            }
            break;
        case CdPlayMode::random:
            if (!cd_is_playing(mixer))
                cd_play_track(mixer, next_random(mixer) % cd.track_count + 1);
            break;
        case CdPlayMode::selected:
            if (!cd_is_playing(mixer) || cd.current_track != cd.selected_track) {
                if (cd.selected_track == 0)
                    cd.selected_track = 1;
                cd_play_track(mixer, cd.selected_track);
            }
            break;
        default:
            break;
        }
    } else {
        // Plays the n-th following track of the wanted kind, n drawn from
        // 1..15 (0 behaves as 1), searching at most (n + 1) passes.
        int32_t skip = next_random(mixer) & random_kind_skip_mask;
        const bool keep = cd_is_playing(mixer) &&
                          static_cast<int32_t>(cd_track_type(mixer, cd.current_track)) == kind;
        if (!keep) {
            int32_t budget = (skip + 1) * cd.track_count;
            int32_t track = cd.current_track;
            bool played = false;
            while (budget > 0) {
                if (++track > cd.track_count)
                    track = 1;
                if (static_cast<int32_t>(cd_track_type(mixer, track)) == kind && --skip < 1) {
                    cd_play_track(mixer, track);
                    played = true;
                    break;
                }
                --budget;
            }
            // Without a match playback stops, then is still marked
            // active.
            if (!played)
                cd_stop(mixer);
        }
    }
    mixer_set_music_volume(mixer, static_cast<int32_t>(mixer.music_volume), true);
    cd.playback = static_cast<int32_t>(CdPlayback::playing);
}

void cd_on_device_change(Mixer& mixer, bool arrival) noexcept {
    cd_stop(mixer);
    if (!arrival)
        return;
    cd_refresh_tracks(mixer);
    if (mixer.cd.disc_change_callback != nullptr)
        mixer.cd.disc_change_callback(mixer.cd.disc_change_user);
}

void cd_on_play_complete(Mixer& mixer, bool successful) noexcept {
    if (successful && mixer.cd.playback == static_cast<int32_t>(CdPlayback::playing) &&
        !cd_is_playing(mixer))
        cd_advance(mixer);
}

void cd_close_foreign_player(Mixer& mixer) noexcept {
    if (mixer.cd.foreign_player == 0)
        return;
    if (mixer.music.close_foreign_player != nullptr)
        mixer.music.close_foreign_player(mixer.music.context, mixer.cd.foreign_player);
    mixer.cd.foreign_player = 0;
}

bool cd_foreign_player_present(const Mixer& mixer) noexcept {
    return mixer.cd.foreign_player != 0;
}

void cd_probe_foreign_player(Mixer& mixer) noexcept {
    const uint32_t handle = mixer.music.find_foreign_player != nullptr
                                ? mixer.music.find_foreign_player(mixer.music.context)
                                : 0;
    if (handle != 0)
        mixer.cd.foreign_player = handle;
}

bool cd_open(Mixer& mixer) noexcept {
    CdAudio& cd = mixer.cd;
    if (cd.device_open != 0)
        return true;
    cd.foreign_player = 0;
    for (int32_t track = 0; track < static_cast<int32_t>(cd_track_type_bytes); ++track)
        cd.track_types[track] = static_cast<uint8_t>(track % default_track_kinds + 1);
    cd.selected_track = 1;
    cd.current_track = 0;
    cd.disc_id = 0;
    cd.disc_change_callback = nullptr;
    cd.disc_change_user = nullptr;
    cd.play_mode = static_cast<int32_t>(CdPlayMode::sequential);
    cd.music_kind = 0;
    cd.device_open = 0;
    cd.track_count = 0;
    cd.first_track_offset = 0;
    const auto open = [&mixer] {
        return mixer.music.open != nullptr && mixer.music.open(mixer.music.context);
    };
    if (!open()) {
        cd_probe_foreign_player(mixer);
        if (!open())
            return false;
    }
    cd_stop(mixer);
    cd.disc_id = 0;
    cd.track_count = cd_refresh_tracks(mixer);
    cd.device_open = 1;
    cd.selected_track = 1;
    cd.current_track = 0;
    cd.disc_change_callback = nullptr;
    cd.disc_change_user = nullptr;
    cd.play_mode = static_cast<int32_t>(CdPlayMode::sequential);
    cd.music_kind = 0;
    cd.enabled = 1;
    return true;
}

void cd_copy_track_types(Mixer& mixer, const uint8_t* types) noexcept {
    int32_t count = mixer.cd.track_count;
    const auto room = static_cast<int32_t>(cd_track_type_bytes) - 1;
    if (count > room)
        count = room;
    if (count > 0)
        std::memmove(mixer.cd.track_types + 1, types, static_cast<std::size_t>(count));
}

void cd_close(Mixer& mixer) noexcept {
    if (mixer.cd.device_open == 0)
        return;
    if (mixer.music.stop != nullptr)
        mixer.music.stop(mixer.music.context);
    if (mixer.music.close != nullptr)
        mixer.music.close(mixer.music.context);
    mixer.cd.device_open = 0;
}

int32_t cd_track_count(const Mixer& mixer) noexcept {
    return mixer.cd.track_count;
}

bool cd_first_track_is_data(Mixer& mixer) noexcept {
    const CdTrackKind kind = mixer.music.first_track_kind != nullptr
                                 ? mixer.music.first_track_kind(mixer.music.context)
                                 : CdTrackKind::unavailable;
    return kind != CdTrackKind::audio;
}

void cd_select_playlist_track(Mixer& mixer, int32_t track) noexcept {
    if (track <= mixer.cd.track_count)
        mixer.cd.selected_track = track;
}

int32_t cd_playlist_track(const Mixer& mixer) noexcept {
    return mixer.cd.selected_track;
}

void cd_on_fade_end(void* user) noexcept {
    auto& mixer = *static_cast<Mixer*>(user);
    remove_timer(mixer, mixer.cd.fade_end_timer);
    mixer.cd.fade_end_timer = no_timer;
    cd_advance(mixer);
}

void cd_on_fade_step(void* user) noexcept {
    auto& mixer = *static_cast<Mixer*>(user);
    CdAudio& cd = mixer.cd;
    cd.fade_level += cd.fade_step;
    if (cd.fade_level >= 1) {
        mixer_set_music_volume(mixer, cd.fade_level, true);
        return;
    }
    remove_timer(mixer, cd.fade_timer);
    cd.fade_timer = no_timer;
    cd.fade_level = 0;
    cd.fade_step = 0;
    mixer_set_music_volume(mixer, cd.fade_level, true);
    if (cd.music_kind == 0)
        cd.fade_end_timer = add_timer(mixer, calm_pause_interval, cd_on_fade_end);
    else
        cd_advance(mixer);
}

int32_t cd_music_kind(const Mixer& mixer) noexcept {
    return mixer.cd.music_kind;
}

void cd_set_music_kind(Mixer& mixer, int32_t kind) noexcept {
    CdAudio& cd = mixer.cd;
    const int32_t previous = cd.music_kind;
    if (previous == kind)
        return;
    // The table has ten entries; larger kinds are not written.
    if (previous > -1 && previous < static_cast<int32_t>(cd_kind_positions))
        cd.kind_positions[previous] = cd.current_track;
    cd.music_kind = kind;
    if (cd.play_mode != static_cast<int32_t>(CdPlayMode::by_kind) && kind != 2 && kind != 3)
        return;
    cd.fade_level = static_cast<int32_t>(mixer.music_volume);
    if (previous == music_kind_stop) {
        if (cd.fade_timer > no_timer) {
            remove_timer(mixer, cd.fade_timer);
            cd.fade_timer = no_timer;
        }
        if (cd.fade_end_timer > no_timer) {
            remove_timer(mixer, cd.fade_end_timer);
            cd.fade_end_timer = no_timer;
        }
        mixer_set_music_volume(mixer, static_cast<int32_t>(mixer.music_volume), false);
    } else {
        if (cd.fade_timer < 0) {
            cd.fade_step = static_cast<int32_t>(mixer.music_volume) / fade_divisor;
            cd.fade_timer = add_timer(mixer, fade_step_interval, cd_on_fade_step);
            return;
        }
        remove_timer(mixer, cd.fade_timer);
        cd.fade_timer = no_timer;
        remove_timer(mixer, cd.fade_end_timer);
        cd.fade_end_timer = no_timer;
    }
    cd_advance(mixer);
}

bool cd_set_play_mode(Mixer& mixer, int32_t mode) noexcept {
    mixer.cd.play_mode = mode;
    return true;
}

void cd_set_track_type(Mixer& mixer, int32_t track, uint8_t kind) noexcept {
    if (track >= 0 && track < static_cast<int32_t>(cd_track_type_bytes))
        mixer.cd.track_types[track] = kind;
}

uint8_t cd_track_type(const Mixer& mixer, int32_t track) noexcept {
    if (track < 0 || track >= static_cast<int32_t>(cd_track_type_bytes))
        return 0;
    return mixer.cd.track_types[track];
}

int32_t cd_current_track(const Mixer& mixer) noexcept {
    return mixer.cd.current_track;
}

bool cd_is_playing(Mixer& mixer) noexcept {
    return mixer.music.is_playing != nullptr && mixer.music.is_playing(mixer.music.context);
}

int32_t cd_select_track(Mixer& mixer, int32_t track) noexcept {
    CdAudio& cd = mixer.cd;
    if (cd.track_count == 0)
        return 0;
    if (cd.track_count < track)
        track %= cd.track_count;
    if (cd.playback == static_cast<int32_t>(CdPlayback::playing))
        cd_play_track(mixer, track);
    else
        cd.current_track = track;
    return cd.current_track;
}

bool cd_set_paused(Mixer& mixer, bool paused) noexcept {
    CdAudio& cd = mixer.cd;
    if (cd.enabled == 0 || cd.playback == static_cast<int32_t>(CdPlayback::stopped))
        return true;
    if (paused) {
        cd.playback = static_cast<int32_t>(CdPlayback::paused);
        return mixer.music.pause != nullptr && mixer.music.pause(mixer.music.context);
    }
    int32_t track = 0;
    if (mixer.music.current_track != nullptr)
        mixer.music.current_track(mixer.music.context, &track);
    const int32_t until = track < cd.track_count ? track + 1 : 0;
    cd.playback = static_cast<int32_t>(CdPlayback::playing);
    return mixer.music.resume != nullptr && mixer.music.resume(mixer.music.context, until);
}

bool cd_play_track(Mixer& mixer, int32_t track) noexcept {
    CdAudio& cd = mixer.cd;
    if (cd.enabled == 0)
        return true;
    cd.playback = static_cast<int32_t>(CdPlayback::playing);
    if (track == 0) {
        cd_advance(mixer);
        return true;
    }
    if (cd_is_playing(mixer) && track == cd.current_track)
        return true;
    cd.current_track = track;
    const int32_t from = track + cd.first_track_offset;
    mixer_set_music_volume(mixer, static_cast<int32_t>(mixer.music_volume), true);
    const int32_t until = from < cd.track_count ? from + 1 : 0;
    return mixer.music.play != nullptr && mixer.music.play(mixer.music.context, from, until);
}

bool cd_stop(Mixer& mixer) noexcept {
    CdAudio& cd = mixer.cd;
    const bool stopped = mixer.music.stop != nullptr && mixer.music.stop(mixer.music.context);
    cd.current_track = cd.track_count == 0 ? 0 : 1;
    cd.playback = static_cast<int32_t>(CdPlayback::stopped);
    cd.fade_step = 0;
    remove_timer(mixer, cd.fade_end_timer);
    remove_timer(mixer, cd.fade_timer);
    cd.fade_timer = no_timer;
    cd.fade_end_timer = no_timer;
    return stopped;
}

void cd_set_enabled(Mixer& mixer, int32_t enabled) noexcept {
    mixer.cd.enabled = enabled;
    if (enabled == 0)
        cd_stop(mixer);
}

void cd_load_disc_cache(DiscCache& cache, const AudioSettings& settings) noexcept {
    uint32_t size = sizeof(DiscCache);
    auto* bytes = reinterpret_cast<uint8_t*>(&cache);
    if (settings.read_blob == nullptr ||
        !settings.read_blob(settings.context, disc_cache_setting, bytes, &size))
        std::memset(bytes, 0, sizeof(DiscCache));
}

void cd_save_disc_cache(
    DiscCache& cache, const Mixer& mixer, const AudioSettings& settings
) noexcept {
    for (int32_t i = 0;
         i < mixer.cd.track_count && i < static_cast<int32_t>(disc_cache_track_types);
         ++i)
        cache.records[0].track_types[i] = cd_track_type(mixer, i + 1);
    if (settings.write_blob != nullptr)
        settings.write_blob(
            settings.context,
            disc_cache_setting,
            reinterpret_cast<const uint8_t*>(&cache),
            sizeof(DiscCache)
        );
}

void cd_handle_disc_change(Mixer& mixer, DiscCache& cache, const Game& game) noexcept {
    const int32_t kind = cd_music_kind(mixer);
    if (mixer.music.stop != nullptr)
        mixer.music.stop(mixer.music.context);
    if (mixer.music.close != nullptr)
        mixer.music.close(mixer.music.context);
    if (mixer.music.open != nullptr)
        mixer.music.open(mixer.music.context);
    cd_set_enabled(mixer, game.music_flags & music_flag_enabled);
    cd_set_play_mode(mixer, game.cd_mode);
    cd_set_music_kind(mixer, kind);

    const uint32_t disc = cd_disc_id(mixer);
    std::size_t found = 0;
    while (found < disc_cache_records && load_le32(cache.records[found].disc_id) != disc)
        ++found;
    if (found < disc_cache_records) {
        const DiscCacheRecord record = cache.records[found];
        for (std::size_t i = found; i > 0; --i)
            cache.records[i] = cache.records[i - 1];
        cache.records[0] = record;
        cd_copy_track_types(mixer, cache.records[0].track_types);
    } else {
        if (cd_track_count(mixer) == disc_cache_default_track_count &&
            cd_first_track_is_data(mixer))
            cd_copy_track_types(mixer, data_disc_track_kinds);
        for (std::size_t i = disc_cache_records - 1; i > 0; --i)
            cache.records[i] = cache.records[i - 1];
        // The front record keeps the rest of its previous contents.
        std::memcpy(
            cache.records[0].track_types, data_disc_track_kinds, sizeof(data_disc_track_kinds)
        );
        write_le32(cache.records[0].disc_id, disc);
    }
    if ((game.session_flags & session_flag_music) != 0 && game.mode == game_mode_music)
        cd_advance(mixer);
    else
        cd_stop(mixer);
}

} // namespace oa::audio
