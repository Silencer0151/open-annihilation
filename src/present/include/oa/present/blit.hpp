// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Rectangle copies between 8-bit surfaces and sprite drawing. Rectangles are
// inclusive; a null target surface draws on the locked display surface.

#include "oa/present/surface.h"

#include <cstdint>

namespace oa::present {

/// Shrinks a source/destination rectangle pair so the destination fits a clip rectangle.
///
/// Each destination edge outside the clip moves in, and the matching source
/// edge moves by the same amount. The result may be empty; it is not checked.
///
/// @param[in,out] src source rectangle
/// @param[in,out] dst destination rectangle, same size as `src`
/// @param clip inclusive clip rectangle
void trim_to_clip(Rect32& src, Rect32& dst, const Rect32& clip) noexcept;

/// Shrinks a source/destination rectangle pair so the destination fits a surface's clip and band.
///
/// trim_to_clip with the surface's clip rectangle, then the destination's
/// rows outside the surface's band (surface_band) go as well, with the
/// matching source rows: each row of a sprite draws alone, so the rows left
/// draw as they would without the band.
///
/// @param[in,out] src source rectangle
/// @param[in,out] dst destination rectangle, same size as `src`
/// @param surface surface whose clip and band the destination must fit
void trim_to_surface(Rect32& src, Rect32& dst, const Surface& surface) noexcept;

/// Copies all of a surface to a position of another, clipped to the destination extent.
///
/// The destination clip rectangle and both origins are ignored.
///
/// @param[in,out] dst destination surface
/// @param src source surface
/// @param x destination column of the source's first pixel; may be negative
/// @param y destination row of the source's first row; may be negative
void copy_surface_clipped(Surface& dst, const Surface& src, int32_t x, int32_t y) noexcept;

/// Copies all of a surface to a position of another, skipping key-colour pixels.
///
/// Clipped to the destination extent as copy_surface_clipped is.
///
/// @param[in,out] dst destination surface
/// @param src source surface
/// @param x destination column of the source's first pixel; may be negative
/// @param y destination row of the source's first row; may be negative
/// @param key transparent source colour
void copy_surface_keyed(
    Surface& dst, const Surface& src, int32_t x, int32_t y, uint8_t key
) noexcept;

/// Copies a source rectangle to a destination position without clipping.
///
/// @param[in,out] dst destination surface
/// @param src source surface
/// @param src_rect inclusive source rectangle; an empty one copies nothing
/// @param dst_pos destination; only its top-left corner (x1, y1) is used
void copy_rect(
    Surface& dst, const Surface& src, const Rect32& src_rect, const Rect32& dst_pos
) noexcept;

/// Copies a source rectangle to a destination position without clipping, skipping key-colour pixels.
///
/// @param[in,out] dst destination surface
/// @param src source surface
/// @param src_rect inclusive source rectangle; an empty one copies nothing
/// @param dst_pos destination; only its top-left corner (x1, y1) is used
/// @param key transparent source colour
void copy_rect_keyed(
    Surface& dst, const Surface& src, const Rect32& src_rect, const Rect32& dst_pos, uint8_t key
) noexcept;

inline constexpr int32_t tile_size = 32;

/// Copies one packed 32x32 tile to a surface position without clipping.
///
/// @param[in,out] dst destination surface
/// @param x destination column
/// @param y destination row
/// @param tile 32 rows of 32 pixels
void copy_tile(Surface& dst, int32_t x, int32_t y, const uint8_t* tile) noexcept;

/// Blits a surface at a position less its origin, or copies the display surface into a surface.
///
/// With both surfaces set, `src` is copied as copy_surface_clipped does. A
/// null `dst` draws onto the locked display surface; a null `src` copies the
/// locked display surface into `dst` at (x, y). Nothing happens when both are
/// null or the display cannot be locked.
///
/// @param[in,out] dst destination surface; null for the display surface
/// @param src source surface; null for the display surface
/// @param x destination column of the source origin
/// @param y destination row of the source origin
void blit_surface(Surface* dst, const Surface* src, int32_t x, int32_t y) noexcept;

/// Blits a surface at a position less its origin, skipping key-colour pixels.
///
/// @param[in,out] dst destination surface; null for the locked display surface
/// @param src source surface
/// @param x destination column of the source origin
/// @param y destination row of the source origin
/// @param key transparent source colour
void blit_surface_keyed(
    Surface* dst, const Surface& src, int32_t x, int32_t y, uint8_t key
) noexcept;

/// Copies a source rectangle as copy_rect does, onto a surface or the locked display surface.
///
/// @param[in,out] dst destination surface; null for the locked display surface
/// @param src source surface
/// @param src_rect inclusive source rectangle
/// @param dst_pos destination; only its top-left corner is used
void blit_rect(
    Surface* dst, const Surface& src, const Rect32& src_rect, const Rect32& dst_pos
) noexcept;

/// Copies a source rectangle as copy_rect_keyed does, onto a surface or the locked display surface.
///
/// @param[in,out] dst destination surface; null for the locked display surface
/// @param src source surface
/// @param src_rect inclusive source rectangle
/// @param dst_pos destination; only its top-left corner is used
/// @param key transparent source colour
void blit_rect_keyed(
    Surface* dst, const Surface& src, const Rect32& src_rect, const Rect32& dst_pos, uint8_t key
) noexcept;

/// Copies a 32x32 tile as copy_tile does, onto a surface or the locked display surface.
///
/// @param[in,out] dst destination surface; null for the locked display surface
/// @param x destination column
/// @param y destination row
/// @param tile 32 rows of 32 pixels
void blit_tile(Surface* dst, int32_t x, int32_t y, const uint8_t* tile) noexcept;

/// Draws a sprite with its key colour transparent, clipped to the target's clip rectangle.
///
/// Raw sprites are copied keyed and row-RLE sprites decoded. A composite
/// sprite draws each child the same way, or blended through the display
/// alpha table when the child's child_draw_mode is non-zero.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param sprite sprite to draw; null draws nothing
/// @param x target column of the sprite hotspot
/// @param y target row of the sprite hotspot
void draw_sprite(Surface* target, const Sprite* sprite, int32_t x, int32_t y) noexcept;

/// Draws a sprite blended through the display alpha table.
///
/// Nothing is drawn unless display_flag_alpha_table is set. Composite
/// sprites draw each child blended.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param sprite sprite to draw; null draws nothing
/// @param x target column of the sprite hotspot
/// @param y target row of the sprite hotspot
void draw_sprite_blended(Surface* target, const Sprite* sprite, int32_t x, int32_t y) noexcept;

/// Draws a sprite blended through a given table laid out as the display
/// alpha table: each pixel takes table[sprite colour * 256 + target colour].
///
/// Composite sprites draw each child blended through the same table.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param sprite sprite to draw; null draws nothing
/// @param x target column of the sprite hotspot
/// @param y target row of the sprite hotspot
/// @param table 256 rows of 256 colours; null draws nothing
void draw_sprite_blended_through(
    Surface* target, const Sprite* sprite, int32_t x, int32_t y, const uint8_t* table
) noexcept;

/// Draws a sprite through one row of the display light table.
///
/// Nothing is drawn unless display_flag_light_table is set. Row-RLE sprites
/// are remapped through row `level`.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param sprite sprite to draw; null draws nothing
/// @param x target column of the sprite hotspot
/// @param y target row of the sprite hotspot
/// @param level light table row (256 bytes each)
/// @quirk Raw sprites pass `level` as the transparent key and blend through
///     the table base instead, and composite sprites draw their children
///     blended through the alpha table.
void draw_sprite_lit(
    Surface* target, const Sprite* sprite, int32_t x, int32_t y, int32_t level
) noexcept;

/// Lights the pixels under a sprite through the display light table, as an
/// explosion's flash lights the battlefield.
///
/// Each of the sprite's light-ramp pixels selects the light table row that
/// relights the pixel under it. Nothing is drawn unless
/// display_flag_light_table is set; composite sprites light through each
/// child.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param sprite sprite whose pixels select the rows; null, or null data, draws nothing
/// @param x target column of the sprite hotspot
/// @param y target row of the sprite hotspot
void draw_sprite_shadow(Surface* target, const Sprite* sprite, int32_t x, int32_t y) noexcept;

/// Draws a sprite opaquely: raw sprites ignore the key colour.
///
/// Row-RLE sprites decode as draw_sprite does, and composite sprites draw
/// their children as draw_sprite does.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param sprite sprite to draw; null draws nothing
/// @param x target column of the sprite hotspot
/// @param y target row of the sprite hotspot
void draw_sprite_opaque(Surface* target, const Sprite* sprite, int32_t x, int32_t y) noexcept;

/// Grays the target pixels under a raw sprite's non-key pixels through the display gray table.
///
/// Runs only while display_flag_gray_table is set and the sprite is raw; a
/// composite sprite grays each raw child.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param sprite mask sprite; null draws nothing
/// @param x target column of the sprite hotspot
/// @param y target row of the sprite hotspot
void draw_sprite_gray(Surface* target, const Sprite* sprite, int32_t x, int32_t y) noexcept;

/// Clears to 0 every other target pixel under a raw sprite's non-key pixels, in a checkerboard.
///
/// A pixel is cleared when its row + column + `phase` is even. Composite
/// sprites erase each child.
///
/// @param[in,out] target target surface; null for the locked display surface
/// @param sprite mask sprite; null or row-RLE draws nothing
/// @param x target column of the sprite hotspot
/// @param y target row of the sprite hotspot
/// @param phase checkerboard shift; its low bit selects the half
void erase_sprite_dithered(
    Surface* target, const Sprite* sprite, int32_t x, int32_t y, int32_t phase
) noexcept;

/// Describes a surface's pixels as a raw sprite with key 0xFF.
///
/// The hotspot is the surface origin; the child count and child_draw_mode are
/// cleared, and reserved_after_child_draw_mode and the aux plane are left
/// alone.
///
/// @param[in,out] sprite sprite to fill
/// @param surface surface whose pixels the sprite views
/// @quirk The sprite width is the surface pitch, not its width.
void sprite_from_surface(Sprite& sprite, const Surface& surface) noexcept;

/// Describes a surface's pixels as a raw sprite with a chosen key colour.
///
/// @param[in,out] sprite sprite to fill, as sprite_from_surface does
/// @param surface surface whose pixels the sprite views
/// @param key transparent colour
void sprite_from_surface_keyed(Sprite& sprite, const Surface& surface, uint8_t key) noexcept;

/// Returns a sprite's width.
///
/// @param sprite sprite to read
/// @return width in pixels
[[nodiscard]] uint16_t sprite_width(const Sprite& sprite) noexcept;

/// Fills a raw sprite with one colour and zeroes its auxiliary plane.
///
/// @param[in,out] sprite sprite to fill; row-RLE sprites are left alone
/// @param color fill colour
void clear_sprite(Sprite& sprite, uint8_t color) noexcept;

/// Copies the screen area under a sprite placed at a position into its pixels.
///
/// The source rectangle is the sprite rectangle at (x, y) less its hotspot;
/// it is not clipped.
///
/// @param screen surface to read
/// @param sprite raw sprite whose pixels receive the area
/// @param x screen column of the sprite hotspot
/// @param y screen row of the sprite hotspot
void capture_under_sprite(
    const Surface& screen, const Sprite& sprite, int32_t x, int32_t y
) noexcept;

inline constexpr int16_t displacement_transparent = 32000;

/// Draws a lens sprite whose pixels displace the background under it.
///
/// With a target, the background under the sprite is first captured into
/// the first half of `aux`. Each output pixel, built in the second half, is
/// the background pixel at its own index plus its displacement, or the key
/// colour for displacement_transparent; the result is drawn as draw_sprite
/// does.
///
/// @param[in,out] target target surface; null draws on the locked display
///     surface over the background already in `aux`
/// @param[in,out] sprite lens sprite: `data` holds one little-endian i16
///     displacement per pixel, `aux` 2 * width * height bytes; its data
///     pointer is restored afterwards
/// @param x target column of the sprite hotspot
/// @param y target row of the sprite hotspot
void draw_displacement_sprite(Surface* target, Sprite& sprite, int32_t x, int32_t y) noexcept;

/// Zeroes every pixel of a raw sprite that is not its key colour.
///
/// @param[in,out] sprite raw sprite to clear
void clear_unkeyed_pixels(Sprite& sprite) noexcept;

/// Writes a sprite's key colour wherever another sprite's opaque pixels overlap it.
///
/// The top-left of `src` lands on dst column dst.origin_x - src.origin_x - x
/// and row dst.origin_y - src.origin_y - y.
///
/// @param src raw mask sprite
/// @param[in,out] dst raw sprite that receives its key colour
/// @param x column offset of `src`
/// @param y row offset of `src`
/// @quirk The row width is limited by src's remaining columns but not by
///     dst's, so a positive destination column can run past a dst row.
void stamp_sprite_mask(const Sprite& src, Sprite& dst, int32_t x, int32_t y) noexcept;

} // namespace oa::present
