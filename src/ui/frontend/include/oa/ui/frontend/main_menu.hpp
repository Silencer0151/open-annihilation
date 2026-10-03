// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// MAINMENU.GUI setup: the version label, menu music and the start-up checks
// the first main menu runs once.
#pragma once

#include "oa/ui/frontend/options.hpp"

#include <cstdint>
#include <string_view>

namespace oa::ui::frontend {

inline constexpr std::string_view kMainMenuVersionControl = "DebugString";
inline constexpr std::string_view kMainMenuVersion = "v3.1v1117a";
// The buttons that play movies: the intro, and the credits over the menu art.
inline constexpr std::string_view kMainMenuIntroControl = "INTRO";
inline constexpr std::string_view kMainMenuCreditsControl = "Credits";
inline constexpr const char* kMainMenuMusic = "BGM";
// CD music kind selected while the frontend shows: no track plays.
inline constexpr int32_t kMainMenuMusicKind = 4;
inline constexpr int32_t kSoundDriverWarningWidth = 500;
inline constexpr const char* kNoSoundDriverMessage = "No sound driver is available for use.\n";

// Checks the setup runs once per process; the flags live as long as the
// process.
struct MainMenuChecks {
    bool cd_player_checked = false;    // set only once the foreign-player prompt ran
    bool sound_driver_checked = false; // set only once the warning was shown
    bool revision_checked = false;
};

// Services of the setup; null members are absent services.
struct MainMenuHost {
    void* context = nullptr;
    void (*play_music)(void* context, const char* sound) = nullptr; // alternate-voice loop
    void (*set_music_kind)(void* context, int32_t kind) = nullptr;
    int32_t (*measure_text)(void* context, const char* text) = nullptr;
    void (*reset_sparks)(void* context) = nullptr; // the SPARKS block, zeroed
    bool (*foreign_cd_player)(void* context) = nullptr;
    void (*close_cd_player)(void* context) = nullptr;
    bool (*sound_driver_missing)(void* context) = nullptr;
    void (*check_revision)(void* context) = nullptr;
    void (*show_message)(void* context, const char* text, int32_t width) = nullptr;
    const char* (*translate)(void* context, const char* text) = nullptr;
    // Whether the game has movies to play; null counts them present.
    bool (*movies_present)(void* context) = nullptr;
    // Whether the game data names the 3.1 revision it is (gamedata/version.tdf);
    // null counts it named.
    bool (*revision_named)(void* context) = nullptr;
    // The version label's text, such as a mod's own version; null shows
    // kMainMenuVersion.
    const char* version_text = nullptr;
};

/// Sets up MAINMENU.GUI after the panel loads.
///
/// In order: the menu music (with no CD track), the version label centred on
/// its authored x, the spark block, then the once-only CD-player,
/// sound-driver and Revision.GPF checks. A check is marked done only once it
/// ran its action (the CD player closed, the warning shown). 3.1c also warns
/// between the first two checks when the system's graphics components are
/// older than it needs; no such warning is shown here.
///
/// Game data that does not name its revision, such as the Total Annihilation
/// demo (1997), is not 3.1c's, and the version label stays hidden. A game
/// without movies grays INTRO out and hides Credits, as the demo does.
///
/// @param[in,out] panel The loaded main menu; its DebugString label is set and moved, INTRO
///                  grayed and Credits hidden.
/// @param[in,out] checks Once-per-process check flags.
/// @param host Music, text, message and check services; null members are skipped.
void main_menu_setup(Panel& panel, MainMenuChecks& checks, const MainMenuHost& host) noexcept;

} // namespace oa::ui::frontend
