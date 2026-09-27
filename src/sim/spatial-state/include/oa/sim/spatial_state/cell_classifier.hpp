// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/sim/spatial_state/spatial.hpp"

namespace oa::sim::spatial_state {

// Movement-class fields the movement-cell classifiers read. The four slope
// limits are ordered as two nested bands: exceeding a maximum blocks the cell;
// exceeding only the bad-slope threshold returns the slower class. A ground
// occupant blocks unless its object tick is at or after occupancy_before_tick;
// the search's occupancy projection treats the searching unit's own tick as
// the current one.
struct CellClassifier {
    int16_t footprint_x{};            // MoveClass.footprint_x
    int16_t footprint_z{};            // MoveClass.footprint_z
    int16_t max_water_depth{};        // MoveClass.max_water_depth
    int16_t min_water_depth{};        // MoveClass.min_water_depth
    uint8_t max_land_slope{};         // MoveClass.max_slope
    uint8_t bad_land_slope{};         // MoveClass.bad_slope
    uint8_t max_water_slope{};        // MoveClass.max_water_slope
    uint8_t bad_water_slope{};        // MoveClass.bad_water_slope
    uint32_t occupancy_before_tick{}; // the movement map's occupancy projection tick
};

/// Classifies a footprint-sized cell for a movement class.
///
/// The footprint must stay on the map, clear of blocking features and settled
/// ground occupants, within the class's water depths and within its maximum
/// slopes; the one-cell perimeter is then classified the same way.
///
/// @param movement movement class: footprint, water depths, slope bands and occupancy tick
/// @param x footprint origin column
/// @param z footprint origin row
/// @param world world holding the plots, units and sea level
/// @return 0 for blocked or off the map, 1 for the slower or insufficiently clear class, 3 only when the footprint and its perimeter are fully clear within the bad-slope limits
[[nodiscard]] uint8_t
classify_movement_cell(const CellClassifier& movement, int32_t x, int32_t z, const World& world);

/// Classifies a single plot for the whole-map movement-map build.
///
/// @param movement movement class: water depths, slope bands and occupancy tick
/// @param x plot column
/// @param z plot row
/// @param world world holding the plots, units and sea level
/// @return 0 blocked or off the map, 1 within the maximum slope, 3 within the bad-slope limit
[[nodiscard]] uint8_t
classify_movement_plot(const CellClassifier& movement, int32_t x, int32_t z, const World& world);

} // namespace oa::sim::spatial_state
