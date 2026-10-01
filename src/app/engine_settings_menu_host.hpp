// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The main menu's OA button and settings dialog
// (Runtime::EngineSettingsMenuHost), which runtime_engine_settings_menu.cpp
// draws and drives as two overlays on the main menu.
#pragma once

#include "oa/app/runtime.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"

#include <cstdint>

namespace oa::app {

struct Runtime::EngineSettingsMenuHost {
    bool button_hovered{}; ///< the pointer is over the OA button
    bool button_pressed{}; ///< a press on the OA button is held
    /// The dialog shows over the main menu: it was opened there and has not
    /// closed since.
    bool dialog_shown{};
    /// The key whose press closed the dialog (Escape or Enter), until it is
    /// released; 0 for none. Its presses do nothing until then, so that a
    /// held key never reaches the main menu, whose Escape ends the program.
    uint32_t latched_key{};

    /// Tells whether the OA button shows on the main menu: the menu's own
    /// panel is drawn, no package owns the frame, and the dialog's fonts are
    /// there.
    ///
    /// @param runtime the runtime
    /// @return true while the button shows and answers the pointer
    [[nodiscard]] static bool button_shown(Runtime& runtime);

    /// Returns where the OA button stands on the main menu's picture: its
    /// bottom-right corner, or its top-right corner while an extension's
    /// overlay stands over the main menu.
    ///
    /// @param runtime the runtime
    /// @return the button's square, in source pixels
    [[nodiscard]] static oa::ui::frontend_renderer::SourceRect button_rect(const Runtime& runtime);

    /// Returns where the dialog's top left corner stands on the main menu,
    /// which centres it on the picture.
    ///
    /// @return the corner, at the picture's scale
    [[nodiscard]] static oa::ui::frontend_renderer::Placement dialog_placement();

    /// The button overlay's input: hovering and pressing the OA button, the
    /// shortcut, and the latched key's presses and release.
    ///
    /// @param context the screen context, with the input
    /// @param state unused
    /// @return 1 when the input was taken
    static int button_event(oa::app::ScreenContext* context, void* state);

    /// Draws the OA button over the composed main menu.
    ///
    /// @param context the screen context, with the frame
    /// @param state unused
    static void button_draw(oa::app::ScreenContext* context, void* state);

    /// The dialog overlay's input: while the dialog shows over the main menu
    /// it takes every input and hands the dialog's actions on.
    ///
    /// @param context the screen context, with the input
    /// @param state unused
    /// @return 1 when the input was taken
    static int dialog_event(oa::app::ScreenContext* context, void* state);

    /// Closes a dialog the main menu opened, as Cancel closes it, once a
    /// screen other than the main menu shows.
    ///
    /// @param context the screen context
    /// @param state unused
    static void dialog_tick(oa::app::ScreenContext* context, void* state);

    /// Darkens the main menu and draws the dialog over it.
    ///
    /// @param context the screen context, with the frame
    /// @param state unused
    static void dialog_draw(oa::app::ScreenContext* context, void* state);

    /// Puts the dialog's action in effect, plays its sound and, when the
    /// dialog closes on a key press, latches that key.
    ///
    /// @param runtime the runtime
    /// @param action what the dialog's event asked
    /// @param key_down the key whose press asked it; 0 for a pointer event
    static void
    take_action(Runtime& runtime, oa::ui::engine_settings::DialogAction action, uint32_t key_down);
};

} // namespace oa::app
