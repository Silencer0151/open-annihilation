// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Open Annihilation settings dialog: its sections and rows, what a press,
// a drag or a key does to them, and how it and the OA button that opens it
// are drawn. The dialog is laid out in the game's 640x480 source pixels and
// drawn without the game's art (oa/ui/frontend_renderer/artless.hpp) in the
// game's own fonts. A host places it, darkens what lies under it, turns its
// events into dialog pixels and puts the settings it reports in effect.
#pragma once

#include "oa/formats/fnt.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/ui/engine_settings.hpp"
#include "oa/ui/frontend_renderer.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::engine_settings {

/// The dialog's width, in source pixels.
inline constexpr int32_t dialog_width = 480;
/// The dialog's height, in source pixels.
inline constexpr int32_t dialog_height = 324;
/// The OA button's side on the main menu, in source pixels.
inline constexpr int32_t menu_button_side = 32;
/// The OA button's side in the in-game menu's column, in source pixels.
inline constexpr int32_t ingame_button_side = 24;

/// The colour the screen under the dialog is darkened with.
inline constexpr oa::ui::frontend_renderer::Rgb backdrop_color{5, 6, 4};
/// How far the main menu is darkened under the dialog, in 256ths.
inline constexpr uint32_t menu_backdrop_opacity = 159;
/// How far the in-game menu's column is darkened beside the dialog, in 256ths.
inline constexpr uint32_t ingame_backdrop_opacity = 128;

/// The dialog's sections, in the order its list shows them.
enum class Page : uint8_t {
    path_search, ///< AI & Pathfinding
    controls,    ///< Controls & Input
    gameplay,    ///< Gameplay
    graphics,    ///< Graphics
    developer,   ///< Developer, after a divider
};

/// The settings, as the dialog's rows show them.
enum class Setting : uint8_t {
    path_search,       ///< Pathfinding cycles: a slider
    wheel_zoom,        ///< Mouse wheel zoom: a switch
    escape_opens_menu, ///< Escape opens the game menu: a switch
    switch_alt,        ///< Select groups without Alt: a switch
    unit_limit,        ///< Unit limit: a slider
    max_frame_rate,    ///< Maximum frame rate: a slider
    anti_aliasing,     ///< Enhanced anti-aliasing: a strip of levels
    frame_stats,       ///< Show performance statistics: a switch
};

/// Returns the settings a section shows, top to bottom.
///
/// @param page the section
/// @return one to three settings
[[nodiscard]] std::span<const Setting> page_settings(Page page) noexcept;

/// No control: what Dialog::hovered, pressed and focused hold when they name none.
inline constexpr int32_t no_control = -1;
/// The first section's entry in the list; the others follow in Page order.
inline constexpr int32_t first_page_control = 0;
/// The open section's first row's control; the next rows' follow it.
inline constexpr int32_t first_row_control = 5;
/// Restore defaults.
inline constexpr int32_t restore_control = 8;
/// Cancel.
inline constexpr int32_t cancel_control = 9;
/// OK.
inline constexpr int32_t ok_control = 10;

/// Returns the control of a section's entry in the list.
///
/// @param page the section
/// @return its control's number
[[nodiscard]] constexpr int32_t page_control(Page page) noexcept {
    return first_page_control + static_cast<int32_t>(page);
}

/// The keys the dialog answers to; a host gives the platform's keys these meanings.
enum class DialogKey : uint8_t {
    enter,    ///< OK
    escape,   ///< Cancel
    up,       ///< the focus to the control above
    down,     ///< the focus to the control below
    left,     ///< the focused control one step down: Off, a lower value
    right,    ///< the focused control one step up: On, a higher value
    space,    ///< presses the focused button or flips the focused switch
    tab,      ///< the focus to the next control
    back_tab, ///< the focus to the previous control
};

/// What an event asks of the host.
enum class DialogAction : uint8_t {
    none,      ///< nothing
    redraw,    ///< only the dialog's look changed: a hover, the focus, a press or the section
    changed,   ///< Dialog::chosen changed: put it in effect and redraw
    accepted,  ///< OK: keep Dialog::chosen in effect, save it and close the dialog
    cancelled, ///< Cancel: put Dialog::opened back in effect and close the dialog
};

/// How the OA button looks.
enum class ButtonLook : uint8_t {
    idle,    ///< at rest
    hovered, ///< the pointer is over it
    pressed, ///< a press on it is held
};

/// One open dialog. A host reads opened, chosen, defaults, restored and
/// page; the members after them are the dialog's own.
struct Dialog {
    EngineSettings opened{};      ///< in effect as it opened; Cancel puts them back
    EngineSettings chosen{};      ///< what it shows; in effect as they change
    EngineSettings defaults{};    ///< what Restore defaults sets
    Locks locks{};                ///< what cannot be changed now
    std::string version;          ///< the header's version text
    Page page{Page::path_search}; ///< the section shown
    bool restored{};              ///< Restore defaults was pressed
    int32_t hovered{no_control};  ///< the control under the pointer
    int32_t pressed{no_control};  ///< the control a held press is on
    int32_t focused{no_control}; ///< the control with the keyboard focus; shown once a key moves it
    bool dragging{};             ///< the held press drags a slider's knob
};

