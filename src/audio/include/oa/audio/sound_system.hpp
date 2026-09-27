// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/audio/mixer.hpp"
#include "oa/audio/unit_announcements.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace oa::audio {

// Flags of the system-sound fallback player.
namespace system_sound_flag {
inline constexpr uint32_t async = 0x1;
inline constexpr uint32_t memory = 0x4;
inline constexpr uint32_t loop = 0x8;
inline constexpr uint32_t no_stop = 0x10;
} // namespace system_sound_flag

// Plays a whole in-memory WAV file without the mixer; used when the
// UseWindowsSound setting selects the fallback path.
struct SystemSound {
    void* context{};
    bool (*play)(void* context, const uint8_t* wav, uint32_t bytes, uint32_t flags){};
    void (*purge)(void* context){};
};

struct WavBlob {
    const uint8_t* data{};
    uint32_t size{};
};

inline constexpr PcmFormat primary_format{11025, 16, 2};
inline constexpr const char* setting_no_direct_sound = "NoDirectSound";
inline constexpr const char* setting_use_windows_sound = "UseWindowsSound";

struct SoundSystem {
    Mixer* mixer{};
    audio::game_audio::AnnouncementQueue* speech{};
    SystemSound system_sound{};
    AudioFiles files{};
    int32_t mixer_disabled{};    // set by NoDirectSound, a missing driver, or the fallback
    int32_t system_sound_only{}; // UseWindowsSound
    int32_t alternate_route{};   // set around a looping registry sound
    int32_t novelty_voice{};
    WavBlob looping_sound{};             // replayed by the speech tick in fallback mode
    std::vector<uint8_t> resolved_sound; // keeps the last file-played WAV alive
};

/// Applies the sound settings, opens the mixer, opens CD audio, and resets the speech queue.
///
/// NoDirectSound disables the mixer and UseWindowsSound selects the
/// system-sound fallback; otherwise the mixer opens at 11025 Hz 16-bit
/// stereo. A missing driver disables the mixer without failing.
///
/// @param[in,out] sound Sound system to start.
/// @param settings Application settings to read.
/// @return False when the mixer fails for a reason other than a missing
///         driver, which the game reports as a fatal error.
bool sound_init_devices(SoundSystem& sound, const AudioSettings& settings) noexcept;

/// Clears the speech queue, restores the saved device volumes, and tears the mixer down.
///
/// @param[in,out] sound Sound system to stop.
void sound_shutdown_devices(SoundSystem& sound) noexcept;

/// Switches to the system-sound fallback and disables the mixer.
///
/// @param[in,out] sound Sound system to switch.
void sound_force_system_sound(SoundSystem& sound) noexcept;

/// Releases a mixer sample; ignored in fallback mode, where sounds are plain file images owned by their loader.
///
/// @param[in,out] sound Sound system that owns the mixer.
/// @param sample Sample to release.
void sound_release_sample(SoundSystem& sound, SampleId sample) noexcept;

/// Schedules a file to stream after a delay while the mixer is enabled.
///
/// @param[in,out] sound Sound system that owns the mixer.
/// @param path Sound path resolved through the game's archive search order.
/// @param volume Stream volume in hundredths of a decibel (0 is full scale).
/// @param delay Delay in timer ticks.
void sound_schedule_stream(
    SoundSystem& sound, const char* path, int32_t volume, uint32_t delay
) noexcept;

/// Pumps one speech record and, in fallback mode, restarts the looping sound.
///
/// @param[in,out] sound Sound system that owns the speech queue.
/// @param catalog Unit sound catalog used to resolve the record.
/// @param gates Presentation gates; the novelty-voice gate is replaced by the sound system's setting.
/// @param rng15 15-bit random value (0..32767) used to pick among variants.
/// @param current_tick Current game tick.
/// @return The announcement to present, if one is due.
std::optional<audio::game_audio::UnitAnnouncement> sound_tick_speech(
    SoundSystem& sound,
    const audio::game_audio::UnitSoundCatalog& catalog,
    audio::game_audio::AnnouncementPresentationGates gates,
    uint16_t rng15,
    uint32_t current_tick
);

/// Stops every mixer voice and, in fallback mode, the system sound.
///
/// @param[in,out] sound Sound system to silence.
void sound_stop_all(SoundSystem& sound) noexcept;

/// Removes a unit's queued speech.
///
/// @param[in,out] sound Sound system that owns the speech queue.
/// @param unit_index Index of the unit whose records are dropped.
void sound_forget_unit(SoundSystem& sound, uint16_t unit_index) noexcept;

/// Toggles the novelty voice used for unit speech.
///
/// @param[in,out] sound Sound system to change.
void sound_toggle_novelty_voice(SoundSystem& sound) noexcept;

/// Stops CD music.
///
/// @param[in,out] sound Sound system that owns the mixer.
void sound_stop_music(SoundSystem& sound) noexcept;

/// Replays the remembered looping system sound without interrupting one that is playing.
///
/// @param[in,out] sound Sound system in fallback mode.
void system_sound_replay(SoundSystem& sound) noexcept;

/// Purges the system sound and forgets the looping and file-played sounds.
///
/// @param[in,out] sound Sound system in fallback mode.
void system_sound_stop(SoundSystem& sound) noexcept;

/// Plays an in-memory WAV file once through the system-sound fallback.
///
/// @param[in,out] sound Sound system in fallback mode.
/// @param wav Whole WAV file; must stay alive while it plays.
/// @return True when the platform accepted the sound.
bool system_sound_play(SoundSystem& sound, WavBlob wav) noexcept;

/// Remembers the sound for replay and plays it looping.
///
/// @param[in,out] sound Sound system in fallback mode.
/// @param wav Whole WAV file; must stay alive while it is remembered.
/// @return True when the platform accepted the sound.
bool system_sound_play_looping(SoundSystem& sound, WavBlob wav) noexcept;

/// Loads a file and plays it once, keeping it alive in place of the previous one.
///
/// @param[in,out] sound Sound system in fallback mode.
/// @param path Sound path resolved through the game's archive search order.
/// @return True when the file loaded and the platform accepted it.
bool system_sound_play_file(SoundSystem& sound, const char* path) noexcept;

} // namespace oa::audio
