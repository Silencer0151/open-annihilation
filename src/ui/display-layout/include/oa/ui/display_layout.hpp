// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdint.h>

namespace oa::ui::display_layout {

inline constexpr int kSourceWidth = 640;
inline constexpr int kSourceHeight = 480;
inline constexpr int kSourceLeft = 128;
inline constexpr int kSourceTop = 32;
inline constexpr int kSourceBottom = 32;
inline constexpr int kSourceBattlefieldWidth = kSourceWidth - kSourceLeft;
inline constexpr int kSourceBattlefieldHeight = kSourceHeight - kSourceTop - kSourceBottom;
inline constexpr int kSourceBottomBarY = kSourceHeight - kSourceBottom;
// The screen column where the bars' art begins, one right of the side
// column's 128: the first piece of each bar is drawn there, unscaled.
inline constexpr int kSourceBarArtLeft = kSourceLeft + 1;
// The chrome grows with the window only up to the 1280x1024 layout (twice the
// game's 640x480 art). Larger windows keep that size, and the bars' art goes
// on along them as the game draws it on a wider screen instead of being
// stretched.
inline constexpr double kMaxChromeScale = 2.0;

/// A point in pixels.
struct Point {
    int x = 0;
    int y = 0;
};

/// A rectangle in pixels: its top-left corner and its size.
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

/// Canvas pixels kept clear on each side (the safe area's insets).
struct Insets {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

/// The most regions a placed layout holds.
inline constexpr std::size_t kMaxPlacedRegions = 48;

/// What a placed region shows.
enum class RegionRole : uint8_t {
    chrome,  ///< a piece of the side column or the bars with no gadget of its own
    minimap, ///< the minimap (the source's radar square)
    readout, ///< a strip of readouts, such as the resources
    gadget,  ///< one gadget of the loaded HUD page
    sheet,   ///< a whole panel (the in-game menu, the unit info panel)
};

/// A rectangle of the 640x480 HUD surface drawn into a rectangle of the canvas, any aspect.
struct PlacedRegion {
    Rect source{};                        ///< 640x480 HUD surface pixels
    Rect canvas{};                        ///< canvas pixels
    int16_t gadget = -1;                  ///< index in the loaded HUD's gadgets, -1 for none
    RegionRole role = RegionRole::chrome; ///< what the region shows
};

/// A source point outside the 640x480 surface: no gadget, no bar.
inline constexpr Point kOutsideSource{-10000, -10000};

// Chrome stays 4:3-proportioned (scaled by min(w/640, h/480, kMaxChromeScale)).
// Extra window pixels become battlefield so a 16:9 window is a 16:9 match, not
// letterboxed 640x480. The side column and top bar hang from the top-left
// corner; the bottom bar sits on the window's bottom edge. Both bars run from
// the side column's right edge to the window's right edge at the chrome's
// scale, their art repeating past the interface's 640 columns as the game
// draws it on a screen that wide. A side column whose pages reach below the
// window at the chrome's scale is drawn smaller, and narrower, as a whole
// (fit_side_column); the bars and the battlefield then start at its right
// edge.
struct MatchLayout {
    int width = kSourceWidth;
    int height = kSourceHeight;
    int left = kSourceLeft; ///< the side column's width, where the bars and battlefield start
    int top = kSourceTop;
    int bottom = kSourceBottom;
    int hud_width = kSourceWidth; ///< the interface's 640 columns at the chrome's scale
    int hud_height = kSourceHeight;
    double scale = 1.0; ///< canvas pixels per source pixel of the bars
    /// Canvas pixels per source pixel of the side column, across and down:
    /// `scale`, or less where the column is narrowed (fit_side_column).
    double column_scale = 1.0;
    /// Placed mode: the battlefield is the whole canvas and the HUD is drawn in placed regions.
    bool phone = false;
    Insets safe{};             ///< canvas pixels to keep clear on each side
    double px_per_point = 1.0; ///< canvas pixels per window point
    double chrome_scale = 0.0; ///< 0: use scale; else the largest region scale (prescale)
    uint8_t placed_count = 0;  ///< regions in use in placed
    /// The placed regions, the first placed_count used; later ones are drawn on top.
    std::array<PlacedRegion, kMaxPlacedRegions> placed{};

