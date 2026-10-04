// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The in-game menu's OA button and settings dialog
// (Runtime::EngineSettingsMatchHost), which runtime_engine_settings_match.cpp
// drives as an overlay on the match and draws as a layer of its own over the
// match's layers.
#pragma once

#include "oa/app/runtime.hpp"
#include "oa/ui/display_layout.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/screen_registry.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace oa::app {

struct Runtime::EngineSettingsMatchHost {
    /// The OA button's left column in the in-game menu's column, in source
    /// pixels: under Resume, clear of its frame.
    static constexpr int32_t button_source_x = 100;
    /// The OA button's top row in the in-game menu's column, in source pixels.
    static constexpr int32_t button_source_y = 455;

    /// What the settings layer shows, at which size: the layer is drawn
    /// again only when this changes.
    struct LayerLook {
        int32_t width{};  ///< the layer's width, in window pixels
        int32_t height{}; ///< the layer's height, in window pixels
        double scale{};   ///< the side column's scale
        /// The OA button's and the dialog's rectangles, x, y, width and
        /// height each, in window pixels: they move with the safe area on a
        /// phone (fits_safe_area).
        std::array<int32_t, 8> placed{};
        bool button_shown{};   ///< the OA button shows
        bool dialog_shown{};   ///< the dialog shows over the darkened screen
        uint8_t button_look{}; ///< the button's oa::ui::engine_settings::ButtonLook
        uint64_t revision{};   ///< revision when it was drawn
        bool operator==(const LayerLook&) const = default;
    };

    bool button_hovered{}; ///< the pointer is over the OA button
    bool button_pressed{}; ///< a press on the OA button is held
    bool dialog_open{};    ///< the dialog this host opened is open
    /// The key that closed the dialog (Enter, Escape or Space): its presses do
    /// nothing until it is released, so that a held key never reaches the
    /// in-game menu.
    std::optional<uint32_t> latched_key;
    /// Counts the changes to what the layer shows that LayerLook cannot see:
    /// the dialog's controls, hovers and sections.
    uint64_t revision{};
    std::optional<LayerLook> drawn; ///< what layer_rgba holds; nothing before the first draw
    /// The layer at the window's size, 4 bytes a pixel (red, green, blue,
    /// opacity), before the display gamma.
    std::vector<uint8_t> layer_rgba;
    oa::ui::display_layout::Rect layer_bounds{}; ///< the part of the layer that is not clear
    std::optional<LayerLook> uploaded;         ///< what the texture holds; nothing when it is stale
    std::array<uint8_t, 256> uploaded_gamma{}; ///< the gamma table the texture was uploaded at
    /// The settings layer's texture, in tiles beyond the renderer's limit;
    /// none before the first draw.
    TiledTexture layer;

    /// Takes one input on the match: the dialog's while it is open, the OA
    /// button's while the in-game menu's column shows, and the shortcut.
    ///
    /// @param runtime the runtime
    /// @param input the input, in window pixels
    /// @return true when the input was taken
    static bool take_input(Runtime& runtime, const ScreenInput& input);

    /// Takes one input while the dialog is open: every input is taken.
    ///
    /// @param runtime the runtime
    /// @param dialog the open dialog
    /// @param input the input, in window pixels
    static void take_dialog_input(
        Runtime& runtime, oa::ui::engine_settings::Dialog& dialog, const ScreenInput& input
    );

    /// Hands a dialog's action to the settings and notes what it changed.
    ///
    /// @param runtime the runtime
    /// @param action what the dialog asked
    /// @return true when the dialog closed
    static bool take_action(Runtime& runtime, oa::ui::engine_settings::DialogAction action);

    /// Closes the dialog as OK closes it when the in-game menu's column no
    /// longer shows under it: the match ended or was left, or a message box
    /// or another panel took the column.
    ///
    /// @param runtime the runtime
    static void close_when_column_hidden(Runtime& runtime);

    /// Tells whether the OA button shows: the in-game menu's column shows
    /// and the dialog's fonts are present.
    ///
    /// @param runtime the runtime
    /// @return true while the button shows
    [[nodiscard]] static bool button_shown(Runtime& runtime);

    /// Tells whether the dialog and the OA button fit the window's safe area
    /// rather than the side column's scale: the touch controls are on and the
    /// window is phone class, whose side column is no longer drawn at a
    /// scale of its own.
    ///
    /// @param runtime the runtime
    /// @return true on a phone with touch controls
    [[nodiscard]] static bool fits_safe_area(const Runtime& runtime);

