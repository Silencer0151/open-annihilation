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

namespace {

using namespace oa::ui::display_layout;

/// Returns whether two layouts hold the same values in every field.
bool same_layout(const MatchLayout& first, const MatchLayout& second) {
    if (first.width != second.width || first.height != second.height || first.left != second.left ||
        first.top != second.top || first.bottom != second.bottom ||
        first.hud_width != second.hud_width || first.hud_height != second.hud_height ||
        first.scale != second.scale || first.phone != second.phone ||
        first.safe.left != second.safe.left || first.safe.top != second.safe.top ||
        first.safe.right != second.safe.right || first.safe.bottom != second.safe.bottom ||
        first.px_per_point != second.px_per_point || first.chrome_scale != second.chrome_scale ||
        first.placed_count != second.placed_count)
        return false;
    return true;
}

/// Returns a region showing a source rectangle in a canvas rectangle.
PlacedRegion region_of(Rect source, Rect canvas, RegionRole role, int16_t gadget = -1) {
    PlacedRegion region{};
    region.source = source;
    region.canvas = canvas;
    region.role = role;
    region.gadget = gadget;
    return region;
}

/// The chrome cap: the two-argument layout is the capped one at
/// kMaxChromeScale, and a larger cap lets a canvas of device pixels keep the
/// chrome's size in points.
void check_chrome_cap() {
    const int sizes[][2] = {{640, 480}, {1180, 820}, {2880, 1800}};
    for (const auto& size : sizes)
        CHECK(same_layout(
            make_match_layout(size[0], size[1]),
            make_match_layout(size[0], size[1], kMaxChromeScale)
        ));
    // An iPad canvas in device pixels at two pixels per point.
    const auto points = make_match_layout(2360, 1640);
    CHECK(points.scale == 2.0);
    const auto pixels = make_match_layout(2360, 1640, 4.0);
    CHECK(std::abs(pixels.scale - 1640.0 / 480.0) < 1e-12);
    CHECK(pixels.scale == std::min(2360.0 / 640.0, 1640.0 / 480.0));
    CHECK(pixels.left == 437 && pixels.top == 109 && pixels.bottom == 109);
    CHECK(pixels.hud_width == 2187 && pixels.hud_height == 1640);
    CHECK(!pixels.phone && pixels.placed_count == 0 && pixels.chrome_scale == 0.0);
    // Under the cap the third argument changes nothing.
    CHECK(same_layout(make_match_layout(1024, 768, 4.0), make_match_layout(1024, 768)));
}

/// The phone layout: the battlefield is the whole canvas, in placed mode.
void check_phone_layout() {
    Insets safe{};
    safe.left = 59;
    safe.right = 59;
    safe.bottom = 21;
    const auto phone = make_phone_layout(852, 393, 1.0, safe);
    CHECK(phone.phone && placed_mode(phone));
    CHECK(phone.width == 852 && phone.height == 393);
    CHECK(phone.left == 0 && phone.top == 0 && phone.bottom == 0);
    CHECK(phone.hud_width == 0 && phone.hud_height == 0);
    CHECK(phone.battlefield_x() == 0 && phone.battlefield_y() == 0);
    CHECK(phone.battlefield_width() == 852 && phone.battlefield_height() == 393);
    CHECK(phone.bottom_bar_y() == 393);
    CHECK(phone.scale == 1.0 && phone.px_per_point == 1.0 && phone.chrome_scale == 0.0);
    CHECK(phone.safe.left == 59 && phone.safe.top == 0 && phone.safe.right == 59);
    CHECK(phone.safe.bottom == 21);
    CHECK(phone.placed_count == 0);
    // A canvas of device pixels: the battlefield stays at scale 1.
    const auto dense = make_phone_layout(2556, 1179, 3.0, {});
    CHECK(dense.scale == 1.0 && dense.px_per_point == 3.0);
    CHECK(dense.battlefield_width() == 2556 && dense.battlefield_height() == 1179);
    const auto unknown = make_phone_layout(852, 393, 0.0, {});
    CHECK(unknown.scale == 1.0 && unknown.px_per_point == 1.0);
    // Placed mode with no regions yet: no pixel reaches a gadget.
    const auto none = canvas_to_source(phone, 10, 10);
    CHECK(none.x == kOutsideSource.x && none.y == kOutsideSource.y);
    CHECK(!hud_covers(phone, 10, 10) && region_at(phone, 10, 10) == nullptr);
    // The source battlefield still lands on the canvas battlefield.
    const auto corner = source_to_canvas(phone, kSourceLeft, kSourceTop);
    CHECK(corner.x == 0 && corner.y == 0);
    const auto dense_point = source_to_canvas(dense, kSourceLeft + 10, kSourceTop + 20);
    CHECK(dense_point.x == 10 && dense_point.y == 20);
    // The desktop layouts are never in placed mode.
    CHECK(!placed_mode(make_match_layout(852, 393)) && !placed_mode(make_battlefield_layout(8, 8)));
}

/// Mapping through placed regions, both ways.
void check_placed_mapping() {
    auto phone = make_phone_layout(852, 393, 1.0, {});
    // A 55x31 order button drawn into a 64x44 cell.
    const Rect button_source{5, 375, 55, 31};
    const Rect button_canvas{700, 100, 64, 44};
    CHECK(phone.add_placed(region_of(button_source, button_canvas, RegionRole::gadget, 7)));
    // The minimap's well drawn into a 104 pt square.
    const Rect minimap_source{0, 0, 128, 128};
    const Rect minimap_canvas{67, 8, 104, 104};
    CHECK(phone.add_placed(region_of(minimap_source, minimap_canvas, RegionRole::minimap)));
    CHECK(phone.placed_count == 2);

    const auto top_left = canvas_to_source(phone, 700, 100);
    CHECK(top_left.x == 5 && top_left.y == 375);
    const auto bottom_right = canvas_to_source(phone, 763, 143);
    CHECK(bottom_right.x == 59 && bottom_right.y == 405);
    const auto top_right = canvas_to_source(phone, 763, 100);
    CHECK(top_right.x == 59 && top_right.y == 375);
    const auto bottom_left = canvas_to_source(phone, 700, 143);
    CHECK(bottom_left.x == 5 && bottom_left.y == 405);
    const auto centre = canvas_to_source(phone, 732, 122);
    CHECK(centre.x == 5 + 27 && centre.y == 375 + 15);
    // Next to the cell, and on open battlefield: nothing.
    const auto beside = canvas_to_source(phone, 764, 122);
    CHECK(beside.x == kOutsideSource.x && beside.y == kOutsideSource.y);
    const auto above = canvas_to_source(phone, 732, 99);
    CHECK(above.x == kOutsideSource.x && above.y == kOutsideSource.y);
    const auto open = canvas_to_source(phone, 400, 200);
    CHECK(open.x == kOutsideSource.x && open.y == kOutsideSource.y);

    // The source maps back onto the cell: its corners are the cell's.
    const auto button_origin = source_to_canvas(phone, 5, 375);
    CHECK(button_origin.x == 700 && button_origin.y == 100);
    const auto button_rect = source_rect_to_canvas(phone, 5, 375, 55, 31);
    CHECK(button_rect.x == 700 && button_rect.y == 100);
    CHECK(button_rect.width == 64 && button_rect.height == 44);
    // Every source pixel of the button maps to a canvas pixel that maps back to it.
    for (int sy = 375; sy < 375 + 31; ++sy)
        for (int sx = 5; sx < 5 + 55; ++sx) {
            const auto canvas = source_to_canvas(phone, sx, sy);
            const auto back = canvas_to_source(phone, canvas.x, canvas.y);
            CHECK(back.x == sx && back.y == sy);
        }

    // The radar picture (126 pixels at offset 1) lands inside the minimap region.
    const auto picture = source_rect_to_canvas(phone, 1, 1, 126, 126);
    CHECK(picture.x >= minimap_canvas.x && picture.y >= minimap_canvas.y);
    CHECK(picture.x + picture.width <= minimap_canvas.x + minimap_canvas.width);
    CHECK(picture.y + picture.height <= minimap_canvas.y + minimap_canvas.height);
    CHECK(picture.x == 68 && picture.width == 103);
    const auto well = source_to_canvas(phone, 64, 64);
    CHECK(well.x == 67 + 52 && well.y == 8 + 52);
    // A rectangle reaching the region's far edge keeps the region's mapping.
    const auto whole = source_rect_to_canvas(phone, 0, 0, 128, 128);
    CHECK(whole.x == 67 && whole.y == 8 && whole.width == 104 && whole.height == 104);
    // A lone point on the region's far edge maps with the region, unless it
    // lies on the source battlefield.
    const auto far_edge = source_to_canvas(phone, 64, 128);
    CHECK(far_edge.x == 67 + 52 && far_edge.y == 8 + 104);
    const auto field_corner = source_to_canvas(phone, 128, 128);
    CHECK(field_corner.x == 0 && field_corner.y == 96);

    // The source battlefield maps to the canvas battlefield.
    const auto field = source_to_canvas(phone, kSourceLeft, kSourceTop);
    CHECK(field.x == phone.battlefield_x() && field.y == phone.battlefield_y());
    const auto field_rect = source_rect_to_canvas(phone, 200, 100, 50, 40);
    CHECK(field_rect.x == 72 && field_rect.y == 68);
    CHECK(field_rect.width == 50 && field_rect.height == 40);
    const auto field_edge = source_rect_to_canvas(phone, kSourceLeft, kSourceTop, 512, 416);
    CHECK(field_edge.width == 512 && field_edge.height == 416);
}

/// The region table: the topmost region wins, and it holds at most kMaxPlacedRegions.
void check_placed_table() {
    auto phone = make_phone_layout(852, 393, 1.0, {});
    CHECK(phone.add_placed(region_of({0, 0, 128, 128}, {10, 10, 100, 100}, RegionRole::minimap)));
    CHECK(phone.add_placed(region_of({0, 128, 128, 352}, {50, 50, 100, 100}, RegionRole::sheet)));
    const auto* lower = region_at(phone, 20, 20);
    CHECK(lower != nullptr && lower->role == RegionRole::minimap);
    const auto* upper = region_at(phone, 60, 60);
    CHECK(upper != nullptr && upper->role == RegionRole::sheet);
    CHECK(region_at(phone, 149, 149) == upper && region_at(phone, 150, 150) == nullptr);
    CHECK(hud_covers(phone, 109, 20) && !hud_covers(phone, 9, 20) && !hud_covers(phone, 300, 300));
    // The overlap maps through the region on top.
    const auto through_top = canvas_to_source(phone, 60, 60);
    CHECK(through_top.x == 12 && through_top.y == 128 + 35);
    // The largest scale: both regions draw 128 source columns in 100.
    CHECK(std::abs(largest_region_scale(phone) - 100.0 / 128.0) < 1e-12);
    CHECK(largest_region_scale(make_phone_layout(10, 10, 1.0, {})) == 0.0);

    for (std::size_t count = phone.placed_count; count < kMaxPlacedRegions; ++count)
        CHECK(phone.add_placed(region_of({0, 0, 1, 1}, {0, 0, 1, 1}, RegionRole::chrome)));
    CHECK(phone.placed_count == kMaxPlacedRegions);
    CHECK(!phone.add_placed(region_of({0, 0, 1, 1}, {500, 0, 1, 1}, RegionRole::chrome)));
    CHECK(phone.placed_count == kMaxPlacedRegions);
    CHECK(!hud_covers(phone, 500, 0));

    // Fitting a panel whole into an area, centred.
    const auto fitted = fit_inside({100, 0, 400, 352}, 128, 352);
    CHECK(fitted.width == 128 && fitted.height == 352 && fitted.x == 236 && fitted.y == 0);
    const auto shrunk = fit_inside({0, 0, 200, 176}, 128, 352);
    CHECK(shrunk.width == 64 && shrunk.height == 176 && shrunk.x == 68 && shrunk.y == 0);
    const auto wide = fit_inside({0, 0, 132, 44}, 55, 31);
    CHECK(wide.height == 44 && wide.width == 78 && wide.x == 27 && wide.y == 0);
    const auto nothing = fit_inside({0, 0, 0, 10}, 55, 31);
    CHECK(nothing.width == 0 && nothing.height == 0);
    // A cap keeps a small panel from filling a large area.
    const auto capped = fit_inside({8, 8, 836, 377}, 325, 190, 1.5);
    CHECK(capped.width == 488 && capped.height == 285);
    CHECK(capped.x == 8 + (836 - 488) / 2 && capped.y == 8 + (377 - 285) / 2);
    const auto uncapped = fit_inside({8, 8, 836, 377}, 325, 190, 0.0);
    CHECK(uncapped.height == 377 && uncapped.width == 645);
}

} // namespace

