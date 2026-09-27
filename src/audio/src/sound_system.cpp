// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/sound_system.hpp"

#include "oa/audio/cd_music.hpp"

#include <cstdint>
#include <utility>

namespace oa::audio {
namespace {

bool play_system(SoundSystem& sound, WavBlob wav, uint32_t flags) noexcept {
    return sound.system_sound.play != nullptr &&
           sound.system_sound.play(sound.system_sound.context, wav.data, wav.size, flags);
}

constexpr uint32_t play_once = system_sound_flag::async | system_sound_flag::memory;
constexpr uint32_t play_looping = play_once | system_sound_flag::loop;
constexpr uint32_t replay_looping = play_once | system_sound_flag::no_stop;

} // namespace

bool sound_init_devices(SoundSystem& sound, const AudioSettings& settings) noexcept {
    const auto setting = [&settings](const char* name) {
        return settings.read_int != nullptr ? settings.read_int(settings.context, name, 0) : 0;
    };
    if (setting(setting_no_direct_sound) != 0)
        sound.mixer_disabled = 1;
    if (setting(setting_use_windows_sound) != 0)
        sound.system_sound_only = 1;
    // The system-sound fallback is always available.
    if (sound.system_sound_only != 0)
        sound.mixer_disabled = 1;
    bool fatal = false;
    if (sound.mixer_disabled == 0 && sound.mixer != nullptr &&
        !mixer_open_device(*sound.mixer, primary_format)) {
        if (!mixer_no_driver(*sound.mixer))
            fatal = true;
        else
            sound.mixer_disabled = 1;
    }
    if (sound.mixer != nullptr)
        cd_open(*sound.mixer);
    if (sound.speech != nullptr)
        sound.speech->clear();
    return !fatal;
}

void sound_shutdown_devices(SoundSystem& sound) noexcept {
    if (sound.speech != nullptr)
        sound.speech->clear();
    if (sound.mixer != nullptr) {
        mixer_restore_volumes(*sound.mixer);
        mixer_teardown(*sound.mixer);
    }
}

void sound_force_system_sound(SoundSystem& sound) noexcept {
    sound.system_sound_only = 1;
    sound.mixer_disabled = 1;
}

void sound_release_sample(SoundSystem& sound, SampleId sample) noexcept {
    // Fallback-mode sounds are plain file images owned by their loader.
    if (sound.system_sound_only == 0 && sound.mixer != nullptr)
        mixer_release_sample(*sound.mixer, sample);
}

void sound_schedule_stream(
    SoundSystem& sound, const char* path, int32_t volume, uint32_t delay
) noexcept {
    if (sound.mixer_disabled == 0 && sound.mixer != nullptr)
        mixer_schedule_stream(*sound.mixer, path, volume, delay);
}

std::optional<audio::game_audio::UnitAnnouncement> sound_tick_speech(
    SoundSystem& sound,
    const audio::game_audio::UnitSoundCatalog& catalog,
    audio::game_audio::AnnouncementPresentationGates gates,
    uint16_t rng15,
    uint32_t current_tick
) {
    std::optional<audio::game_audio::UnitAnnouncement> result;
    if (sound.speech != nullptr) {
        gates.novelty_voice = sound.novelty_voice != 0;
        result = sound.speech->pump(catalog, gates, rng15, current_tick);
    }
    if (sound.system_sound_only != 0)
        system_sound_replay(sound);
    return result;
}

void sound_stop_all(SoundSystem& sound) noexcept {
    if (sound.mixer_disabled == 0 && sound.mixer != nullptr)
        mixer_stop_voices(*sound.mixer);
    if (sound.system_sound_only != 0)
        system_sound_stop(sound);
}

void sound_forget_unit(SoundSystem& sound, uint16_t unit_index) noexcept {
    if (sound.speech != nullptr)
        sound.speech->remove_unit(unit_index);
}

void sound_toggle_novelty_voice(SoundSystem& sound) noexcept {
    sound.novelty_voice = sound.novelty_voice == 0 ? 1 : 0;
}

void sound_stop_music(SoundSystem& sound) noexcept {
    if (sound.mixer != nullptr)
        cd_stop(*sound.mixer);
}

void system_sound_replay(SoundSystem& sound) noexcept {
    if (sound.looping_sound.data != nullptr)
        play_system(sound, sound.looping_sound, replay_looping);
}

void system_sound_stop(SoundSystem& sound) noexcept {
    if (sound.system_sound.purge != nullptr)
        sound.system_sound.purge(sound.system_sound.context);
    std::vector<uint8_t>().swap(sound.resolved_sound);
    sound.looping_sound = WavBlob{};
}

bool system_sound_play(SoundSystem& sound, WavBlob wav) noexcept {
    return play_system(sound, wav, play_once);
}

bool system_sound_play_looping(SoundSystem& sound, WavBlob wav) noexcept {
    sound.looping_sound = wav;
    return play_system(sound, wav, play_looping);
}

bool system_sound_play_file(SoundSystem& sound, const char* path) noexcept {
    std::vector<uint8_t> bytes;
    if (path == nullptr || sound.files.load == nullptr ||
        !sound.files.load(sound.files.context, path, &bytes) || bytes.empty() ||
        bytes.size() > 0xffffffffU)
        return false;
    const WavBlob wav{bytes.data(), static_cast<uint32_t>(bytes.size())};
    const bool played = play_system(sound, wav, play_once);
    sound.resolved_sound = std::move(bytes);
    return played;
}

} // namespace oa::audio
