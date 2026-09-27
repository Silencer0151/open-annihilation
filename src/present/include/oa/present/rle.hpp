// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Row run-length sprite streams: per row a little-endian u16 byte count, then
// command bytes (bit 0: skip n>>1 transparent pixels; bit 1: repeat the next
// byte (n>>2)+1 times; otherwise copy (n>>2)+1 literal bytes).
//
// The decoders read only inside the stream they are given, and a row's
// commands only inside that row. A row that leaves the stream, or whose
// commands end before the decoded rectangle's right edge, is malformed: the
// decode stops there and reports it, keeping the pixels already drawn.
//
// The sprite drawers of blit.hpp have no stream size to give, since a Sprite
// record holds none: they find a row-RLE sprite's rows through its length
// words and trust the producer to have written them whole (relocate_gaf
// checks a GAF's, and the encoders write whole rows). On that path only each
// row's commands are bounded.

#include "oa/present/surface.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace oa::present {

inline constexpr size_t rle_row_length_bytes = 2; // the u16 byte count before each row's commands
// Bytes in one row of a blend, shade or light table, one for each palette
// index; the blend and shade draws select a row with the source pixel.
inline constexpr int32_t palette_table_row_bytes = 0x100;

/// Decodes a rectangle of a row-RLE stream onto 8-bit pixels.
///
/// Rows src_rect.y1..y2 and columns src_rect.x1..x2 are decoded; skipped
/// pixels leave the destination unchanged, and a command straddling the left
/// edge resumes with its inside part.
///
/// @param[in,out] dst destination pixels
/// @param pitch destination bytes per row
/// @param dst_rect destination; only its top-left corner (x1, y1) is used
/// @param stream row-RLE stream starting at its first row
/// @param src_rect inclusive rectangle of the sprite to decode
/// @return true when every row of the rectangle was decoded (an empty
///     rectangle decodes nothing); false when the stream is malformed or
///     src_rect.y1 is negative
bool decode_rle_rows(
    uint8_t* dst,
    int32_t pitch,
    const Rect32& dst_rect,
    std::span<const uint8_t> stream,
    const Rect32& src_rect
) noexcept;

/// Copies a raw rectangle, blending non-key pixels through a 256x256 table.
///
/// Each drawn pixel becomes table[source * 256 + destination].
///
/// @param[in,out] dst destination surface
/// @param src source surface
/// @param src_rect inclusive source rectangle; not clipped
/// @param dst_rect destination; only its top-left corner is used
/// @param key transparent source colour
/// @param table 256x256 blend table
void copy_rect_blended(
    Surface& dst,
    const Surface& src,
    const Rect32& src_rect,
    const Rect32& dst_rect,
    uint8_t key,
    const uint8_t* table
) noexcept;

/// Decodes a rectangle of a row-RLE stream, blending every drawn pixel through a 256x256 table.
///
/// Each drawn pixel becomes table[source * 256 + destination].
///
/// @param[in,out] dst destination pixels
/// @param pitch destination bytes per row
/// @param dst_rect destination; only its top-left corner is used
/// @param stream row-RLE stream starting at its first row
/// @param src_rect inclusive rectangle of the sprite to decode
/// @param table 256x256 blend table
/// @return true when every row of the rectangle was decoded; false as decode_rle_rows returns it
bool decode_rle_rows_blended(
    uint8_t* dst,
    int32_t pitch,
    const Rect32& dst_rect,
    std::span<const uint8_t> stream,
    const Rect32& src_rect,
    const uint8_t* table
) noexcept;

/// Remaps destination pixels through a 256-byte table wherever a source rectangle is not the key colour.
///
/// Used by the shadow and mask passes.
///
/// @param[in,out] dst destination surface
/// @param src mask surface
/// @param src_rect inclusive mask rectangle; not clipped
/// @param dst_rect destination; only its top-left corner is used
/// @param key transparent mask colour
/// @param table 256-byte remap table
void remap_under_mask(
    Surface& dst,
    const Surface& src,
    const Rect32& src_rect,
    const Rect32& dst_rect,
    uint8_t key,
    const uint8_t* table
) noexcept;

// Scratch the row encoder carries between its calls: the pending literal
// bytes and the count of bytes the current row has produced.
inline constexpr int32_t rle_literal_capacity = 0x100;
inline constexpr int32_t rle_short_run_max = 0x40; // literal and fill commands
inline constexpr int32_t rle_skip_max = 0x7F;
inline constexpr int32_t rle_run_max = 0x80;
inline constexpr int32_t rle_min_repeat = 3;

