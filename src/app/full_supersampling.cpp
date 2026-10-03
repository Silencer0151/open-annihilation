// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The rule by which a Full frame is drawn through the world target and
// reduced (full_supersampling.hpp).
#include "full_supersampling.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace oa::app::full_supersampling {

float texel_scale_of(float zoom) noexcept {
    float scale = 1.0F;
    while (zoom < 0.5F * scale && scale > least_texel_scale)
        scale *= 0.5F;
    return scale;
}

WorldTargetPlan plan_world_target(
    float zoom, uint32_t factor, uint32_t battlefield_width, uint32_t battlefield_height
) noexcept {
    WorldTargetPlan plan;
    plan.factor = factor;
    if (factor <= 1 || battlefield_width == 0 || battlefield_height == 0 || !(zoom > 0.0F)) {
        plan.factor = 1;
        plan.size_width = battlefield_width;
        plan.size_height = battlefield_height;
        plan.draw_scale = zoom;
        plan.source_part = {
            0, 0, static_cast<int32_t>(battlefield_width), static_cast<int32_t>(battlefield_height)
        };
        plan.destination = plan.source_part;
        return plan;
    }
    plan.size_width = rounded_up(battlefield_width, target_grain);
    plan.size_height = rounded_up(battlefield_height, target_grain);
    const uint32_t texture_width = plan.size_width * factor;
    const uint32_t texture_height = plan.size_height * factor;
    if (zoom >= 1.0F) {
        // The stages draw at the zoom; the texture holds the factor's
        // pixels a window pixel and the halvings reduce it to the size.
        plan.draw_scale = zoom;
        plan.source_part = {
            0, 0, static_cast<int32_t>(texture_width), static_cast<int32_t>(texture_height)
        };
        plan.destination = {
            0, 0, static_cast<int32_t>(plan.size_width), static_cast<int32_t>(plan.size_height)
        };
        return plan;
    }
    // Zoomed out: the stages draw at the texel scale, 1, one half or one
    // quarter texel a map pixel, over the part the battlefield shows at the
    // zoom, and the two-level blend reduces the part by the zoom over the
    // texel scale, which the texel scale keeps within one half to 1. The
    // part is rounded up to part_grain, so the destination is rounded up
    // with it, never short of the battlefield.
    plan.two_level = true;
    plan.texel_scale = texel_scale_of(zoom);
    plan.draw_scale = plan.texel_scale / static_cast<float>(factor);
    const double texels_a_window_pixel =
        static_cast<double>(plan.texel_scale) / static_cast<double>(zoom);
    const auto part_of = [&](uint32_t pixels, uint32_t texture_extent) {
        const double needed = std::ceil(static_cast<double>(pixels) * texels_a_window_pixel);
        const auto whole = static_cast<uint32_t>(std::min(needed, static_cast<double>(UINT32_MAX)));
        return std::min(rounded_up(whole, part_grain), texture_extent);
    };
    const uint32_t part_width = part_of(battlefield_width, texture_width);
    const uint32_t part_height = part_of(battlefield_height, texture_height);
    plan.source_part = {0, 0, static_cast<int32_t>(part_width), static_cast<int32_t>(part_height)};
    const auto landed = [&](uint32_t part) {
        return static_cast<int32_t>(std::ceil(static_cast<double>(part) / texels_a_window_pixel));
    };
    plan.destination = {0, 0, landed(part_width), landed(part_height)};
    return plan;
}

} // namespace oa::app::full_supersampling
