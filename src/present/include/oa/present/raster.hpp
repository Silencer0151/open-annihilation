// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// 8-bit line and rectangle primitives. The leaf primitives draw unclipped;
// the wrappers clip against Surface::clip and,
// given a null target, draw on the locked display surface.

#include "oa/present/surface.h"

#include <cstddef>
#include <cstdint>

namespace oa::present {

// Byte bitmap addressed through a 16-bit pitch, the record the XOR primitives
// draw into; no game screen draws with them.
struct PitchedBitmap {
    uint16_t pitch = 0;
    uint16_t reserved_after_pitch = 0; // no known use: left zero, never read
    uint8_t* pixels = nullptr;
};

/// Clips a segment to the extent [0, width) x [0, height).
///
/// Endpoints outside are moved along the segment onto the nearest edge,
/// with the offsets computed from 64-bit products.
///
/// @param width extent width in pixels
/// @param height extent height in rows
/// @param[in,out] x0 start column
/// @param[in,out] y0 start row
/// @param[in,out] x1 end column
/// @param[in,out] y1 end row
/// @return 0 when nothing remains, non-zero otherwise
[[nodiscard]] int32_t clip_line_to_extent(
    int32_t width, int32_t height, int32_t& x0, int32_t& y0, int32_t& x1, int32_t& y1
) noexcept;

/// Clips a segment to the surface extent, then fills it with a colour (Bresenham, both ends included).
///
/// @param[in,out] surface target surface; its clip rectangle is not used
/// @param x0 start column
/// @param y0 start row
/// @param x1 end column
/// @param y1 end row
/// @param color line colour
void fill_line(
    Surface& surface, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color
) noexcept;

/// Remaps the pixels along an unclipped segment through one row of a 256-wide table.
///
/// @param[in,out] surface target surface
/// @param x0 start column
/// @param y0 start row
/// @param x1 end column
/// @param y1 end row
/// @param row table row (256 bytes each)
/// @param table remap table
/// @quirk The vertical case addresses rows by width rather than pitch, and the
///     sloped cases never advance x the way fill_line does (x-major lines stay
///     in one column; y-major lines drift left).
void remap_line(
    Surface& surface,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t row,
    const uint8_t* table
) noexcept;

/// XORs a value along a segment of a pitched bitmap, clipped to an extent.
///
/// No game screen draws with this primitive. The segment is clipped to the
/// given extent, which the span code assumes.
///
/// @param bitmap target bitmap
/// @param width extent width in pixels
/// @param height extent height in rows
/// @param x0 start column
/// @param y0 start row
/// @param x1 end column
/// @param y1 end row
/// @param value XOR mask
void xor_line(
    const PitchedBitmap& bitmap,
    int32_t width,
    int32_t height,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    uint8_t value
) noexcept;

/// Clips a segment held as {x1, y1, x2, y2} to a 16-bit width x height extent.
///
/// @param width extent width in pixels
/// @param height extent height in rows
/// @param[in,out] segment start (x1, y1) and end (x2, y2)
/// @return 0 when nothing remains, non-zero otherwise
/// @quirk With y growing, the "entirely above" test reads only the low 16 bits
///     of the end row (segment.y2).
[[nodiscard]] int32_t
clip_segment_to_extent(uint16_t width, uint16_t height, Rect32& segment) noexcept;

/// Draws the four edges of an inclusive rectangle, each clipped to the surface extent.
///
/// @param[in,out] surface target surface
/// @param rect inclusive rectangle
/// @param color edge colour
void outline_rect(Surface& surface, const Rect32& rect, uint8_t color) noexcept;

/// XORs the edges of an inclusive rectangle of a pitched bitmap, visiting each corner once.
///
/// @param bitmap target bitmap
/// @param width extent width in pixels
/// @param height extent height in rows
/// @param rect inclusive rectangle
/// @param value XOR mask
void xor_rect_outline(
    const PitchedBitmap& bitmap, int32_t width, int32_t height, const Rect32& rect, uint8_t value
) noexcept;

/// Fills an inclusive, already clipped rectangle.
///
/// A non-positive width draws nothing.
///
/// @param[in,out] surface target surface
/// @param rect inclusive rectangle inside the surface
/// @param color fill colour
/// @quirk Rows run as a do-while over y2 - y1, so an inverted rectangle still
///     fills its first row.
void fill_rect(Surface& surface, const Rect32& rect, uint8_t color) noexcept;

/// XORs every byte of an inclusive rectangle of a pitched bitmap.
///
/// @param bitmap target bitmap
/// @param rect inclusive rectangle; not clipped
/// @param value XOR mask
/// @quirk The row offset is a 16-bit multiply that keeps the high half of y,
///     and an inverted rectangle still XORs its first row.
void xor_rect(const PitchedBitmap& bitmap, const Rect32& rect, uint8_t value) noexcept;

/// Remaps a block of rows through a 256-byte table.
///
/// @param[in,out] first first pixel of the block
/// @param pitch bytes between rows
/// @param width row width in pixels; non-positive does nothing
/// @param height number of rows; non-positive does nothing
/// @param table 256-byte remap table
void remap_rows(
    uint8_t* first, int32_t pitch, int32_t width, int32_t height, const uint8_t* table
) noexcept;

/// Clips a segment to the surface clip rectangle.
///
/// Offsets come from 32-bit products that wrap, as the game computes them.
///
/// @param surface surface whose clip rectangle bounds the segment
/// @param[in,out] x0 start column
/// @param[in,out] y0 start row
/// @param[in,out] x1 end column
/// @param[in,out] y1 end row
/// @return 1 when the segment is drawable, 0 when it lies outside
[[nodiscard]] int32_t clip_line_to_rect(
    const Surface& surface, int32_t& x0, int32_t& y0, int32_t& x1, int32_t& y1
) noexcept;

/// Draws a line clipped to the target's clip rectangle.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param x0 start column
/// @param y0 start row
/// @param x1 end column
/// @param y1 end row
/// @param color line colour
/// @return 1 with a target; with the display surface, 0 when it cannot be locked
int32_t draw_clipped_line(
    Surface* target, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color
) noexcept;

/// Remaps a clipped line through one row of the display light table (see remap_line).
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param x0 start column
/// @param y0 start row
/// @param x1 end column
/// @param y1 end row
/// @param level light table row
/// @return 0 without a display light table or when the display cannot be locked; 1 otherwise
int32_t light_clipped_line(
    Surface* target, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t level
) noexcept;

/// Draws one pixel, clipped to the target's clip rectangle.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param x column
/// @param y row
/// @param color pixel colour
/// @return 1 with a target; with the display surface, 0 when it cannot be locked
int32_t draw_point(Surface* target, int32_t x, int32_t y, uint8_t color) noexcept;

/// Clamps an inclusive rectangle to the surface clip rectangle.
///
/// @param surface surface whose clip rectangle bounds the rectangle
/// @param[in,out] rect rectangle to clamp; unchanged when it lies wholly outside
/// @return true when a non-empty rectangle remains
[[nodiscard]] bool clip_rect(const Surface& surface, Rect32& rect) noexcept;

/// Clips a rectangle to the target clip rectangle and fills what remains.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param rect inclusive rectangle
/// @param color fill colour
/// @return true when anything was filled
bool fill_clipped_rect(Surface* target, const Rect32& rect, uint8_t color) noexcept;

/// Lights the four edges of a rectangle through one row of the display light table.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param rect inclusive rectangle
/// @param level light table row
/// @return 0 with a target; with the display surface, 0 when it cannot be
///     locked and non-zero otherwise
int32_t light_rect_edges(Surface* target, const Rect32& rect, int32_t level) noexcept;

// shade_rect_level levels: negative levels select display shade table rows,
// the others display light table rows.
inline constexpr int32_t shade_level_darkest = -0x20;
inline constexpr int32_t light_level_brightest = 0x1F;

/// Remaps a clipped rectangle through one row of the display shade or light table.
///
/// A negative level selects shade table row (level + 0x20), clamped at
/// -0x20; other levels select light table rows, clamped at 0x1F. A null
/// target with no table releases the display lock.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param[in,out] rect rectangle to remap, clipped in place; null for the whole display
/// @param level shade (negative) or light level
/// @return 0 without a display, when the display cannot be locked or when the
///     selected table is not loaded; 1 otherwise
/// @quirk Pixels index the row as signed bytes, so values 0x80..0xFF read the
///     previous row; in row 0, which has no previous row, they are left
///     unchanged.
int32_t shade_rect_level(Surface* target, Rect32* rect, int32_t level) noexcept;

/// Draws the four edges of a rectangle, each clipped to the target's clip rectangle.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param rect inclusive rectangle
/// @param color edge colour
/// @return 0 when the display surface cannot be locked; 1 otherwise
int32_t draw_rect_outline(Surface* target, const Rect32& rect, uint8_t color) noexcept;

/// Grays a clipped rectangle through the display gray table.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param rect inclusive rectangle
/// @return 0 when display_flag_gray_table is clear or the display cannot be
///     locked; 1 otherwise
int32_t gray_rect(Surface* target, const Rect32& rect) noexcept;

/// Clears to 0 every other pixel of a clipped rectangle in a checkerboard.
///
/// A pixel is cleared when its row + column + `phase` is even.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param rect inclusive rectangle
/// @param phase checkerboard shift; its low bit selects the half
/// @return 0 when the display surface cannot be locked; 1 otherwise
/// @quirk The middle of each row is processed four pixels at a time from the first
///     aligned column up to x2 rounded up to four, so up to three pixels right
///     of x2 (and left of the first aligned column in narrow rectangles) are
///     also cleared; writes stay inside the surface.
int32_t clear_dithered_rect(Surface* target, const Rect32& rect, int32_t phase) noexcept;

/// Draws a 1-bit font string with its baseline at a row, without clipping.
///
/// The font holds its height in byte 0, a signed baseline in byte 2, the
/// first character code in byte 3, then u16 glyph offsets; a glyph is its
/// width, then row-major bits, MSB first, packed continuously across rows.
/// Drawing stops at NUL or newline. Characters below the first code or
/// without a glyph are skipped without advancing. A font of zero height draws
/// nothing and only advances the pen.
///
/// @param[in,out] pixels target pixels
/// @param pitch target bytes per row
/// @param font font data
/// @param text text to draw
/// @param x column of the first character
/// @param y baseline row
/// @param fg colour of set bits
/// @param bg colour of clear bits
/// @param transparent colour that is not drawn (either fg or bg may equal it)
/// @quirk A zero glyph width draws 256 columns.
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
) noexcept;

// Characters kept by the copy that trimming works on.
inline constexpr size_t text_trim_capacity = 299;
// draw_text max_width that never trims.
inline constexpr int32_t text_width_unbounded = -1;

/// Draws text in the active font with the display text colours, its baseline at a row.
///
/// When the text is wider than `max_width` (other than
/// text_width_unbounded), a copy of up to 299 characters loses trailing
/// characters until it fits. Nothing is drawn unless (x, y)-(x + width,
/// y + font height) lies inside the target clip rectangle. The trimmed copy
/// is always terminated, and an empty text is never trimmed.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param text text to draw; null draws nothing, as does a display without a font
/// @param x column of the first character
/// @param y baseline row
/// @param max_width widest text in pixels, or text_width_unbounded
void draw_text(Surface* target, const char* text, int32_t x, int32_t y, int32_t max_width) noexcept;

} // namespace oa::present