/// The font a text of the dialog is drawn in.
enum class DialogFont : uint8_t {
    regular, ///< DialogFonts::regular
    small,   ///< DialogFonts::small
};

/// One part of the dialog as it is drawn now: a text or a control, and the
/// rectangle it keeps to.
struct LayoutPart {
    oa::ui::frontend_renderer::SourceRect rect{}; ///< in source pixels from the dialog's top left
    std::string text;                             ///< the text drawn in it; empty for a control
    DialogFont font{};                            ///< the font of text
    int32_t tracking{};          ///< extra columns after each of text's glyphs but the last
    int32_t control{no_control}; ///< the control it is; no_control for a text
};

/// Returns the parts the dialog draws now: the header's texts, the list's
/// entries, the open section's heading, labels, hints, locks, controls and
/// values, and the footer's buttons. No two overlap, and each lies inside
/// the dialog's edge.
///
/// @param dialog the dialog
/// @return the parts
[[nodiscard]] std::vector<LayoutPart> dialog_layout(const Dialog& dialog);

/// The fonts the dialog and the OA button draw their texts in.
struct DialogFonts {
    oa::ui::frontend_renderer::TextFont regular; ///< labels, values and buttons
    oa::ui::frontend_renderer::TextFont small;   ///< the section heading, the hints and the version
};

/// Loads the dialog's fonts from the game's files: the game's button font as
/// the regular one and its label font as the small one, each readied for
/// text in one colour (oa::ui::frontend_renderer::text_font): its letters
/// keep their shading and lose the dark outline round them.
///
/// Throws std::runtime_error when a font or the game's palette is missing, or a
/// font is malformed.
///
/// @param assets the game's files
/// @return the fonts
[[nodiscard]] DialogFonts load_dialog_fonts(oa::AssetStore& assets);

/// Opens the dialog over settings in effect.
///
/// @param[out] dialog the dialog; whatever it held is replaced
/// @param current the settings in effect
/// @param defaults what Restore defaults sets
/// @param locks what cannot be changed now
/// @param version the header's version text
/// @param page the section to show
void open_dialog(
    Dialog& dialog,
    const EngineSettings& current,
    const EngineSettings& defaults,
    const Locks& locks,
    std::string_view version,
    Page page
);

/// Moves the pointer: hovers a control, or drags a held slider's knob.
///
/// @param[in,out] dialog the dialog
/// @param x the pointer's column, in source pixels from the dialog's left edge
/// @param y the pointer's row, in source pixels from the dialog's top edge
/// @return what the move asks of the host
[[nodiscard]] DialogAction dialog_pointer_move(Dialog& dialog, int32_t x, int32_t y);

/// Presses the pointer's button: a press on a control holds it.
///
/// @param[in,out] dialog the dialog
/// @param x the pointer's column, in source pixels from the dialog's left edge
/// @param y the pointer's row, in source pixels from the dialog's top edge
/// @return what the press asks of the host
[[nodiscard]] DialogAction dialog_pointer_down(Dialog& dialog, int32_t x, int32_t y);

/// Releases the pointer's button: a release over the control the press held
/// acts on it.
///
/// @param[in,out] dialog the dialog
/// @param x the pointer's column, in source pixels from the dialog's left edge
/// @param y the pointer's row, in source pixels from the dialog's top edge
/// @return what the release asks of the host
[[nodiscard]] DialogAction dialog_pointer_up(Dialog& dialog, int32_t x, int32_t y);

/// Takes a key.
///
/// @param[in,out] dialog the dialog
/// @param key the key's meaning
/// @return what the key asks of the host
[[nodiscard]] DialogAction dialog_key(Dialog& dialog, DialogKey key);

/// Tells whether a point lies on the dialog.
///
/// @param x the point's column, in source pixels from the dialog's left edge
/// @param y the point's row, in source pixels from the dialog's top edge
/// @return true inside its dialog_width by dialog_height
[[nodiscard]] bool dialog_contains(int32_t x, int32_t y) noexcept;

/// Draws the dialog.
///
/// @param[in,out] target the surface
/// @param placement where the dialog's top left corner lands, and its scale
/// @param dialog the dialog
/// @param fonts its fonts
void draw_dialog(
    oa::ui::frontend_renderer::Surface& target,
    const oa::ui::frontend_renderer::Placement& placement,
    const Dialog& dialog,
    const DialogFonts& fonts
);

/// Draws the OA button: the mark in a small bevelled square.
///
/// @param[in,out] target the surface
/// @param placement where the button's top left corner lands, and its scale
/// @param side the button's side, in source pixels (menu_button_side or ingame_button_side)
/// @param look how it looks
/// @param fonts the dialog's fonts
void draw_oa_button(
    oa::ui::frontend_renderer::Surface& target,
    const oa::ui::frontend_renderer::Placement& placement,
    int32_t side,
    ButtonLook look,
    const DialogFonts& fonts
);

} // namespace oa::ui::engine_settings
