// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Opens the installed game's archives for a game-data test, so tests read
// the packed files an ordinary installation ships rather than an unpacked
// copy. Link oa-formats-hpi beside oa-test-game-data.
#pragma once

#include "oa/formats/hpi.hpp"
#include "oa/test/game_data.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::test {

// The game revision in the name of its patch archive (rev31.gp3).
inline constexpr std::string_view kGameRevision = "31";

/// Mounts an installed game's archives as oa-game does.
///
/// Runs the discovery scan with the installation also standing in for the
/// CD-ROM root, so archives past the *.HPI limit mount after the rest. An
/// archive that fails to open is reported on stderr and left out.
///
/// @param folder the installation
/// @return the store; loose files resolve below the installation first
[[nodiscard]] inline oa::AssetStore open_game_assets(const std::filesystem::path& folder) {
    oa::AssetStore assets(folder);
    const std::filesystem::path disc_roots[]{folder};
    for (const auto& outcome : assets.discover(kGameRevision, disc_roots))
        if (!outcome.mounted && !outcome.already_mounted)
            std::cerr << "skipping archive " << outcome.path.string() << ": " << outcome.error
                      << '\n';
    return assets;
}

/// Mounts the installed game OA_GAME_DIR names, or ends the test without one.
///
/// Without one the test ends as require_game_directory() ends it: skipped,
/// or failed when the build requires the installed game.
///
/// @param what the cases that need the installation, for the skip line
/// @return the store over that installation
[[nodiscard]] inline oa::AssetStore require_game_assets(std::string_view what) {
    return open_game_assets(require_game_directory(what));
}

/// Reads a whole resource through a store.
///
/// @param assets mounted store
/// @param resource '/'-separated path, matched ignoring case
/// @return the bytes, or empty when nothing provides the resource or it is empty
[[nodiscard]] inline std::vector<uint8_t>
read_game_file(const oa::AssetStore& assets, std::string_view resource) {
    auto contents = assets.load_file_contents(resource);
    return contents ? std::move(*contents) : std::vector<uint8_t>{};
}

} // namespace oa::test
