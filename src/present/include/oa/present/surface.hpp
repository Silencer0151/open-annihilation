// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// C++ helpers around the canonical 8-bit records in surface.h.

#include "oa/present/surface.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace oa::present {

/// Describes caller-owned pixels as a memory surface with a full clip rectangle.
///
/// The origin is cleared, the two reserved words get their initial values
/// and the memory flag is set (OA_SURFACE_FLAG_CLEARED_ON_INIT and
/// OA_SURFACE_FLAG_BANDED cleared, other flags kept).
///
/// @param[in,out] surface surface to initialise
/// @param width width in pixels
/// @param height height in rows
/// @param pitch bytes per row
/// @param pixels first row; owned by the caller
void init_surface(
    Surface& surface, int32_t width, int32_t height, int32_t pitch, uint8_t* pixels
) noexcept;

/// Resets the clip rectangle to the whole surface.
///
/// @param[in,out] surface surface whose clip becomes (0, 0)-(width - 1, height - 1)
void reset_clip(Surface& surface) noexcept;

/// Returns the surface clip rectangle.
///
/// @param surface surface to read
/// @return the inclusive clip rectangle
[[nodiscard]] Rect32 surface_clip(const Surface& surface) noexcept;

/// Replaces the surface clip rectangle.
///
/// @param[in,out] surface surface to update
/// @param clip inclusive clip rectangle; not checked against the surface
void set_surface_clip(Surface& surface, const Rect32& clip) noexcept;

/// The rows of a surface its draws may change: from `first` up to but not
/// including `end`.
struct SurfaceRows {
    int32_t first{}; ///< the first row
    int32_t end{};   ///< the row after the last
};

/// Tells whether a surface's draws are limited to a band of rows (OA_SURFACE_FLAG_BANDED).
///
/// The draws that do not honour a band check this in debug builds and must
/// not be given a banded surface (see surface.h).
///
/// @param surface surface to read
/// @return true with OA_SURFACE_FLAG_BANDED set
[[nodiscard]] inline bool surface_banded(const Surface& surface) noexcept {
    return (surface.flags & OA_SURFACE_FLAG_BANDED) != 0;
}

/// Returns the rows a surface's draws may change.
///
/// @param surface surface to read
/// @return the band's rows with OA_SURFACE_FLAG_BANDED, else every row a
///     32-bit row number names
[[nodiscard]] SurfaceRows surface_band(const Surface& surface) noexcept;

/// Limits a surface's draws to a band of rows (OA_SURFACE_FLAG_BANDED).
///
/// The draws that honour a band (listed in surface.h) still work out what
/// they draw from the clip alone: a draw over the band's edge changes the
/// band's rows as a draw of the whole surface would, and no other row.
///
/// @param[in,out] surface surface to limit
/// @param first_row the first row draws may change
/// @param end_row the row after the last one draws may change
void set_surface_band(Surface& surface, int32_t first_row, int32_t end_row) noexcept;

/// Lets a surface's draws change every row again.
///
/// @param[in,out] surface surface whose OA_SURFACE_FLAG_BANDED is cleared
void clear_surface_band(Surface& surface) noexcept;

/// Views a raw sprite's pixels as a memory surface whose origin is the sprite hotspot.
///
/// @param[in,out] surface surface to initialise; pitch is the sprite width
/// @param sprite raw sprite whose data holds its pixels
void surface_from_sprite(Surface& surface, const Sprite& sprite) noexcept;

// Owning pixel storage for a surface; pitch == width.
struct SurfaceBuffer {
    Surface surface{};
    std::vector<uint8_t> pixels;
};

/// Allocates a zero-filled memory surface with a full clip rectangle.
///
/// @param width width in pixels; negative sizes allocate nothing
/// @param height height in rows
/// @return the surface and its pixels; pitch equals width
[[nodiscard]] SurfaceBuffer create_surface(int32_t width, int32_t height);

// Owning storage for a raw sprite: its header and pixels together.
struct SpriteBuffer {
    Sprite sprite{};
    std::vector<uint8_t> pixels;
};

/// Allocates a zero-filled raw sprite with zeroed header fields.
///
/// @param width width in pixels
/// @param height height in rows
/// @return the sprite, whose data points at its pixels
[[nodiscard]] SpriteBuffer create_sprite(uint16_t width, uint16_t height);

/// Builds a palette from four-byte entries (the PaletteBytes layout).
///
/// @param bytes r, g, b, flags per entry; up to 256 entries are read and the
///     rest of the palette is zero
/// @return the palette
[[nodiscard]] Palette palette_from_bytes(std::span<const uint8_t> bytes) noexcept;

/// Expands 8-bit pixels to packed RGB through a palette.
///
/// @param surface surface to expand; nothing happens when it has no pixels
/// @param palette palette the indices select from
/// @param[out] rgb height rows of width * 3 bytes; null does nothing
/// @param rgb_stride bytes between output rows
void expand_to_rgb(
    const Surface& surface, const Palette& palette, uint8_t* rgb, std::size_t rgb_stride
) noexcept;

/// Returns a packed RGB copy of a surface.
///
/// @param surface surface to expand
/// @param palette palette the indices select from
/// @return width * height * 3 bytes
[[nodiscard]] std::vector<uint8_t> to_rgb(const Surface& surface, const Palette& palette);

} // namespace oa::present
