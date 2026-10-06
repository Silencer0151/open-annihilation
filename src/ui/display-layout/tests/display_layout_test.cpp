// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The side column fitted to its tallest page: a column that fits keeps the
// chrome's layout; a taller one is narrowed as a whole, and the bars and
// the battlefield, and points on them, follow its edge. A panel that reaches
// the bottom bar's rows lies on the bar, whole at the bars' scale.
#include "oa/test/check.hpp"

#include "oa/ui/display_layout.hpp"

namespace {

namespace layout = oa::ui::display_layout;

// The rows of a page laid out for a 768-row screen.
constexpr int kTallPageRows = 768;

void keeps_a_column_that_fits() {
    // 480 rows always fit; so do 540 at 1920x1080, whose chrome is at 2.
    for (const auto rows : {0, 480, 540}) {
        const auto chrome = layout::make_match_layout(1920, 1080);
        const auto fitted = layout::fit_side_column(chrome, rows);
        OA_CHECK(!fitted.column_narrowed());
        OA_CHECK(fitted.left == chrome.left && fitted.left == 256);
        OA_CHECK(fitted.hud_width == chrome.hud_width);
        OA_CHECK(fitted.column_scale == chrome.scale);
        // The bars reach the window's right edge: (1920 - 256) / 2 columns.
        OA_CHECK(fitted.bar_columns() == 832);
    }
}

void narrows_a_taller_column_as_a_whole() {
    const auto chrome = layout::make_match_layout(1920, 1080);
    const auto fitted = layout::fit_side_column(chrome, kTallPageRows);
    OA_CHECK(fitted.column_narrowed());
    // The tallest page ends on the window's last row, and the column is its
    // 128 columns at that scale wide: 1080 / 768 = 1.40625.
    OA_CHECK(fitted.column_scale == 1.40625);
    OA_CHECK(fitted.left == 180);
    // The bars keep the chrome's scale and reach from the column's edge to
    // the window's right edge, so they take the width the column gives up:
    // (1920 - 180) / 2 columns.
    OA_CHECK(fitted.scale == chrome.scale);
    OA_CHECK(fitted.hud_width == chrome.hud_width);
    OA_CHECK(fitted.bar_columns() == 870);
    OA_CHECK(fitted.battlefield_x() == 180 && fitted.battlefield_width() == 1920 - 180);
    // 1280x720: 720 / 768, 120 columns; 640x480: 80.
    OA_CHECK(
        layout::fit_side_column(layout::make_match_layout(1280, 720), kTallPageRows).left == 120
    );
    // On a 4:3 window the bars still reach the window's right edge.
    const auto small = layout::fit_side_column(layout::make_match_layout(640, 480), kTallPageRows);
    OA_CHECK(small.left == 80 && small.bar_columns() == 640 - 80);
    // 1280x720: 1160 / 1.5 = 773.3 columns, the last cut by the window's
    // edge, which the strip passes by a pixel.
    const auto wide = layout::fit_side_column(layout::make_match_layout(1280, 720), kTallPageRows);
    OA_CHECK(wide.bar_columns() == 774 && wide.bar_width() == 1161);
}

void maps_points_through_the_narrowed_column() {
    const auto fitted =
        layout::fit_side_column(layout::make_match_layout(1920, 1080), kTallPageRows);
    // A point of the column lands at its scale, across and down.
    const auto button = layout::source_to_canvas(fitted, 64, 640);
    OA_CHECK(button.x == 90 && button.y == 900);
    const auto back = layout::canvas_to_source(fitted, button.x, button.y);
    OA_CHECK(back.x == 64 && back.y == 640);
    // The column's last canvas column is its, the next the battlefield's.
    OA_CHECK(layout::canvas_to_source(fitted, 179, 500).x == 127);
    OA_CHECK(layout::canvas_to_source(fitted, 180, 500).x == layout::kSourceLeft);
    // The top bar counts from the column's edge at the chrome's scale.
    const auto bar = layout::source_to_canvas(fitted, layout::kSourceLeft + 100, 8);
    OA_CHECK(bar.x == 180 + 200 && bar.y == 16);
    const auto radar = layout::source_rect_to_canvas(fitted, 0, 0, 128, 128);
    OA_CHECK(radar.x == 0 && radar.y == 0 && radar.width == 180 && radar.height == 180);
    const auto strip = layout::source_rect_to_canvas(fitted, layout::kSourceLeft, 0, 512, 32);
    OA_CHECK(strip.x == 180 && strip.width == 1024 && strip.height == 64);
}

void keeps_a_panel_on_the_bottom_bar() {
    // The tab menu: 510x33 from (130, 447), its top row above the bar's 32.
    const auto native =
        layout::source_panel_to_canvas(layout::make_match_layout(640, 480), 130, 447, 510, 33);
    OA_CHECK(native.x == 130 && native.y == 447 && native.width == 510 && native.height == 33);
    // On a 1920x1080 window the chrome is at 2 and the bar sits on the
    // window's bottom edge, 120 rows below the chrome's 960: the menu lies
    // on the bar whole, its top row the two canvas rows above it.
    const auto wide = layout::make_match_layout(1920, 1080);
    OA_CHECK(wide.bottom_bar_y() == 1016);
    const auto menu = layout::source_panel_to_canvas(wide, 130, 447, 510, 33);
    OA_CHECK(menu.x == 260 && menu.y == 1014 && menu.width == 1020 && menu.height == 66);
    // Beside a narrowed column it counts from the column's edge.
    const auto beside = layout::source_panel_to_canvas(
        layout::fit_side_column(wide, kTallPageRows), 130, 447, 510, 33
    );
    OA_CHECK(beside.x == 184 && beside.y == 1014 && beside.width == 1020 && beside.height == 66);
    // A panel above the bar, ALLIES.GUI's, lies where source_rect_to_canvas
    // places it: from the top, at the chrome's scale.
    const auto allies = layout::source_panel_to_canvas(wide, 240, 108, 241, 339);
    const auto rect = layout::source_rect_to_canvas(wide, 240, 108, 241, 339);
    OA_CHECK(allies.x == rect.x && allies.y == rect.y);
    OA_CHECK(allies.width == rect.width && allies.height == rect.height);
    OA_CHECK(allies.y == 216 && allies.height == 678);
}

} // namespace

int main() {
    keeps_a_column_that_fits();
    narrows_a_taller_column_as_a_whole();
    maps_points_through_the_narrowed_column();
    keeps_a_panel_on_the_bottom_bar();
    return oa::test::check_exit_status();
}
