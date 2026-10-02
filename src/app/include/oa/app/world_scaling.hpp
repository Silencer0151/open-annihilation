// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// How a match frame draws the battlefield: the draw scale, the scene pixels
// per map pixel, apart from the zoom, the screen pixels per map pixel, and
// the size of the scene the terrain, the draws and the fog go into.
#pragma once

#include <cstdint>
#include <optional>

namespace oa::app {

/// How a frame draws the battlefield: its draw scale and the scene it draws into.
///
/// The scene starts at the camera's map pixel, at its own top-left corner,
/// as the battlefield does.
struct WorldScaling {
    /// Scene columns and rows past the battlefield's right and bottom edges,
    /// at least, when the scene is drawn at another scale than the zoom.
    static constexpr int32_t margin = 2;

    /// Scene pixels per map pixel; the zoom when the scene is the battlefield.
    float draw_scale{1.0F};
    int32_t scene_width{};  ///< scene columns
    int32_t scene_height{}; ///< scene rows
    /// The scene is drawn apart from the world layer and resampled nearest
    /// into it at the zoom; otherwise it is the world layer.
    bool apart{};
};

/// Returns how a frame draws the battlefield at a zoom.
///
/// Without a draw scale the frame draws at the zoom into the world layer
/// itself, and the scene is the battlefield. A draw scale draws the scene
/// apart from the world layer. At the zoom that scene is the battlefield's
/// size; at another scale it covers the battlefield's map pixels at the draw
/// scale with WorldScaling::margin more columns and rows, rounded up to an
/// even number of each: even(ceil(battlefield * draw_scale / zoom) + margin)
/// in each axis. A draw scale that is not above 0 counts as none, and a zoom
/// that is not above 0 gives the battlefield's size.
///
/// @param zoom screen pixels per map pixel
/// @param battlefield_width battlefield columns in screen pixels
/// @param battlefield_height battlefield rows in screen pixels
/// @param draw_scale scene pixels per map pixel to draw the scene at apart from the world layer;
///        none draws at the zoom
/// @return the draw scale and the scene
[[nodiscard]] WorldScaling world_scaling(
    float zoom,
    int32_t battlefield_width,
    int32_t battlefield_height,
    std::optional<float> draw_scale
) noexcept;

} // namespace oa::app
