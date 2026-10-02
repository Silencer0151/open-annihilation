// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The install the network-play tests read: OA_GAME_DIR names the 3.1c install, whose base archives
// hold the game's own data, and reaches the tests through their
// environment. A test without the install it needs prints one line saying
// what it skipped and exits with oa::test::kSkipExitCode, which ctest reports
// as skipped.
#pragma once

#include "oa/formats/hpi.hpp"
#include "oa/test/game_data.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace oa::test::netgame {

// The base game's first archive, which carries its palettes.
inline constexpr const char* kBaseArchive = "totala1.hpi";

/// Tests whether an install holds every listed file.
///
/// @param root the install
/// @param files '/'-separated paths below it
/// @return true when each is a regular file
[[nodiscard]] inline bool
holds(const std::filesystem::path& root, std::initializer_list<const char*> files) {
    std::error_code error;
    for (const char* file : files)
        if (!std::filesystem::is_regular_file(root / file, error))
            return false;
    return true;
}

/// Reads one file from one archive of an install.
///
/// @param root the install
/// @param archive the archive's file name in the install
/// @param path '/'-separated path inside the archive
/// @return the bytes, or empty when the archive or the file is missing
[[nodiscard]] inline std::vector<uint8_t>
read_archived(const std::filesystem::path& root, const char* archive, const char* path) {
    try {
        return oa::HpiArchive(root / archive).read(path).value.value_or(std::vector<uint8_t>{});
    } catch (const std::exception&) {
        return {};
    }
}

// Bytes of the game's palette file: four per colour, 256 colours.
inline constexpr std::size_t kPaletteFileBytes = 1024;

/// Reads the base game's palette from an install's totala1.hpi.
///
/// Ends the test as failed when the archive lacks a whole palette.
///
/// @param root the install
/// @return the palette file's bytes, four per colour
[[nodiscard]] inline std::vector<uint8_t> base_palette(const std::filesystem::path& root) {
    auto bytes = read_archived(root, kBaseArchive, "palettes/palette.pal");
    if (bytes.size() < kPaletteFileBytes) {
        std::fprintf(
            stderr,
            "FAIL: %s holds no palettes/palette.pal\n",
            (root / kBaseArchive).string().c_str()
        );
        std::exit(1);
    }
    return bytes;
}

} // namespace oa::test::netgame
