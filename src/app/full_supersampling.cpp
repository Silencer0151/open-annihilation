// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The rule by which a Full frame is drawn through the world target and
// reduced (full_supersampling.hpp).
#include "full_supersampling.hpp"

#include <cstdint>

namespace oa::app::full_supersampling {

WorldTargetPlan plan_world_target(
    float zoom, uint32_t factor, uint32_t battlefield_width, uint32_t battlefield_height
) noexcept {
    WorldTargetPlan plan;
    plan.factor = factor;
    // Below zoom 1 the zoomed-out target supersamples the battlefield.
    if (factor <= 1 || battlefield_width == 0 || battlefield_height == 0 || !(zoom >= 1.0F)) {
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
    // The stages draw at the zoom; the texture holds the factor's pixels a
    // window pixel and the halvings reduce it to the size.
    plan.size_width = rounded_up(battlefield_width, target_grain);
    plan.size_height = rounded_up(battlefield_height, target_grain);
    plan.draw_scale = zoom;
    plan.source_part = {
        0,
        0,
        static_cast<int32_t>(plan.size_width * factor),
        static_cast<int32_t>(plan.size_height * factor)
    };
    plan.destination = {
        0, 0, static_cast<int32_t>(plan.size_width), static_cast<int32_t>(plan.size_height)
    };
    return plan;
}

} // namespace oa::app::full_supersampling
