// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The WAV player's one stream on SDL's dummy device, which plays in real
// time: it waits out its delay, plays once, stops at once, and a new stream
// takes the place of the one playing; stop_all, which silences every
// effect, the loop and the stream at once; and the effects' voice policy.
#include "audio_test_support.hpp"
#include "oa/audio/sdl_audio.hpp"
#include "oa/formats/hpi.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using oa::audio::game_audio::SdlWavPlayer;
using audio_test::require;

namespace {

constexpr uint32_t kRate = 22050;
constexpr uint32_t kHeaderBytes = 8; // a RIFF chunk's tag and size

// Writes a 16-bit mono square wave of `milliseconds`.
void write_tone(const std::filesystem::path& path, uint32_t milliseconds) {
    std::vector<uint8_t> samples;
    for (uint32_t i = 0; i < kRate * milliseconds / 1000; ++i)
        audio_test::put16(samples, static_cast<uint16_t>((i % 50) < 25 ? 3000 : -3000));
    auto riff = audio_test::make_riff(kRate, 16, 1, samples);
    const auto riff_size = static_cast<uint32_t>(riff.size()) - kHeaderBytes;
    for (uint32_t byte = 0; byte < 4; ++byte)
        riff[4 + byte] = static_cast<uint8_t>(riff_size >> (8 * byte));
    std::ofstream(path, std::ios::binary)
        .write(
            reinterpret_cast<const char*>(riff.data()), static_cast<std::streamsize>(riff.size())
        );
}

// Waits until `done` holds or `timeout_ms` passes.
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

} // namespace

int main() {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    require(SDL_Init(SDL_INIT_AUDIO), "SDL audio init");
    const auto root = std::filesystem::temp_directory_path() / "oa-sdl-player-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "sounds");
    write_tone(root / "sounds" / "long.wav", 5000);
    write_tone(root / "sounds" / "short.wav", 100);
    for (int other = 0; other < 6; ++other)
        write_tone(root / "sounds" / ("other" + std::to_string(other) + ".wav"), 5000);
    {
        const oa::AssetStore assets(root);
        SdlWavPlayer player(assets);
        std::string error;
        require(!player.stream_busy(), "no stream plays before one starts");

        require(player.play_stream("sounds/long.wav", 0, error), "a stream starts");
        require(player.stream_busy(), "a started stream plays");
        player.stop_stream();
        require(!player.stream_busy(), "a stopped stream is silent at once");

        require(player.play_stream("sounds/short.wav", 0, error), "a short stream starts");
        require(
            wait_for([&] { return !player.stream_busy(); }, 3000), "a stream plays once and ends"
        );

        require(player.play_stream("sounds/short.wav", 1000, error), "a delayed stream starts");
        SDL_Delay(500);
        require(player.stream_busy(), "a stream waiting out its delay is busy");
        player.stop_stream();
        require(!player.stream_busy(), "a stream stopped during its delay never plays");

        // The long sound is replaced: the short one plays out in its place.
        require(player.play_stream("sounds/long.wav", 0, error), "the long stream starts");
        require(player.play_stream("sounds/short.wav", 0, error), "the short stream replaces it");
        require(
            wait_for([&] { return !player.stream_busy(); }, 3000),
            "a new stream takes the place of the one playing"
        );

        // The stream is not the looping sound.
        require(player.play_stream("sounds/long.wav", 0, error), "the stream starts again");
        player.stop_loop();
        require(player.stream_busy(), "stopping the looping sound leaves the stream playing");
        player.collect_finished();
        require(player.stream_busy(), "a playing stream is not collected");
        player.stop_stream();

        require(
            !player.play_stream("sounds/missing.wav", 0, error) && !error.empty(),
            "a missing sound starts no stream and says why"
        );
        require(!player.stream_busy(), "a failed stream is not busy");

        // stop_all silences an effect, the loop and the stream together.
        require(!player.playing(), "nothing plays before the effects start");
        require(player.play_resource("sounds/long.wav", error), "an effect starts");
        require(player.playing(), "a started effect plays");
        player.stop_all();
        require(!player.playing(), "stop_all ends a playing effect");
        require(player.start_loop_resource("sounds/short.wav", error), "a loop starts");
        require(player.play_stream("sounds/long.wav", 0, error), "a stream starts beside it");
        require(player.play_resource("sounds/long.wav", error), "an effect starts beside them");
        require(player.playing() && player.stream_busy(), "the effect, loop and stream play");
        player.stop_all();
        require(!player.playing(), "stop_all ends the effect and the loop");
        require(!player.stream_busy(), "stop_all ends the stream");
        player.stop_all();
        require(!player.playing(), "stop_all with nothing playing changes nothing");
        // The player plays on afterwards; the sounds outlast the checks.
        require(player.play_resource("sounds/long.wav", error), "an effect starts after stop_all");
        require(player.playing(), "an effect after stop_all plays");
        require(player.play_stream("sounds/long.wav", 0, error), "a stream starts after stop_all");
        require(player.stream_busy(), "a stream after stop_all plays");
        player.stop_all();
        require(player.effect_voices() == 0, "stop_all frees every voice");

        // One sound plays on four voices at most; a fifth start restarts one,
        // which then holds two voices.
        for (int start = 1; start <= 4; ++start) {
            require(player.play_resource("sounds/long.wav", error), "a sound starts again");
            require(player.effect_voices() == start, "each start of a sound takes a voice");
        }
        require(player.play_resource("sounds/long.wav", error), "a fifth start restarts one");
        require(player.effect_voices() == 5, "a restarted sound holds two voices");
        // Eight voices at most: further sounds stop the oldest voices.
        for (int other = 0; other < 6; ++other)
            require(
                player.play_resource("sounds/other" + std::to_string(other) + ".wav", error),
                "another sound starts"
            );
        require(player.effect_voices() == 8, "no more than eight voices play");
        require(player.playing(), "the newest sounds play on");
        player.stop_all();
        require(player.effect_voices() == 0 && !player.playing(), "stop_all ends them all");
    }
    std::filesystem::remove_all(root);
    SDL_Quit();
    return 0;
}
