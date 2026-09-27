// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/audio/cd_music.hpp"
#include "oa/audio/music_mood.hpp"
#include "oa/core/game_state.h"

#include <cstdint>

namespace oa::audio {

// The game-side CD music owner: the events that drive the CD player
// through a session, from startup through the menus to a match and back.
// Game supplies music_volume, music_flags bit 0 (music on), cd_mode (play
// mode), session_flags bit 2 (in a game), mode, load_flags, local_player_index
// and players[].unit_count.
struct MusicSession {
    Mixer* mixer{};
    Game* game{};
    AudioSettings settings{};
    DiscCache cache{};
    MusicMood mood{};
    int32_t panel_track{}; // track the music panel shows
};

inline constexpr uint8_t game_session_flag_in_game = 4;
inline constexpr int32_t game_mode_loading = 6;
inline constexpr int32_t music_volume_shift = 10;
inline constexpr int32_t music_kind_calm = 0;
inline constexpr int32_t music_kind_battle = 1;
inline constexpr int32_t music_activity_hit = 1;
inline constexpr int32_t music_activity_kill = 5;

/// Applies the music preference to the CD mixer line.
///
/// Part of the options screens' saved-volume reapply.
///
/// @param[in,out] session Music session; reads Game.music_volume (0..64),
///        shifted left by 10 so 64 reaches full scale after clamping.
void music_apply_volume(MusicSession& session) noexcept;

/// Runs cd_handle_disc_change() on the session.
///
/// Disc-change callback installed by music_session_start().
///
/// @param[in,out] session The MusicSession, passed as the callback's user value.
void music_on_disc_change(void* session) noexcept;

/// Starts CD music at startup.
///
/// Opens the CD player when needed, loads CDLISTS, applies the music on/off
/// and play mode, installs the disc-change callback (which runs it once) and
/// runs it again, selects calm music and applies the volume. Outside a game
/// the disc-change handler leaves the player stopped.
///
/// @param[in,out] session Music session to start.
/// @return True when the CD player is open.
bool music_session_start(MusicSession& session) noexcept;

/// Stores the disc's track kinds in CDLISTS, restores the saved volumes, and closes the player.
///
/// @param[in,out] session Music session to stop.
void music_session_shutdown(MusicSession& session) noexcept;

/// Selects music kind 4 for the main menu.
///
/// Music stops at the end of the current track (or at once in by-kind mode).
///
/// @param[in,out] session Music session.
void music_session_main_menu(MusicSession& session) noexcept;

/// Prepares music for a game that is loading.
///
/// Resets the whole mood (activity ring, applied kind, switch timer and last
/// update tick), sets Game.mode to 6 and the in-game flag, selects calm
/// music, and starts a track when none plays. 3.1c resets only the activity
/// ring, so there the other three carry over from the previous game.
///
/// @param[in,out] session Music session; its game state is changed.
void music_session_begin_match(MusicSession& session) noexcept;

/// Leaves the in-game state at game teardown.
///
/// Clears the in-game flag, resets the mood, stops the player and selects kind 4.
///
/// @param[in,out] session Music session; its game state is changed.
void music_session_end_match(MusicSession& session) noexcept;

/// Runs the mood update from the per-frame idle tick during a game.
///
/// @param[in,out] session Music session.
/// @param game_tick Current game tick.
/// @return True when the mood update ran (see music_mood_update()).
bool music_session_tick(MusicSession& session, uint32_t game_tick) noexcept;

/// Records a non-healing hit with an attacker, reported by the damage handler.
///
/// Adds one unit of activity when either side belongs to the local player.
///
/// @param[in,out] session Music session.
/// @param attacker_player Player index of the attacking unit.
/// @param target_player Player index of the unit hit.
void music_note_hit(MusicSession& session, uint8_t attacker_player, uint8_t target_player) noexcept;

/// Records a unit killed through the kill-statistics path.
///
/// Adds five units of activity when the local player made the kill.
///
/// @param[in,out] session Music session.
/// @param killer_player Player index of the killer.
void music_note_kill(MusicSession& session, uint8_t killer_player) noexcept;

/// Pauses music when the in-game options panel opens and resumes it when it closes.
///
/// @param[in,out] session Music session.
/// @param paused True on open, false on close.
/// @return The result of cd_set_paused().
bool music_set_paused(MusicSession& session, bool paused) noexcept;

/// Runs the console command "CDPlay <track>".
///
/// @param[in,out] session Music session.
/// @param track Track number; 0 advances by mode.
/// @return The result of cd_play_track().
bool music_command_cd_play(MusicSession& session, int32_t track) noexcept;

/// Runs the console command "CDStop", the same stop sound_stop_music() performs.
///
/// @param[in,out] session Music session.
/// @return The result of cd_stop().
bool music_command_cd_stop(MusicSession& session) noexcept;

/// Runs the console command "MusicMode <kind>".
///
/// @param[in,out] session Music session.
/// @param kind Music kind passed to cd_set_music_kind().
void music_command_music_mode(MusicSession& session, int32_t kind) noexcept;

// Music options panel (MUSIC.GUI / MUSICRT.GUI) CD-side actions.

/// Sets up the panel's shown track on entry.
///
/// In selected mode the shown track follows the playlist track.
///
/// @param[in,out] session Music session.
void music_panel_enter(MusicSession& session) noexcept;

/// Handles the panel closing.
///
/// Outside a game the player stops; in a game playback resumes by mode.
///
/// @param[in,out] session Music session.
void music_panel_leave(MusicSession& session) noexcept;

/// Applies the NOTRAK toggle: music on or off.
///
/// @param[in,out] session Music session; Game.music_flags bit 0 is changed.
/// @param enabled True to turn music on.
void music_panel_set_enabled(MusicSession& session, bool enabled) noexcept;

/// Applies the TRACKMODE stage.
///
/// Stages 0..3 select play modes 1..4; selected mode adopts the playlist
/// track, by-kind mode reapplies the stored kind of the shown track.
///
/// @param[in,out] session Music session; Game.cd_mode is changed.
/// @param stage TRACKMODE button stage, 0..3.
/// @param track_type_stage Current TRACKTYPE button stage.
/// @return The kind the panel should show for the shown track.
uint8_t
music_panel_set_mode(MusicSession& session, uint8_t stage, uint8_t track_type_stage) noexcept;

/// Applies the TRACKTYPE stage to the shown track.
///
/// @param[in,out] session Music session.
/// @param kind Music kind for the shown track.
void music_panel_set_track_type(MusicSession& session, uint8_t kind) noexcept;

/// Handles CDPLAY: plays the shown track.
///
/// @param[in,out] session Music session.
void music_panel_play(MusicSession& session) noexcept;

/// Handles CDSTOP: stops, then shows track 1.
///
/// @param[in,out] session Music session.
void music_panel_stop(MusicSession& session) noexcept;

/// Handles CDNEXT / CDPREV: steps the shown track with wraparound and selects it.
///
/// @param[in,out] session Music session.
/// @param forward True for CDNEXT, false for CDPREV.
void music_panel_step(MusicSession& session, bool forward) noexcept;

/// Handles RESTORE: volume 32, by-kind mode, and music on.
///
/// Starts a track when music was off, then applies the volume. Only the
/// game's options change; the player's own play mode is left as it is.
///
/// @param[in,out] session Music session; Game.music_volume, cd_mode and music_flags change.
void music_panel_restore(MusicSession& session) noexcept;

/// Records the shown track as the playlist track in selected mode.
///
/// Tail of the panel's display refresh.
///
/// @param[in,out] session Music session.
void music_panel_sync_playlist(MusicSession& session) noexcept;

/// Makes the panel adopt the player's current track when the shown number differs.
///
/// Panel idle tick.
///
/// @param[in,out] session Music session.
/// @param shown_track Track number the panel currently shows.
/// @return True when the shown track changed.
bool music_panel_follow(MusicSession& session, int32_t shown_track) noexcept;

} // namespace oa::audio