    /// Appends a region; past kMaxPlacedRegions it is dropped.
    ///
    /// @param region the region to append
    /// @return whether it was kept
    bool add_placed(const PlacedRegion& region) noexcept {
        if (placed_count >= kMaxPlacedRegions)
            return false;
        placed[placed_count] = region;
        ++placed_count;
        return true;
    }

    /// Returns whether the side column is drawn smaller than the bars.
    [[nodiscard]] bool column_narrowed() const noexcept {
        return column_scale > 0.0 && column_scale < scale;
    }

    /// Returns the battlefield's left edge in canvas pixels.
    [[nodiscard]] int battlefield_x() const noexcept { return left; }

    /// Returns the battlefield's top edge in canvas pixels.
    [[nodiscard]] int battlefield_y() const noexcept { return top; }

    /// Returns the battlefield width in canvas pixels.
    [[nodiscard]] int battlefield_width() const noexcept { return width - left; }

    /// Returns the battlefield height in canvas pixels.
    [[nodiscard]] int battlefield_height() const noexcept { return height - top - bottom; }

    /// Returns the bottom bar's top edge in canvas pixels.
    [[nodiscard]] int bottom_bar_y() const noexcept { return height - bottom; }

    /// Returns how many source columns the bars show, from column 128 at the
    /// side column's right edge to the window's right edge at the bars'
    /// scale, the last of them cut by that edge: 512 on a 4:3 window beside
    /// a column at the chrome's scale, more on a wider window or beside a
    /// narrowed column.
    [[nodiscard]] int bar_columns() const noexcept {
        if (scale <= 0.0 || width <= left)
            return 0;
        // A hair under a whole column counts as that column.
        constexpr double kWhole = 1e-9;
        return static_cast<int>(std::ceil(static_cast<double>(width - left) / scale - kWhole));
    }

