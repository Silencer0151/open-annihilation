// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The in-engine game folder chooser on the game's window: the folders found
// on this machine, a folder browser driven by touch, a gamepad, the keys or
// the pointer, the 1997 demo's folder and, outside Steam's Game Mode, the
// desktop's folder dialog (src/ui/folder-chooser holds its model).
#pragma once

#include "oa/app/app.hpp"
#include "oa/app/game_directory.hpp"
#include <SDL3/SDL.h>
#include <optional>

namespace oa::app {

class RendererHost;

/// How the chooser reaches the game's window, which opens on first use.
struct FolderChooserDisplayHooks {
    void* context{};
    /// Opens the window if it is not open yet and returns it; null: no window can open.
    SDL_Window* (*window)(void* context){};
    /// Returns the window's renderer; null: none.
    SDL_Renderer* (*renderer)(void* context){};
    /// Returns what made the renderer, whose start-up frames the chooser counts; null: none.
    RendererHost* (*host)(void* context){};
};

/// Returns whether the in-engine chooser may be shown: not unattended (unless a check asks),
/// the window can open, and no platform Game files import is offered.
///
/// @param options the parsed command line
/// @return whether resolution may ask for the chooser
[[nodiscard]] bool folder_chooser_offered(const Options& options);

/// Shows the chooser while resolution asks for it (needed.chooser): found folders, the browser
/// driven by touch, a gamepad, the keys or the pointer, the 1997 demo's folder and, outside
/// Game Mode, the desktop's dialog; a pick is inspected (take_chosen_folder) and a refused one
/// comes back with its reason.
///
/// @param options the parsed command line
/// @param display how the chooser reaches the game's window
/// @param[in,out] needed why there is no folder; cleared once one is chosen
/// @param[out] game_directory the folder, once one is chosen
/// @return false when the player quit or the window closed
bool run_folder_chooser_until_resolved(
    const Options& options,
    const FolderChooserDisplayHooks& display,
    GameFilesNeeded& needed,
    std::optional<GameDirectory>& game_directory
);

} // namespace oa::app
