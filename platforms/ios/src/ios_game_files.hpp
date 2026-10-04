// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The iOS and iPadOS side of the Game files screen (oa/app/game_files_hooks.hpp): the system's
// document picker, access to what the player chose, listing and copying it (downloading files
// a cloud service holds), free space, time to finish a copy away from the screen, device
// backups and the words that name the device and its apps. ios_platform.mm installs the hooks
// and hands over the game's window.
#pragma once

struct SDL_Window;

/// Installs the iOS GameFilesHooks (oa::app::set_game_files_hooks); called by
/// oa_extension_init_ios_platform on the main thread, before the command line is read.
void install_ios_game_files_hooks();

/// Gives the hooks the game's window, from whose root view controller the picker is shown;
/// called by ios_platform.mm's window_ready hook on the main thread.
///
/// @param window the game's window
void ios_game_files_window_ready(SDL_Window* window);
