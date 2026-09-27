// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Textured quadrilateral scan conversion for 3DO primitives. The projectors
// walk the four corners from the topmost vertex down both sides, writing
// one span row per scanline (edge x, texture u/v, depth and shade in 16.16),
// then hand each row to a span sampler that steps the texture across it.
// The depth variants draw into a Sprite whose `aux` plane is an 8-bit depth
// buffer; a texel lands when the stored depth is <= the span depth's integer
// byte. Without a depth plane every texel lands.
//
// Texture sprites are raw (unkeyed) frames whose rows are `width` bytes. The
// power-of-two samplers for widths 16..128 address a 64 KiB window from the
// frame start, so texture storage must stay readable that far.

#include "oa/present/polygon.hpp"
#include "oa/present/surface.h"

#include <cstdint>

namespace oa::present::model {

// One scanline of a projected quad. Same 0x28-byte layout as
// present::PolygonSpanRow.
struct MeshSpanRow {
    int32_t left{};
    int32_t right{};  // exclusive
    int32_t u_left{}; // 16.16
    int32_t v_left{};
    int32_t u_right{};
    int32_t v_right{};
    int32_t depth_left{}; // 16.16
    int32_t depth_right{};
    int32_t shade_left{}; // 16.16 shade-table row
    int32_t shade_right{};
};

static_assert(sizeof(MeshSpanRow) == 0x28);

// Texture coordinate of one quad corner, in texels.
struct TexturePoint {
    int32_t u{};
    int32_t v{};
};

/// Samples one depth-tested textured span into a sprite row.
///
/// u, v and depth are stepped linearly across the span; a texel lands, with
/// its depth, where the stored depth is not above the span depth's integer
/// byte. Without a depth plane every texel lands, through the power-of-two
/// row samplers for widths 16..128.
///
/// @param y sprite row
/// @param[in,out] row span row; clipped in place to x >= 0 and x < width - 1,
///     the left interpolants moved along with the left edge
/// @param[in,out] target sprite (and depth plane) to draw into
/// @param texture raw texture frame
/// @quirk Without a depth plane a 128-wide texture is sampled twice: the
///     128-wide row pass is followed by the 64-wide one over the same pixels.
void sample_mesh_span(int32_t y, MeshSpanRow& row, Sprite& target, const Sprite& texture) noexcept;

/// Samples one depth-tested textured span, remapping each texel through the interpolated shade-table row.
///
/// The remap applies only in the depth-tested path and only when the
/// display has a shade table; otherwise this samples as sample_mesh_span does.
///
/// @param y sprite row
/// @param[in,out] row span row with 16.16 shade rows; clipped in place as
///     sample_mesh_span does
/// @param[in,out] target sprite (and depth plane) to draw into
/// @param texture raw texture frame
/// @quirk Without a depth plane a 128-wide texture is sampled twice, as in
///     sample_mesh_span.
void sample_shaded_mesh_span(
    int32_t y, MeshSpanRow& row, Sprite& target, const Sprite& texture
) noexcept;

/// Textures a quad into a depth sprite, clipped to its last row and column.
///
/// @param target sprite to draw into; null draws nothing
/// @param texture raw texture frame; null draws nothing
/// @param quad four projected corners with integer depth; null draws nothing
/// @param uv texture point of each corner; null maps the corners to the
///     texture's corners clockwise from (0, 0)
void texture_depth_quad(
    Sprite* target, const Sprite* texture, const present::DepthVertex* quad, const TexturePoint* uv
) noexcept;

/// Textures a quad into a depth sprite with per-corner shade rows interpolated along the spans.
///
/// Edges whose lower corner is not below row 0 are skipped.
///
/// @param target sprite to draw into; null draws nothing
/// @param texture raw texture frame; null draws nothing
/// @param quad four projected corners with integer depth and shade row; null draws nothing
/// @param uv texture point of each corner; null maps the corners to the
///     texture's corners clockwise from (0, 0)
void texture_shaded_depth_quad(
    Sprite* target, const Sprite* texture, const present::ShadedVertex* quad, const TexturePoint* uv
) noexcept;

/// Samples one textured span into a surface row.
///
/// @param y surface row
/// @param[in,out] row span row; clipped in place to the surface clip's left
///     edge and (exclusive) right edge, the left u/v moved along
/// @param[in,out] target surface to draw into
/// @param texture raw texture frame
void sample_texture_span(
    int32_t y, MeshSpanRow& row, Surface& target, const Sprite& texture
) noexcept;

/// Textures a quad onto a surface, clipped to its clip rectangle.
///
/// The last row and column of the clip rectangle stay unfilled.
///
/// @param target surface to draw into; null for the locked display surface
/// @param texture raw texture frame; null draws nothing
/// @param quad four projected corners; null draws nothing
/// @param uv texture point of each corner; null maps the corners to the
///     texture's corners clockwise from (0, 0)
void texture_quad(
    Surface* target,
    const Sprite* texture,
    const present::PolygonVertex* quad,
    const TexturePoint* uv
) noexcept;

} // namespace oa::present::model
