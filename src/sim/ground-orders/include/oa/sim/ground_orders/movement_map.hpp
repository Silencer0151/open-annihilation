// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/core/unit.h"
#include <cstdint>
#include <span>
#include <vector>

namespace oa::sim::ground_orders {

// Unit occupancy rectangle used by movement-map refreshes. Both pairs are packed signed
// little-endian halves: x in the low half, z in the high half. A refresh covers
// every anchor whose class footprint or its one-cell rim touches the rectangle.
struct OccupancyRectangle {
    uint32_t origin{}; // Unit.cell_x, Unit.cell_z
    uint32_t extent{}; // Unit.footprint_x, Unit.footprint_z
};

/// Returns the occupancy rectangle of a unit's current cell and footprint.
///
/// @param unit the unit
/// @return the packed cell (Unit.cell_x, Unit.cell_z) and footprint (Unit.footprint_x,
///         Unit.footprint_z)
[[nodiscard]] OccupancyRectangle occupancy_rectangle(const oa::Unit& unit) noexcept;

// One entry of the live unit list prepare_search walks.
struct OccupancyChange {
    OccupancyRectangle rectangle{};
    uint32_t changed_tick{}; // GroundRuntime::occupancy_changed_tick
    bool live{};             // Unit.flags has OA_UNIT_FLAG_LIVE
};

class MovementMapSampler {
  public:

    virtual ~MovementMapSampler() = default;
    /// Classifies a map cell for the map's movement class.
    ///
    /// The platform map/occupancy owner implements the cell classifier.
    ///
    /// @param x cell column
    /// @param z cell row
    /// @return the two-bit class: 0 blocked, 1 slow or not clear, 3 clear
    virtual uint8_t classify_cell(int32_t x, int32_t z) = 0;

    /// Classifies a single plot for the whole-map build.
    ///
    /// Samplers that already hold one class per cell keep the default, which
    /// forwards to classify_cell.
    ///
    /// @param x plot column
    /// @param z plot row
    /// @return the two-bit class
    virtual uint8_t classify_plot(int32_t x, int32_t z) { return classify_cell(x, z); }
};

// Two-bit movement-class projection consumed by the path search classifier.
class MovementMap {
  public:

    /// Sizes the packed grid for a movement class.
    ///
    /// The grid holds ((height + 15) / 16) * width words, none when a side is 0.
    /// The footprint and sampler come from the movement class the grid belongs
    /// to.
    ///
    /// @param width map width in cells
    /// @param height map height in cells
    /// @param footprint_x class footprint width in cells
    /// @param footprint_z class footprint depth in cells
    /// @param sampler cell classifier; must outlive the map
    MovementMap(
        uint32_t width,
        uint32_t height,
        int16_t footprint_x,
        int16_t footprint_z,
        MovementMapSampler& sampler
    );

    /// Returns the map width in cells.
    [[nodiscard]] uint32_t width() const noexcept { return width_; }

    /// Returns the map height in cells.
    [[nodiscard]] uint32_t height() const noexcept { return height_; }

    /// Returns the tick the occupancy projection stands at.
    [[nodiscard]] uint32_t projection_tick() const noexcept { return projection_tick_; }

    /// Returns the class footprint width in cells.
    [[nodiscard]] int16_t footprint_x() const noexcept { return footprint_x_; }

    /// Returns the class footprint depth in cells.
    [[nodiscard]] int16_t footprint_z() const noexcept { return footprint_z_; }

    /// Returns a cell's two-bit class.
    ///
    /// @param x cell column
    /// @param z cell row
    /// @return the class, 0 off the map
    [[nodiscard]] uint8_t cell(uint32_t x, uint32_t z) const noexcept;
    /// Reclassifies every anchor cell whose footprint or one-cell rim touches a rectangle.
    ///
    /// @param rectangle packed origin and extent to refresh around
    void refresh(OccupancyRectangle rectangle);
    /// Brings the occupancy projection up to date before a search.
    ///
    /// Moves the projection to 30 ticks ago and reclassifies the rectangles of live
    /// units whose last move fell between the old and new projection. The searching
    /// unit's tick reads as current_tick meanwhile, so its own footprint is never a
    /// wall; its rectangle is reclassified when the old projection held it as one.
    ///
    /// @param active the searching unit's rectangle
    /// @param[in,out] active_changed_tick the searching unit's object tick; current_tick during the refresh, then restored
    /// @param units the live unit list
    /// @param current_tick current game tick
    void prepare_search(
        OccupancyRectangle active,
        uint32_t& active_changed_tick,
        std::span<const OccupancyChange> units,
        uint32_t current_tick
    );
    /// Reclassifies a unit's rectangle when it changed after the projection.
    ///
    /// @param rectangle the unit's packed origin and extent
    /// @param changed_tick tick the unit last moved
    void release_unit(OccupancyRectangle rectangle, uint32_t changed_tick);
    /// Builds the whole map.
    ///
    /// Every plot takes its own class, then a row and a column pass keep the
    /// footprint-wide minimum and drop a clear window to class 1 when the cell just
    /// outside either end of the window is not clear.
    ///
    /// @quirk The running minimum is only rescanned when the cell leaving the window was the minimum, as in 3.1c.
    void rebuild();

  private:

    /// Writes one cell's two-bit class into its packed word.
    ///
    /// @param x cell column
    /// @param z cell row
    /// @param value class; only the low two bits are kept
    void store(uint32_t x, uint32_t z, uint8_t value) noexcept;
    uint32_t width_{}, height_{}, projection_tick_{};
    int16_t footprint_x_{}, footprint_z_{};
    MovementMapSampler* sampler_{};
    std::vector<uint32_t> cells_;
};

} // namespace oa::sim::ground_orders
