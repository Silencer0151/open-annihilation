// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/audio/mixer.hpp"
#include "oa/audio/music_disc.hpp"

namespace oa::audio {

// Music-file backend for the CD-audio boundary. Each disc track is a file
// of the MusicDisc, decoded by MusicDecoder on the sound output's thread
// and played through a stream of its own (oa/audio/sound_output.hpp), so
// pausing it leaves effects alone. The CD-audio mixer line maps to that
// stream's gain.
struct SdlMusicDevice;

/// Creates a music device over a scanned disc.
///
/// The device opens SDL audio only on open().
///
/// @param disc Music disc whose track files are played; moved into the device.
/// @return The device, or null when out of memory.
[[nodiscard]] SdlMusicDevice* sdl_music_device_create(MusicDisc disc);

/// Closes and frees a music device.
///
/// @param device Device to free; null is ignored.
void sdl_music_device_destroy(SdlMusicDevice* device) noexcept;

/// Returns the CD-audio boundary table bound to a device.
///
/// @param device Device that becomes the table's context.
/// @return Boundary callbacks for Mixer::music.
[[nodiscard]] MusicDevice sdl_music_device(SdlMusicDevice* device) noexcept;

/// Polls from the main thread for the end of a play or resume range.
///
/// A range stopped or replaced before its end reports nothing.
///
/// @param device Device to poll; null reports nothing.
/// @return True once when the range has been played out, which the caller
///         reports through cd_on_play_complete().
[[nodiscard]] bool sdl_music_device_poll_complete(SdlMusicDevice* device) noexcept;

/// Reports whether this build has a compressed-audio decoder.
///
/// Every build has one: MP3, Ogg Vorbis and FLAC files play as WAVE files do.
///
/// @return True.
[[nodiscard]] bool sdl_music_decoder_available() noexcept;

/// Returns the last open or decode failure, for diagnostics.
///
/// @param device Device to query; null yields an empty string.
/// @return Error text, empty when none.
[[nodiscard]] const char* sdl_music_device_error(const SdlMusicDevice* device) noexcept;

/// Returns how much of the current track has been decoded, for diagnostics.
///
/// @param device Device to query; null yields 0.
/// @return Decoded time in seconds.
[[nodiscard]] double sdl_music_device_position(SdlMusicDevice* device) noexcept;

} // namespace oa::audio
