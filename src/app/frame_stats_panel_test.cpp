// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The "+stats" overlay's panel laid out over texts of known widths: the
// title, the column names over a rule, labels at the left, values
// right-aligned in columns as wide as their widest text, each note after
// its row's last value, the columns moved right together when the graph is
// the wider, the graph under the table, and the same layout whatever the
// figures; its place and scale on windows of several sizes; the height of
// each frame's bar and of the graph's lines; and each grade's colour.
#include "frame_stats_panel.hpp"

#include "oa/app/frame_pacing.hpp"
#include "oa/ui/display_layout.hpp"
#include "oa/ui/hud/health_bar.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>
#include <utility>

namespace {

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

namespace frame_pacing = oa::app::frame_pacing;
namespace panel = oa::app::frame_stats_panel;
using frame_pacing::kNanosecondsPerMillisecond;

/// Source pixels from one row to the next in these tests.
constexpr int kRowHeight = 13;
/// Source pixels across the graph and its edge: one bar for each column of
/// the history.
constexpr int kFramedGraph =
    static_cast<int>(frame_pacing::kFrameGraphColumns) * panel::kBarWidth + 2 * panel::kGraphEdge;
/// Source pixels between the panel's outside and its contents.
constexpr int kInner = panel::kBevel + panel::kPadding;

/// Measures every character as a given number of pixels wide.
///
/// @param context the pixels a character, an int
/// @param text the text
/// @return the text's width
int fixed_width(void* context, std::string_view text) {
    return static_cast<int>(text.size()) * *static_cast<const int*>(context);
}

/// Measures a text in the widths of the match label font's glyphs
/// (fonts/CONSOLE.FNT): six pixels for most, three for a full stop, four
/// for i and l, five for s, seven for a space and eight for m, v, w and y.
///
/// @param text the text
/// @return the text's width
int console_width(void*, std::string_view text) {
    int width = 0;
    for (const char glyph : text) {
        switch (glyph) {
        case '.':
            width += 3;
            break;
        case 'i':
        case 'l':
            width += 4;
            break;
        case 's':
            width += 5;
            break;
        case ' ':
            width += 7;
            break;
        case 'm':
        case 'v':
        case 'w':
        case 'y':
            width += 8;
            break;
        default:
            width += 6;
            break;
        }
    }
    return width;
}

/// Writes a text into one of a table's texts.
///
/// @param[out] out the table's text
/// @param text the text, shorter than the text's bytes
void put(frame_pacing::FrameStatsText& out, std::string_view text) {
    out.text = {};
    for (std::size_t at = 0; at < text.size(); ++at)
        out.text[at] = text[at];
}

/// Adds a row to a table.
///
/// @param[in,out] table the table
/// @param kind what the row shows
/// @param label the row's label
/// @param least its first value column; empty for none
/// @param mean its second
/// @param most its third
/// @param note its note; empty for none
void add_row(
    frame_pacing::FrameStatsTable& table,
    frame_pacing::FrameStatsRowKind kind,
    std::string_view label,
    std::string_view least,
    std::string_view mean,
    std::string_view most,
    std::string_view note
) {
    auto& row = table.rows[table.row_count++];
    row.kind = kind;
    put(row.label, label);
    put(row.values[0], least);
    put(row.values[1], mean);
    put(row.values[2], most);
    put(row.note, note);
}

// Six pixels a character: the graph is wider than the table, so the values
// and notes move right until the widest row ends at the padding; the title
// runs across the columns, and the rows under the column names sit below
// the rule.
void test_columns_under_a_wider_graph() {
    frame_pacing::FrameStatsTable table{};
    add_row(table, frame_pacing::FrameStatsRowKind::title, "Frame stats (ms)", "", "", "", "");
    add_row(table, frame_pacing::FrameStatsRowKind::heading, "", "min", "avg", "max", "");
    add_row(
        table,
        frame_pacing::FrameStatsRowKind::measure,
        "present",
        "10.00",
        "8.33",
        "123.45",
        "30/s"
    );
    add_row(table, frame_pacing::FrameStatsRowKind::rate, "FPS", "", "120", "", "limit 120");
    add_row(table, frame_pacing::FrameStatsRowKind::count, "units", "", "", "", "40 drawn");
    int pixels = 6;
    const panel::TextWidthHooks measure{&pixels, fixed_width};
    const auto layout = panel::lay_out_panel(table, measure, kRowHeight);
    // Labels 42 wide ("present"; the title sets no column); columns 30, 24
    // and 36 wide; the widest row ends with the 24 of "30/s", and the graph
    // is wider, so everything right of the labels moves right by the
    // difference.
    constexpr int kGap = panel::kColumnGap;
    constexpr std::array<int, 3> kEnds{42 + kGap + 30, 42 + 2 * kGap + 54, 42 + 3 * kGap + 90};
    constexpr int kWidest = kEnds[2] + kGap + 24;
    constexpr int kShift = kFramedGraph - kWidest;
    static_assert(kShift > 0);
    CHECK(layout.label_x == kInner);
    for (std::size_t column = 0; column < kEnds.size(); ++column)
        CHECK(layout.value_right[column] == kInner + kEnds[column] + kShift);
    // Notes after the last value each row shows, or after the labels.
    CHECK(layout.note_x[2] == layout.value_right[2] + kGap);
    CHECK(layout.note_x[3] == layout.value_right[1] + kGap);
    CHECK(layout.note_x[4] == kInner + 42 + kGap + kShift);
    // The widest row ends at the padding.
    CHECK(layout.note_x[2] + 4 * pixels == layout.width - kInner);
    CHECK(layout.width == 2 * kInner + kFramedGraph);
    CHECK(layout.rule_width == kFramedGraph);
    // The values in a column end together, the widest starting a gap after
    // the column before.
    CHECK(layout.value_right[1] - 4 * pixels - panel::kColumnGap >= layout.value_right[0]);
    CHECK(layout.value_right[2] - 6 * pixels - panel::kColumnGap == layout.value_right[1]);
    // The title and the column names a row apart; the rule a row under the
    // names, and the next row the rule and its gap below that.
    CHECK(layout.row_height == kRowHeight);
    CHECK(layout.row_y[0] == kInner);
    CHECK(layout.row_y[1] == kInner + kRowHeight);
    CHECK(layout.rule_y == kInner + 2 * kRowHeight);
    CHECK(layout.row_y[2] == layout.rule_y + 1 + panel::kRuleGap);
    CHECK(layout.row_y[3] == layout.row_y[2] + kRowHeight);
    CHECK(layout.row_y[4] == layout.row_y[3] + kRowHeight);
    // The graph fills the width under the rows.
    CHECK(layout.graph.x == kInner + panel::kGraphEdge);
    CHECK(layout.graph.y == layout.row_y[4] + kRowHeight + panel::kGraphGap + panel::kGraphEdge);
    CHECK(layout.graph.width == static_cast<int>(frame_pacing::kFrameGraphColumns));
    CHECK(layout.graph.height == panel::kGraphHeight);
    CHECK(layout.height == layout.graph.y + panel::kGraphHeight + panel::kGraphEdge + kInner);
}

// Twenty pixels a character: the table is wider than the graph, which ends
// at the right padding; nothing moves. A table with no column names has no
// rule.
void test_graph_under_a_wider_table() {
    frame_pacing::FrameStatsTable table{};
    add_row(table, frame_pacing::FrameStatsRowKind::measure, "frame", "8.00", "8.50", "9.00", "");
    add_row(
        table, frame_pacing::FrameStatsRowKind::measure, "tick", "2.00", "3.00", "4.00", "30/s"
    );
    int pixels = 20;
    const panel::TextWidthHooks measure{&pixels, fixed_width};
    const auto layout = panel::lay_out_panel(table, measure, kRowHeight);
    const int labels = 5 * pixels;
    const int column = 4 * pixels;
    CHECK(layout.value_right[0] == kInner + labels + panel::kColumnGap + column);
    CHECK(layout.value_right[2] == kInner + labels + 3 * (panel::kColumnGap + column));
    const int table_width = labels + 3 * (panel::kColumnGap + column) + panel::kColumnGap + column;
    CHECK(table_width > kFramedGraph);
    CHECK(layout.width == 2 * kInner + table_width);
    CHECK(layout.graph.x + layout.graph.width + panel::kGraphEdge == layout.width - kInner);
    CHECK(layout.rule_y < 0);
    CHECK(layout.row_y[1] == kInner + kRowHeight);
}

// With no measure every text is 0 wide: the columns are a gap apart and
// the graph sets the width.
void test_without_a_measure() {
    frame_pacing::FrameStatsTable table{};
    add_row(table, frame_pacing::FrameStatsRowKind::measure, "frame", "8.00", "8.50", "9.00", "");
    const auto layout = panel::lay_out_panel(table, {}, kRowHeight);
    CHECK(layout.width == 2 * kInner + kFramedGraph);
    CHECK(layout.value_right[1] - layout.value_right[0] == panel::kColumnGap);
    CHECK(layout.value_right[2] - layout.value_right[1] == panel::kColumnGap);
    CHECK(layout.value_right[2] == layout.width - kInner);
}

// Laid out from the widest table, the panel is the same whatever the
// figures: idle or busy, with no units or with thousands, before the
// first second or after a hitch. In the match label font's widths and 13
// pixels a row, it fits the battlefield's bottom right quarter of a
// 640x480 screen (256 by 208 pixels) with its inset.
void test_layout_stays_in_place() {
    const panel::TextWidthHooks measure{nullptr, console_width};
    const auto widest = frame_pacing::frame_stats_widest_table();
    CHECK(widest.row_count == frame_pacing::kFrameStatsRowsMost);
    const auto layout = panel::lay_out_panel(widest, measure, kRowHeight);
    // Every text the table shows in play fits the room the widest kept.
    frame_pacing::FrameStatsWindow window{};
    uint64_t now = frame_pacing::kNanosecondsPerSecond;
    (void)frame_pacing::roll_frame_stats(window, now);
    for (uint32_t frame = 0; frame < 120; ++frame) {
        const uint64_t frame_ns = (frame % 30 == 0 ? 140 : 8) * kNanosecondsPerMillisecond;
        now += frame_ns;
        for (std::size_t measure_index = 0; measure_index < frame_pacing::kFrameMeasureCount;
             ++measure_index)
            frame_pacing::note_frame_measure(
                window, static_cast<frame_pacing::FrameMeasure>(measure_index), frame_ns
            );
    }
    (void)frame_pacing::roll_frame_stats(window, now);
    for (const frame_pacing::FrameStatsNotes notes : {
             frame_pacing::FrameStatsNotes{120, 120, 0, 0},
             frame_pacing::FrameStatsNotes{120, 30, 25, 0},
             frame_pacing::FrameStatsNotes{1000, 1000, 1200, 1200},
             frame_pacing::FrameStatsNotes{0, 0, 9999, 9999},
         }) {
        const auto table = frame_pacing::frame_stats_table(window, notes);
        CHECK(table.row_count == widest.row_count);
        for (std::size_t row = 0; row < table.row_count; ++row) {
            CHECK(table.rows[row].kind == widest.rows[row].kind);
            for (std::size_t column = 0; column < table.rows[row].values.size(); ++column)
                CHECK(
                    table.rows[row].values[column].view().size() <=
                    widest.rows[row].values[column].view().size()
                );
            CHECK(table.rows[row].note.view().size() <= widest.rows[row].note.view().size());
        }
    }
    constexpr int kQuarterWidth = 256;
    constexpr int kQuarterHeight = 208;
    CHECK(layout.width + panel::kInset <= kQuarterWidth);
    CHECK(layout.height + panel::kInset <= kQuarterHeight);
}

// The panel sits kInset in from the battlefield's bottom right corner, at
// the HUD's text scale where it fits the quarter, and smaller where not.
void test_panel_place() {
    namespace display_layout = oa::ui::display_layout;
    const panel::TextWidthHooks measure{nullptr, console_width};
    const auto layout =
        panel::lay_out_panel(frame_pacing::frame_stats_widest_table(), measure, kRowHeight);
    const auto inside = [](const display_layout::Rect& box, const display_layout::Rect& area) {
        return box.x >= area.x && box.y >= area.y && box.x + box.width <= area.x + area.width &&
               box.y + box.height <= area.y + area.height;
    };
    // 640x480: the battlefield from (128, 32) to (640, 448); its quarter
    // from (384, 240).
    auto match = display_layout::make_match_layout(640, 480);
    const auto quarter = panel::battlefield_quarter(match);
    CHECK(quarter.x == 384 && quarter.y == 240 && quarter.width == 256 && quarter.height == 208);
    auto place = panel::place_panel(layout, match, 1);
    CHECK(place.scale == 1);
    CHECK(place.panel.x + place.panel.width + panel::kInset == 640);
    CHECK(place.panel.y + place.panel.height + panel::kInset == match.bottom_bar_y());
    CHECK(inside(place.panel, quarter));
    // 1024x768 rounds the HUD's scale of 1.6 up to 2, at which the panel
    // would cover more than the quarter: it stays at 1.
    match = display_layout::make_match_layout(1024, 768);
    place = panel::place_panel(layout, match, 2);
    CHECK(place.scale == 1);
    CHECK(inside(place.panel, panel::battlefield_quarter(match)));
    // 1280x960 and 1920x1080 hold it at 2.
    for (const auto& [width, height] : {std::pair{1280, 960}, std::pair{1920, 1080}}) {
        match = display_layout::make_match_layout(width, height);
        place = panel::place_panel(layout, match, 2);
        CHECK(place.scale == 2);
        CHECK(place.panel.width == 2 * layout.width);
        CHECK(place.panel.x + place.panel.width + 2 * panel::kInset == width);
        CHECK(inside(place.panel, panel::battlefield_quarter(match)));
    }
    // A text scale below 1 draws at 1.
    CHECK(panel::place_panel(layout, display_layout::make_match_layout(640, 480), 0).scale == 1);
}

void test_bar_heights() {
    // One pixel a millisecond, rounded to the nearest; at least one pixel,
    // at most the graph's height.
    CHECK(panel::bar_height(0) == 1);
    CHECK(panel::bar_height(kNanosecondsPerMillisecond) == 1);
    CHECK(panel::bar_height(8'333'333) == 8);
    CHECK(panel::bar_height(8'500'000) == 9);
    CHECK(panel::bar_height(frame_pacing::kTickBudgetNs) == 33);
    CHECK(panel::bar_height(panel::kGraphTopNs) == panel::kGraphHeight);
    CHECK(panel::bar_height(frame_pacing::kNanosecondsPerSecond) == panel::kGraphHeight);
    CHECK(panel::bar_height(std::numeric_limits<uint64_t>::max()) == panel::kGraphHeight);
    // A line sits on the row above a bar of its time, so a bar on time
    // stays under the line at the frame's allowance and a later one rises
    // over it; the tick's line sits inside the graph.
    const uint64_t allowance =
        frame_pacing::frame_allowance_ns(120, frame_pacing::FrameWait::precise);
    CHECK(panel::line_rise(allowance) == 10);
    CHECK(panel::bar_height(8'333'333) < panel::line_rise(allowance));
    CHECK(panel::bar_height(allowance) < panel::line_rise(allowance));
    CHECK(panel::bar_height(9'500'000) >= panel::line_rise(allowance));
    CHECK(panel::line_rise(frame_pacing::kTickBudgetNs) == 34);
    CHECK(panel::line_rise(frame_pacing::kTickBudgetNs) <= panel::kGraphHeight);
}

// Each grade's colour: a mid green within the frame's allowance, the
// health bar's yellow within a tick and its red over a tick.
void test_severity_slots() {
    constexpr uint8_t kUngraded = 23;
    CHECK(
        panel::severity_slot(frame_pacing::TimeSeverity::within_frame, kUngraded) ==
        panel::kWithinFrameSlot
    );
    CHECK(
        panel::severity_slot(frame_pacing::TimeSeverity::within_tick, kUngraded) ==
        oa::ui::hud::kHealthMidColor
    );
    CHECK(
        panel::severity_slot(frame_pacing::TimeSeverity::over_tick, kUngraded) ==
        oa::ui::hud::kHealthLowColor
    );
    CHECK(panel::severity_slot(frame_pacing::TimeSeverity::none, kUngraded) == kUngraded);
    CHECK(panel::kWithinFrameSlot != oa::ui::hud::kHealthHighColor);
}

} // namespace

int main() {
    test_columns_under_a_wider_graph();
    test_graph_under_a_wider_table();
    test_without_a_measure();
    test_layout_stays_in_place();
    test_panel_place();
    test_bar_heights();
    test_severity_slots();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
