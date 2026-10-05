// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A prompt of Open Annihilation's own: a notice (notice.hpp) that asks a
// question with one to three buttons of its own captions, and can show a
// progress bar under its text. It shares the notice's look and text
// placement; its text is given finished, in the language shown, and drawn
// as given.
#pragma once

#include "oa/ui/engine_settings/notice.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace oa::ui::engine_settings {

/// The most buttons a prompt has.
inline constexpr std::size_t most_prompt_buttons = 3;
/// A full progress bar (Prompt::progress).
inline constexpr int32_t prompt_progress_whole = 1000;
/// The control prompt_layout gives the progress bar's part.
inline constexpr int32_t prompt_bar_control = 100;

/// One of a prompt's buttons.
struct PromptButton {
    std::string caption{}; ///< drawn as given, in the language shown
    /// Drawn as OK is, in the accent colour: the answer the prompt leads to.
    bool accent{};
};

/// One prompt.
struct Prompt {
    std::string title{};                       ///< the header's title, as given
    std::vector<NoticeParagraph> paragraphs{}; ///< its text, top to bottom, as given
    std::vector<PromptButton> buttons{};       ///< 1 to most_prompt_buttons, left to right
    int32_t cancel_button{};                   ///< the button Escape and N answer
    int32_t primary_button{};                  ///< the button Y answers
    /// Why something it asked for failed, drawn in amber under the text;
    /// empty for none.
    std::string failure{};
    /// A bar under the text, 0 to prompt_progress_whole filled; -1 for none.
    int32_t progress{-1};
    int32_t hovered{no_control}; ///< the button under the pointer
    int32_t pressed{no_control}; ///< the button a held press is on
    /// The button the keys mark, which Enter and Space answer, ringed in green.
    int32_t marked{};
    /// The columns a finger's held press was moved by to reach the button it
    /// took (prompt_finger_down); its moves and its release are moved as far.
    int32_t finger_shift_x{};
    int32_t finger_shift_y{}; ///< the rows, as finger_shift_x
};

/// What an event on a prompt asks of the host.
enum class PromptAction : uint8_t {
    none,     ///< nothing
    redraw,   ///< only its look changed: a hover, a press or the mark
    answered, ///< a button was pressed: PromptAnswer::button says which
};

/// What an event on a prompt did.
struct PromptAnswer {
    PromptAction action{PromptAction::none};
    int32_t button{-1}; ///< the button answered, from 0; -1 when none was
};

/// Returns a prompt's height: its header, its text and progress bar wrapped
/// in the fonts (at estimated_character_width a character without them)
/// and its footer, from least_notice_height to greatest_notice_height.
///
/// @param prompt the prompt
/// @param fonts the fonts it is drawn in; null to estimate widths
/// @return the height, in source pixels
[[nodiscard]] int32_t prompt_height(const Prompt& prompt, const DialogFonts* fonts = nullptr);

/// Returns the parts a prompt draws: its icon's place, its title, each line
/// of its text that its height does not cut, the failure, the progress bar
/// (prompt_bar_control) and its buttons, whose controls are their numbers.
///
/// @param prompt the prompt
/// @param fonts the fonts it is drawn in; null to estimate widths
/// @return the parts, in source pixels from the prompt's top left corner
[[nodiscard]] std::vector<LayoutPart>
prompt_layout(const Prompt& prompt, const DialogFonts* fonts = nullptr);

/// Tells whether every line of a prompt's text fits its height.
///
/// @param prompt the prompt
/// @param fonts the fonts it is drawn in; null to estimate widths
/// @return true when no line is cut
[[nodiscard]] bool prompt_fits(const Prompt& prompt, const DialogFonts* fonts = nullptr);

/// Moves the pointer: the button under it lights.
///
/// @param[in,out] prompt the prompt
/// @param x the pointer's column, in source pixels from the prompt's left edge
/// @param y the pointer's row, in source pixels from the prompt's top edge
/// @param height the prompt's height (prompt_height)
/// @return a redraw when the look changed
[[nodiscard]] PromptAnswer
prompt_pointer_move(Prompt& prompt, int32_t x, int32_t y, int32_t height);

/// Presses the pointer's button: a press on a button holds it.
///
/// @param[in,out] prompt the prompt
/// @param x the pointer's column
/// @param y the pointer's row
/// @param height the prompt's height
/// @return a redraw when a button is held
[[nodiscard]] PromptAnswer
prompt_pointer_down(Prompt& prompt, int32_t x, int32_t y, int32_t height);

/// Presses with a finger: as prompt_pointer_down, but a press on no button
/// takes the nearest one within `reach`, pressed at its point nearest the
/// finger; the press's moves and its release are moved as far.
///
/// @param[in,out] prompt the prompt
/// @param x the finger's column
/// @param y the finger's row
/// @param height the prompt's height
/// @param reach how far a button may lie from the finger, in source pixels
/// @return a redraw when a button is held
[[nodiscard]] PromptAnswer
prompt_finger_down(Prompt& prompt, int32_t x, int32_t y, int32_t height, int32_t reach);

/// Releases the pointer's button: a release over the button the press held
/// answers it.
///
/// @param[in,out] prompt the prompt
/// @param x the pointer's column
/// @param y the pointer's row
/// @param height the prompt's height
/// @return the answer, or a redraw
[[nodiscard]] PromptAnswer prompt_pointer_up(Prompt& prompt, int32_t x, int32_t y, int32_t height);

/// Takes a key: Enter and Space answer the marked button, Escape and N the
/// cancel button, Y the primary button; Left, Up and Shift+Tab mark the
/// button before, Right, Down and Tab the one after, round to the other end.
///
/// @param[in,out] prompt the prompt
/// @param key the key's meaning
/// @return the answer, a redraw, or nothing for a key it does not take
[[nodiscard]] PromptAnswer prompt_key(Prompt& prompt, DialogKey key);

/// Draws a prompt in a notice's look: its header with the Open Annihilation
/// icon (or the OA mark) and its title, its text, the failure in amber, the
/// progress bar, and its buttons right-aligned in the footer, an accent one
/// as OK looks and the others as Cancel, the marked one ringed in green.
///
/// @param[in,out] target the surface
/// @param placement where the prompt's top left corner lands, and its scale
/// @param prompt the prompt
/// @param fonts its fonts
/// @param icon the Open Annihilation icon; an empty picture draws the OA mark
void draw_prompt(
    oa::ui::frontend_renderer::Surface& target,
    const oa::ui::frontend_renderer::Placement& placement,
    const Prompt& prompt,
    const DialogFonts& fonts,
    const oa::ui::frontend_renderer::RgbaPicture& icon
);

} // namespace oa::ui::engine_settings
