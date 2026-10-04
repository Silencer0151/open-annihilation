// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Where a notice puts each of its parts, in source pixels from its top left
// corner. Its events (notice.cpp) and its drawing (dialog_draw.cpp) both
// place things through place_notice, so a button is pressed where it is
// drawn.
#pragma once

#include "oa/ui/engine_settings/notice.hpp"

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
