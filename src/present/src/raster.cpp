// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/raster.hpp"

#include "oa/present/display.hpp"
#include "oa/present/surface.hpp"
#include "oa/base/text.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace oa::present {

namespace {

// Signed 32-bit product (wrapping) and quotient, as the game computes them.
int32_t mul_div32(int32_t a, int32_t b, int32_t divisor) noexcept {
    const auto product = static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
    return product / divisor;
}

// 64-bit product followed by a 32-bit quotient.
int32_t mul_div64(int32_t a, int32_t b, int32_t divisor) noexcept {
    return static_cast<int32_t>((static_cast<int64_t>(a) * b) / divisor);
}

uint8_t* pixel_at(uint8_t* base, int32_t pitch, int32_t x, int32_t y) noexcept {
    return base + static_cast<std::ptrdiff_t>(y) * pitch + x;
}

// Both horizontal edges of `inner` within `outer`, then both vertical ones.
bool rect_inside(const Rect32& inner, const Rect32& outer) noexcept {
    return outer.x1 <= inner.x1 && inner.x1 <= outer.x2 && outer.x1 <= inner.x2 &&
           inner.x2 <= outer.x2 && outer.y1 <= inner.y1 && inner.y1 <= outer.y2 &&
           outer.y1 <= inner.y2 && inner.y2 <= outer.y2;
}

bool in_extent(int32_t value, int32_t limit) noexcept {
    return value < limit && static_cast<uint32_t>(value) < static_cast<uint32_t>(limit);
}

// Bresenham setup shared by the three span routines: after the caller has
// ordered the endpoints so x grows, returns the major/minor lengths and the
// error terms, and flips `step` when y shrinks.
struct LineSteps {
    int32_t major = 0;
    int32_t error = 0;
    int32_t minor_add = 0;    // 2*minor
    int32_t diagonal_add = 0; // 2*minor - 2*major
    bool y_major = false;
};

LineSteps line_steps(int32_t dx, int32_t dy) noexcept {
    LineSteps s;
    int32_t major = dx;
    int32_t minor = dy;
    if (dy > dx) {
        s.y_major = true;
        major = dy;
        minor = dx;
    }
    s.major = major;
    s.minor_add = minor * 2;
    s.error = minor * 2 - major;
    s.diagonal_add = minor * 2 - major * 2;
    return s;
}

// Rectangle edges in the game's draw order: top, right, bottom, left.
template <typename Draw>
void for_each_edge(const Rect32& r, Draw draw) {
    draw(r.x1, r.y1, r.x2, r.y1);
    draw(r.x2, r.y1, r.x2, r.y2);
    draw(r.x1, r.y2, r.x2, r.y2);
    draw(r.x1, r.y1, r.x1, r.y2);
}

} // namespace

int32_t clip_line_to_extent(
    int32_t width, int32_t height, int32_t& x0, int32_t& y0, int32_t& x1, int32_t& y1
) noexcept {
    int32_t result = 1;
    for (;;) {
        if (in_extent(x0, width) && in_extent(x1, width) && in_extent(y0, height) &&
            in_extent(y1, height)) {
            return result;
        }
        const int32_t dx = x1 - x0;
        if (dx < 0) {
            if (x0 < 0 || x1 >= width) {
                return 0;
            }
        } else if (x0 >= width || x1 < 0) {
            return 0;
        }
        const int32_t dy = y1 - y0;
        if (dy < 0) {
            if (y0 < 0 || y1 >= height) {
                return 0;
            }
        } else if (y0 >= height || y1 < 0) {
            return 0;
        }
        if (x0 < 0) {
            y0 += mul_div64(-x0, dy, dx);
            x0 = 0;
        } else if (x0 - width >= 0) {
            y0 += mul_div64(-(x0 - width + 1), dy, dx);
            x0 = width - 1;
        }
        if (y0 < 0) {
            x0 += mul_div64(-y0, dx, dy);
            y0 = 0;
        } else if (y0 - height >= 0) {
            x0 += mul_div64(-(y0 - height + 1), dx, dy);
            y0 = height - 1;
        }
        if (x1 < 0) {
            y1 += mul_div64(-x1, dy, dx);
            x1 = 0;
        } else if (x1 - width >= 0) {
            y1 += mul_div64(-(x1 - width + 1), dy, dx);
            x1 = width - 1;
        }
        if (y1 < 0) {
            x1 += mul_div64(-y1, dx, dy);
            y1 = 0;
        } else if (y1 - height >= 0) {
            x1 += mul_div64(-(y1 - height + 1), dx, dy);
            y1 = height - 1;
            ++result;
        }
    }
}

