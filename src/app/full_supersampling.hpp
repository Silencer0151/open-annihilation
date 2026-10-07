// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Full tier's anti-aliasing: how a frame is drawn through the world
// target at a supersample factor and reduced to the window. The Enhanced
// anti-aliasing row chooses the factor (render_policy::supersample_factor)
// and the budget S and the texture limit lower it
// (render_policy::fit_supersample_factor); this header holds the rule that
// follows from the zoom: from zoom 1 up the stages draw at the zoom into a
// target whose texture holds the factor's pixels a window pixel, reduced by
// exact halvings (card::Operation::resolve); below zoom 1 no world target
// is drawn, since the zoomed-out target the battlefield is drawn into
// there already holds two texels a display pixel. Pure: no SDL and no
// Runtime. runtime_full.cpp draws by it and the render tiers check reads it
// back by it.
#pragma once

#include "oa/app/card.hpp"

#include <cstdint>

namespace oa::app::full_supersampling {

/// The world target's size is a multiple of this, in window pixels.
inline constexpr uint32_t target_grain = 12;

/// How a Full frame at a zoom is drawn through the world target and
/// reduced to the battlefield.
struct WorldTargetPlan {
    /// The target's supersample factor; 1 means no target: the frame
    /// draws as without anti-aliasing.
    uint32_t factor{1};
    /// The target's size in window pixels, the battlefield rounded up to
    /// target_grain; its texture is the size times the factor.
    uint32_t size_width{};
    uint32_t size_height{};
    /// Size pixels per map pixel the stages draw at into the target: the
    /// zoom, so the texture holds factor times zoom texels a map pixel.
    float draw_scale{1.0F};
    /// The part of the texture the frame fills, in texture pixels from its
    /// corner: the whole texture.
    card::Rect source_part{};
    /// Where the part lands, in window pixels from the battlefield's
    /// corner: the size, which the battlefield's scissor clips.
    card::Rect destination{};
};

/// Returns how a frame at a zoom is drawn through the world target at a
/// factor and reduced to a battlefield. At a factor of 1, and below zoom 1
/// at any factor, the plan makes no target: the battlefield's own size and
/// the zoom.
///
/// @param zoom window pixels per map pixel, above 0
/// @param factor the supersample factor in use, a power of two up to 16
/// @param battlefield_width window pixels across the battlefield, above 0
/// @param battlefield_height window pixels down it, above 0
/// @return the plan
[[nodiscard]] WorldTargetPlan plan_world_target(
    float zoom, uint32_t factor, uint32_t battlefield_width, uint32_t battlefield_height
) noexcept;

/// Returns a count rounded up to a multiple of a grain.
///
/// @param pixels the count
/// @param grain the grain, above 0
/// @return the least multiple of the grain not below the count
[[nodiscard]] constexpr uint32_t rounded_up(uint32_t pixels, uint32_t grain) noexcept {
    return (pixels + grain - 1U) / grain * grain;
}

} // namespace oa::app::full_supersampling
