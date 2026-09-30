// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The transitions between two shots, as blends of two RGB frames of the
// same tick: the outgoing shot's final view and the incoming shot's camera.
// A transition of n frames draws steps 0 to n - 1; step k is at progress
// p = (k + 1) / (n + 1), so the frame before it is all outgoing and the
// frame after it all incoming. Every blend is integer arithmetic on bytes,
// so every platform draws the same frames:
//
//   dissolve      a = floor(256 p); each byte (o (256 - a) + i a + 128) / 256
//   fade          q = floor(512 p); below 256 each byte (o (256 - q) + 128) / 256,
//                 from 256 on (i (q - 256) + 128) / 256: through black
//   wipe          columns left of floor(width p) incoming, the rest outgoing
//   checkerboard  squares of checkerboard_cell(height) pixels; while 2p < 1
//                 the even squares ((column + row) even) fill from their left
//                 edge, floor(cell 2p) columns of each, and after that the odd
//                 squares fill, floor(cell (2p - 1)) columns
//
// Divisions round toward zero; every quantity is at least zero.
#pragma once

#include "oa/formats/oascript.hpp"

#include <cstdint>
#include <span>

namespace oa::media::director {

/// Rows of squares the checkerboard transition cuts the frame into.
inline constexpr uint32_t checkerboard_rows = 9;

/// Returns the side of the checkerboard transition's squares.
///
/// @param height the frame's height in pixels
/// @return height / checkerboard_rows, at least 1
[[nodiscard]] uint32_t checkerboard_cell(uint32_t height) noexcept;

/// Draws one step of a transition.
///
/// @param kind the transition
/// @param step the step, 0 to steps - 1
/// @param steps the transition's length in frames, at least 1
/// @param outgoing the outgoing view's frame, RGB, 3 bytes a pixel, row by row
/// @param incoming the incoming view's frame, the same size
/// @param width the frames' width in pixels
/// @param height the frames' height in pixels
/// @param[out] frame the blended frame, the same size; may not overlap the others
/// @return false, with nothing written, when a span is not width * height * 3
///         bytes or step is not below steps
[[nodiscard]] bool blend_transition(
    oa::formats::oascript::TransitionKind kind,
    uint32_t step,
    uint32_t steps,
    std::span<const uint8_t> outgoing,
    std::span<const uint8_t> incoming,
    uint32_t width,
    uint32_t height,
    std::span<uint8_t> frame
) noexcept;

} // namespace oa::media::director
