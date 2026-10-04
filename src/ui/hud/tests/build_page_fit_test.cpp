// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"

#include "oa/ui/hud/build_page_fit.hpp"

#include <cstdint>

using namespace oa::ui::hud;

namespace {

// The side panel's top edge, under the radar picture.
constexpr int32_t kPanelTop = 128;

void leaves_a_page_that_fits() {
    // A page that ends on the column's last row is drawn as authored.
    const auto scale = side_page_scale(0, kPanelTop, 480, 480);
    CHECK(!scale.scaled());
    CHECK(scale.authored_rows == 352 && scale.shown_rows == 352);
    CHECK(scale.to_column(200) == 200 && scale.to_page(200) == 200);
}

void scales_a_taller_page_to_the_column() {
    // A page reaching row 768 in a column of 480 rows: 640 rows into 352.
    const auto scale = side_page_scale(0, kPanelTop, 768, 480);
    CHECK(scale.scaled());
    CHECK(scale.authored_rows == 640 && scale.shown_rows == 352);
    // The page's last row meets the column's, and its first stays.
    CHECK(scale.to_column(0) == 0);
    CHECK(scale.to_column(640) == 352);
    // Widths scale as heights do: the 128-column panel takes 70 columns.
    CHECK(scale.to_column(128) == 70);
    // A point in the column maps back to the page row it shows.
    CHECK(scale.to_page(176) == 320);
    CHECK(scale.to_column(scale.to_page(100)) <= 100);
}

void keeps_a_row_of_a_column_without_room() {
    const auto scale = side_page_scale(0, kPanelTop, 768, 100);
    CHECK(scale.shown_rows == 1);
    CHECK(scale.to_column(639) == 0);
}

} // namespace

int main() {
    leaves_a_page_that_fits();
    scales_a_taller_page_to_the_column();
    keeps_a_row_of_a_column_without_room();
    return 0;
}
