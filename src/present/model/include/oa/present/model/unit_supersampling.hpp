// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Enhanced anti-aliasing of units: each unit's model, and its shadow, drawn
// at a whole number of samples along each axis of a game pixel into samples
// of its own and reduced into the frame, each frame pixel blending the
// average of the samples the unit covers with what lies under it by that
// coverage, so that the unit's edges are smoothed. The unit's cached image
// is kept at the same number of samples and built again exactly when the
// game's image is. Terrain, sprites, the fog and the interface are drawn as
// they are. At UnitSupersampling::off a unit draws exactly as without the
// setting. Presentation only: nothing the simulation reads changes.
#pragma once

#include "oa/present/model/model_draw.hpp"
#include "oa/present/model/rgb_bridge.hpp"

#include <cstdint>
#include <optional>

namespace oa::present::model {

/// Samples along each axis of a frame pixel a unit is drawn with; the
/// value is that number.
enum class UnitSupersampling : uint8_t {
    off = 1,
    x2 = 2,
    x3 = 3,
    x4 = 4,
    x8 = 8,
    x16 = 16,
};

/// Returns the samples along each axis a level draws with.
///
/// @param level the level
/// @return 1 for off, else the level's number
[[nodiscard]] constexpr uint32_t supersampling_factor(UnitSupersampling level) noexcept {
    return static_cast<uint32_t>(level);
}

/// Returns the level that draws with a number of samples along each axis.
///
/// @param factor samples along each axis
/// @return the level; nothing for a number no level draws with
[[nodiscard]] std::optional<UnitSupersampling>
unit_supersampling_from_factor(uint32_t factor) noexcept;

/// Most samples one unit's draw may cover; a unit whose region would need
/// more at its level draws at the highest level that fits, and at
/// UnitSupersampling::off when none does.
inline constexpr uint64_t supersample_max_samples = 16 * 1024 * 1024;

/// The buffers supersampled unit draws use, kept from unit to unit and frame
/// to frame so that drawing allocates only when a unit needs more.
struct SupersampleScratch {
    SampledRegion region{}; ///< the samples a unit is drawn into
};

/// Returns the level a region of the bridge draws at under a chosen level.
///
/// @param region inclusive rectangle in the bridge's 8-bit pixels, already
///     clipped to the bridge's surface
/// @param level the level chosen
/// @return the highest level not above `level` whose samples over the region
///     stay within supersample_max_samples; off for an empty region
[[nodiscard]] UnitSupersampling
fitting_supersampling(const Rect32& region, UnitSupersampling level) noexcept;

/// Draws a unit at a level of enhanced anti-aliasing.
///
/// At UnitSupersampling::off the unit draws into the bridge as it always
/// has: the region is captured (bridge_open) and draw_linked_model draws the
/// unit there, to be written back with the bridge's other draws; any finer
/// image the unit kept is let go. At the other levels the unit is readied as
/// draw_linked_model readies it (prepare_linked_draw), everything the bridge
/// holds is written back (bridge_end), and the unit, its shadow and its
/// carried units are drawn at the level (fitting_supersampling) into samples
/// over the part of the region the draw covers (bridge_open_sampled), from
/// its finer image (ModelState::finer), which is built again whenever the
/// game's image is, and reduced straight into the bridge's frame
/// (bridge_end_sampled).
///
/// @param[in,out] renderer drawing context
/// @param[in,out] bridge the frame's model bridge
/// @param[in,out] scratch the draw's buffers
/// @param model the unit to draw
/// @param region the inclusive rectangle, in the bridge's 8-bit pixels, the
///     unit may draw in
/// @param movement_idle the movement object's idle flag, for mobile units
/// @param level the level
/// @param[in,out] bridge_holds_draws true while the bridge holds draws not
///     yet written back to the frame; the call keeps it true after drawing
///     into the bridge and makes it false after writing the bridge back
void draw_unit_supersampled(
    ModelRenderer& renderer,
    RgbBridge& bridge,
    SupersampleScratch& scratch,
    const ModelRef& model,
    const Rect32& region,
    bool movement_idle,
    UnitSupersampling level,
    bool& bridge_holds_draws
);

} // namespace oa::present::model
