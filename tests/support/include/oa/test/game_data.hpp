// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Finds the installed game that a game-data test reads: the folder the
// OA_GAME_DIR environment variable names, an ordinary Total Annihilation 3.1c
// installation holding totala1.hpi. Without one a test prints a single line
// saying what it skipped and why, and exits with kSkipExitCode, which ctest
// reports as skipped; when OA_REQUIRE_GAME_DATA is set in its environment it
// fails instead (see cmake/OaGameData.cmake).
#pragma once

#ifndef OA_GAME_DATA_SKIP_CODE
#error "link oa-test-game-data, which defines OA_GAME_DATA_SKIP_CODE"
#endif

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace oa::test {

// ctest's SKIP_RETURN_CODE for a skipped game-data test.
inline constexpr int kSkipExitCode = OA_GAME_DATA_SKIP_CODE;
// Exit code of a game-data test that fails.
inline constexpr int kFailExitCode = 1;
// The environment variable naming the installed game.
inline constexpr const char* kGameDirVariable = "OA_GAME_DIR";
// The environment variable, set by a build configured with
// OA_REQUIRE_GAME_DATA, that makes a missing installed game a failure.
inline constexpr const char* kRequireGameDataVariable = "OA_REQUIRE_GAME_DATA";
// The command-line switch that runs a test program's game-data cases instead
// of its self-contained ones.
inline constexpr std::string_view kGameDataSwitch = "--data";

/// Ends the test as skipped.
///
/// Prints "skipped <what>: <why>" on one line and exits with kSkipExitCode.
///
/// @param what the cases that did not run
/// @param why the reason, naming what was missing
[[noreturn]] inline void skip_test(std::string_view what, std::string_view why) {
    std::printf(
        "skipped %.*s: %.*s\n",
        static_cast<int>(what.size()),
        what.data(),
        static_cast<int>(why.size()),
        why.data()
    );
    std::fflush(stdout);
    std::exit(kSkipExitCode);
}

/// Tests whether the build requires the installed game.
///
/// @return true when OA_REQUIRE_GAME_DATA is set to anything but empty or "0"
[[nodiscard]] inline bool game_data_required() {
    const char* value = std::getenv(kRequireGameDataVariable);
    return value != nullptr && *value != '\0' && std::string_view(value) != "0";
}

/// Ends the test for want of the installed game.
///
/// Skips as skip_test() does; when the build requires the installed game it
/// prints "FAIL <what>: <why>" on one line and exits with kFailExitCode.
///
/// @param what the cases that did not run
/// @param why the reason, naming what was missing
[[noreturn]] inline void missing_game_directory(std::string_view what, std::string_view why) {
    if (!game_data_required())
        skip_test(what, why);
    std::printf(
        "FAIL %.*s: %.*s (OA_REQUIRE_GAME_DATA is set)\n",
        static_cast<int>(what.size()),
        what.data(),
        static_cast<int>(why.size()),
        why.data()
    );
    std::fflush(stdout);
    std::exit(kFailExitCode);
}

/// Returns the installed game OA_GAME_DIR names.
///
/// @return the folder, or an empty path when the variable is unset, empty or
///     names no directory
[[nodiscard]] inline std::filesystem::path game_directory() {
    const char* value = std::getenv(kGameDirVariable);
    if (value == nullptr || *value == '\0')
        return {};
    std::filesystem::path folder(value);
    std::error_code error;
    return std::filesystem::is_directory(folder, error) ? folder : std::filesystem::path{};
}

/// Returns the installed game, or ends the test without one.
///
/// Without one the test ends through missing_game_directory(): skipped, or
/// failed when the build requires the installed game.
///
/// @param what the cases that need the installation, for the skip line
/// @return the folder OA_GAME_DIR names
[[nodiscard]] inline std::filesystem::path require_game_directory(std::string_view what) {
    const char* value = std::getenv(kGameDirVariable);
    if (value == nullptr || *value == '\0')
        missing_game_directory(
            what,
            "OA_GAME_DIR is not set; set it to the Total Annihilation folder holding totala1.hpi"
        );
    auto folder = game_directory();
    if (folder.empty())
        missing_game_directory(what, std::string("OA_GAME_DIR names no directory: ") + value);
    return folder;
}

/// Tests whether a test program was asked for its game-data cases.
///
/// @param argc argument count from main()
/// @param argv arguments from main()
/// @return true when the first argument is kGameDataSwitch
[[nodiscard]] inline bool game_data_requested(int argc, char** argv) {
    return argc > 1 && argv[1] == kGameDataSwitch;
}

} // namespace oa::test
