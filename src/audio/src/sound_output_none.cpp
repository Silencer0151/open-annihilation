// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// SDL's output in a build without SDL, compiled only where its own source
// is not.

#include "oa/audio/sound_output_backends.hpp"

namespace oa::audio {

#if !OA_AUDIO_HAVE_SDL_OUTPUT
std::unique_ptr<SoundOutput> sdl_sound_output_create() {
    return nullptr;
}
#endif

} // namespace oa::audio
