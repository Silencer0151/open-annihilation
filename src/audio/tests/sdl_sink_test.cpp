// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "audio_test_support.hpp"
#include "oa/audio/mixer.hpp"
#include "oa/audio/sdl_audio.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

using namespace oa::audio;
using audio_test::require;

int main() {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    require(SDL_Init(SDL_INIT_AUDIO), "SDL audio init");
    SdlAudioDevice* device = sdl_audio_device_create();
    auto mixer = std::make_unique<Mixer>();
    mixer->sink = sdl_audio_sink(device);
    mixer_init(*mixer);
    require(mixer_open_device(*mixer, PcmFormat{11025, 16, 2}), "open");

    std::vector<uint8_t> pcm(2048, 0x80);
    auto cursor = wave_cursor(pcm.data(), static_cast<uint32_t>(pcm.size()));
    const SampleId sample = mixer_create_sample(*mixer, cursor, 2048, PcmFormat{11025, 8, 1});
    require(sample != no_sample, "sample");
    require(mixer_play_sample(*mixer, sample, volume_near, nullptr), "play");
    require(mixer_play_sample(*mixer, sample, volume_far, nullptr), "duplicate play");
    require(mixer->voice_count == 2, "two voices");
    require(!mixer_set_wave_out_volume(*mixer, 0x8000), "master volume");
    mixer_stop_voices(*mixer);
    mixer_teardown(*mixer);
    sdl_audio_device_destroy(device);
    SDL_Quit();
    return 0;
}
