// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/audio/sound_output.hpp"

#include <memory>

namespace oa::audio {

/// Creates the output that plays through SDL's audio streams.
///
/// start() and stop() start and stop SDL's audio subsystem, which counts
/// them as SDL_Init does; each stream is an SDL audio stream on its own
/// logical playback device, so SDL converts and mixes them.
///
/// @return the output; null in a build without SDL
[[nodiscard]] std::unique_ptr<SoundOutput> sdl_sound_output_create();

/// Creates the output that mixes its streams itself and plays them on the
/// Windows wave-out device.
///
/// @return the output; null on a system without that device
[[nodiscard]] std::unique_ptr<SoundOutput> wave_out_sound_output_create();

} // namespace oa::audio
