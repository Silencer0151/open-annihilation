// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// 8-bit drawing over an RGB frame. The model routines draw into
// palette-indexed surfaces and blend through palette tables, so they need the
// indices of what is already on screen. A bridge lays an 8-bit surface over a
// rectangle of an RGB frame (optionally scaled): regions are captured on
// demand by mapping their RGB pixels back to palette indices, drawing is
// clipped to the captured region, and at the end every 8-bit pixel that
// changed is written back through the palette. Unchanged pixels keep their
// RGB values, so colours outside the palette survive untouched.
//
// This is presentation support for the RGB match frame; it changes no 3.1c
// behaviour.

#include "oa/present/surface.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace oa::present::model {

inline constexpr int32_t bridge_tile_side = 32;

// A packed 24-bit RGB frame.
struct RgbFrame {
    uint8_t* rgb{};
    int32_t width{};
    int32_t height{};
    int32_t stride{}; // bytes per row
};

struct RgbBridge {
    Surface surface{}; // the 8-bit view the routines draw into
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> baseline; // indices as captured
    std::vector<uint8_t> captured; // per tile
    int32_t tiles_x{};
    int32_t tiles_y{};
    RgbFrame frame{};
    Rect32 area{};     // RGB rectangle covered, inclusive
    float scale{1.0F}; // RGB pixels per 8-bit pixel
    Palette palette{};
    std::vector<uint32_t> exact_keys; // open-addressed RGB -> index for palette colours
    std::vector<uint8_t> exact_values;
    /// Other colours and the index of their nearest entry, one slot per
    /// colour's hash: a colour remembered there is not searched for again
    /// until another colour takes its slot.
    std::vector<uint32_t> nearest_keys;
    std::vector<uint8_t> nearest_values; ///< by slot, as nearest_keys
};

/// Covers a rectangle of an RGB frame with an 8-bit surface of the rectangle's size divided by the scale.
///
/// Nothing is captured yet. The colour lookup is rebuilt when the palette changes.
///
/// @param[in,out] bridge bridge to set up; its buffers are reused
/// @param frame RGB frame; must outlive the bridge's use
/// @param area inclusive RGB rectangle to cover
/// @param scale RGB pixels per 8-bit pixel; non-positive counts as 1
/// @param palette palette the indices refer to
void bridge_begin(
    RgbBridge& bridge,
    const RgbFrame& frame,
    const Rect32& area,
    float scale,
    const Palette& palette
);

/// Captures the tiles overlapping a region and clips the surface to it.
///
/// Draws can then only change captured pixels; a region outside the surface
/// leaves an empty clip that rejects every draw.
///
/// @param[in,out] bridge bridge set up by bridge_begin
/// @param region inclusive rectangle in 8-bit pixels
void bridge_open(RgbBridge& bridge, const Rect32& region);

/// Writes every changed pixel of the captured tiles back to the frame through the palette.
///
/// Every tile counts as uncaptured afterwards.
///
/// @param[in,out] bridge bridge whose captured tiles are committed
void bridge_end(RgbBridge& bridge);

/// Slots of the colours outside the palette a bridge remembers the nearest
/// entry of; a power of two.
inline constexpr std::size_t bridge_nearest_slots = std::size_t{1} << 16;

/// An 8-bit surface laid over a region of a bridge's surface at a whole
/// number of samples along each axis of a bridge pixel, for drawing finer
/// than the bridge (enhanced anti-aliasing). Writing it back blends each
/// frame pixel with the samples drawn over its bridge pixel.
struct SampledRegion {
    Surface surface{};             ///< the samples the routines draw into
    std::vector<uint8_t> samples;  ///< the surface's pixels
    std::vector<uint8_t> baseline; ///< each bridge pixel's index as captured, row by row
    /// The first sample row of each bridge row as captured, so that a sample
    /// row nothing was drawn into is passed over at once.
    std::vector<uint8_t> captured_rows;
    Rect32 region{0, 0, -1, -1}; ///< bridge pixels covered, inclusive; empty when x1 > x2
    uint32_t factor{1};          ///< samples along each axis of a bridge pixel
};

/// Covers a region of a bridge's surface with samples.
///
/// The region is clipped to the bridge's surface. Each bridge pixel is
/// captured as bridge_open captures it, and every sample of it starts as
/// that index, so that blended draws blend with what lies under them. The
/// surface's clip is the whole region. The bridge's own tiles are left as
/// they are.
///
/// @param bridge bridge set up by bridge_begin; its colour lookup learns
///     the colours captured
/// @param[out] sampled region to set up; its buffers are reused
/// @param region inclusive rectangle in the bridge's 8-bit pixels
/// @param factor samples along each axis of a bridge pixel; 0 counts as 1
void bridge_open_sampled(
    RgbBridge& bridge, SampledRegion& sampled, const Rect32& region, uint32_t factor
);

/// Writes a sampled region back to the frame.
///
/// A sample whose index differs from its bridge pixel's captured index was
/// drawn. Each frame pixel of a bridge pixel with drawn samples becomes the
/// average of its samples, a drawn one counting as its palette colour and
/// one not drawn as the frame pixel's own colour, rounded to nearest. A
/// bridge pixel drawn all over in one index becomes that palette colour, as
/// bridge_end writes it, and one with no sample drawn is left as it is.
/// Frame pixels map to bridge pixels as bridge_end maps them.
///
/// @param bridge bridge whose frame receives the samples
/// @param sampled region set up by bridge_open_sampled and drawn into
void bridge_end_sampled(const RgbBridge& bridge, const SampledRegion& sampled);

/// Returns the palette index of an RGB colour.
///
/// @param[in,out] bridge bridge whose lookup caches nearest matches
/// @param r red
/// @param g green
/// @param b blue
/// @return the index of an exactly matching entry, else of the nearest by squared distance
[[nodiscard]] uint8_t bridge_index(RgbBridge& bridge, uint8_t r, uint8_t g, uint8_t b);

} // namespace oa::present::model
