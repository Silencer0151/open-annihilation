// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/music_session.hpp"

namespace oa::audio {
namespace {

constexpr uint32_t restore_music_volume = 32;
constexpr uint8_t restore_cd_mode = static_cast<uint8_t>(CdPlayMode::by_kind);
constexpr uint8_t cd_mode_selected = static_cast<uint8_t>(CdPlayMode::selected);
constexpr uint8_t cd_mode_by_kind = static_cast<uint8_t>(CdPlayMode::by_kind);

bool in_game(const Game& game) noexcept {
    return (game.session_flags & game_session_flag_in_game) != 0;
}

} // namespace

void music_apply_volume(MusicSession& session) noexcept {
    mixer_set_music_volume(
        *session.mixer,
        static_cast<int32_t>(session.game->music_volume << music_volume_shift),
        false
    );
}

void music_on_disc_change(void* user) noexcept {
    auto& session = *static_cast<MusicSession*>(user);
    cd_handle_disc_change(*session.mixer, session.cache, *session.game);
}

bool music_session_start(MusicSession& session) noexcept {
    Mixer& mixer = *session.mixer;
    const bool open = mixer.cd.device_open != 0 || cd_open(mixer);
    cd_load_disc_cache(session.cache, session.settings);
    cd_set_enabled(mixer, session.game->music_flags & music_flag_enabled);
    cd_set_play_mode(mixer, session.game->cd_mode);
    cd_set_disc_change_callback(mixer, music_on_disc_change, &session);
    music_on_disc_change(&session);
    cd_set_music_kind(mixer, music_kind_calm);
    music_apply_volume(session);
    return open;
}

void music_session_shutdown(MusicSession& session) noexcept {
    cd_save_disc_cache(session.cache, *session.mixer, session.settings);
    mixer_restore_volumes(*session.mixer);
    cd_close(*session.mixer);
}

void music_session_main_menu(MusicSession& session) noexcept {
    cd_set_music_kind(*session.mixer, music_kind_stop);
}

void music_session_begin_match(MusicSession& session) noexcept {
    session.mood = MusicMood{};
    session.game->mode = game_mode_loading;
    session.game->session_flags =
        static_cast<uint8_t>(session.game->session_flags | game_session_flag_in_game);
    cd_set_music_kind(*session.mixer, music_kind_calm);
    if (!cd_is_playing(*session.mixer))
        cd_advance(*session.mixer);
}

void music_session_end_match(MusicSession& session) noexcept {
    session.game->session_flags =
        static_cast<uint8_t>(session.game->session_flags & ~game_session_flag_in_game);
    session.mood = MusicMood{};
    cd_stop(*session.mixer);
    cd_set_music_kind(*session.mixer, music_kind_stop);
}

bool music_session_tick(MusicSession& session, uint32_t game_tick) noexcept {
    return music_mood_update(session.mood, *session.mixer, *session.game, game_tick);
}

void music_note_hit(
    MusicSession& session, uint8_t attacker_player, uint8_t target_player
) noexcept {
    const uint8_t local = session.game->local_player_index;
    if (attacker_player == local || target_player == local)
        music_mood_add_activity(session.mood, music_activity_hit);
}

void music_note_kill(MusicSession& session, uint8_t killer_player) noexcept {
    if (killer_player == session.game->local_player_index)
        music_mood_add_activity(session.mood, music_activity_kill);
}

bool music_set_paused(MusicSession& session, bool paused) noexcept {
    return cd_set_paused(*session.mixer, paused);
}

bool music_command_cd_play(MusicSession& session, int32_t track) noexcept {
    return cd_play_track(*session.mixer, track);
}

bool music_command_cd_stop(MusicSession& session) noexcept {
    return cd_stop(*session.mixer);
}

void music_command_music_mode(MusicSession& session, int32_t kind) noexcept {
    cd_set_music_kind(*session.mixer, kind);
}

void music_panel_enter(MusicSession& session) noexcept {
    if (session.game->cd_mode == cd_mode_selected)
        session.panel_track = cd_playlist_track(*session.mixer);
    music_panel_sync_playlist(session);
}

void music_panel_leave(MusicSession& session) noexcept {
    if (!in_game(*session.game))
        cd_stop(*session.mixer);
    else
        cd_advance(*session.mixer);
}

void music_panel_set_enabled(MusicSession& session, bool enabled) noexcept {
    Game& game = *session.game;
    game.music_flags = static_cast<uint16_t>(
        (game.music_flags & ~music_flag_enabled) | (enabled ? music_flag_enabled : 0)
    );
    cd_set_enabled(*session.mixer, game.music_flags & music_flag_enabled);
}

uint8_t
music_panel_set_mode(MusicSession& session, uint8_t stage, uint8_t track_type_stage) noexcept {
    Game& game = *session.game;
    game.cd_mode = static_cast<uint8_t>(stage + 1);
    cd_set_play_mode(*session.mixer, game.cd_mode);
    uint8_t shown_kind = track_type_stage;
    if (game.cd_mode == cd_mode_selected) {
        session.panel_track = cd_playlist_track(*session.mixer);
    } else if (game.cd_mode == cd_mode_by_kind) {
        shown_kind = cd_track_type(*session.mixer, session.panel_track);
        cd_set_track_type(*session.mixer, session.panel_track, shown_kind);
    }
    music_panel_sync_playlist(session);
    return shown_kind;
}

void music_panel_set_track_type(MusicSession& session, uint8_t kind) noexcept {
    cd_set_track_type(*session.mixer, session.panel_track, kind);
}

void music_panel_play(MusicSession& session) noexcept {
    cd_play_track(*session.mixer, session.panel_track);
}

void music_panel_stop(MusicSession& session) noexcept {
    cd_stop(*session.mixer);
    session.panel_track = cd_select_track(*session.mixer, 1);
    music_panel_sync_playlist(session);
}

void music_panel_step(MusicSession& session, bool forward) noexcept {
    const int32_t count = cd_track_count(*session.mixer);
    int32_t track = session.panel_track + (forward ? 1 : -1);
    if (forward && track > count)
        track = 1;
    else if (!forward && track < 1)
        track = count;
    session.panel_track = cd_select_track(*session.mixer, track);
    music_panel_sync_playlist(session);
}

void music_panel_restore(MusicSession& session) noexcept {
    Game& game = *session.game;
    game.music_volume = restore_music_volume;
    game.cd_mode = restore_cd_mode;
    if ((game.music_flags & music_flag_enabled) == 0) {
        game.music_flags = static_cast<uint16_t>(game.music_flags | music_flag_enabled);
        cd_advance(*session.mixer);
    }
    music_apply_volume(session);
}

void music_panel_sync_playlist(MusicSession& session) noexcept {
    if (session.game->cd_mode == cd_mode_selected)
        cd_select_playlist_track(*session.mixer, session.panel_track);
}

bool music_panel_follow(MusicSession& session, int32_t shown_track) noexcept {
    const int32_t current = cd_current_track(*session.mixer);
    if (shown_track == current)
        return false;
    session.panel_track = current;
    music_panel_sync_playlist(session);
    return true;
}

} // namespace oa::audio