    /// Returns the window's safe area on the match's canvas.
    ///
    /// @param layout the match's layout
    /// @return the canvas less its safe-area insets, in window pixels
    [[nodiscard]] static oa::ui::display_layout::Rect
    safe_area(const oa::ui::display_layout::MatchLayout& layout) noexcept;

    /// Returns where the OA button shows on the match's canvas: under Resume
    /// in the side column at its scale; fitted to the safe area, where a
    /// placed region showing that part of the column puts it, else in the
    /// column as the 640x480 frame fitted to the safe area from its top left
    /// corner places it.
    ///
    /// @param layout the match's layout
    /// @param fit the button fits the safe area (fits_safe_area)
    /// @return the button's rectangle, in window pixels
    [[nodiscard]] static oa::ui::display_layout::Rect
    button_rect(const oa::ui::display_layout::MatchLayout& layout, bool fit = false) noexcept;

    /// Draws the OA button's face as the match's layer shows it: at the
    /// whole-number scale at or above the side column's, so that its icon
    /// keeps its detail when the face is fitted to button_rect, and darkened
    /// as the screen is while the dialog shows.
    ///
    /// @param layout the match's layout
    /// @param look how the button looks
    /// @param darkened the dialog shows over the screen
    /// @param fonts the dialog's fonts
    /// @param icon the Open Annihilation icon; empty draws the OA mark
    /// @param fit the button fits the safe area (fits_safe_area): the face
    ///     takes the whole-number scale at or above the button's own
    /// @return the face, ingame_button_side source pixels a side at that scale
    [[nodiscard]] static oa::ui::frontend_renderer::Surface button_face(
        const oa::ui::display_layout::MatchLayout& layout,
        oa::ui::engine_settings::ButtonLook look,
        bool darkened,
        const oa::ui::engine_settings::DialogFonts& fonts,
        const oa::ui::frontend_renderer::RgbaPicture& icon,
        bool fit = false
    );

    /// Returns where the dialog shows on the match's canvas: at the side
    /// column's scale, centred in the area right of the column; fitted to
    /// the safe area, at the largest scale that shows it whole there
    /// (min(safe width / dialog_width, safe height / dialog_height)), centred
    /// in it.
    ///
    /// @param layout the match's layout
    /// @param fit the dialog fits the safe area (fits_safe_area)
    /// @return the dialog's rectangle, in window pixels
    [[nodiscard]] static oa::ui::display_layout::Rect
    dialog_rect(const oa::ui::display_layout::MatchLayout& layout, bool fit = false) noexcept;

    /// Maps a point on the canvas to the dialog's source pixels.
    ///
    /// @param layout the match's layout
    /// @param x the point's column, in window pixels
    /// @param y the point's row, in window pixels
    /// @param fit the dialog fits the safe area (fits_safe_area)
    /// @return the point, in source pixels from the dialog's top left corner
    [[nodiscard]] static oa::ui::display_layout::Point dialog_point(
        const oa::ui::display_layout::MatchLayout& layout, float x, float y, bool fit = false
    ) noexcept;

    /// Copies a surface into a rectangle of an opaque-or-clear layer, each
    /// layer pixel taking the surface pixel it lands on (nearest), fully
    /// opaque.
    ///
    /// @param[in,out] rgba the layer, 4 bytes a pixel
    /// @param width the layer's width
    /// @param height the layer's height
    /// @param source the surface
    /// @param rect where the surface lands, in layer pixels; clipped to the layer
    static void stamp(
        std::vector<uint8_t>& rgba,
        int32_t width,
        int32_t height,
        const oa::ui::frontend_renderer::Surface& source,
        const oa::ui::display_layout::Rect& rect
    );

    /// Draws the layer again when what it shows changed.
    ///
    /// @param runtime the runtime
    /// @return true when the layer shows anything
    static bool refresh_layer(Runtime& runtime);

    /// The match overlay's input callback (OverlayDesc::event).
    ///
    /// @param context the screen context, with its input
    /// @param state the runtime
    /// @return nonzero when the input was taken
    static int overlay_event(ScreenContext* context, void* state);

    /// The cleanup overlay's frame callback (OverlayDesc::tick), on every
    /// screen: closes a dialog the in-game menu no longer shows.
    ///
    /// @param context the screen context
    /// @param state the runtime
    static void overlay_tick(ScreenContext* context, void* state);
};

} // namespace oa::app
