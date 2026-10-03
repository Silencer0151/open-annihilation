// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Finds an installed mod for a test that reads a mod's own files: the game
// folder OA_MOD_GAME_DIR names, with a mod installed over it, and the
// reference profile in OA_MOD_PROFILES_DIR that describes that mod, the one
// whose revision archive the folder holds. Without both a test prints one
// line saying what it skipped and why, and exits with kModSkipExitCode, which
// ctest reports as skipped. Link oa-data-mod-profile and oa-formats-hpi.
#pragma once

#include "oa/data/mod_profile.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/platform/system.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::test {

// ctest's SKIP_RETURN_CODE for a test that finds no installed mod.
inline constexpr int kModSkipExitCode = 77;
// The environment variable naming a game folder with a mod installed over it.
inline constexpr const char* kModGameDirVariable = "OA_MOD_GAME_DIR";
// The environment variable naming the folder of reference mod profiles.
inline constexpr const char* kModProfilesDirVariable = "OA_MOD_PROFILES_DIR";

/// An installed mod and the profile that describes it.
struct ModInstall {
    std::filesystem::path folder;       ///< the game folder the mod is installed over
    std::filesystem::path profile_file; ///< the profile's oamod.yaml
    data::mod_profile::ModProfile profile;
};

/// Ends the test as skipped, printing "skipped <what>: <why>".
///
/// @param what the cases that did not run
/// @param why the reason
[[noreturn]] inline void skip_mod_test(std::string_view what, std::string_view why) {
    std::printf(
        "skipped %.*s: %.*s\n",
        static_cast<int>(what.size()),
        what.data(),
        static_cast<int>(why.size()),
        why.data()
    );
    std::fflush(stdout);
    std::exit(kModSkipExitCode);
}

/// Returns a folder an environment variable names, or nothing.
///
/// @param variable the variable
/// @return the folder, when it is set and is a directory
[[nodiscard]] inline std::optional<std::filesystem::path> folder_from(const char* variable) {
    const auto value = oa::platform::environment_value(variable);
    if (!value || value->empty() || !std::filesystem::is_directory(*value))
        return std::nullopt;
    return std::filesystem::path{*value};
}

/// Finds a file in a folder by name, ignoring ASCII case.
///
/// @param folder the folder
/// @param name the file name
/// @return the file's path, or nothing
[[nodiscard]] inline std::optional<std::filesystem::path>
file_in(const std::filesystem::path& folder, std::string_view name) {
    const auto lowered = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return text;
    };
    const std::string want = lowered(std::string{name});
    for (const auto& entry : std::filesystem::directory_iterator{folder})
        if (lowered(entry.path().filename().string()) == want)
            return entry.path();
    return std::nullopt;
}

/// Finds the installed mod and its profile, or ends the test as skipped.
///
/// Every profile in the reference folder is resolved, accepting hacks this
/// engine does not implement yet; the one whose revision archive is in the
/// game folder describes the installed mod.
///
/// @param what the cases that need the mod, for the skip line
/// @return the installed mod
[[nodiscard]] inline ModInstall require_mod_install(std::string_view what) {
    const auto folder = folder_from(kModGameDirVariable);
    if (!folder)
        skip_mod_test(what, "OA_MOD_GAME_DIR names no folder");
    const auto profiles = folder_from(kModProfilesDirVariable);
    if (!profiles)
        skip_mod_test(what, "OA_MOD_PROFILES_DIR names no folder");
    for (const auto& directory : std::filesystem::directory_iterator{*profiles}) {
        if (!directory.is_directory())
            continue;
        const auto file = file_in(directory.path(), "oamod.yaml");
        if (!file)
            continue;
        std::ifstream in{*file, std::ios::binary};
        const std::string text{std::istreambuf_iterator<char>{in}, {}};
        data::mod_profile::ResolveOptions options{};
        options.accept_unimplemented_hacks = true;
        const auto result = data::mod_profile::resolve_profile(
            {reinterpret_cast<const uint8_t*>(text.data()), text.size()}, file->string(), options
        );
        if (!result.resolution)
            continue;
        const auto& profile = result.resolution->profile;
        if (profile.layout.revision_archive.empty() ||
            !file_in(*folder, profile.layout.revision_archive))
            continue;
        return {*folder, *file, profile};
    }
    skip_mod_test(what, "no reference profile describes the mod in OA_MOD_GAME_DIR");
}

/// Mounts an installed mod's archives: its revision archive first, then the
/// game's own as the game finds them.
///
/// @param install the installed mod
/// @return the store
[[nodiscard]] inline AssetStore open_mod_assets(const ModInstall& install) {
    AssetStore assets(install.folder);
    if (const auto revision = file_in(install.folder, install.profile.layout.revision_archive)) {
        std::string error;
        if (!assets.try_mount(*revision, &error))
            std::cerr << "cannot mount " << revision->string() << ": " << error << '\n';
    }
    const std::filesystem::path disc_roots[]{install.folder};
    for (const auto& outcome : assets.discover("31", disc_roots))
        if (!outcome.mounted && !outcome.already_mounted)
            std::cerr << "skipping archive " << outcome.path.string() << ": " << outcome.error
                      << '\n';
    return assets;
}

} // namespace oa::test
