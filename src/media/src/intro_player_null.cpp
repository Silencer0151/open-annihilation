// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The intro player for builds without SDL3: it runs the player's SMK2 check
// on each movie, then skips it with one logged line, so the game carries on
// to the next screen as after a finished movie.

#include "oa/media/intro_player.hpp"
#include "oa/formats/smacker.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace oa::media {

struct IntroPlayer::Impl {
    std::filesystem::path path;
};

IntroPlayer::IntroPlayer(std::unique_ptr<Impl> implementation)
    : implementation_(std::move(implementation)) {
}

IntroPlayer::IntroPlayer(IntroPlayer&&) noexcept = default;
IntroPlayer& IntroPlayer::operator=(IntroPlayer&&) noexcept = default;
IntroPlayer::~IntroPlayer() = default;

OpenPlayerResult IntroPlayer::open(const std::filesystem::path& path, const PlayerLimits& limits) {
    if (path.empty())
        return OpenPlayerResult{std::nullopt, "intro movie path is empty"};
    const auto container = formats::smacker::SmackerReader::open(path);
    if (!container)
        return OpenPlayerResult{std::nullopt, "SMK2 preflight: " + container.error};
    const auto& header = container.reader->header();
    if (static_cast<uint64_t>(header.width) * header.height > limits.max_video_pixels)
        return OpenPlayerResult{std::nullopt, "intro video dimensions exceed bounds"};
    auto implementation = std::make_unique<Impl>();
    implementation->path = path;
    IntroPlayer player(std::move(implementation));
    player.info_.width = static_cast<int>(header.width);
    player.info_.height = static_cast<int>(header.height);
    player.info_.frame_count = header.frame_count;
    player.info_.frame_rate = header.frame_rate_hz();
    return OpenPlayerResult{std::optional<IntroPlayer>(std::move(player)), {}};
}

PlaybackResult IntroPlayer::play(const PlaybackOptions&) {
    PlaybackResult playback;
    if (implementation_ == nullptr) {
        playback.error = "intro player is not initialized";
        return playback;
    }
    std::fprintf(
        stderr,
        "this build plays no movies; skipping %s\n",
        implementation_->path.filename().string().c_str()
    );
    playback.skipped = true;
    return playback;
}

} // namespace oa::media
