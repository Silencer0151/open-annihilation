// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/sim/unit_movement/movement.hpp"
#include "oa/formats/tnt.hpp"
#include "oa/formats/objects3d.hpp"
#include <optional>

namespace oa::sim::unit_movement {
// Terrain samples use the integer high words of signed 16.16 X/Z, 16-world-unit
// attribute spacing, and truncation after each interpolation.
// The referenced map must outlive the view and keep its dimensions/attributes unchanged.
class Terrain {
  public:

    /// Makes a terrain view of a TNT height grid.
    ///
    /// Throws std::invalid_argument for a grid smaller than 2x2, too large, inconsistent or with sea level past 255.
    ///
    /// @param map parsed TNT; must outlive the view
    explicit Terrain(const oa::formats::tnt::Map& map);
    /// Returns the interpolated ground height at a 16.16 X/Z.
    ///
    /// The four attribute heights around the point are interpolated along X, then
    /// Z, each signed division truncating toward zero.
    ///
    /// @param x signed 16.16 world X
    /// @param z signed 16.16 world Z
    /// @return the height in whole world units, -1 outside the map interior
    int32_t height(Fixed x, Fixed z) const noexcept;
    /// Returns the map's sea level.
    ///
    /// @return sea level in whole world units
    uint8_t sea_level() const noexcept;

  private:

    const oa::formats::tnt::Map* map_;
};

/// Returns the integer altitude of the surface at a 16.16 X/Z.
///
/// @param terrain terrain view
/// @param x signed 16.16 world X
/// @param z signed 16.16 world Z
/// @return sea level unless the ground height is strictly greater, so the out-of-map -1 stays at sea level
/// @quirk The greater branch samples the height again and returns that second result.
uint32_t surface_height(const Terrain& terrain, Fixed x, Fixed z) noexcept;

using GroundQuad =
    std::array<std::array<Fixed, 2>, 4>; // runtime X/Z in the primitive's vertex order
/// Returns the first four vertices of the root object's selection primitive.
///
/// X and Z are negated as the 3DO loader stores them; root offsets are not
/// added.
///
/// Throws std::invalid_argument for a selection index, vertex count or vertex index out of range.
///
/// @param model the unit's model
/// @return the quad, or nullopt when the root has no selection primitive
std::optional<GroundQuad> ground_quad(const oa::formats::objects3d::Model& model);

struct GroundPose {
    std::array<Fixed, 3> position{};
    uint16_t heading{};    // Unit.heading
    int16_t pitch{};       // Unit.pitch
    int16_t roll{};        // Unit.bank
    uint16_t bob_phase{};  // Unit.bob_phase
    uint32_t flags{};      // Unit.flags
    uint32_t type_flags{}; // UnitDef.flags
    Fixed maximum_speed{}; // UnitDef.max_velocity
};

/// Returns the scaled uptime the water bob reads.
///
/// @param uptime_milliseconds platform uptime in milliseconds
/// @param clock_scale the game clock's scale, the multiplier unit-script SLEEP uses too
/// @return the 32-bit wrapping product divided by 1000, unsigned
uint32_t scaled_bob_tick(uint32_t uptime_milliseconds, uint32_t clock_scale) noexcept;

struct GroundClock {
    uint32_t simulation_tick{};
    // Per-corner scaled uptime readings (scaled_bob_tick). They may differ
    // within one fitting pass, so the caller takes four platform readings.
    std::array<uint32_t, 4> bob_ticks{};
};

/// Fits a unit to the ground under its selection quad.
///
/// Samples the four rotated quad corners; a live hovercraft not marked to die
/// takes at least sea level plus a speed-damped water bob. Height is the mean of the
/// front and back pairs, written into the high word so the previous low 16
/// fractional bits stay; pitch and roll come from the front/back and left/right
/// height differences. Rotation and atan use host libm with explicit
/// nearest-even integer conversion. Throws std::domain_error in the bob for a
/// zero half maximum speed.
///
/// @param terrain terrain view
/// @param quad the model's selection quad, or nullopt
/// @param[in,out] pose position, angles, bob phase and flags of the unit
/// @param movement movement object: speed and last motion tick for the bob
/// @param clock simulation tick and the four per-vertex bob readings
/// @return false, leaving the pose unchanged, for a missing quad or an off-map sample
bool fit_ground(
    const Terrain& terrain,
    const std::optional<GroundQuad>& quad,
    GroundPose& pose,
    const Movement& movement,
    const GroundClock& clock
);
} // namespace oa::sim::unit_movement
