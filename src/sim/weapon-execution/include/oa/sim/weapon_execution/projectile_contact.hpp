// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/core/feature_def.h"
#include "oa/core/map_plot.h"
#include "oa/core/projectile.h"
#include "oa/core/world.h"

#include <cstdint>

// Per-tick map-cell contact of a moving projectile with the shot it
// intercepts, the plot's occupants, the plot's feature, the ground and the
// water surface.
namespace oa::sim::weapon_execution {

enum class ContactKind : uint8_t {
    none,    // keeps flying
    off_map, // no plot under the shot: retire it (retire_projectile)
    unit,    // detonate on `unit`
    feature, // entered a feature cell below the feature's top: detonate with no unit
    ground,  // below the plot's low height: detonate with no unit
    bounce,  // below ground with groundbounce: vertical velocity reflected at a quarter
    water,   // below sea level: detonate with no unit
};

struct ProjectileContact {
    bool intercepted{}; // within area_of_effect of intercept_target: detonate with no unit first
    ContactKind kind{};
    oa_ref32 unit{}; // Unit for ContactKind::unit
};

/// Returns the feature definition standing on a plot.
///
/// A MapPlot.feature below OA_PLOT_FEATURE_RESERVED within the table names the feature;
/// a 0xfffe continuation names the origin cell's (rows back in the low byte of the
/// plot's feature_record, columns back in its high byte).
///
/// @param world plots and feature definitions
/// @param plot plot to look at
/// @return the feature, or null; a walk back before the first plot or a word
///         past the table gives null
[[nodiscard]]
const FeatureDef* plot_feature_def(const World& world, const MapPlot& plot) noexcept;

// The plot fields the contact test reads.
struct ContactPlot {
    uint16_t ground_unit{}; // MapPlot.ground_unit, a unit slot
    uint16_t air_unit{};    // MapPlot.air_unit, a unit slot
    uint8_t high_height{};  // MapPlot.high_height
    uint8_t low_height{};   // MapPlot.low_height
    bool has_feature{};     // plot_feature_def found a feature
    uint8_t feature_height{};
};

/// Returns the contact fields of a canonical plot, its feature through plot_feature_def.
///
/// @param world plots and feature definitions
/// @param plot plot under the shot
/// @return occupants, heights and feature height
[[nodiscard]] ContactPlot contact_plot(const World& world, const MapPlot& plot) noexcept;

/// Runs one contact test after a projectile moved.
///
/// The intercept test runs first and does not stop the map tests. A ground occupant is
/// hit below its model top and an air occupant between its bounds, never by its own
/// player's shot. A unitsonly weapon skips feature, ground and water. Entering a new
/// feature cell below the feature height plus the low plot height detonates; the same
/// cell again falls through to the ground test. Below the low height a groundbounce
/// shot reflects, anything else detonates. Otherwise a non-water weapon below sea level
/// detonates unless `water_surface_ignored` holds.
///
/// @param[in,out] world units, projectiles and weapons
/// @param[in,out] projectile moved projectile; its feature cell and bounce are updated
/// @param plot plot fields under the shot, or null off the map
/// @param water_surface_ignored whether a shot below sea level flies on instead of
///        detonating, which 3.1c takes from a game option not otherwise identified;
///        the match passes false
/// @return whether it intercepted and what it hit
[[nodiscard]]
ProjectileContact projectile_plot_contact(
    World& world, Projectile& projectile, const ContactPlot* plot, bool water_surface_ignored
) noexcept;

/// Runs projectile_plot_contact over the canonical plot under the shot.
///
/// @param[in,out] world plots, units, projectiles and weapons
/// @param[in,out] projectile moved projectile
/// @param water_surface_ignored whether a shot below sea level flies on instead of
///        detonating, which 3.1c takes from a game option not otherwise identified
/// @return whether it intercepted and what it hit
[[nodiscard]]
ProjectileContact
projectile_map_contact(World& world, Projectile& projectile, bool water_surface_ignored) noexcept;

} // namespace oa::sim::weapon_execution
