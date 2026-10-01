// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The conversion of the game's RGB frames into the 32-bit pixels the window
// shows (SDL_PIXELFORMAT_XRGB8888, 0xXXRRGGBB), through the display gamma's
// table when the gamma is not 1, in bands of rows on the drawing threads.
#pragma once

#include "oa/platform/job_pool.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oa::app {

/// Rows of the frame each band of convert_rgb24_xrgb converts.
inline constexpr uint32_t xrgb_band_rows = 32;

/// Packs one RGB24 row into XRGB8888 words (0xffRRGGBB).
///
/// Four pixels are read as three little-endian words to keep the loop short.
///
/// @param[out] out `width` opaque words
/// @param rgb 3 bytes per pixel
/// @param width pixels in the row
void pack_rgb24_row(uint32_t* out, const uint8_t* rgb, int width) noexcept;

/// Maps packed XRGB pixels through the display gamma's per-channel table.
///
/// @param[in,out] row `width` packed pixels; the top byte is kept
/// @param width pixels in the row
/// @param table the gamma table, applied alike to red, green and blue
void gamma_xrgb_row(uint32_t* row, int width, const std::array<uint8_t, 256>& table) noexcept;

/// Converts an RGB24 frame into XRGB8888 rows, in bands of xrgb_band_rows rows.
///
/// Every row is the same whichever thread converts it.
///
/// @param rgb the frame, `width` * 3 bytes a row, rows one after another
/// @param width pixels in a row
/// @param height rows
/// @param[out] pixels the converted rows, each at least `width` * 4 bytes, 4-byte aligned
/// @param pitch bytes from one row of `pixels` to the next
/// @param gamma the display gamma's table; null when the gamma is 1
/// @param pool threads to convert the bands on; null converts them on the calling thread
void convert_rgb24_xrgb(
    const uint8_t* rgb,
    uint32_t width,
    uint32_t height,
    uint8_t* pixels,
    std::size_t pitch,
    const std::array<uint8_t, 256>* gamma,
    platform::job_pool::Pool* pool
) noexcept;

} // namespace oa::app
