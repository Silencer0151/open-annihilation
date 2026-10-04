// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The player's own folder's opener and the notice of the engine's own that
// shows over a screen (Runtime::UserFolderState): the main menu's notice of
// the saved games' move, and the warning that the mod's games cannot start
// (runtime_mod_warning.cpp), which runtime_user_folder.cpp draws and drives
// as an overlay.
#pragma once

#include "oa/app/runtime.hpp"
#include "oa/app/user_folder.hpp"
#include "oa/ui/engine_settings/notice.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace oa::app {

struct Runtime::UserFolderState {
    /// Shows folders: the system's file manager in a run someone watches,
    /// else a record of the requests (opened).
    FolderOpenerHooks opener{};
    /// The folders a run nobody watches asked to show, in order.
    std::vector<fs::path> opened;
    /// The notice while it shows: the move's, or the mod's warning; empty
    /// while none shows.
    std::optional<oa::ui::engine_settings::Notice> notice;
    /// The folder the notice's button shows: Saves in the player's own
    /// folder, or the mod's folder.
    fs::path notice_folder;
    /// The screen the notice shows over, which it closes on leaving: the
    /// main menu, or the screen whose start the mod's warning refused.
    Screen notice_screen{Screen::main_menu};
    /// Frames in a row the main menu has shown as itself.
    uint32_t main_menu_frames{};
    /// The main menu's warning that the mod's games cannot start waits to be
    /// told: once from each start, and again after a refused start the
    /// screen it was refused on could not show it over.
    bool mod_warning_due{true};
    /// Frames in a row the main menu has shown as itself while the mod's
    /// warning waits.
    uint32_t mod_warning_frames{};
    /// Warnings that the mod's games cannot start this run has shown.
    uint32_t mod_warnings_shown{};
    /// The key whose press closed the notice (Escape or Enter), until it is
    /// released; 0 for none. Its presses do nothing until then, so that a
    /// held key never reaches the main menu.
    uint32_t latched_key{};
    /// --check-user-folder and --check-mod-warning show the main menu's
    /// notices although nobody watches the run.
    bool check_shows_notice{};
    /// Notices this run has shown.
    uint32_t notices_shown{};

    /// Returns where the notice's top left corner stands on the main menu,
    /// which centres it on the picture.
    ///
    /// @param height the notice's height
    /// @return the corner, at the picture's scale
    [[nodiscard]] static oa::ui::frontend_renderer::Placement notice_placement(int32_t height);

    /// Tells whether the run is one nobody watches: unattended, on CI, or on
    /// a video driver that shows no window.
    ///
    /// @param unattended the run is unattended (Options::unattended)
    /// @return true when nobody watches
    [[nodiscard]] static bool unwatched(bool unattended);

    /// The notice overlay's input: while the notice shows over its screen
    /// it takes every input; OK, Enter and Escape close it, and its button
    /// shows its folder. The key that closed it is latched.
    ///
    /// @param context the screen context, with the input
    /// @param state unused
    /// @return 1 when the input was taken
    static int notice_event(oa::app::ScreenContext* context, void* state);

    /// Closes the notice once a screen other than its own shows.
    ///
    /// @param context the screen context
    /// @param state unused
    static void notice_tick(oa::app::ScreenContext* context, void* state);

    /// Darkens the notice's screen and draws the notice over it.
    ///
    /// @param context the screen context, with the frame
    /// @param state unused
    static void notice_draw(oa::app::ScreenContext* context, void* state);
};

} // namespace oa::app
