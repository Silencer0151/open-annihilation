// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The --check-game-files check: scripted platform and game files hooks, the
// route driven through the Game files screen by taps and keys, pictures of
// each step for people, and the verdict line.
#pragma once

#include "game_files_screen.hpp"
#include "oa/app/app.hpp"

namespace oa::app {

/// Installs the check's scripted PlatformHooks (default folder = <work>/Documents/Total
/// Annihilation when it exists, no advice) and GameFilesHooks (every capability; the picker
/// answers --game-files-source; listing and copy through std::filesystem, throttled by
/// --game-files-copy-rate; free space --game-files-free-bytes; keep_running and
/// set_backed_up recorded), for --check-game-files. <work> is the working directory.
///
/// @param options the parsed command line
void install_game_files_check(const Options& options);
/// The check's per-pass hooks for the screen (steps of the route, taps, pictures).
///
/// @param options the parsed command line
/// @return the hooks the screen runs each pass
[[nodiscard]] GameFilesScreenCheckHooks game_files_check_hooks(const Options& options);
/// Runs the management route's pass over an installed folder (before the Runtime).
///
/// @param options the parsed command line
/// @param window the game's window
/// @param renderer its renderer
/// @return false when the check failed
[[nodiscard]] bool
run_game_files_manage_check(const Options& options, SDL_Window* window, SDL_Renderer* renderer);
/// Prints the check's verdict line and returns the process status.
///
/// @param options the parsed command line
/// @param status the run's status so far
/// @return `status` when the route reached what --game-files-expect names, else 1
[[nodiscard]] int finish_game_files_check(const Options& options, int status);

} // namespace oa::app