    /// Returns the canvas width of the bars' source columns at the bars'
    /// scale: the width from the side column's right edge to the window's
    /// right edge, or a pixel or two more where the window's edge cuts the
    /// last column.
    [[nodiscard]] int bar_width() const noexcept {
        return static_cast<int>(std::lround(bar_columns() * scale));
    }
};

/// Lays out the match chrome and battlefield on a window, with the chrome's scale capped.
///
/// Touch controls on a canvas of device pixels cap the chrome at a multiple
/// of kMaxChromeScale, so the HUD keeps its size in points.
///
/// @param pixel_width canvas width in pixels; at least 160 is used
/// @param pixel_height canvas height in pixels; at least 96 is used
/// @param max_chrome_scale the largest scale the chrome is drawn at
/// @return the layout, with the chrome scaled by min(w/640, h/480, max_chrome_scale)
[[nodiscard]] inline MatchLayout
make_match_layout(int pixel_width, int pixel_height, double max_chrome_scale) noexcept {
    pixel_width = std::max(pixel_width, kSourceLeft + 32);
    pixel_height = std::max(pixel_height, kSourceTop + kSourceBottom + 32);
    const auto scale = std::min(
        {static_cast<double>(pixel_width) / kSourceWidth,
         static_cast<double>(pixel_height) / kSourceHeight,
         max_chrome_scale}
    );
    auto left = std::max(1, static_cast<int>(std::lround(kSourceLeft * scale)));
    auto top = std::max(1, static_cast<int>(std::lround(kSourceTop * scale)));
    auto bottom = std::max(1, static_cast<int>(std::lround(kSourceBottom * scale)));
    if (left >= pixel_width)
        left = std::max(1, pixel_width / 4);
    if (top + bottom >= pixel_height) {
        top = std::max(1, pixel_height / 16);
        bottom = std::max(1, pixel_height / 16);
    }
    MatchLayout layout{};
    layout.width = pixel_width;
    layout.height = pixel_height;
    layout.left = left;
    layout.top = top;
    layout.bottom = bottom;
    layout.hud_width = std::max(left + 32, static_cast<int>(std::lround(kSourceWidth * scale)));
    layout.hud_height =
        std::max(top + bottom + 32, static_cast<int>(std::lround(kSourceHeight * scale)));
    layout.scale = scale;
    layout.column_scale = scale;
    return layout;
}

/// Lays out the match chrome and battlefield on a window.
///
/// @param pixel_width Canvas width in pixels; at least 160 is used.
/// @param pixel_height Canvas height in pixels; at least 96 is used.
/// @return The layout, with the chrome scaled by min(w/640, h/480, 2).
[[nodiscard]] inline MatchLayout make_match_layout(int pixel_width, int pixel_height) noexcept {
    return make_match_layout(pixel_width, pixel_height, kMaxChromeScale);
}

/// Fits a side column of `column_rows` source rows to the window's height.
///
/// The side column is one picture, 128 source columns wide: the radar at its
/// top and the panel and page under it, down to the tallest page it shows.
/// Where those rows reach below the window at the chrome's scale, the whole
/// column is drawn at the one smaller scale that puts its last row on the
/// window's last, across as down, so that every part keeps its place in it;
/// it is then 128 columns at that scale wide, and the bars and the
/// battlefield start at its right edge and take the width it gives up: the
/// bars, at their own scale, still reach the window's right edge, so they
/// are as many source columns longer as that width holds. Otherwise the
/// layout is returned as it is.
///
/// @param layout a match layout (make_match_layout)
/// @param column_rows source rows of the side column's tallest page; 480 or
///        fewer leave any layout as it is
/// @return the layout with the side column's scale and right edge
[[nodiscard]] inline MatchLayout fit_side_column(MatchLayout layout, int column_rows) noexcept {
    if (column_rows <= 0 || layout.left <= 0 || layout.scale <= 0.0 ||
        std::lround(column_rows * layout.scale) <= layout.height)
        return layout;
    layout.column_scale = static_cast<double>(layout.height) / column_rows;
    layout.left = std::max(1, static_cast<int>(std::lround(kSourceLeft * layout.column_scale)));
    return layout;
}

/// Lays out a frame that shows the battlefield alone, with no interface.
///
/// Director mode draws its frames this way: no side column, top bar or
/// bottom bar, and no HUD, so the battlefield is the whole frame and every
/// pixel is the world's.
///
/// @param pixel_width frame width in pixels; at least 1 is used
/// @param pixel_height frame height in pixels; at least 1 is used
/// @return the layout: left, top and bottom 0, a HUD of 0 by 0 pixels and
///         scale 1
[[nodiscard]] inline MatchLayout
make_battlefield_layout(int pixel_width, int pixel_height) noexcept {
    MatchLayout layout{};
    layout.width = std::max(pixel_width, 1);
    layout.height = std::max(pixel_height, 1);
    layout.left = 0;
    layout.top = 0;
    layout.bottom = 0;
    layout.hud_width = 0;
    layout.hud_height = 0;
    layout.scale = 1.0;
    layout.column_scale = 1.0;
    return layout;
}

/// Lays out a phone canvas: full-bleed battlefield, the HUD in placed regions.
///
/// The battlefield is the whole canvas (left, top and bottom 0, a HUD of 0
/// by 0 pixels), at scale 1, in placed mode, with no regions yet; the caller
/// adds them.
///
/// @param pixel_width canvas width in pixels; at least 1 is used
/// @param pixel_height canvas height in pixels; at least 1 is used
/// @param px_per_point canvas pixels per window point; 1 is used when not positive
/// @param safe canvas pixels to keep clear on each side
/// @return the layout
[[nodiscard]] inline MatchLayout
make_phone_layout(int pixel_width, int pixel_height, double px_per_point, Insets safe) noexcept {
    MatchLayout layout = make_battlefield_layout(pixel_width, pixel_height);
    layout.phone = true;
    layout.safe = safe;
    layout.px_per_point = px_per_point > 0.0 ? px_per_point : 1.0;
    return layout;
}

/// Returns whether a layout is in placed mode: a phone layout, or one that holds regions.
///
/// @param layout the layout
/// @return whether the HUD is drawn and hit-tested through placed regions
[[nodiscard]] inline bool placed_mode(const MatchLayout& layout) noexcept {
    return layout.phone || layout.placed_count > 0;
}

/// Returns the largest scale any placed region draws its source at.
///
/// @param layout the layout whose regions are measured
/// @return the largest of each region's horizontal and vertical scale; 0 without regions
[[nodiscard]] inline double largest_region_scale(const MatchLayout& layout) noexcept {
    double largest = 0.0;
    const std::size_t count = std::min<std::size_t>(layout.placed_count, kMaxPlacedRegions);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& region = layout.placed[index];
        if (region.source.width <= 0 || region.source.height <= 0)
            continue;
        largest = std::max(
            {largest,
             static_cast<double>(region.canvas.width) / region.source.width,
             static_cast<double>(region.canvas.height) / region.source.height}
        );
    }
    return largest;
}

/// Returns the canvas rectangle that shows a source rectangle whole inside an
/// area, at the largest scale that fits, centred.
///
/// @param area canvas rectangle to fit inside
/// @param source_width source width in pixels
/// @param source_height source height in pixels
/// @param max_scale the largest scale used; 0 or less for none
/// @return the rectangle; empty when the area or the source is empty
[[nodiscard]] inline Rect
fit_inside(const Rect& area, int source_width, int source_height, double max_scale = 0.0) noexcept {
    Rect fitted{};
    if (area.width <= 0 || area.height <= 0 || source_width <= 0 || source_height <= 0)
        return fitted;
    double scale = std::min(
        static_cast<double>(area.width) / source_width,
        static_cast<double>(area.height) / source_height
    );
    if (max_scale > 0.0)
        scale = std::min(scale, max_scale);
    fitted.width = std::clamp(static_cast<int>(std::lround(source_width * scale)), 1, area.width);
    fitted.height =
        std::clamp(static_cast<int>(std::lround(source_height * scale)), 1, area.height);
    fitted.x = area.x + (area.width - fitted.width) / 2;
    fitted.y = area.y + (area.height - fitted.height) / 2;
    return fitted;
}

/// Returns the topmost placed region containing a canvas pixel.
///
/// @param layout the layout whose regions are searched
/// @param x canvas x in pixels
/// @param y canvas y in pixels
/// @return the last region in placed order that contains the pixel, or null
[[nodiscard]] inline const PlacedRegion*
region_at(const MatchLayout& layout, int x, int y) noexcept {
    const std::size_t count = std::min<std::size_t>(layout.placed_count, kMaxPlacedRegions);
    for (std::size_t index = count; index > 0; --index) {
        const auto& region = layout.placed[index - 1];
        if (x >= region.canvas.x && y >= region.canvas.y &&
            x < region.canvas.x + region.canvas.width && y < region.canvas.y + region.canvas.height)
            return &region;
    }
    return nullptr;
}

/// Returns whether a placed region covers a canvas pixel.
///
/// @param layout the layout whose regions are searched
/// @param x canvas x in pixels
/// @param y canvas y in pixels
/// @return whether a region contains the pixel; false without regions
[[nodiscard]] inline bool hud_covers(const MatchLayout& layout, int x, int y) noexcept {
    return region_at(layout, x, y) != nullptr;
}

/// Returns the topmost placed region whose source rectangle holds a source point.
///
/// @param layout the layout whose regions are searched
/// @param x source x in 640x480 pixels
/// @param y source y in 640x480 pixels
/// @return the last region in placed order whose source holds the point, or null
[[nodiscard]] inline const PlacedRegion*
source_region_at(const MatchLayout& layout, int x, int y) noexcept {
    const std::size_t count = std::min<std::size_t>(layout.placed_count, kMaxPlacedRegions);
    for (std::size_t index = count; index > 0; --index) {
        const auto& source = layout.placed[index - 1].source;
        if (source.width > 0 && source.height > 0 && x >= source.x && y >= source.y &&
            x < source.x + source.width && y < source.y + source.height)
            return &layout.placed[index - 1];
    }
    return nullptr;
}

/// Returns the topmost placed region whose source rectangle's right or bottom
/// edge a source point lies on, so a rectangle's far corner maps with it.
///
/// @param layout the layout whose regions are searched
/// @param x source x in 640x480 pixels
/// @param y source y in 640x480 pixels
/// @return the last region in placed order whose source, edges included, holds the point
[[nodiscard]] inline const PlacedRegion*
source_region_edge_at(const MatchLayout& layout, int x, int y) noexcept {
    const std::size_t count = std::min<std::size_t>(layout.placed_count, kMaxPlacedRegions);
    for (std::size_t index = count; index > 0; --index) {
        const auto& source = layout.placed[index - 1].source;
        if (source.width > 0 && source.height > 0 && x >= source.x && y >= source.y &&
            x <= source.x + source.width && y <= source.y + source.height)
            return &layout.placed[index - 1];
    }
    return nullptr;
}

/// Maps a source point through a placed region onto the canvas: the first
/// canvas pixel whose source pixel (canvas_to_source) is at or past the point.
///
/// @param region the region; its source must not be empty
/// @param x source x in 640x480 pixels
/// @param y source y in 640x480 pixels
/// @return the point in canvas pixels
[[nodiscard]] inline Point region_to_canvas(const PlacedRegion& region, int x, int y) noexcept {
    const auto through = [](int value, int source_start, int source_length, int start, int length) {
        const auto offset = static_cast<int64_t>(value - source_start) * length;
        // Ceiling division, for offsets either side of the region's start.
        const auto steps = offset >= 0 ? (offset + source_length - 1) / source_length
                                       : -((-offset) / source_length);
        return start + static_cast<int>(steps);
    };
    Point point{};
    point.x =
        through(x, region.source.x, region.source.width, region.canvas.x, region.canvas.width);
    point.y =
        through(y, region.source.y, region.source.height, region.canvas.y, region.canvas.height);
    return point;
}

/// Returns whether a source point lies on the 640x480 source's battlefield.
///
/// @param x source x in pixels
/// @param y source y in pixels
/// @return whether it is right of the side column, under the top bar and above the bottom bar
[[nodiscard]] inline bool source_battlefield_holds(int x, int y) noexcept {
    return x >= kSourceLeft && x < kSourceWidth && y >= kSourceTop && y < kSourceBottomBarY;
}

/// Maps a canvas pixel back to the game's 640x480 HUD and gadget space.
///
/// Bottom bar pixels follow the bar's bottom-edge anchor; battlefield pixels
/// stay inside the source battlefield so a tall window never lands them on a
/// bar gadget. Pixels in the blank strip areas map outside the source canvas.
/// In placed mode a pixel maps through the topmost region on it, rounding
/// down, and a pixel on no region maps to kOutsideSource. In a narrowed side
/// column a pixel maps to the source pixel drawn there, and right of it the
/// bars and battlefield count from its edge.
///
/// @param layout Match layout of the canvas.
/// @param x Canvas x in pixels.
/// @param y Canvas y in pixels.
/// @return The point in 640x480 source coordinates.
[[nodiscard]] inline Point canvas_to_source(const MatchLayout& layout, int x, int y) noexcept {
    if (placed_mode(layout)) {
        const auto* region = region_at(layout, x, y);
        if (region == nullptr || region->canvas.width <= 0 || region->canvas.height <= 0)
            return kOutsideSource;
        // Inside the region the offsets are never negative, so the division
        // rounds down.
        const auto through = [](int value, int start, int length, int source, int source_length) {
            return source +
                   static_cast<int>(static_cast<int64_t>(value - start) * source_length / length);
        };
        Point point{};
        point.x = through(
            x, region->canvas.x, region->canvas.width, region->source.x, region->source.width
        );
        point.y = through(
            y, region->canvas.y, region->canvas.height, region->source.y, region->source.height
        );
        return point;
    }
    const auto scale = layout.scale == 0.0 ? 1.0 : layout.scale;
    const auto unscaled = [scale](int value) {
        return static_cast<int>(std::lround(static_cast<double>(value) / scale));
    };
    const bool narrowed = layout.column_narrowed();
    if (narrowed && x < layout.left) {
        const auto in_column = [&layout](int value) {
            return static_cast<int>(std::floor(static_cast<double>(value) / layout.column_scale));
        };
        return {in_column(x), in_column(y)};
    }
    Point point{narrowed ? kSourceLeft + unscaled(x - layout.left) : unscaled(x), unscaled(y)};
    if (x >= layout.left && y >= layout.bottom_bar_y())
        point.y = kSourceBottomBarY + unscaled(y - layout.bottom_bar_y());
    else if (x >= layout.left && y >= layout.top)
        point.y = std::min(point.y, kSourceBottomBarY - 1);
    return point;
}

/// Maps a source point on the source battlefield onto the canvas battlefield.
///
/// @param layout Match layout of the canvas.
/// @param x Source x in 640x480 pixels.
/// @param y Source y in 640x480 pixels.
/// @return The point in canvas pixels, from the battlefield's corner at the layout's scale.
[[nodiscard]] inline Point
source_battlefield_to_canvas(const MatchLayout& layout, int x, int y) noexcept {
    const auto scaled = [&layout](int value) {
        return static_cast<int>(std::lround(static_cast<double>(value) * layout.scale));
    };
    Point point{};
    point.x = layout.battlefield_x() + scaled(x - kSourceLeft);
    point.y = layout.battlefield_y() + scaled(y - kSourceTop);
    return point;
}

/// Maps the game's 640x480 HUD coordinates onto the live match canvas.
///
/// Chrome is uniformly scaled (not stretched across extra 16:9 battlefield);
/// bottom bar coordinates are offset from the bar's bottom-edge anchor. A
/// narrowed side column's points take its scale, and the bars' count from its
/// right edge.
/// In placed mode a point maps through the region whose source holds it
/// (source_region_at), else a point on the source battlefield maps onto the
/// canvas battlefield at the layout's scale, else a point on a region's far
/// edge maps through that region, else as without regions.
///
/// @param layout Match layout of the canvas.
/// @param x Source x in 640x480 pixels.
/// @param y Source y in 640x480 pixels.
/// @return The point in canvas pixels.
[[nodiscard]] inline Point source_to_canvas(const MatchLayout& layout, int x, int y) noexcept {
    if (placed_mode(layout)) {
        if (const auto* region = source_region_at(layout, x, y))
            return region_to_canvas(*region, x, y);
        if (source_battlefield_holds(x, y))
            return source_battlefield_to_canvas(layout, x, y);
        if (const auto* region = source_region_edge_at(layout, x, y))
            return region_to_canvas(*region, x, y);
    }
    const auto scaled = [&layout](int value) {
        return static_cast<int>(std::lround(static_cast<double>(value) * layout.scale));
    };
    const bool narrowed = layout.column_narrowed();
    if (narrowed && x < kSourceLeft) {
        const auto in_column = [&layout](int value) {
            return static_cast<int>(std::lround(static_cast<double>(value) * layout.column_scale));
        };
        return {in_column(x), in_column(y)};
    }
    Point point{narrowed ? layout.left + scaled(x - kSourceLeft) : scaled(x), scaled(y)};
    if (x >= kSourceLeft && y >= kSourceBottomBarY)
        point.y = layout.bottom_bar_y() + scaled(y - kSourceBottomBarY);
    return point;
}

/// Maps a source rectangle onto the canvas, placed by its origin's anchor.
///
/// The rectangle never straddles the blank gap between the top-anchored
/// chrome and the bottom bar. In placed mode both corners map through the
/// region (or the battlefield) that places the origin.
///
/// @param layout Match layout of the canvas.
/// @param x Source left edge in 640x480 pixels.
/// @param y Source top edge in 640x480 pixels.
/// @param width Source width in pixels.
/// @param height Source height in pixels.
/// @return The rectangle in canvas pixels.
[[nodiscard]] inline Rect
source_rect_to_canvas(const MatchLayout& layout, int x, int y, int width, int height) noexcept {
    if (placed_mode(layout)) {
        Point origin{};
        Point corner{};
        bool placed = false;
        const auto* region = source_region_at(layout, x, y);
        if (region == nullptr && !source_battlefield_holds(x, y))
            region = source_region_edge_at(layout, x, y);
        if (region != nullptr) {
            origin = region_to_canvas(*region, x, y);
            corner = region_to_canvas(*region, x + width, y + height);
            placed = true;
        } else if (source_battlefield_holds(x, y)) {
            origin = source_battlefield_to_canvas(layout, x, y);
            corner = source_battlefield_to_canvas(layout, x + width, y + height);
            placed = true;
        }
        if (placed) {
            Rect rect{};
            rect.x = origin.x;
            rect.y = origin.y;
            rect.width = corner.x - origin.x;
            rect.height = corner.y - origin.y;
            return rect;
        }
    }
    // A narrowed side column's rectangle takes its scale; the bars' count
    // across from the column's edge.
    const bool column = layout.column_narrowed() && x < kSourceLeft;
    const auto factor = column ? layout.column_scale : layout.scale;
    const auto scaled = [factor](int value) {
        return static_cast<int>(std::lround(static_cast<double>(value) * factor));
    };
    const auto across = layout.column_narrowed() && !column ? x - kSourceLeft : x;
    const auto origin = source_to_canvas(layout, x, y);
    return {
        origin.x, origin.y, scaled(across + width) - scaled(across), scaled(y + height) - scaled(y)
    };
}

} // namespace oa::ui::display_layout
