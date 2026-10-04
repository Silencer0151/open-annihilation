// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/audio/sound_output.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace oa::audio {

/// The sound outputs a process can play through.
enum class SoundOutputKind : uint8_t {
    sdl,      ///< SDL's (sdl_sound_output_create)
    wave_out, ///< the wave-out mixer (wave_out_sound_output_create)
};

/// The environment variable that chooses the sound output.
inline constexpr const char* sound_output_variable = "OA_SOUND_OUTPUT";

/// Chooses the sound output a process plays through first.
///
/// The environment variable sound_output_variable chooses: "waveout" the
/// wave-out mixer, "sdl" SDL's. Without either, Windows before Vista plays
/// through the wave-out mixer, whose device wakes its thread only as each
/// buffer plays out, where SDL's output wakes its thread every millisecond
/// to ask how far the device has played; every other system plays through
/// SDL's.
///
/// @param variable the environment variable's value; empty when it is not set
/// @param windows_before_vista true on Windows before Vista
/// @return the output to try first; the other is the fallback
[[nodiscard]] SoundOutputKind
choose_sound_output(std::optional<std::string_view> variable, bool windows_before_vista) noexcept;

/// Creates the output that plays through SDL.
///
/// start() and stop() start and stop SDL's audio subsystem, which counts
/// them as SDL_Init does. The output's SoftwareMixer mixes every stream into
/// one SDL audio stream of 16-bit stereo at mixer_output_rate on the default
/// playback device, opened by the first start() or stream; it closes with
/// the last stop() once no stream is open.
///
/// @return the output; null in a build without SDL
[[nodiscard]] std::unique_ptr<SoundOutput> sdl_sound_output_create();

/// Creates the output that mixes its streams itself and plays them on the
/// Windows wave-out device.
///
/// @return the output; null on a system without that device
[[nodiscard]] std::unique_ptr<SoundOutput> wave_out_sound_output_create();

} // namespace oa::audio
