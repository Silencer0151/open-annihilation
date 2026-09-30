// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/display_layout.hpp"

#include <cmath>
#include <stdexcept>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)

int main() {
    using namespace oa::ui::display_layout;
    const auto original = make_match_layout(640, 480);
    CHECK(original.left == 128 && original.top == 32 && original.bottom == 32);
    CHECK(original.battlefield_width() == 512 && original.battlefield_height() == 416);
    CHECK(original.bottom_bar_y() == kSourceBottomBarY);
    const auto ident = canvas_to_source(original, 200, 100);
    CHECK(ident.x == 200 && ident.y == 100);
    const auto ident_bar = canvas_to_source(original, 200, 450);
    CHECK(ident_bar.x == 200 && ident_bar.y == 450);
    const auto ident_out = source_to_canvas(original, 400, 458);
    CHECK(ident_out.x == 400 && ident_out.y == 458);

    // Below the 1280x1024 layout the chrome scales with the window.
    const auto xga = make_match_layout(1024, 768);
    CHECK(xga.left == 205 && xga.top == 51 && xga.bottom == 51);
    CHECK(xga.hud_width == 1024 && xga.hud_height == 768);

    // 1280x1024 is the largest chrome: twice the 640x480 art, bottom bar on
    // the window's bottom edge, 64 blank rows under the side column.
    const auto sxga = make_match_layout(1280, 1024);
    CHECK(sxga.scale == 2.0);
    CHECK(sxga.left == 256 && sxga.top == 64 && sxga.bottom == 64);
    CHECK(sxga.hud_width == 1280 && sxga.hud_height == 960);
    CHECK(sxga.battlefield_width() == 1024 && sxga.battlefield_height() == 896);
    CHECK(sxga.bottom_bar_y() == 960);

    // Larger windows keep the 1280x1024 chrome and gain battlefield.
    const auto wide = make_match_layout(1920, 1080);
    CHECK(wide.width == 1920 && wide.height == 1080);
    CHECK(wide.scale == 2.0);
    CHECK(wide.left == 256 && wide.top == 64 && wide.bottom == 64);
    CHECK(wide.hud_width == 1280 && wide.hud_height == 960);
    CHECK(wide.battlefield_width() == 1664 && wide.battlefield_height() == 952);
    CHECK(wide.bottom_bar_y() == 1016);
    const auto qhd = make_match_layout(2560, 1440);
    CHECK(qhd.scale == 2.0 && qhd.left == 256 && qhd.hud_width == 1280 && qhd.hud_height == 960);

    // Top-anchored chrome maps uniformly.
    const auto left = canvas_to_source(wide, 0, 0);
    CHECK(left.x == 0 && left.y == 0);
    const auto header = canvas_to_source(wide, 0, 256);
    CHECK(header.x == 0 && header.y == 128);
    const auto playfield = canvas_to_source(wide, 256, 64);
    CHECK(playfield.x == 128 && playfield.y == 32);
    const auto produced = source_to_canvas(wide, 358, 5);
    CHECK(produced.x == 716 && produced.y == 10);
    const auto metal_bar = source_to_canvas(wide, 218, 12);
    CHECK(metal_bar.x == 436 && metal_bar.y == 24);

    // Bottom bar coordinates hang from the window's bottom edge.
    const auto unit_name = source_to_canvas(wide, 245, 452);
    CHECK(unit_name.x == 490 && unit_name.y == 1016 + 8);
    const auto bar_top = canvas_to_source(wide, 490, 1016);
    CHECK(bar_top.x == 245 && bar_top.y == kSourceBottomBarY);
    const auto bar_pixel = canvas_to_source(wide, 490, 1024);
    CHECK(bar_pixel.y == 452);
    const auto damage = source_rect_to_canvas(wide, 200, 463, 90, 2);
    CHECK(damage.x == 400 && damage.y == 1016 + 30 && damage.width == 180 && damage.height == 4);
    // The side column keeps its own bottom rows top-anchored.
    const auto column_foot = source_to_canvas(wide, 64, 470);
    CHECK(column_foot.x == 128 && column_foot.y == 940);
    const auto column_rect = source_rect_to_canvas(wide, 0, 128, 128, 352);
    CHECK(column_rect.y == 256 && column_rect.height == 704);

    // Blank areas and the battlefield never reach a bar gadget.
    const auto blank_right = canvas_to_source(wide, 1500, 10);
    CHECK(blank_right.x >= kSourceWidth);
    const auto blank_column = canvas_to_source(wide, 100, 1000);
    CHECK(blank_column.y >= kSourceHeight);
    const auto field_low = canvas_to_source(wide, 700, 1010);
    CHECK(field_low.y == kSourceBottomBarY - 1);
    const auto field_high = canvas_to_source(wide, 700, 300);
    CHECK(field_high.x == 350 && field_high.y == 150);

    const int cases[][2] = {
        {640, 480},
        {800, 600},
        {1024, 768},
        {1280, 720},
        {1280, 1024},
        {1920, 1080},
        {2560, 1440},
        {3840, 2160},
        {7680, 4320}
    };
    for (const auto& pair : cases) {
        const auto layout = make_match_layout(pair[0], pair[1]);
        CHECK(layout.width == pair[0] && layout.height == pair[1]);
        CHECK(layout.scale <= kMaxChromeScale);
        CHECK(layout.left + layout.battlefield_width() == layout.width);
        CHECK(layout.top + layout.battlefield_height() + layout.bottom == layout.height);
        CHECK(layout.left >= 1 && layout.top >= 1 && layout.bottom >= 1);
        CHECK(layout.battlefield_width() >= 32 && layout.battlefield_height() >= 32);
        CHECK(layout.hud_width <= layout.width && layout.hud_height <= layout.height);
        const auto origin = source_to_canvas(layout, kSourceLeft, kSourceTop);
        CHECK(origin.x == layout.left && origin.y == layout.top);
        const auto bar = source_to_canvas(layout, kSourceLeft, kSourceBottomBarY);
        CHECK(bar.y == layout.bottom_bar_y());
        const auto back = canvas_to_source(layout, layout.left, layout.bottom_bar_y());
        CHECK(back.x == kSourceLeft && back.y == kSourceBottomBarY);
    }

    // A battlefield-only frame: no chrome at any size, the battlefield the
    // whole frame, drawn at scale 1 with no HUD.
    for (const auto& pair : cases) {
        const auto layout = make_battlefield_layout(pair[0], pair[1]);
        CHECK(layout.width == pair[0] && layout.height == pair[1]);
        CHECK(layout.left == 0 && layout.top == 0 && layout.bottom == 0);
        CHECK(layout.battlefield_x() == 0 && layout.battlefield_y() == 0);
        CHECK(layout.battlefield_width() == pair[0] && layout.battlefield_height() == pair[1]);
        CHECK(layout.bottom_bar_y() == layout.height);
        CHECK(layout.hud_width == 0 && layout.hud_height == 0 && layout.scale == 1.0);
    }
    // Sizes the chrome layout would raise stay as asked, down to one pixel.
    const auto small = make_battlefield_layout(33, 17);
    CHECK(small.battlefield_width() == 33 && small.battlefield_height() == 17);
    const auto empty = make_battlefield_layout(0, -4);
    CHECK(empty.width == 1 && empty.height == 1);
    CHECK(empty.battlefield_width() == 1 && empty.battlefield_height() == 1);
    return 0;
}
