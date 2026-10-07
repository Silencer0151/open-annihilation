// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Where the map's pixels land on a scene drawn at a scale. The terrain fill,
// the model bridge's write-back and the battlefield's sprites all lay the
// map's pixels over the scene's this one way, counted from the map pixel at
// the scene's first pixel and how far into it that pixel starts (its
// phase), so that whatever stands on a map pixel is drawn over the ground
// of that pixel at every scale and from every camera. A scene whose first
// pixel is one of the scene pixels laid from the map's corner
// (scene_origin) shows every map pixel on the same scene pixels, less a
// whole number of them, wherever it starts. At a scale of 1 each map pixel
// is the scene pixel of the same place.

#include <cmath>
#include <cstdint>

namespace oa::present {

/// One in the 16.16 fixed point of a scene's step.
inline constexpr uint32_t scene_step_one = 65536;

/// Returns the 16.16 step of a scale: scene pixels per map pixel times
/// scene_step_one, rounded to the nearest and at least 1.
///
/// @param scale scene pixels per map pixel; 0 or less, or no number, is 1
/// @return the step
[[nodiscard]] inline uint32_t scene_step(float scale) noexcept {
    if (!(scale > 0.0F))
        return scene_step_one;
    const auto step = std::lround(static_cast<double>(scale) * scene_step_one);
    return step < 1 ? 1U : static_cast<uint32_t>(step);
}

/// Returns the map pixel a scene pixel shows, counted from the one the
/// scene's first pixel shows: pixel times scene_step_one, plus the phase,
/// over the step, rounded toward negative infinity.
///
/// @param pixel scene pixel; below 0 before the scene's first
/// @param step the scene's step (scene_step), at least 1
/// @param phase how far into its map pixel the scene's first pixel starts,
///     in 16.16 parts of the step, below the step; 0 at the map pixel's start
/// @return the map pixel
[[nodiscard]] constexpr int64_t
map_pixel_shown(int64_t pixel, uint32_t step, uint32_t phase = 0) noexcept {
    const int64_t scaled = pixel * int64_t{scene_step_one} + int64_t{phase};
    const int64_t quotient = scaled / step;
    return quotient * step > scaled ? quotient - 1 : quotient;
}

/// Returns the first scene pixel that shows a map pixel, or the first that
/// shows one after it when the scale leaves it out: the map pixel times the
/// step, less the phase, over scene_step_one, rounded toward positive
/// infinity. A map pixel covers the scene pixels from its own first up to
/// the next map pixel's.
///
/// @param map_pixel map pixel, counted from the one the scene's first pixel shows
/// @param step the scene's step (scene_step), at least 1
/// @param phase how far into its map pixel the scene's first pixel starts,
///     in 16.16 parts of the step, below the step (map_pixel_shown)
/// @return the scene pixel
[[nodiscard]] constexpr int64_t
first_scene_pixel(int64_t map_pixel, uint32_t step, uint32_t phase = 0) noexcept {
    const int64_t scaled = map_pixel * int64_t{step} - int64_t{phase};
    const int64_t quotient = scaled / int64_t{scene_step_one};
    return quotient * int64_t{scene_step_one} < scaled ? quotient + 1 : quotient;
}

/// Returns the scene pixel, of those laid from the map's corner at a step,
/// that a point of the map lies in: the point times the step, rounded
/// toward negative infinity. At a step of scene_step_one it is the point's
/// whole map pixel; zoomed in, a point moving by less than a map pixel moves
/// by the scene pixels it crosses.
///
/// @param point 16.16 map pixels from the map's corner, which may be below 0
/// @param step the scene's step (scene_step), at least 1
/// @return the scene pixel
[[nodiscard]] constexpr int64_t scene_pixel_of(int32_t point, uint32_t step) noexcept {
    const int64_t scaled = int64_t{point} * int64_t{step};
    const int64_t whole = int64_t{scene_step_one} * int64_t{scene_step_one};
    const int64_t quotient = scaled / whole;
    return quotient * whole > scaled ? quotient - 1 : quotient;
}

/// Where a scene starts on the map: the map pixel its first pixel shows and
/// how far into that map pixel the pixel starts.
struct SceneOrigin {
    int64_t map_pixel{}; ///< the map pixel the scene's first pixel shows
    uint32_t phase{};    ///< 16.16 parts of the step into it, below the step
};

/// Returns where a scene starts whose first pixel is the scene pixel
/// nearest a place on the map, of the scene pixels laid from the map's
/// corner at the step: scene pixel n of those starts at n times
/// scene_step_one over the step, in map pixels. Scenes started so show
/// every map pixel on the same scene pixels, less a whole number of them,
/// wherever they start, so the ground of a view so placed moves by whole
/// scene pixels and is never sampled afresh. At a step of scene_step_one
/// the scene starts on the map pixel nearest the place, with no phase.
///
/// @param place map pixels from the map's corner, which may be below 0;
///     halves round away from zero
/// @param step the scene's step (scene_step), at least 1
/// @return the map pixel and the phase
[[nodiscard]] inline SceneOrigin scene_origin(double place, uint32_t step) noexcept {
    const auto pixel = static_cast<int64_t>(
        std::llround(place * static_cast<double>(step) / static_cast<double>(scene_step_one))
    );
    const int64_t map_pixel = map_pixel_shown(pixel, step);
    return {
        map_pixel,
        static_cast<uint32_t>(pixel * int64_t{scene_step_one} - map_pixel * int64_t{step})
    };
}

} // namespace oa::present
