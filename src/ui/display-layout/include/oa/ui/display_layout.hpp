// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <algorithm>
#include <cmath>

namespace oa::ui::display_layout {

inline constexpr int kSourceWidth = 640;
inline constexpr int kSourceHeight = 480;
inline constexpr int kSourceLeft = 128;
inline constexpr int kSourceTop = 32;
inline constexpr int kSourceBottom = 32;
inline constexpr int kSourceBattlefieldWidth = kSourceWidth - kSourceLeft;
inline constexpr int kSourceBattlefieldHeight = kSourceHeight - kSourceTop - kSourceBottom;
inline constexpr int kSourceBottomBarY = kSourceHeight - kSourceBottom;
// The chrome grows with the window only up to the 1280x1024 layout (twice the
// game's 640x480 art). Larger windows keep that size and leave the rest of
// the strips blank instead of stretching the art.
inline constexpr double kMaxChromeScale = 2.0;

// Chrome stays 4:3-proportioned (scaled by min(w/640, h/480, kMaxChromeScale)).
// Extra window pixels become battlefield so a 16:9 window is a 16:9 match, not
// letterboxed 640x480. The side column and top bar hang from the top-left
// corner; the bottom bar sits on the window's bottom edge.
struct MatchLayout {
    int width = kSourceWidth;
    int height = kSourceHeight;
    int left = kSourceLeft;
    int top = kSourceTop;
    int bottom = kSourceBottom;
    int hud_width = kSourceWidth;
    int hud_height = kSourceHeight;
    double scale = 1.0;

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
};

/// Lays out the match chrome and battlefield on a window.
///
/// @param pixel_width Canvas width in pixels; at least 160 is used.
/// @param pixel_height Canvas height in pixels; at least 96 is used.
/// @return The layout, with the chrome scaled by min(w/640, h/480, 2).
[[nodiscard]] inline MatchLayout make_match_layout(int pixel_width, int pixel_height) noexcept {
    pixel_width = std::max(pixel_width, kSourceLeft + 32);
    pixel_height = std::max(pixel_height, kSourceTop + kSourceBottom + 32);
    const auto scale = std::min(
        {static_cast<double>(pixel_width) / kSourceWidth,
         static_cast<double>(pixel_height) / kSourceHeight,
         kMaxChromeScale}
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
    const auto hud_width = std::max(left + 32, static_cast<int>(std::lround(kSourceWidth * scale)));
    const auto hud_height =
        std::max(top + bottom + 32, static_cast<int>(std::lround(kSourceHeight * scale)));
    return {pixel_width, pixel_height, left, top, bottom, hud_width, hud_height, scale};
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
    return {std::max(pixel_width, 1), std::max(pixel_height, 1), 0, 0, 0, 0, 0, 1.0};
}

struct Point {
    int x = 0;
    int y = 0;
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

/// Maps a canvas pixel back to the game's 640x480 HUD and gadget space.
///
/// Bottom bar pixels follow the bar's bottom-edge anchor; battlefield pixels
/// stay inside the source battlefield so a tall window never lands them on a
/// bar gadget. Pixels in the blank strip areas map outside the source canvas.
///
/// @param layout Match layout of the canvas.
/// @param x Canvas x in pixels.
/// @param y Canvas y in pixels.
/// @return The point in 640x480 source coordinates.
[[nodiscard]] inline Point canvas_to_source(const MatchLayout& layout, int x, int y) noexcept {
    const auto scale = layout.scale == 0.0 ? 1.0 : layout.scale;
    const auto unscaled = [scale](int value) {
        return static_cast<int>(std::lround(static_cast<double>(value) / scale));
    };
    Point point{unscaled(x), unscaled(y)};
    if (x >= layout.left && y >= layout.bottom_bar_y())
        point.y = kSourceBottomBarY + unscaled(y - layout.bottom_bar_y());
    else if (x >= layout.left && y >= layout.top)
        point.y = std::min(point.y, kSourceBottomBarY - 1);
    return point;
}

/// Maps the game's 640x480 HUD coordinates onto the live match canvas.
///
/// Chrome is uniformly scaled (not stretched across extra 16:9 battlefield);
/// bottom bar coordinates are offset from the bar's bottom-edge anchor.
///
/// @param layout Match layout of the canvas.
/// @param x Source x in 640x480 pixels.
/// @param y Source y in 640x480 pixels.
/// @return The point in canvas pixels.
[[nodiscard]] inline Point source_to_canvas(const MatchLayout& layout, int x, int y) noexcept {
    const auto scaled = [&layout](int value) {
        return static_cast<int>(std::lround(static_cast<double>(value) * layout.scale));
    };
    Point point{scaled(x), scaled(y)};
    if (x >= kSourceLeft && y >= kSourceBottomBarY)
        point.y = layout.bottom_bar_y() + scaled(y - kSourceBottomBarY);
    return point;
}

/// Maps a source rectangle onto the canvas, placed by its origin's anchor.
///
/// The rectangle never straddles the blank gap between the top-anchored
/// chrome and the bottom bar.
///
/// @param layout Match layout of the canvas.
/// @param x Source left edge in 640x480 pixels.
/// @param y Source top edge in 640x480 pixels.
/// @param width Source width in pixels.
/// @param height Source height in pixels.
/// @return The rectangle in canvas pixels.
[[nodiscard]] inline Rect
source_rect_to_canvas(const MatchLayout& layout, int x, int y, int width, int height) noexcept {
    const auto scaled = [&layout](int value) {
        return static_cast<int>(std::lround(static_cast<double>(value) * layout.scale));
    };
    const auto origin = source_to_canvas(layout, x, y);
    return {origin.x, origin.y, scaled(x + width) - scaled(x), scaled(y + height) - scaled(y)};
}

} // namespace oa::ui::display_layout
