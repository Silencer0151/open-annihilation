// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/media/intro_player.hpp"

#include <SDL3/SDL_main.h>

#include <charconv>
#include <cstddef>
#include <iostream>
#include <string_view>

namespace {

void usage(const char* program) {
    std::cerr << "usage: " << program
              << " INPUT.zrb [--frames N] [--headless-check] [--mute]"
                 " [--snapshot PATH.ppm]\n";
}

bool parse_count(std::string_view text, std::size_t& result) {
    if (text.empty())
        return false;
    std::size_t parsed = 0;
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto conversion = std::from_chars(begin, end, parsed);
    if (conversion.ec != std::errc{} || conversion.ptr != end)
        return false;
    result = parsed;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }
    std::filesystem::path input;
    oa::media::PlaybackOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--headless-check") {
            options.headless_check = true;
        } else if (argument == "--mute") {
            options.play_audio = false;
        } else if (argument == "--snapshot" && i + 1 < argc) {
            options.snapshot_path = argv[++i];
        } else if (argument == "--frames" && i + 1 < argc) {
            if (!parse_count(argv[++i], options.frame_limit)) {
                std::cerr << "invalid --frames value\n";
                return 2;
            }
        } else if (input.empty() && argument.rfind("--", 0) != 0) {
            input = argv[i];
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (input.empty()) {
        usage(argv[0]);
        return 2;
    }

    auto opened = oa::media::IntroPlayer::open(input);
    if (!opened) {
        std::cerr << "intro open failed: " << opened.error << '\n';
        return 1;
    }
    const auto& info = opened.player->info();
    std::cout << "intro " << info.width << 'x' << info.height << " fps=" << info.frame_rate
              << " frames=" << info.frame_count << " audio=" << (info.has_audio ? "yes" : "no")
              << '\n';
    const auto playback = opened.player->play(options);
    if (playback.skipped)
        std::cout << "intro skipped\n";
    std::cout << "decoded_frames=" << playback.decoded_frames
              << " decoded_audio_bytes=" << playback.decoded_audio_bytes << '\n';
    if (!playback.ok()) {
        std::cerr << "intro playback failed: " << playback.error << '\n';
        return 1;
    }
    return 0;
}