void fill_line(
    Surface& surface, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color
) noexcept {
    if (clip_line_to_extent(surface.width, surface.height, x0, y0, x1, y1) == 0) {
        return;
    }
    // A band (surface_band) leaves the line's pixels outside it unwritten;
    // the steps between them are the same.
    const SurfaceRows band = surface_band(surface);
    const auto in_band = [&band](int32_t y) { return y >= band.first && y < band.end; };
    const int32_t pitch = surface.pitch;
    int32_t dx = x1 - x0;
    if (dx == 0) {
        int32_t top = y0;
        int32_t count = y1 - y0;
        if (count < 0) {
            count = -count;
            top = y1;
        }
        const int32_t first = std::max(top, band.first);
        const int32_t last = std::min(top + count, band.end - 1);
        if (first > last) {
            return;
        }
        uint8_t* p = pixel_at(surface.pixels, pitch, x0, first);
        for (int32_t y = first; y <= last; ++y, p += pitch) {
            *p = color;
        }
        return;
    }
    if (dx < 0) {
        dx = -dx;
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    int32_t dy = y1 - y0;
    if (dy == 0) {
        if (!in_band(y0)) {
            return;
        }
        uint8_t* p = pixel_at(surface.pixels, pitch, x0, y0);
        for (int32_t i = 0; i <= dx; ++i) {
            p[i] = color;
        }
        return;
    }
    int32_t step = pitch;
    int32_t row_step = 1;
    if (dy < 0) {
        dy = -dy;
        step = -pitch;
        row_step = -1;
    }
    const LineSteps s = line_steps(dx, dy);
    int32_t error = s.error;
    uint8_t* p = pixel_at(surface.pixels, pitch, x0, y0);
    int32_t y = y0;
    for (int32_t i = 0; i <= s.major; ++i) {
        if (in_band(y)) {
            *p = color;
        }
        if (s.y_major) {
            p += step;
            y += row_step;
            if (error >= 0) {
                error += s.diagonal_add;
                ++p;
            } else {
                error += s.minor_add;
            }
        } else {
            ++p;
            if (error >= 0) {
                error += s.diagonal_add;
                p += step;
                y += row_step;
            } else {
                error += s.minor_add;
            }
        }
    }
}

void remap_line(
    Surface& surface,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t row,
    const uint8_t* table
) noexcept {
    assert(!surface_banded(surface) && "remap_line does not honour a surface's band");
    const uint8_t* lut = table + static_cast<std::ptrdiff_t>(row) * 0x100;
    int32_t pitch = surface.pitch;
    int32_t dx = x1 - x0;
    if (dx == 0) {
        int32_t top = y0;
        int32_t count = y1 - y0;
        if (count < 0) {
            count = -count;
            top = y1;
        }
        uint8_t* p = pixel_at(surface.pixels, surface.width, x0, top);
        for (int32_t i = 0; i <= count; ++i, p += pitch) {
            *p = lut[*p];
        }
        return;
    }
    if (dx < 0) {
        dx = -dx;
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    int32_t dy = y1 - y0;
    if (dy == 0) {
        uint8_t* p = pixel_at(surface.pixels, pitch, x0, y0);
        for (int32_t i = 0; i <= dx; ++i) {
            p[i] = lut[p[i]];
        }
        return;
    }
    int32_t step = pitch;
    if (dy < 0) {
        dy = -dy;
        step = -pitch;
    }
    const LineSteps s = line_steps(dx, dy);
    int32_t error = s.error;
    uint8_t* p = pixel_at(surface.pixels, pitch, x0, y0);
    for (int32_t i = 0; i <= s.major; ++i) {
        *p = lut[*p];
        if (s.y_major) {
            p += step;
            if (error >= 0) {
                error += s.diagonal_add;
            } else {
                error += s.minor_add;
                --p;
            }
        } else if (error >= 0) {
            error += s.diagonal_add;
            p += step;
        } else {
            error += s.minor_add;
        }
    }
}

void xor_line(
    const PitchedBitmap& bitmap,
    int32_t width,
    int32_t height,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    uint8_t value
) noexcept {
    if (clip_line_to_extent(width, height, x0, y0, x1, y1) == 0) {
        return;
    }
    const int32_t pitch = bitmap.pitch;
    int32_t dx = x1 - x0;
    if (dx == 0) {
        int32_t top = y0;
        int32_t count = y1 - y0;
        if (count < 0) {
            count = -count;
            top = y1;
        }
        uint8_t* p = pixel_at(bitmap.pixels, pitch, x0, top);
        for (int32_t i = 0; i <= count; ++i, p += pitch) {
            *p ^= value;
        }
        return;
    }
    if (dx < 0) {
        dx = -dx;
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    int32_t dy = y1 - y0;
    if (dy == 0) {
        uint8_t* p = pixel_at(bitmap.pixels, pitch, x0, y0);
        for (int32_t i = 0; i <= dx; ++i) {
            p[i] ^= value;
        }
        return;
    }
    int32_t step = pitch;
    if (dy < 0) {
        dy = -dy;
        step = -pitch;
    }
    const LineSteps s = line_steps(dx, dy);
    int32_t error = s.error;
    uint8_t* p = pixel_at(bitmap.pixels, pitch, x0, y0);
    for (int32_t i = 0; i <= s.major; ++i) {
        *p ^= value;
        if (s.y_major) {
            p += step + 1;
            if (error >= 0) {
                error += s.diagonal_add;
            } else {
                error += s.minor_add;
                --p;
            }
        } else {
            ++p;
            if (error >= 0) {
                error += s.diagonal_add;
                p += step;
            } else {
                error += s.minor_add;
            }
        }
    }
}

int32_t clip_segment_to_extent(uint16_t width, uint16_t height, Rect32& s) noexcept {
    const int32_t w = width;
    const int32_t h = height;
    int32_t result = 1;
    auto scaled = [](int32_t a, int32_t b, int32_t divisor) {
        return divisor == 0 ? 0 : mul_div64(a, b, divisor);
    };
    for (;;) {
        if (in_extent(s.x1, w) && in_extent(s.x2, w) && in_extent(s.y1, h) && in_extent(s.y2, h)) {
            return result;
        }
        const int32_t dx = s.x2 - s.x1;
        if (dx < 0) {
            if (s.x1 < 0 || s.x2 >= w) {
                return 0;
            }
        } else if (s.x1 >= w || s.x2 < 0) {
            return 0;
        }
        const int32_t dy = s.y2 - s.y1;
        if (dy < 0) {
            if (s.y1 < 0 || s.y2 >= h) {
                return 0;
            }
        } else if (s.y1 >= h || static_cast<int16_t>(s.y2) < 0) {
            return 0;
        }
        if (s.x1 < 0) {
            s.y1 += scaled(-s.x1, dy, dx);
            s.x1 = 0;
        } else if (s.x1 >= w) {
            s.y1 += scaled(-(s.x1 - w + 1), dy, dx);
            s.x1 = w - 1;
        }
        if (s.y1 < 0) {
            s.x1 += scaled(-s.y1, dx, dy);
            s.y1 = 0;
        } else if (s.y1 >= h) {
            s.x1 += scaled(-(s.y1 - h + 1), dx, dy);
            s.y1 = h - 1;
        }
        if (s.x2 < 0) {
            s.y2 += scaled(-s.x2, dy, dx);
            s.x2 = 0;
        } else if (s.x2 >= w) {
            s.y2 += scaled(-(s.x2 - w + 1), dy, dx);
            s.x2 = w - 1;
        }
        if (s.y2 < 0) {
            s.x2 += scaled(-s.y2, dx, dy);
            s.y2 = 0;
        } else if (s.y2 >= h) {
            s.x2 += scaled(-(s.y2 - h + 1), dx, dy);
            s.y2 = h - 1;
            ++result;
        }
    }
}

void outline_rect(Surface& surface, const Rect32& r, uint8_t color) noexcept {
    fill_line(surface, r.x1, r.y1, r.x2, r.y1, color);
    fill_line(surface, r.x1, r.y2, r.x2, r.y2, color);
    fill_line(surface, r.x1, r.y1, r.x1, r.y2, color);
    fill_line(surface, r.x2, r.y1, r.x2, r.y2, color);
}

void xor_rect_outline(
    const PitchedBitmap& bitmap, int32_t width, int32_t height, const Rect32& r, uint8_t value
) noexcept {
    xor_line(bitmap, width, height, r.x1, r.y1, r.x2, r.y1, value);
    xor_line(bitmap, width, height, r.x1, r.y2, r.x2, r.y2, value);
    xor_line(bitmap, width, height, r.x1, r.y1 + 1, r.x1, r.y2 - 1, value);
    xor_line(bitmap, width, height, r.x2, r.y1 + 1, r.x2, r.y2 - 1, value);
}

void fill_rect(Surface& surface, const Rect32& r, uint8_t color) noexcept {
    assert(!surface_banded(surface) && "fill_rect does not honour a surface's band");
    const int32_t width = r.x2 - r.x1 + 1;
    if (width <= 0) {
        return;
    }
    uint8_t* p = pixel_at(surface.pixels, surface.pitch, r.x1, r.y1);
    int32_t rows = r.y2 - r.y1;
    do {
        for (int32_t i = 0; i < width; ++i) {
            p[i] = color;
        }
        p += surface.pitch;
        --rows;
    } while (rows >= 0);
}

void xor_rect(const PitchedBitmap& bitmap, const Rect32& r, uint8_t value) noexcept {
    const auto y = static_cast<uint32_t>(r.y1);
    const uint32_t low = (y * bitmap.pitch) & 0xFFFFu;
    const uint32_t offset = (y & 0xFFFF0000u) | low;
    uint8_t* p = bitmap.pixels + offset + r.x1;
    const int32_t width = r.x2 - r.x1 + 1;
    if (width <= 0) {
        return;
    }
    int32_t rows = r.y2 - r.y1;
    do {
        for (int32_t i = 0; i < width; ++i) {
            p[i] ^= value;
        }
        p += bitmap.pitch;
        --rows;
    } while (rows >= 0);
}

void remap_rows(
    uint8_t* first, int32_t pitch, int32_t width, int32_t height, const uint8_t* table
) noexcept {
    if (width <= 0 || height <= 0) {
        return;
    }
    uint8_t* row = first;
    for (int32_t y = 0; y < height; ++y, row += pitch) {
        for (int32_t x = width - 1; x >= 0; --x) {
            row[x] = table[row[x]];
        }
    }
}

int32_t clip_line_to_rect(
    const Surface& surface, int32_t& x0, int32_t& y0, int32_t& x1, int32_t& y1
) noexcept {
    const bool x_grows = x0 <= x1;
    const bool y_grows = y0 <= y1;
    const int32_t dx = x1 - x0;
    const int32_t dy = y1 - y0;
    const Rect32 c = surface.clip;
    if (x0 < c.x1) {
        if (!x_grows || dx == 0) {
            return 0;
        }
        y0 += mul_div32(c.x1 - x0, dy, dx);
        x0 = c.x1;
    }
    if (y0 < c.y1) {
        if (!y_grows || dy == 0) {
            return 0;
        }
        x0 += mul_div32(c.y1 - y0, dx, dy);
        y0 = c.y1;
    }
    if (c.x2 < x0) {
        if (x_grows || dx == 0) {
            return 0;
        }
        y0 += mul_div32(c.x2 - x0, dy, dx);
        x0 = c.x2;
    }
    if (c.y2 < y0) {
        if (y_grows || dy == 0) {
            return 0;
        }
        x0 += mul_div32(c.y2 - y0, dx, dy);
        y0 = c.y2;
    }
    if (x1 < c.x1) {
        if (x_grows || dx == 0) {
            return 0;
        }
        y1 += mul_div32(c.x1 - x1, dy, dx);
        x1 = c.x1;
    }
    if (y1 < c.y1) {
        if (y_grows || dy == 0) {
            return 0;
        }
        x1 += mul_div32(c.y1 - y1, dx, dy);
        y1 = c.y1;
    }
    if (c.x2 < x1) {
        if (!x_grows || dx == 0) {
            return 0;
        }
        y1 += mul_div32(c.x2 - x1, dy, dx);
        x1 = c.x2;
    }
    if (c.y2 < y1) {
        if (!y_grows || dy == 0) {
            return 0;
        }
        x1 += mul_div32(c.y2 - y1, dx, dy);
        y1 = c.y2;
    }
    return 1;
}

int32_t draw_clipped_line(
    Surface* target, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color
) noexcept {
    if (target != nullptr) {
        if (clip_line_to_rect(*target, x0, y0, x1, y1) != 0) {
            fill_line(*target, x0, y0, x1, y1, color);
        }
        return 1;
    }
    Surface locked{};
    const int32_t ok = lock_display_surface(locked);
    if (ok != 0) {
        if (clip_line_to_rect(locked, x0, y0, x1, y1) != 0) {
            fill_line(locked, x0, y0, x1, y1, color);
        }
        unlock_display_surface();
    }
    return ok;
}

int32_t light_clipped_line(
    Surface* target, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t level
) noexcept {
    const DisplayContext* display = display_context();
    if (display == nullptr || display->light_table == nullptr) {
        return 0;
    }
    const uint8_t* table = display->light_table;
    if (target != nullptr) {
        if (clip_line_to_rect(*target, x0, y0, x1, y1) != 0) {
            remap_line(*target, x0, y0, x1, y1, level, table);
        }
        return 1;
    }
    Surface locked{};
    const int32_t ok = lock_display_surface(locked);
    if (ok != 0) {
        if (clip_line_to_rect(locked, x0, y0, x1, y1) != 0) {
            remap_line(locked, x0, y0, x1, y1, level, table);
        }
        unlock_display_surface();
    }
    return ok;
}

int32_t draw_point(Surface* target, int32_t x, int32_t y, uint8_t color) noexcept {
    if (target != nullptr) {
        draw_clipped_line(target, x, y, x, y, color);
        return 1;
    }
    Surface locked{};
    const int32_t ok = lock_display_surface(locked);
    if (ok != 0) {
        draw_clipped_line(&locked, x, y, x, y, color);
        unlock_display_surface();
    }
    return ok;
}

bool clip_rect(const Surface& surface, Rect32& rect) noexcept {
    const Rect32 c = surface.clip;
    if (c.x1 > rect.x2 || rect.x1 > c.x2 || c.y1 > rect.y2 || rect.y1 > c.y2) {
        return false;
    }
    if (rect.x1 < c.x1) {
        rect.x1 = c.x1;
    }
    if (rect.y1 < c.y1) {
        rect.y1 = c.y1;
    }
    if (c.x2 < rect.x2) {
        rect.x2 = c.x2;
    }
    if (c.y2 < rect.y2) {
        rect.y2 = c.y2;
    }
    return rect.x1 <= rect.x2 && rect.y1 <= rect.y2;
}

bool fill_clipped_rect(Surface* target, const Rect32& rect, uint8_t color) noexcept {
    Rect32 clipped = rect;
    if (target != nullptr) {
        if (!clip_rect(*target, clipped)) {
            return false;
        }
        fill_rect(*target, clipped, color);
        return true;
    }
    Surface locked{};
    if (lock_display_surface(locked) == 0) {
        return false;
    }
    const bool filled = clip_rect(locked, clipped);
    if (filled) {
        fill_rect(locked, clipped, color);
    }
    unlock_display_surface();
    return filled;
}

int32_t light_rect_edges(Surface* target, const Rect32& rect, int32_t level) noexcept {
    if (target != nullptr) {
        for_each_edge(rect, [&](int32_t ax, int32_t ay, int32_t bx, int32_t by) {
            light_clipped_line(target, ax, ay, bx, by, level);
        });
        return 0;
    }
    Surface locked{};
    const int32_t ok = lock_display_surface(locked);
    if (ok != 0) {
        for_each_edge(rect, [&](int32_t ax, int32_t ay, int32_t bx, int32_t by) {
            light_clipped_line(&locked, ax, ay, bx, by, level);
        });
        unlock_display_surface();
    }
    return ok;
}

int32_t shade_rect_level(Surface* target, Rect32* rect, int32_t level) noexcept {
    const DisplayContext* display = display_context();
    if (display == nullptr) {
        return 0;
    }
    Surface locked{};
    Surface* surface = target;
    if (surface == nullptr) {
        if (lock_display_surface(locked) == 0) {
            return 0;
        }
        surface = &locked;
    }
    assert(!surface_banded(*surface) && "shade_rect_level does not honour a surface's band");
    Rect32 whole{0, 0, display->width, display->height};
    if (rect == nullptr) {
        rect = &whole;
    }
    int32_t result = 1;
    if (clip_rect(*surface, *rect)) {
        const uint8_t* table = nullptr;
        int32_t row = level;
        if (level < 0) {
            row = std::max(level, shade_level_darkest) - shade_level_darkest;
            table = display->shade_table;
        } else {
            row = std::min(level, light_level_brightest);
            table = display->light_table;
        }
        if (table == nullptr) {
            result = 0;
        } else {
            const uint8_t* lut = table + static_cast<ptrdiff_t>(row) * 0x100;
            for (int32_t y = rect->y1; y <= rect->y2; ++y) {
                uint8_t* p = pixel_at(surface->pixels, surface->pitch, rect->x1, y);
                for (int32_t x = rect->x1; x <= rect->x2; ++x, ++p) {
                    const int32_t index = static_cast<int8_t>(*p);
                    if (row != 0 || index >= 0) {
                        *p = lut[index];
                    }
                }
            }
        }
    }
    if (surface == &locked) {
        unlock_display_surface();
    }
    return result;
}

int32_t draw_rect_outline(Surface* target, const Rect32& rect, uint8_t color) noexcept {
    Surface locked{};
    Surface* surface = target;
    if (surface == nullptr) {
        if (lock_display_surface(locked) == 0) {
            return 0;
        }
        surface = &locked;
    }
    for_each_edge(rect, [&](int32_t ax, int32_t ay, int32_t bx, int32_t by) {
        draw_clipped_line(surface, ax, ay, bx, by, color);
    });
    if (surface == &locked) {
        unlock_display_surface();
    }
    return 1;
}

int32_t gray_rect(Surface* target, const Rect32& rect) noexcept {
    const DisplayContext* display = display_context();
    if (display == nullptr || (display->flags & display_flag_gray_table) == 0) {
        return 0;
    }
    Rect32 clipped = rect;
    Surface locked{};
    Surface* surface = target;
    if (surface == nullptr) {
        if (lock_display_surface(locked) == 0) {
            return 0;
        }
        surface = &locked;
    }
    assert(!surface_banded(*surface) && "gray_rect does not honour a surface's band");
    if (clip_rect(*surface, clipped)) {
        remap_rows(
            pixel_at(surface->pixels, surface->pitch, clipped.x1, clipped.y1),
            surface->pitch,
            clipped.x2 - clipped.x1 + 1,
            clipped.y2 - clipped.y1 + 1,
            display->gray_table
        );
    }
    if (surface == &locked) {
        unlock_display_surface();
    }
    return 1;
}

int32_t clear_dithered_rect(Surface* target, const Rect32& rect, int32_t phase) noexcept {
    Surface locked{};
    Surface* surface = target;
    if (surface == nullptr) {
        if (lock_display_surface(locked) == 0) {
            return 0;
        }
        surface = &locked;
    }
    assert(!surface_banded(*surface) && "clear_dithered_rect does not honour a surface's band");
    Rect32 r = rect;
    if (clip_rect(*surface, r)) {
        const int32_t first_aligned = (r.x1 + 3) & ~3;
        const int32_t end_aligned = (r.x2 + 3) & ~3;
        const std::ptrdiff_t limit = static_cast<std::ptrdiff_t>(surface->pitch) * surface->height;
        for (int32_t y = r.y1; y <= r.y2; ++y) {
            const std::ptrdiff_t row = static_cast<std::ptrdiff_t>(surface->pitch) * y;
            auto clear = [&](int32_t x) {
                const std::ptrdiff_t at = row + x;
                if (at >= 0 && at < limit) {
                    surface->pixels[at] = 0;
                }
            };
            for (int32_t x = r.x1 + ((r.x1 + y + phase) & 1); x < first_aligned; x += 2) {
                clear(x);
            }
            const bool even_row = ((phase + y) & 1) == 0;
            for (int32_t x = first_aligned; x < end_aligned; x += 2) {
                clear(even_row ? x : x + 1);
            }
            for (int32_t x = even_row ? end_aligned : end_aligned + 1; x <= r.x2; x += 2) {
                clear(x);
            }
        }
    }
    if (surface == &locked) {
        unlock_display_surface();
    }
    return 1;
}

void draw_font_text(
    uint8_t* pixels,
    int32_t pitch,
    const uint8_t* font,
    const char* text,
    int32_t x,
    int32_t y,
    uint8_t fg,
    uint8_t bg,
    uint8_t transparent
) noexcept {
    constexpr std::size_t baseline_offset = 2;
    constexpr std::size_t first_char_offset = 3;
    constexpr std::size_t glyph_table_offset = 4;
    const int32_t height = font[0];
    const uint8_t first = font[first_char_offset];
    const auto baseline = static_cast<int8_t>(font[baseline_offset]);
    uint8_t* pen = pixels + static_cast<std::ptrdiff_t>(y - baseline) * pitch + x;
    for (const auto* p = reinterpret_cast<const uint8_t*>(text); *p != 0 && *p != '\n'; ++p) {
        if (*p < first) {
            continue;
        }
        const std::size_t slot = glyph_table_offset + static_cast<std::size_t>(*p - first) * 2;
        const uint16_t offset = static_cast<uint16_t>(font[slot] | (font[slot + 1] << 8));
        if (offset == 0) {
            continue;
        }
        const uint8_t* glyph = font + offset;
        const uint8_t width = *glyph++;
        if (height == 0) {
            pen += width;
            continue;
        }
        uint8_t* out = pen;
        uint8_t bits = 0;
        uint8_t bits_left = 1;
        for (int32_t row = height; row != 0; --row) {
            uint8_t column = width;
            do {
                if (--bits_left == 0) {
                    bits = *glyph++;
                    bits_left = 8;
                }
                const uint8_t value = (bits & 0x80u) != 0 ? fg : bg;
                bits = static_cast<uint8_t>(bits << 1);
                if (value != transparent) {
                    *out = value;
                }
                ++out;
            } while (--column != 0);
            out += pitch - width;
        }
        pen += width;
    }
}

void draw_text(
    Surface* target, const char* text, int32_t x, int32_t y, int32_t max_width
) noexcept {
    const DisplayContext* display = display_context();
    if (display == nullptr || display->font == nullptr || text == nullptr) {
        return;
    }
    const auto* font = static_cast<const uint8_t*>(display->font);
    int32_t width = measure_text_width(font, text);
    char trimmed[text_trim_capacity + 1];
    if (max_width != text_width_unbounded && max_width < width) {
        oa::base::text::copy_padded(trimmed, text, text_trim_capacity);
        trimmed[text_trim_capacity] = 0;
        text = trimmed;
        const size_t length = std::strlen(trimmed);
        char* last = trimmed + (length != 0 ? length - 1 : 0);
        while (length != 0) {
            *last = 0;
            if (last == trimmed) {
                break;
            }
            --last;
            width = measure_text_width(font, trimmed);
            if (max_width >= width) {
                break;
            }
        }
    }
    const Rect32 box{x, y, x + width, y + font_height(font)};
    Surface locked{};
    Surface* surface = target;
    if (surface == nullptr) {
        if (lock_display_surface(locked) == 0) {
            return;
        }
        surface = &locked;
    }
    assert(!surface_banded(*surface) && "draw_text does not honour a surface's band");
    if (rect_inside(box, surface->clip)) {
        draw_font_text(
            surface->pixels,
            surface->pitch,
            font,
            text,
            x,
            y,
            static_cast<uint8_t>(display->text_color),
            static_cast<uint8_t>(display->text_background),
            static_cast<uint8_t>(display->text_transparent)
        );
    }
    if (surface == &locked) {
        unlock_display_surface();
    }
}

} // namespace oa::present
