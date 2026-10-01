// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The "+stats" overlay's panel: where the table frame_stats_table describes
// and the graph of the latest frames sit, in the source pixels the match's
// labels are drawn at; where the panel sits on the canvas and at what
// scale; how tall each frame's bar is and where the graph's lines sit; and
// the colour each grade shows in. The layout is made from the widest texts
// the table shows (frame_stats_widest_table), so the columns and the panel
// keep their places from frame to frame.
#pragma once

#include "oa/app/frame_pacing.hpp"
#include "oa/ui/display_layout.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace oa::app::frame_stats_panel {

/// Source pixels between the panel and the battlefield's right and bottom edges.
inline constexpr int kInset = 4;
/// Source pixels of the panel's edge: a black outline, and inside it a 3.1c
/// gadget's raised edge, light at the top and left and dark at the bottom
/// and right.
inline constexpr int kBevel = 2;
/// Source pixels between the panel's edge and its table and graph.
inline constexpr int kPadding = 3;
/// Source pixels between two of the table's columns.
inline constexpr int kColumnGap = 8;
/// Source pixels between the rule under the column names and the row under it.
inline constexpr int kRuleGap = 2;
/// Source pixels between the table's last row and the graph's edge.
inline constexpr int kGraphGap = 2;
/// Source pixels of the sunken edge round the graph.
inline constexpr int kGraphEdge = 1;
/// Source pixels across each column of the graph.
inline constexpr int kBarWidth = 1;
/// Source pixels of the graph's height: one for each millisecond up to
/// kGraphTopNs.
inline constexpr int kGraphHeight = 40;
/// The frame time a bar of the graph's whole height stands for, in
/// nanoseconds; a longer frame's bar stops there.
inline constexpr uint64_t kGraphTopNs =
    uint64_t{kGraphHeight} * frame_pacing::kNanosecondsPerMillisecond;
/// Source pixels from one dot of the graph's line at the frame's allowance
/// to the next.
inline constexpr int kDotPitch = 2;
/// Source pixels a tinted cell reaches past the time it sits behind, on
/// every side.
inline constexpr int kCellMargin = 1;

/// GUI palette slot of a time within the frame's allowance: a mid green,
/// darker than the health bar's, so that it stands apart from the yellow.
inline constexpr uint8_t kWithinFrameSlot = 240;

/// Returns the GUI palette slot a graded time shows in.
///
/// @param severity the time's grade
/// @param ungraded the slot for frame_pacing::TimeSeverity::none
/// @return kWithinFrameSlot within the frame's allowance; the health bar's
///     yellow (oa::ui::hud::kHealthMidColor) within a tick; its red
///     (oa::ui::hud::kHealthLowColor) over a tick; `ungraded` otherwise
[[nodiscard]] uint8_t severity_slot(frame_pacing::TimeSeverity severity, uint8_t ungraded) noexcept;

/// A rectangle in source pixels from the panel's top left corner.
struct Box {
    int x{};      ///< left edge
    int y{};      ///< top edge
    int width{};  ///< columns
    int height{}; ///< rows
};

/// Where the panel's parts sit, in source pixels from its top left corner.
struct PanelLayout {
    int width{};      ///< the whole panel, its edge included
    int height{};     ///< the whole panel, its edge included
    int row_height{}; ///< from one row of the table to the next
    /// Each row's top: the tops of its glyphs.
    std::array<int, frame_pacing::kFrameStatsRowsMost> row_y{};
    int rule_y{};     ///< the row of the rule under the column names; negative for none
    int label_x{};    ///< the labels' and the title's left edge, and the rule's
    int rule_width{}; ///< columns of the rule, as wide as the table and graph
    /// Each value column's right edge, where its values end.
    std::array<int, frame_pacing::kFrameStatsValueColumns> value_right{};
    /// Where each row's note starts.
    std::array<int, frame_pacing::kFrameStatsRowsMost> note_x{};
    Box graph{}; ///< the bars' area, inside the graph's sunken edge
};

/// Measures the table's texts as the drawing draws them.
struct TextWidthHooks {
    void* context{}; ///< handed back to width
    /// Returns a text's width in source pixels; null measures every text as 0 wide.
    int (*width)(void* context, std::string_view text){};
};

/// Lays the panel out for the widest texts its table shows.
///
/// Inside the panel's edge (kBevel) and padding (kPadding), the rows follow
/// each other a row_height apart, with a rule under the column names
/// (FrameStatsRowKind::heading) and kRuleGap under the rule. The labels take
/// the left column, as wide as the widest label; the title's label runs on
/// across the columns. The three value columns follow, kColumnGap apart,
/// each as wide as its widest text, every value ending at its column's
/// right edge. A row's note starts kColumnGap after the last value column
/// the row fills, or after the labels in a row that fills none. Under the
/// table, kGraphGap below its last row, the graph holds a column kBarWidth
/// across for each column of the frame history
/// (frame_pacing::kFrameGraphColumns) and is kGraphHeight high, inside a
/// sunken edge of kGraphEdge. The panel is as wide as the wider of the
/// table and the graph: when the graph is wider, the value columns and the
/// notes move right together, so that the widest row ends at the panel's
/// padding and the labels stay at the left; when the table is wider, the
/// graph ends at the right padding.
///
/// @param table the rows, each text the widest its cell shows
///     (frame_pacing::frame_stats_widest_table)
/// @param measure the widths of the table's texts
/// @param row_height source pixels from one row to the next: the font's
///     line height
/// @return the layout
[[nodiscard]] PanelLayout lay_out_panel(
    const frame_pacing::FrameStatsTable& table, const TextWidthHooks& measure, int row_height
) noexcept;

/// Returns the battlefield's bottom right quarter on a canvas.
///
/// @param match the canvas's layout
/// @return canvas pixels: from the battlefield's middle to its right edge
///     and to the bottom bar, the odd pixel of an odd width or height in it
[[nodiscard]] oa::ui::display_layout::Rect
battlefield_quarter(const oa::ui::display_layout::MatchLayout& match) noexcept;

/// Where the panel sits on a canvas.
struct PanelPlace {
    oa::ui::display_layout::Rect panel{}; ///< canvas pixels, its edge included
    int scale{};                          ///< canvas pixels to a source pixel
};

/// Places the panel at the battlefield's bottom right, kInset in from its
/// right edge and from the bottom bar.
///
/// The scale is the largest whole one, up to `text_scale`, at which the
/// panel and its inset fit the battlefield's bottom right quarter
/// (battlefield_quarter), and 1 when none does.
///
/// @param layout the panel's layout
/// @param match the canvas's layout
/// @param text_scale the HUD's text scale (Runtime::hud_text_scale)
/// @return the panel's place
[[nodiscard]] PanelPlace place_panel(
    const PanelLayout& layout, const oa::ui::display_layout::MatchLayout& match, int text_scale
) noexcept;

/// Returns the height of a frame's bar in the graph.
///
/// @param frame_ns the frame's time, nanoseconds
/// @return source pixels: frame_ns in milliseconds rounded to the nearest
///     (kGraphHeight for kGraphTopNs), at least 1 and at most kGraphHeight
[[nodiscard]] int bar_height(uint64_t frame_ns) noexcept;

/// Returns how high above the graph's bottom the line at a time sits: on
/// the row just above a bar of that time, so that a bar of a longer time
/// rises over it.
///
/// @param time_ns the line's time, nanoseconds
/// @return source pixels from the graph's bottom to the line's row's top:
///     bar_height(time_ns) + 1
[[nodiscard]] int line_rise(uint64_t time_ns) noexcept;

} // namespace oa::app::frame_stats_panel
