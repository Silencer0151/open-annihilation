// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "audio_test_support.hpp"
#include "oa/audio/cd_music.hpp"
#include "oa/audio/music_disc.hpp"
#include "oa/audio/sdl_music.hpp"
#include "oa/test/game_data.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace oa::audio;
using audio_test::require;

namespace {

void write_tone(const std::filesystem::path& path, uint32_t frames) {
    std::vector<uint8_t> samples;
    for (uint32_t i = 0; i < frames; ++i)
        audio_test::put16(samples, static_cast<uint16_t>((i % 50) < 25 ? 3000 : -3000));
    const auto riff = audio_test::make_riff(22050, 16, 1, samples);
    std::ofstream(path, std::ios::binary)
        .write(
            reinterpret_cast<const char*>(riff.data()), static_cast<std::streamsize>(riff.size())
        );
}

// Pumps until `done` holds or the timeout (ms) passes.
template <typename Done>
bool wait_for(Done done, uint64_t timeout_ms) {
    const uint64_t start = SDL_GetTicks();
    while (SDL_GetTicks() - start < timeout_ms) {
        if (done())
            return true;
        SDL_Delay(5);
    }
    return done();
}

std::unique_ptr<Mixer> make_mixer(SdlMusicDevice* device, audio_test::FakeTimers& timers) {
    auto mixer = std::make_unique<Mixer>();
    mixer->music = sdl_music_device(device);
    mixer->timers = timers.table();
    mixer_init(*mixer);
    return mixer;
}

void generated_disc() {
    // Every music file, WAV included, plays through the decoder.
    if (!sdl_music_decoder_available())
        oa::test::skip_test("the generated music", "this build has no music decoder");
    const auto root = std::filesystem::temp_directory_path() / "oa-sdl-music-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    write_tone(root / "2.wav", 22050 / 2);
    write_tone(root / "3.wav", 22050 * 2);
    const MusicDisc disc = music_disc_scan(root);
    require(disc.track_count == 3, "generated disc has a data track and two audio tracks");

    SdlMusicDevice* device = sdl_music_device_create(disc);
    audio_test::FakeTimers timers;
    auto mixer = make_mixer(device, timers);
    require(
        mixer->aux_device == 0 && mixer->music_volume == max_device_volume,
        "music line found at full volume"
    );
    require(cd_open(*mixer), sdl_music_device_error(device));
    require(cd_track_count(*mixer) == 2 && mixer->cd.first_track_offset == 1, "data track skipped");
    cd_set_play_mode(*mixer, static_cast<int32_t>(CdPlayMode::sequential));
    mixer_set_music_volume(*mixer, 0x8000, false);

    // Track 1 plays disc track 2 through to the disc end, crossing into 3.
    require(cd_play_track(*mixer, 1), sdl_music_device_error(device));
    require(cd_is_playing(*mixer), "playing");
    int32_t device_track = 0;
    require(
        wait_for(
            [&] {
                mixer->music.current_track(mixer->music.context, &device_track);
                return device_track == 3;
            },
            5000
        ),
        "range continues into the next file"
    );

    require(cd_set_paused(*mixer, true) && !cd_is_playing(*mixer), "paused");
    require(!sdl_music_device_poll_complete(device), "no completion while paused");
    require(cd_set_paused(*mixer, false) && cd_is_playing(*mixer), "resumed");

    require(
        wait_for([&] { return sdl_music_device_poll_complete(device); }, 5000),
        "completion reported at the disc end"
    );
    require(!cd_is_playing(*mixer), "stopped at the end");
    cd_on_play_complete(*mixer, true);
    require(
        cd_is_playing(*mixer) && cd_current_track(*mixer) == 2,
        "completion advances sequential play"
    );
    require(cd_stop(*mixer) && !cd_is_playing(*mixer), "stop");
    require(!sdl_music_device_poll_complete(device), "a stopped range reports nothing");
    mixer_teardown(*mixer);
    sdl_music_device_destroy(device);
    std::filesystem::remove_all(root);
}

// The installed game's music folder: its sixteen tracks open and decode. An
// install that plays its music from the disc has no such folder.
void installed_disc(const std::filesystem::path& game_dir) {
    if (!sdl_music_decoder_available())
        oa::test::skip_test("the installed music", "this build has no music decoder");
    const auto directory = music_disc_directory(game_dir);
    const MusicDisc disc = music_disc_scan(directory);
    if (!music_disc_present(disc))
        oa::test::skip_test("the installed music", "the install has no music folder");
    require(disc.track_count == 17, "installed music: tracks 2..17");
    SdlMusicDevice* device = sdl_music_device_create(disc);
    audio_test::FakeTimers timers;
    auto mixer = make_mixer(device, timers);
    require(cd_open(*mixer), sdl_music_device_error(device));
    require(cd_track_count(*mixer) == 16, "sixteen playable tracks");
    for (int32_t track : {1, 16}) {
        require(cd_play_track(*mixer, track), sdl_music_device_error(device));
        require(
            wait_for([&] { return sdl_music_device_position(device) > 0.2; }, 5000),
            "installed track decodes"
        );
    }
    std::printf(
        "installed music: %s, %d tracks, id %08x\n",
        disc.directory.string().c_str(),
        disc.track_count,
        static_cast<unsigned>(disc.disc_id)
    );
    mixer_teardown(*mixer);
    sdl_music_device_destroy(device);
}

} // namespace

int main(int argc, char** argv) {
    const bool installed = oa::test::game_data_requested(argc, argv);
    const auto game_dir = installed ? oa::test::require_game_directory("the installed music")
                                    : std::filesystem::path{};
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    require(SDL_Init(0), "SDL init");
    if (installed)
        installed_disc(game_dir);
    else
        generated_disc();
    SDL_Quit();
    return 0;
}
