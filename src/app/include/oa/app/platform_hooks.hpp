// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the platform the game runs on provides beyond SDL: haptics, a default
// game folder, the advice shown without one and the label of its look-again
// button, word that the window is open, its own way of showing a folder in
// its file manager, and the files the system opened in the game, such as a
// mod package, brought where the game can read them. A platform's extension
// init fills the hooks; the desktop leaves them null.
#pragma once

#include <stdint.h>
#include <string>

namespace oa::app {

/// The moments a touch control gives a haptic.
enum class Haptic : uint8_t {
    hold_started,  ///< a long press began (radial, help, factory -1 armed)
    box_started,   ///< a selection box began
    site_refused,  ///< a building was placed on a refused site
    queue_reduced, ///< a factory queue was reduced by a hold
};

/// What the platform the game runs on provides beyond SDL. Filled once by the platform's
/// extension init, before the command line is read; every member may be null.
/// No member may throw.
struct PlatformHooks {
    void* context{}; ///< passed back to every hook
    /// Plays a short haptic; null plays none.
    void (*haptic)(void* context, Haptic kind){};
    /// Writes the platform's default game folder (absolute, UTF-8) into `folder` and returns
    /// true; null or false: there is none. It ranks above the remembered folder.
    bool (*default_game_folder)(void* context, std::string* folder){};
    /// Returns the advice shown when no game folder is usable and no dialog can ask (UTF-8,
    /// static storage); null or a null result: the --game-dir advice.
    const char* (*missing_game_folder_advice)(void* context){};
    /// Tells the platform the game's window is open (an SDL_Window*); null does nothing.
    void (*window_ready)(void* context, void* window){};
    /// Returns the label of the button the missing-folder notice shows to look again, such as
    /// "Check again" (UTF-8, static storage). Null, or a null result: the notice has no such
    /// button and the game ends after it, as before.
    const char* (*game_folder_check_again)(void* context){};
    /// Shows a folder (absolute, UTF-8, one that exists) in the platform's file manager and
    /// returns true once it has asked for it; false, writing why into `why` (UTF-8), when it
    /// cannot. Null: the desktop's own way, or none where the build starts no other programs
    /// (system_folder_opener).
    bool (*show_folder)(void* context, const char* folder, std::string* why){};
    /// Brings a file the system opened in the game (absolute, UTF-8), such as a mod package
    /// tapped in the Files app, where the game can read it: writes the path the game reads
    /// (absolute, UTF-8) into `copy` and returns true; false, writing why into `why` (UTF-8),
    /// when the platform could not bring the file in. The path written is the platform's own
    /// copy of the file, or `path` itself when the platform holds none. The game hands the path
    /// written to release_opened_file once it is done with it, and never deletes it itself.
    /// Null: the game reads the file where it is.
    bool (*take_opened_file)(
        void* context, const char* path, std::string* copy, std::string* why
    ){};
    /// Tells the platform the game is done with a file take_opened_file gave (installed,
    /// refused or cancelled; absolute, UTF-8): the platform removes its own copy, with the
    /// folder it made for it, and leaves any other file alone. Null does nothing.
    void (*release_opened_file)(void* context, const char* path){};
};

/// Installs the platform's hooks (a copy is kept).
///
/// @param hooks the hooks; members left null are not used
void set_platform_hooks(const PlatformHooks& hooks) noexcept;
/// Returns the installed hooks; all null when none were installed.
///
/// @return the hooks set_platform_hooks kept
[[nodiscard]] const PlatformHooks& platform_hooks() noexcept;

} // namespace oa::app
