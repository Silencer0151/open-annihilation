// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Where a notice, or a prompt, puts each of its parts, in source pixels from
// its top left corner. Their events (notice.cpp, prompt.cpp) and their
// drawing (dialog_draw.cpp) place things through place_notice and
// place_prompt, so a button is pressed where it is drawn.
#pragma once

#include "oa/ui/engine_settings/notice.hpp"
#include "oa/ui/engine_settings/prompt.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::engine_settings::notice_geometry {

using oa::ui::frontend_renderer::SourceRect;

/// The width of the notice's raised edge.
inline constexpr int32_t edge = 1;
/// The header's height, under the top edge.
inline constexpr int32_t header_height = 26;
/// The row of the line between the header and the text.
inline constexpr int32_t header_rule_row = edge + header_height;
/// The space between the notice's edge and what it holds.
inline constexpr int32_t padding = 12;
/// The Open Annihilation icon, in the header's middle rows.
inline constexpr SourceRect icon{padding, 4, 20, 20};
/// The columns between the icon and the title.
inline constexpr int32_t title_gap = 6;
/// Extra columns after each glyph of the title.
inline constexpr int32_t title_tracking = 1;
/// The text's first row.
inline constexpr int32_t text_top = header_rule_row + 1 + 10;
/// The text's first column.
inline constexpr int32_t text_left = padding;
/// The text's width.
inline constexpr int32_t text_width = notice_width - 2 * padding;
/// A line's height in the small font.
inline constexpr int32_t small_line_height = 12;
/// A line's height in the regular font, as a path's lines are drawn.
inline constexpr int32_t path_line_height = 16;
/// The rows between two paragraphs.
inline constexpr int32_t paragraph_gap = 6;
/// The rows between the last line of text and the footer's line.
inline constexpr int32_t text_bottom_gap = 10;
/// The footer's height, over the bottom edge.
inline constexpr int32_t footer_height = 32;
/// A button's height.
inline constexpr int32_t button_height = 17;
/// OK's width.
inline constexpr int32_t ok_width = 52;
/// The open button's width.
inline constexpr int32_t open_width = 96;
/// The columns between the two buttons.
inline constexpr int32_t button_gap = 5;
/// A prompt button's least width: OK's.
inline constexpr int32_t least_prompt_button_width = ok_width;
/// The columns a prompt button's caption keeps clear on its two sides together.
inline constexpr int32_t prompt_button_padding = 16;
/// A prompt's progress bar's height.
inline constexpr int32_t progress_bar_height = 8;

/// One line of the notice's text, placed.
struct Line {
    std::string text; ///< the line, UTF-8
    bool path{};      ///< drawn in the regular font, as a path
    bool failure{};   ///< drawn in amber: why the folder could not be opened
    SourceRect rect{};
};

/// The notice, placed.
struct Placed {
    int32_t height{};          ///< the notice's height
    SourceRect title{};        ///< the title's place in the header
    std::vector<Line> lines{}; ///< the lines that fit, top to bottom
    int32_t footer_rule{};     ///< the row of the line over the footer
    SourceRect open_button{};  ///< the button that opens the folder
    SourceRect ok_button{};    ///< OK, at the footer's right
};

/// A notice's or a prompt's text, placed under its header.
struct PlacedText {
    std::vector<Line> lines{}; ///< every line, top to bottom, before any is cut
    int32_t bottom{};          ///< the row under the last line
};

/// Places paragraphs and a failure under the header: paths in the regular
/// font broken at their separators, the rest in the small font broken
/// between words, paragraph_gap rows between them.
///
/// @param paragraphs the text
/// @param failure the failure line's text; empty for none
/// @param regular_width a text's width in the regular font
/// @param small_width a text's width in the small font
/// @return the lines and the row under them
[[nodiscard]] PlacedText place_text(
    const std::vector<NoticeParagraph>& paragraphs,
    std::string_view failure,
    const std::function<int32_t(std::string_view)>& regular_width,
    const std::function<int32_t(std::string_view)>& small_width
);

/// Returns the height of a box whose text ends at a row: its text, the gap
/// under it and the footer, from least_notice_height to greatest_notice_height.
///
/// @param text_bottom the row under the text
/// @return the height
[[nodiscard]] int32_t height_for(int32_t text_bottom) noexcept;

/// Returns the row of the line over a box's footer.
///
/// @param height the box's height
/// @return the row
[[nodiscard]] int32_t footer_rule_of(int32_t height) noexcept;

/// Returns the lines of placed text a box's height does not cut.
///
/// @param lines the lines
/// @param footer_rule the row of the line over the footer
/// @return the lines that fit
[[nodiscard]] std::vector<Line> lines_that_fit(std::vector<Line> lines, int32_t footer_rule);

/// A prompt, placed.
struct PlacedPrompt {
    int32_t height{};                  ///< the prompt's height
    SourceRect title{};                ///< the title's place in the header
    std::vector<Line> lines{};         ///< the lines that fit, top to bottom
    bool cut{};                        ///< a line of its text did not fit
    SourceRect bar{};                  ///< the progress bar; empty for none
    int32_t footer_rule{};             ///< the row of the line over the footer
    std::vector<SourceRect> buttons{}; ///< its buttons, left to right
};

/// Returns a prompt's buttons' places, right-aligned in the footer of a box
/// of a height, each as wide as its caption at estimated_character_width a
/// character and prompt_button_padding, at least least_prompt_button_width:
/// they depend on no font, so that a press lands where a button is drawn
/// whatever the fonts.
///
/// @param prompt the prompt
/// @param height the prompt's height
/// @return the buttons' rectangles, left to right
[[nodiscard]] std::vector<SourceRect> prompt_button_rects(const Prompt& prompt, int32_t height);

/// Places a prompt: its text wrapped, its progress bar under it, its
/// height, and its buttons (prompt_button_rects).
///
/// @param prompt the prompt
/// @param regular_width a text's width in the regular font
/// @param small_width a text's width in the small font
/// @return the placed prompt
[[nodiscard]] PlacedPrompt place_prompt(
    const Prompt& prompt,
    const std::function<int32_t(std::string_view)>& regular_width,
    const std::function<int32_t(std::string_view)>& small_width
);

/// Places the notice: its text wrapped, its height from it, and its buttons.
///
/// @param notice the notice
/// @param regular_width a text's width in the regular font
/// @param small_width a text's width in the small font
/// @return the placed notice
[[nodiscard]] Placed place_notice(
    const Notice& notice,
    const std::function<int32_t(std::string_view)>& regular_width,
    const std::function<int32_t(std::string_view)>& small_width
);

} // namespace oa::ui::engine_settings::notice_geometry
