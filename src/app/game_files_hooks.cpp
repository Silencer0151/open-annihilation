// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The platform's game files hooks, kept for the run (game_files_hooks.hpp).
#include "oa/app/game_files_hooks.hpp"

namespace oa::app {
namespace {

/// Returns the hooks of the run, all null until set_game_files_hooks.
///
/// @return the kept copy
GameFilesHooks& installed_hooks() noexcept {
    static GameFilesHooks hooks{};
    return hooks;
}

} // namespace

void set_game_files_hooks(const GameFilesHooks& hooks) noexcept {
    installed_hooks() = hooks;
}

const GameFilesHooks& game_files_hooks() noexcept {
    return installed_hooks();
}

bool game_files_import_offered(const GameFilesHooks& hooks) noexcept {
    return hooks.capabilities != nullptr && hooks.game_folder != nullptr;
}

} // namespace oa::app
