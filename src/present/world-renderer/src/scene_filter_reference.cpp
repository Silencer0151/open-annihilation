// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The reference the area pass is held to: the exact average under a
// footprint, from its geometry, in double precision. Tests use it; the game
// does not.
#include "oa/present/world_renderer/scene_filter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace oa::present::world_renderer {

double area_sample_reference(
    const RgbSource& scene, double scale, uint32_t x, uint32_t y, uint32_t channel
) noexcept {
    if (!(scale > 0.0) || channel >= area_pixel_bytes || scene.rgb == nullptr)
        return 0.0;
    const double left = static_cast<double>(x) / scale;
    const double right = static_cast<double>(x + 1U) / scale;
    const double top = static_cast<double>(y) / scale;
    const double bottom = static_cast<double>(y + 1U) / scale;
    const double last_column = std::min(std::ceil(right), static_cast<double>(scene.width));
    const double last_row = std::min(std::ceil(bottom), static_cast<double>(scene.height));
    double sum = 0.0;
    for (double row = std::floor(top); row < last_row; row += 1.0) {
        const double covered_rows = std::min(bottom, row + 1.0) - std::max(top, row);
        const uint8_t* line =
            scene.rgb + static_cast<std::size_t>(row) * scene.stride_pixels * area_pixel_bytes;
        for (double column = std::floor(left); column < last_column; column += 1.0) {
            const double covered_columns = std::min(right, column + 1.0) - std::max(left, column);
            const uint8_t level =
                line[static_cast<std::size_t>(column) * area_pixel_bytes + channel];
            sum += covered_columns * covered_rows * static_cast<double>(level);
        }
    }
    return sum / ((right - left) * (bottom - top));
}

} // namespace oa::present::world_renderer
