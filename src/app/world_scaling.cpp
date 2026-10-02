// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/app/world_scaling.hpp"

#include <cmath>

namespace oa::app {

WorldScaling world_scaling(
    float zoom,
    int32_t battlefield_width,
    int32_t battlefield_height,
    std::optional<float> draw_scale
) noexcept {
    if (!draw_scale || !(*draw_scale > 0.0F))
        return {zoom, battlefield_width, battlefield_height, false};
    const float scale = *draw_scale;
    if (scale == zoom || !(zoom > 0.0F))
        return {scale, battlefield_width, battlefield_height, true};
    // The map pixels the battlefield shows, at the scene's scale, with the
    // margin past its right and bottom edges, rounded up to an even count.
    const auto extent = [&](int32_t battlefield) {
        const auto pixels = static_cast<int32_t>(std::ceil(
                                static_cast<double>(battlefield) * static_cast<double>(scale) /
                                static_cast<double>(zoom)
                            )) +
                            WorldScaling::margin;
        return pixels + (pixels % 2);
    };
    return {scale, extent(battlefield_width), extent(battlefield_height), true};
}

} // namespace oa::app