struct RleEncoder {
    uint8_t literal[rle_literal_capacity]{};
    int32_t emitted = 0; // bytes produced by the current row
};

/// Emits pending literal bytes as copy commands of at most 0x40 bytes each.
///
/// @param[in,out] encoder encoder whose literal buffer holds the bytes; its
///     emitted count grows by the bytes produced
/// @param[out] out output position; null only counts
/// @param count number of literal bytes; at least one command is emitted
/// @return the advanced output pointer (null when `out` is null)
uint8_t* emit_literal_runs(RleEncoder& encoder, uint8_t* out, int32_t count) noexcept;

/// Emits a run of one colour as skip commands (colour == key, at most 0x7F
/// each) or fill commands (at most 0x40 each).
///
/// @param[in,out] encoder encoder whose emitted count grows by the bytes produced
/// @param[out] out output position; null only counts
/// @param count run length in pixels; at least one command is emitted
/// @param color run colour
/// @param key transparent colour
/// @return the advanced output pointer (null when `out` is null)
uint8_t* emit_repeat_runs(
    RleEncoder& encoder, uint8_t* out, int32_t count, uint8_t color, uint8_t key
) noexcept;

/// Encodes one row of pixels as RLE commands.
///
/// Repeats of three or more become fill (or skip) commands; literals are
/// flushed at 0x80 pending bytes.
///
/// @param[in,out] encoder encoder scratch
/// @param[out] out output position; null only measures
/// @param row row pixels
/// @param width row width in pixels
/// @param key transparent colour
/// @return encoded size in bytes; 0 for an all-key row, which emits nothing
int32_t encode_rle_row(
    RleEncoder& encoder, uint8_t* out, const uint8_t* row, int32_t width, uint8_t key
) noexcept;

/// Encodes a raw sprite as a row-RLE stream, each row prefixed with its u16 length.
///
/// @param[in,out] encoder encoder scratch
/// @param[out] out output buffer; null only measures
/// @param sprite raw sprite; its key is the transparent colour
/// @return stream size in bytes
int32_t encode_rle_sprite(RleEncoder& encoder, uint8_t* out, const Sprite& sprite) noexcept;

// First palette index of the shade ramp: shaded draws select table row
// (pixel - shade_ramp_base), so indices below the ramp address rows before
// the table as in the game.
inline constexpr int32_t shade_ramp_base = 0x4F;

/// Decodes a rectangle of a row-RLE stream, darkening the destination through table row (pixel - 0x4F).
///
/// @param[in,out] dst destination pixels
/// @param pitch destination bytes per row
/// @param dst_rect destination; only its top-left corner is used
/// @param stream row-RLE stream starting at its first row
/// @param src_rect inclusive rectangle of the sprite to decode
/// @param table light table of 256-byte rows
/// @return true when every row of the rectangle was decoded; false as decode_rle_rows returns it
/// @quirk Pixels below shade_ramp_base address rows before the table.
bool decode_rle_rows_shaded(
    uint8_t* dst,
    int32_t pitch,
    const Rect32& dst_rect,
    std::span<const uint8_t> stream,
    const Rect32& src_rect,
    const uint8_t* table
) noexcept;

/// Copies a keyed raw rectangle, darkening the destination through table row (pixel - 0x4F).
///
/// @param[in,out] dst destination surface
/// @param src source surface whose pixels select the rows
/// @param src_rect inclusive source rectangle; not clipped
/// @param dst_rect destination; only its top-left corner is used
/// @param key transparent source colour
/// @param table light table of 256-byte rows
/// @quirk Pixels below shade_ramp_base address rows before the table.
void copy_rect_shaded(
    Surface& dst,
    const Surface& src,
    const Rect32& src_rect,
    const Rect32& dst_rect,
    uint8_t key,
    const uint8_t* table
) noexcept;

/// Decodes a rectangle of a row-RLE stream, writing table[pixel] instead of the pixel.
///
/// @param[in,out] dst destination pixels
/// @param pitch destination bytes per row
/// @param dst_rect destination; only its top-left corner is used
/// @param stream row-RLE stream starting at its first row
/// @param src_rect inclusive rectangle of the sprite to decode
/// @param table 256-byte remap table
/// @return true when every row of the rectangle was decoded; false as decode_rle_rows returns it
bool decode_rle_rows_remapped(
    uint8_t* dst,
    int32_t pitch,
    const Rect32& dst_rect,
    std::span<const uint8_t> stream,
    const Rect32& src_rect,
    const uint8_t* table
) noexcept;

} // namespace oa::present
