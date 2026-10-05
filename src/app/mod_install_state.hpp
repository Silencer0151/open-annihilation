// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The mod packages a run installs (Runtime::ModInstallState): the package
// being installed, what its plan asks, its unpacking, the prompt over the
// main menu that asks and tells, and the folders left to delete, which
// runtime_mod_install.cpp drives and draws as an overlay.
#pragma once

#include "oa/app/mod_install.hpp"
#include "oa/app/mod_install/prompts.hpp"
#include "oa/app/runtime.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace oa::app {

struct Runtime::ModInstallState {
    /// Where the install of the package taken is.
    enum class Stage : uint8_t {
        idle,      ///< no package is being installed; the prompt, if any, tells
        asking,    ///< its question shows
        unpacking, ///< its files are unpacked, the progress shown
        telling,   ///< what came of it shows
    };
    Stage stage{Stage::idle};
    /// The prompt over the main menu and what its buttons answer; empty
    /// while none shows.
    std::optional<mod_install::ModPrompt> shown;
    /// The package taken from the inbox, as it was opened.
    std::filesystem::path file;
    /// The copy the platform made of it, which it releases once the install
    /// is done with it; empty when the game reads the file where it is.
    std::filesystem::path opened_copy;
    /// The package, read and checked.
    std::optional<mod_install::Package> package;
    mod_install::Incoming incoming{}; ///< the mod it installs
    mod_install::InstallPlan plan{};  ///< where it goes and what it asks
    /// The unpacking while it runs, and until its change is put in place.
    std::unique_ptr<mod_install::Unpacking> unpacking;
    std::string target;                   ///< the folder in Mods the change acts on
    mod_install::Change change{};         ///< the change the answer picked
    mod_install::InstalledMod expected{}; ///< what the target held when it was planned
    bool target_played{};                 ///< the target is the mod the game plays
    /// The unpacking is done and the prompt says the files are being put in
    /// place: the change is made on the next frame, after that is drawn.
    bool placing{};
    std::filesystem::path told_folder; ///< the folder OPEN FOLDER shows
    std::filesystem::path play_folder; ///< the folder PLAY NOW plays
    /// The folders to delete, a step each main menu frame.
    mod_install::Discarder discarder;
    /// Frames in a row the main menu has shown as itself.
    uint32_t settled_frames{};
    /// The key whose press closed the prompt, until it is released; 0 for none.
    uint32_t latched_key{};
    /// --check-mod-install shows the prompts although nobody watches the run.
    bool check_shows_prompts{};
    /// Prompts this run has shown.
    uint32_t prompts_shown{};
    /// When the hand-off folder is looked in next, in SDL ticks.
    uint64_t next_handoff_ms{};

    /// Shows a prompt over the main menu.
    ///
    /// @param made the prompt
    void show(mod_install::ModPrompt made);

    /// Gives the copy the platform made of the package back to it to remove.
    void release_opened_copy();

    /// Ends the install of the package taken: its copy released and the
    /// inbox told.
    void finish_package();

    /// Returns where the prompt's top left corner stands on the main menu,
    /// which centres it on the picture.
    ///
    /// @param height the prompt's height
    /// @return the corner, at the picture's scale
    [[nodiscard]] static oa::ui::frontend_renderer::Placement prompt_placement(int32_t height);

    /// The prompt overlay's input: while the prompt shows over the main menu
    /// it takes every input; its buttons answer, and the key that answered
    /// is latched.
    ///
    /// @param context the screen context, with the input
    /// @param state unused
    /// @return 1 when the input was taken
    static int prompt_event(oa::app::ScreenContext* context, void* state);

    /// Sets a question aside once a screen other than the main menu shows:
    /// its package waits again in the inbox.
    ///
    /// @param context the screen context
    /// @param state unused
    static void prompt_tick(oa::app::ScreenContext* context, void* state);

    /// Darkens the main menu and draws the prompt over it.
    ///
    /// @param context the screen context, with the frame
    /// @param state unused
    static void prompt_draw(oa::app::ScreenContext* context, void* state);
};

} // namespace oa::app