int main() {
    using namespace oa::ui::display_layout;
    const auto original = make_match_layout(640, 480);
    CHECK(original.left == 128 && original.top == 32 && original.bottom == 32);
    CHECK(original.battlefield_width() == 512 && original.battlefield_height() == 416);
    CHECK(original.bottom_bar_y() == kSourceBottomBarY);
    CHECK(original.bar_columns() == 512);
    // The bars' art begins one column right of the side column's 128.
    CHECK(kSourceBarArtLeft == 129);
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
    CHECK(xga.bar_columns() == 512);

    // 1280x1024 is the largest chrome: twice the 640x480 art, bottom bar on
    // the window's bottom edge, 64 blank rows under the side column.
    const auto sxga = make_match_layout(1280, 1024);
    CHECK(sxga.scale == 2.0);
    CHECK(sxga.left == 256 && sxga.top == 64 && sxga.bottom == 64);
    CHECK(sxga.hud_width == 1280 && sxga.hud_height == 960);
    CHECK(sxga.battlefield_width() == 1024 && sxga.battlefield_height() == 896);
    CHECK(sxga.bottom_bar_y() == 960);
    CHECK(sxga.bar_columns() == 512);

    // Larger windows keep the 1280x1024 chrome and gain battlefield; the
    // bars reach their right edge at the chrome's scale.
    const auto wide = make_match_layout(1920, 1080);
    CHECK(wide.width == 1920 && wide.height == 1080);
    CHECK(wide.scale == 2.0);
    CHECK(wide.left == 256 && wide.top == 64 && wide.bottom == 64);
    CHECK(wide.hud_width == 1280 && wide.hud_height == 960);
    CHECK(wide.battlefield_width() == 1664 && wide.battlefield_height() == 952);
    CHECK(wide.bottom_bar_y() == 1016);
    CHECK(wide.bar_columns() == 832);
    const auto qhd = make_match_layout(2560, 1440);
    CHECK(qhd.scale == 2.0 && qhd.left == 256 && qhd.hud_width == 1280 && qhd.hud_height == 960);
    CHECK(qhd.bar_columns() == 1152);
    // A window wider than 4:3 below the largest chrome: 1088 / 1.5 = 725.3
    // columns, the last cut by the window's right edge.
    const auto hd = make_match_layout(1280, 720);
    CHECK(hd.bar_columns() == 726 && hd.bar_width() == 1089);
    // Between the scales the bars keep the 640x480 interface's 512 columns.
    CHECK(xga.bar_width() == 819 && make_match_layout(800, 600).bar_columns() == 512);

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

    // The bars past the interface's 640 columns, the blank areas and the
    // battlefield never reach a bar gadget.
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
        // The bars reach the window's right edge, and their last column
        // starts inside it.
        CHECK(layout.left + layout.bar_width() >= layout.width);
        CHECK(layout.left + (layout.bar_columns() - 1) * layout.scale < layout.width);
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

    check_chrome_cap();
    check_phone_layout();
    check_placed_mapping();
    check_placed_table();
    return 0;
}
