// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A notice of Open Annihilation's own: a box in the settings dialog's look
// that tells the player something once, with a folder's path, a button that
// opens the folder and OK. The main menu shows it over itself, darkened as
// under the settings dialog; what a press, a release or a key does to it, and
// how it is drawn in the game's own fonts, are here.
#pragma once

#include "oa/ui/engine_settings/dialog.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::engine_settings {

/// The notice's width, in source pixels.
inline constexpr int32_t notice_width = 400;
/// The notice's least height, in source pixels; it grows with its text.
inline constexpr int32_t least_notice_height = 150;
/// The notice's greatest height, in source pixels: what the game's 640 by 480
/// picture holds with a margin; text below it is cut.
inline constexpr int32_t greatest_notice_height = 440;

/// OK, which closes the notice.
inline constexpr int32_t notice_ok_control = 0;
/// The button that opens the notice's folder.
inline constexpr int32_t notice_open_control = 1;

/// A paragraph of a notice's text.
struct NoticeParagraph {
    std::string text; ///< UTF-8; wrapped between words, or for a path at its separators
    /// A folder's path: drawn in the regular font and broken after a
    /// separator, a component too long for a line within it; any other
    /// paragraph is drawn in the small font, broken between words.
    bool path{};
};

/// One notice.
struct Notice {
    std::string title;                       ///< the header's title, in capitals
    std::vector<NoticeParagraph> paragraphs; ///< its text, top to bottom
    std::string open_caption;                ///< the caption of the button that opens the folder
    /// Why the folder could not be opened, drawn in amber under the text;
    /// empty for none.
    std::string failure;
    int32_t hovered{no_control}; ///< the button under the pointer
    int32_t pressed{no_control}; ///< the button a held press is on
    /// The button the keys mark and Space presses, ringed in green: OK at
    /// first.
    int32_t marked{notice_ok_control};
    /// The columns a finger's held press was moved by to reach the button it
    /// took (notice_finger_down): its moves and its release are moved as far.
    int32_t finger_shift_x{};
    int32_t finger_shift_y{}; ///< the rows a finger's held press was moved by, as finger_shift_x
};

/// What an event on the notice asks of the host.
enum class NoticeAction : uint8_t {
    none,        ///< nothing
    redraw,      ///< only its look changed: a hover, a press or the mark
    open_folder, ///< open the notice's folder in the file manager; the notice stays
    closed,      ///< OK, Enter or Escape: the notice is done with
};

/// Returns the notice's height: its header, its text wrapped in the fonts
/// (at estimated_character_width a character without them) and its footer,
/// from least_notice_height to greatest_notice_height.
///
/// @param notice the notice
/// @param fonts the fonts it is drawn in; null to estimate widths
/// @return the height, in source pixels
[[nodiscard]] int32_t notice_height(const Notice& notice, const DialogFonts* fonts = nullptr);

/// Returns the parts the notice draws: its icon's place, its title, each
/// line of its text as it is wrapped, the failure, and its two buttons. A
/// line the notice's height cuts is left out.
///
/// @param notice the notice
/// @param fonts the fonts it is drawn in; null to estimate widths
/// @return the parts, in source pixels from the notice's top left corner
[[nodiscard]] std::vector<LayoutPart>
notice_layout(const Notice& notice, const DialogFonts* fonts = nullptr);

/// Returns a text broken between words into lines no wider than a width; a
/// word wider than a line is broken between its characters.
///
/// @param text the text, UTF-8
/// @param width the room, in source pixels
/// @param text_width a text's width in the font it is drawn in
/// @return the lines; none for an empty text
[[nodiscard]] std::vector<std::string> wrap_text(
    std::string_view text, int32_t width, const std::function<int32_t(std::string_view)>& text_width
);

/// Returns a folder's path broken into lines no wider than a width: after a
/// separator ('/' or '\\'), each line as many whole components as fit, a
/// component wider than a line broken between its characters.
///
/// @param path the path, UTF-8
/// @param width the room, in source pixels
/// @param text_width a text's width in the font it is drawn in
/// @return the lines, which put together give the path
[[nodiscard]] std::vector<std::string> wrap_path(
    std::string_view path, int32_t width, const std::function<int32_t(std::string_view)>& text_width
);

/// Moves the pointer: the button under it lights.
///
/// @param[in,out] notice the notice
/// @param x the pointer's column, in source pixels from the notice's left edge
/// @param y the pointer's row, in source pixels from the notice's top edge
/// @param height the notice's height (notice_height)
/// @return NoticeAction::redraw when the look changed, else none
[[nodiscard]] NoticeAction
notice_pointer_move(Notice& notice, int32_t x, int32_t y, int32_t height);

/// Presses the pointer's button: a press on a button holds it; anywhere
/// else it does nothing.
///
/// @param[in,out] notice the notice
/// @param x the pointer's column, in source pixels from the notice's left edge
/// @param y the pointer's row, in source pixels from the notice's top edge
/// @param height the notice's height (notice_height)
/// @return NoticeAction::redraw when a button is held, else none
[[nodiscard]] NoticeAction
notice_pointer_down(Notice& notice, int32_t x, int32_t y, int32_t height);

/// Presses with a finger: as notice_pointer_down, but a press on neither
/// button takes the nearer one that lies within `reach` of it, pressed at
/// that button's point nearest the finger. The press's moves and its
/// release are then moved as far as the press was (Notice::finger_shift_x
/// and finger_shift_y), so that a release where the finger landed presses
/// the button it took. With neither within reach it holds nothing.
///
/// @param[in,out] notice the notice
/// @param x the finger's column, in source pixels from the notice's left edge
/// @param y the finger's row, in source pixels from the notice's top edge
/// @param height the notice's height (notice_height)
/// @param reach how far a button may lie from the finger, in source pixels
/// @return NoticeAction::redraw when a button is held, else none
[[nodiscard]] NoticeAction
notice_finger_down(Notice& notice, int32_t x, int32_t y, int32_t height, int32_t reach);

/// Releases the pointer's button: a release over the button the press held
/// presses it, OK closing the notice and the other asking for the folder.
///
/// @param[in,out] notice the notice
/// @param x the pointer's column, in source pixels from the notice's left edge
/// @param y the pointer's row, in source pixels from the notice's top edge
/// @param height the notice's height (notice_height)
/// @return what the release asks of the host
[[nodiscard]] NoticeAction notice_pointer_up(Notice& notice, int32_t x, int32_t y, int32_t height);

/// Takes a key: Enter and Escape close the notice, Space presses the
/// marked button, and Left, Right, Up, Down, Tab and Shift+Tab move the mark
/// to the other button. Any other key does nothing.
///
/// @param[in,out] notice the notice
/// @param key the key's meaning
/// @return what the key asks of the host
[[nodiscard]] NoticeAction notice_key(Notice& notice, DialogKey key);

/// Draws the notice: a raised panel in the settings dialog's colours, a
/// header with the Open Annihilation icon (or the OA mark) and the title,
/// the text in white with the paths in the regular font, the failure in
/// amber, and the open button and OK as Cancel and OK look, the marked one
/// ringed in green.
///
/// @param[in,out] target the surface
/// @param placement where the notice's top left corner lands, and its scale
/// @param notice the notice
/// @param fonts its fonts
/// @param icon the Open Annihilation icon; an empty picture draws the OA mark
void draw_notice(
    oa::ui::frontend_renderer::Surface& target,
    const oa::ui::frontend_renderer::Placement& placement,
    const Notice& notice,
    const DialogFonts& fonts,
    const oa::ui::frontend_renderer::RgbaPicture& icon
);

} // namespace oa::ui::engine_settings
