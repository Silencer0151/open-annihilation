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

#include <cstdint>
#include <unordered_map>
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
    std::unordered_map<uint32_t, uint8_t> nearest; // other colours, resolved once
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

/// Returns the palette index of an RGB colour.
///
/// @param[in,out] bridge bridge whose lookup caches nearest matches
/// @param r red
/// @param g green
/// @param b blue
/// @return the index of an exactly matching entry, else of the nearest by squared distance
[[nodiscard]] uint8_t bridge_index(RgbBridge& bridge, uint8_t r, uint8_t g, uint8_t b);

} // namespace oa::present::model
