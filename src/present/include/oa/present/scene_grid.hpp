// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Where the map's pixels land on a scene drawn at a scale. The terrain fill,
// the model bridge's write-back and the battlefield's sprites all lay the
// map's pixels over the scene's this one way, counted from the map pixel at
// the scene's first pixel, so that whatever stands on a map pixel is drawn
// over the ground of that pixel at every scale and from every camera. At a
// scale of 1 each map pixel is the scene pixel of the same place.

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
/// scene's first pixel shows: pixel times scene_step_one over the step,
/// rounded toward negative infinity.
///
/// @param pixel scene pixel; below 0 before the scene's first
/// @param step the scene's step (scene_step), at least 1
/// @return the map pixel
[[nodiscard]] constexpr int64_t map_pixel_shown(int64_t pixel, uint32_t step) noexcept {
    const int64_t scaled = pixel * int64_t{scene_step_one};
    const int64_t quotient = scaled / step;
    return quotient * step > scaled ? quotient - 1 : quotient;
}

/// Returns the first scene pixel that shows a map pixel, or the first that
/// shows one after it when the scale leaves it out: the map pixel times the
/// step over scene_step_one, rounded toward positive infinity. A map pixel
/// covers the scene pixels from its own first up to the next map pixel's.
///
/// @param map_pixel map pixel, counted from the one the scene's first pixel shows
/// @param step the scene's step (scene_step), at least 1
/// @return the scene pixel
[[nodiscard]] constexpr int64_t first_scene_pixel(int64_t map_pixel, uint32_t step) noexcept {
    const int64_t scaled = map_pixel * int64_t{step};
    const int64_t quotient = scaled / int64_t{scene_step_one};
    return quotient * int64_t{scene_step_one} < scaled ? quotient + 1 : quotient;
}

} // namespace oa::present
