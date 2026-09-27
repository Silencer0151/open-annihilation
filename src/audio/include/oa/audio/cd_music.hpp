// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/audio/mixer.hpp"
#include "oa/core/game_state.h"

#include <cstddef>
#include <cstdint>

namespace oa::audio {

// CD-audio music player. Tracks are numbered from 1; track_count excludes a
// leading data track, which first_track_offset skips when playing.

/// Returns the identity of the disc read by the last track refresh.
///
/// @param mixer Mixer that owns the CD player.
/// @return Disc identity reported by the music device, 0 before any disc was read.
[[nodiscard]] uint32_t cd_disc_id(const Mixer& mixer) noexcept;

/// Stores the disc-change callback, rereads the track list, and invokes the callback.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @param callback Function run after every disc change; may be null.
/// @param user Value passed to `callback`.
/// @return Always true.
bool cd_set_disc_change_callback(Mixer& mixer, TimerCallback callback, void* user) noexcept;

/// Rereads the disc identity and track count, excluding a leading data track.
///
/// Resets the current track to 1 (0 without tracks) and marks playback stopped.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @return Number of playable tracks, 0 when the device cannot report a count.
int32_t cd_refresh_tracks(Mixer& mixer) noexcept;

/// Chooses and starts the next track for the play mode and music kind.
///
/// Music kinds 2 and 3 and the by-kind play mode pick the n-th following
/// track of the wanted kind, n drawn from 1..15; other modes play in
/// sequence, at random, or the selected track. Afterwards it reapplies the
/// music volume and marks playback active. Does nothing without tracks or
/// while paused; the stop kind stops playback.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @quirk When no track of the wanted kind exists it stops playback and then
///        still marks playback active.
void cd_advance(Mixer& mixer) noexcept;

/// Handles a device-change notification from the music device.
///
/// Stops playback; on arrival it also rereads the disc and runs the
/// disc-change callback.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @param arrival True when a disc arrived, false when it was removed.
void cd_on_device_change(Mixer& mixer, bool arrival) noexcept;

/// Handles a play-completion notification, advancing when playback should continue.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @param successful True when the device finished the requested range normally.
void cd_on_play_complete(Mixer& mixer, bool successful) noexcept;

/// Closes another CD player recorded by cd_probe_foreign_player() and forgets it.
///
/// @param[in,out] mixer Mixer that owns the CD player.
void cd_close_foreign_player(Mixer& mixer) noexcept;

/// Reports whether another CD player holding the drive has been recorded.
///
/// @param mixer Mixer that owns the CD player.
/// @return True when a foreign player handle is recorded.
[[nodiscard]] bool cd_foreign_player_present(const Mixer& mixer) noexcept;

/// Records another CD player that holds the drive, if one is found.
///
/// @param[in,out] mixer Mixer that owns the CD player.
void cd_probe_foreign_player(Mixer& mixer) noexcept;

/// Opens the music device once and resets the player.
///
/// Retries once after probing for a foreign player, then stops playback,
/// rereads the tracks and resets to sequential mode with the default track
/// kinds (1..4 repeating) and playback enabled.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @return True when the device is open.
bool cd_open(Mixer& mixer) noexcept;

/// Copies per-track kinds for tracks 1..track_count.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @param types Kinds for track 1 onwards; at least track_count bytes (capped at 99).
void cd_copy_track_types(Mixer& mixer, const uint8_t* types) noexcept;

/// Stops and closes the music device when it is open.
///
/// @param[in,out] mixer Mixer that owns the CD player.
void cd_close(Mixer& mixer) noexcept;

/// Returns the number of playable tracks.
///
/// @param mixer Mixer that owns the CD player.
/// @return Track count without a leading data track.
[[nodiscard]] int32_t cd_track_count(const Mixer& mixer) noexcept;

/// Reports whether track 1 is a data track.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @return True unless the device reports track 1 as audio.
[[nodiscard]] bool cd_first_track_is_data(Mixer& mixer) noexcept;

/// Selects the track played by the selected-track mode.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @param track Track number; ignored when above the track count.
void cd_select_playlist_track(Mixer& mixer, int32_t track) noexcept;

/// Returns the track played by the selected-track mode.
///
/// @param mixer Mixer that owns the CD player.
/// @return Selected track number.
[[nodiscard]] int32_t cd_playlist_track(const Mixer& mixer) noexcept;

/// Ends the pause after a fade and advances to the next track.
///
/// Timer callback.
///
/// @param[in,out] mixer The Mixer, passed as the timer's user value.
void cd_on_fade_end(void* mixer) noexcept;

/// Steps the music fade-out by one timer tick.
///
/// Timer callback. At silence it either waits 120 ticks before the next calm
/// track (music kind 0) or advances at once.
///
/// @param[in,out] mixer The Mixer, passed as the timer's user value.
void cd_on_fade_step(void* mixer) noexcept;

/// Returns the wanted music kind.
///
/// @param mixer Mixer that owns the CD player.
/// @return Music kind; music_kind_stop stops music.
[[nodiscard]] int32_t cd_music_kind(const Mixer& mixer) noexcept;

/// Switches the wanted music kind.
///
/// Remembers the current track for the old kind, then fades out or advances
/// when the mode selects tracks by kind (by-kind mode, or kinds 2 and 3).
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @param kind New music kind; music_kind_stop stops music.
/// @quirk Only kinds 0..9 record a remembered track position.
void cd_set_music_kind(Mixer& mixer, int32_t kind) noexcept;

/// Sets the play mode.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @param mode A CdPlayMode value.
/// @return Always true.
bool cd_set_play_mode(Mixer& mixer, int32_t mode) noexcept;

/// Sets the kind of one track.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @param track Track number, 0..99; other values are ignored.
/// @param kind Music kind of the track.
void cd_set_track_type(Mixer& mixer, int32_t track, uint8_t kind) noexcept;

/// Returns the kind of one track.
///
/// @param mixer Mixer that owns the CD player.
/// @param track Track number, 0..99.
/// @return Music kind of the track, 0 outside 0..99.
[[nodiscard]] uint8_t cd_track_type(const Mixer& mixer, int32_t track) noexcept;

/// Returns the current track number.
///
/// @param mixer Mixer that owns the CD player.
/// @return Current track, 0 without tracks.
[[nodiscard]] int32_t cd_current_track(const Mixer& mixer) noexcept;

/// Asks the music device whether a track is playing.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @return True while the device plays.
[[nodiscard]] bool cd_is_playing(Mixer& mixer) noexcept;

/// Plays a track when playback is active, otherwise makes it current.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @param track Track number; reduced modulo the track count when above it.
/// @return Current track afterwards, 0 without tracks.
int32_t cd_select_track(Mixer& mixer, int32_t track) noexcept;

/// Pauses, or resumes from the paused position to the end of that track.
///
/// Does nothing while disabled or stopped.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @param paused True to pause, false to resume.
/// @return The device's result, true when nothing was done.
bool cd_set_paused(Mixer& mixer, bool paused) noexcept;

/// Plays one track to its end and marks playback active.
///
/// A track that is already playing is left alone. Does nothing while disabled.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @param track Track number; 0 advances by mode instead.
/// @return The device's result, true when nothing needed starting.
bool cd_play_track(Mixer& mixer, int32_t track) noexcept;

/// Stops playback, rewinds to track 1, and cancels any fade.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @return The device's stop result.
bool cd_stop(Mixer& mixer) noexcept;

/// Bit of Game.music_flags that turns CD music on.
inline constexpr uint16_t music_flag_enabled = 1;

/// Enables or disables CD music; disabling stops playback.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @param enabled Nonzero to enable.
void cd_set_enabled(Mixer& mixer, int32_t enabled) noexcept;

// Most-recently-used table of per-disc track kinds, persisted as the
// CDLISTS application setting.
inline constexpr std::size_t disc_cache_records = 20;
inline constexpr std::size_t disc_cache_track_types = 100;
inline constexpr const char* disc_cache_setting = "CDLISTS";

#pragma pack(push, 1)

// One disc's entry in the CDLISTS setting.
struct DiscCacheRecord {
    // Carried through the CDLISTS setting and never given a value of its
    // own: zeroed when the setting cannot be read, shifted with its record,
    // and inherited by a new disc's front record from the record that was at
    // the front before.
    uint8_t block_before_disc_id[0x20];
    uint8_t disc_id[4]; // little-endian
    uint8_t track_types[disc_cache_track_types];
};

struct DiscCache {
    DiscCacheRecord records[disc_cache_records];
};

#pragma pack(pop)

static_assert(sizeof(DiscCacheRecord) == 0x88);
static_assert(sizeof(DiscCache) == 0xaa0);

/// Loads the disc cache from the CDLISTS setting, zeroing it when unreadable.
///
/// @param[out] cache Cache to fill.
/// @param settings Application settings to read from.
void cd_load_disc_cache(DiscCache& cache, const AudioSettings& settings) noexcept;

/// Stores the current disc's track kinds in the front record and persists the cache.
///
/// @param[in,out] cache Disc cache; record 0 is overwritten.
/// @param mixer Mixer that owns the CD player.
/// @param settings Application settings to write to.
void cd_save_disc_cache(
    DiscCache& cache, const Mixer& mixer, const AudioSettings& settings
) noexcept;

/// Handles a disc insertion or removal.
///
/// Reopens the device, reapplies the music options from the game, moves the
/// disc's cache record to the front (or starts a new front record) and
/// restores its track kinds, then advances when bit 2 of Game.session_flags is
/// set and Game.mode is 6, and stops otherwise.
///
/// @param[in,out] mixer Mixer that owns the CD player.
/// @param[in,out] cache Most-recently-used disc cache.
/// @param game Game state supplying music_flags, cd_mode and the play gate.
/// @quirk A new front record takes the 16-track data-disc kinds whatever the
///        disc, and keeps the rest of its previous contents, block_before_disc_id
///        included.
void cd_handle_disc_change(Mixer& mixer, DiscCache& cache, const Game& game) noexcept;

} // namespace oa::audio
