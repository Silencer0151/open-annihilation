// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/core/unit.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace oa::sim::spatial_state {
using UnitId = uint16_t;
inline constexpr UnitId no_unit = 0;
inline constexpr uint32_t terrain_occupancy_mask = OA_UNIT_FLAG_OCCUPANCY_MASK;
inline constexpr uint32_t masked_footprint_flag = OA_UNIT_FLAG_BUILDING;
inline constexpr uint32_t collision_other = OA_UNIT_FLAG_COLLISION_OTHER;
inline constexpr uint32_t collision_self = OA_UNIT_FLAG_COLLISION_SELF;

// Plot feature words (MapPlot.feature). Words from first_reserved_feature up
// carry no feature index of their own.
inline constexpr uint16_t no_feature = 0xffff;
inline constexpr uint16_t feature_continuation =
    0xfffe; // footprint cell; feature_back_x/feature_back_z lead back to the origin
inline constexpr uint16_t first_reserved_feature = 0xfffb;

// MapPlot.flags bit set under a placed building's claiming yard cell.
inline constexpr uint8_t plot_claimed = 0x02;

struct Plot {
    UnitId ground{};                    // MapPlot.ground_unit, occupancy kind 1
    UnitId air{};                       // MapPlot.air_unit, occupancy kind 2
    uint8_t flags{};                    // MapPlot.flags; masked-footprint branch remains separate
    uint8_t high_height{};              // MapPlot.high_height
    uint8_t low_height{};               // MapPlot.low_height
    bool blocking_feature{};            // the feature blocking test for this plot
    bool metal_feature{};               // TDF metal != 0
    bool geo_feature{};                 // TDF geothermal (OA_FEATURE_FLAG_GEOTHERMAL)
    bool indestructible_feature{};      // TDF indestructible (OA_FEATURE_FLAG_INDESTRUCTIBLE)
    uint8_t metal{};                    // MapPlot.metal
    uint16_t feature_word = no_feature; // MapPlot.feature
    // Columns and rows from a continuation cell back to its feature's origin:
    // the high and low bytes of MapPlot.feature_record.
    uint8_t feature_back_x{};
    uint8_t feature_back_z{};
    uint8_t feature_height{};        // FeatureDef.height of the resolved feature
    int16_t feature_footprint_x = 1; // FeatureDef.footprint_x
    int16_t feature_footprint_z = 1; // FeatureDef.footprint_z
};

/// Resolves a plot's feature word to a feature-table index.
///
/// A word below 0xfffb indexes the table when it is in range. A 0xfffe
/// continuation is followed back by the plot's footprint offset (back_x, back_z)
/// to the origin's word. Any other word is no feature.
///
/// @param word the plot's feature word (MapPlot.feature)
/// @param back_x columns back to the footprint origin
/// @param back_z rows back to the footprint origin
/// @param map_width map width in plots
/// @param plot_index row-major index of the plot the word came from
/// @param words every plot's feature word, row-major
/// @param table_count number of entries in the feature table
/// @return the feature index, or nullopt for no feature or an origin outside `words`
/// @quirk The index reached through a 0xfffe continuation is not checked against the table limit.
[[nodiscard]] inline std::optional<uint16_t> resolve_feature_index(
    uint16_t word,
    uint8_t back_x,
    uint8_t back_z,
    uint32_t map_width,
    std::size_t plot_index,
    std::span<const uint16_t> words,
    uint32_t table_count
) noexcept {
    if (word < first_reserved_feature) {
        if (word < table_count)
            return word;
        return std::nullopt;
    }
    if (word != feature_continuation)
        return std::nullopt;
    const auto back = static_cast<std::size_t>(back_z) * map_width + back_x;
    if (back > plot_index || plot_index - back >= words.size())
        return std::nullopt;
    const auto redirected = words[plot_index - back];
    if (redirected < first_reserved_feature)
        return redirected;
    return std::nullopt;
}

/// Converts a signed 16.16 X/Z position to its map cell.
///
/// The cell is each coordinate shifted down 20 bits. A cell outside the map has
/// no plot; a projectile over such a cell is retired by its caller.
///
/// @param x signed 16.16 world X
/// @param z signed 16.16 world Z
/// @param width map width in plots
/// @param height map height in plots
/// @param[out] cell_x cell column, written even when off the map
/// @param[out] cell_z cell row, written even when off the map
/// @return true when the cell lies on the map
[[nodiscard]] inline bool position_to_cell(
    int32_t x, int32_t z, int32_t width, int32_t height, int32_t& cell_x, int32_t& cell_z
) noexcept {
    cell_x = x >> 20;
    cell_z = z >> 20;
    return cell_x >= 0 && cell_x < width && cell_z >= 0 && cell_z < height;
}

/// Returns the row-major plot index of an integer map cell.
///
/// A negative cell, or a cell at or past the map width or height, is no plot.
/// This does not shift a 16.16 position and does not follow a 0xfffe footprint
/// continuation.
///
/// @param cell_x cell column
/// @param cell_z cell row
/// @param plots the map's plots, row-major
/// @param width map width in plots
/// @param height map height in plots
/// @return the plot index, or nullopt off the map or past the end of `plots`
[[nodiscard]] inline std::optional<std::size_t> plot_index_at(
    int32_t cell_x, int32_t cell_z, std::span<const Plot> plots, int32_t width, int32_t height
) noexcept {
    if (cell_x < 0 || cell_x >= width || cell_z < 0 || cell_z >= height)
        return std::nullopt;
    const auto index = static_cast<std::size_t>(cell_z) * static_cast<std::size_t>(width) +
                       static_cast<std::size_t>(cell_x);
    if (index >= plots.size())
        return std::nullopt;
    return index;
}

/// Converts a signed plot slot back to its map cell.
///
/// The slot is the row-major index width * z + x that plot_index_at forms. x is
/// slot % width and z is slot / width, each truncated to a signed 16-bit word;
/// the division truncates toward zero. Feature removal packs this pair for the
/// movement-map refresh.
///
/// @param slot signed row-major plot index
/// @param width map width in plots
/// @return {x, z}, or nullopt for a zero width
[[nodiscard]] inline std::optional<std::array<int16_t, 2>>
plot_index_to_cell(int32_t slot, int32_t width) noexcept {
    if (width == 0)
        return std::nullopt;
    const auto narrow = [](int32_t value) noexcept {
        return static_cast<int16_t>(static_cast<uint16_t>(value));
    };
    return std::array<int16_t, 2>{narrow(slot % width), narrow(slot / width)};
}

/// Returns the plot index of the feature origin under a 16.16 position.
///
/// Uses the same signed 16.16 cell shift as position_to_cell. Word 0xfffe is a
/// footprint continuation, so the origin at (cell_x - back_x, cell_z - back_z) is
/// returned; any other word stays on this cell. The walk is one step.
///
/// @param x signed 16.16 world X
/// @param z signed 16.16 world Z
/// @param plots the map's plots, row-major
/// @param width map width in plots
/// @param height map height in plots
/// @return the origin's plot index, or nullopt when the cell or the origin is off the map
[[nodiscard]] inline std::optional<std::size_t> feature_origin_plot_at(
    int32_t x, int32_t z, std::span<const Plot> plots, int32_t width, int32_t height
) noexcept {
    const auto cell_x = x >> 20;
    const auto cell_z = z >> 20;
    if (cell_x < 0 || cell_x >= width || cell_z < 0 || cell_z >= height)
        return std::nullopt;
    const auto index = static_cast<std::size_t>(cell_z) * static_cast<std::size_t>(width) +
                       static_cast<std::size_t>(cell_x);
    if (index >= plots.size())
        return std::nullopt;
    if (plots[index].feature_word != feature_continuation)
        return index;
    const auto origin_x = cell_x - static_cast<int32_t>(plots[index].feature_back_x);
    const auto origin_z = cell_z - static_cast<int32_t>(plots[index].feature_back_z);
    if (origin_x < 0 || origin_x >= width || origin_z < 0 || origin_z >= height)
        return std::nullopt;
    const auto origin = static_cast<std::size_t>(origin_z) * static_cast<std::size_t>(width) +
                        static_cast<std::size_t>(origin_x);
    if (origin >= plots.size())
        return std::nullopt;
    return origin;
}

/// Returns the mean of a plot's high and low heights under a 16.16 position.
///
/// The signed high words of X and Z are divided by 16 toward zero to give the
/// cell, which plot_index_at looks up. The plot's high and low heights are added
/// and shifted right once. This does not interpolate like the terrain height
/// sample and does not follow a 0xfffe footprint.
///
/// @param x signed 16.16 world X
/// @param z signed 16.16 world Z
/// @param plots the map's plots, row-major
/// @param width map width in plots
/// @param height map height in plots
/// @return (low_height + high_height) >> 1, or -1 when there is no plot
[[nodiscard]] inline int32_t plot_mean_height_at(
    int32_t x, int32_t z, std::span<const Plot> plots, int32_t width, int32_t height
) noexcept {
    const auto world = [](int32_t fixed) noexcept {
        return static_cast<int32_t>(
            static_cast<int16_t>(static_cast<uint16_t>(static_cast<uint32_t>(fixed) >> 16))
        );
    };
    const auto index = plot_index_at(world(x) / 16, world(z) / 16, plots, width, height);
    if (!index)
        return -1;
    return (static_cast<int32_t>(plots[*index].low_height) +
            static_cast<int32_t>(plots[*index].high_height)) >>
           1;
}

// Bucket.edges bits: the map edges a bucket lies on.
inline constexpr uint32_t bucket_edge_north = 0x01; // first row
inline constexpr uint32_t bucket_edge_south = 0x02; // last row
inline constexpr uint32_t bucket_edge_west = 0x04;  // first column
inline constexpr uint32_t bucket_edge_east = 0x08;  // last column
inline constexpr uint32_t bucket_off_map = 0x10;    // the off-map bucket
inline constexpr uint32_t off_map_bucket_edges = 0x1f;

// One 8 by 8 plot block of the unit grid.
struct Bucket {
    uint8_t high_height{};      // highest plot high_height, at least sea level
    uint8_t area_high_height{}; // highest high_height of it and its eight neighbours
    uint32_t edges{};           // bucket_edge_* bits
    UnitId head{};              // first unit of the bucket's chain
};

// The spatial fields of one unit, projected from its canonical records.
struct Unit {
    UnitId id{};                        // Unit.id; zero is the empty plot/list sentinel
    std::array<uint32_t, 3> position{}; // Unit.position, signed 16.16 words
    std::array<int16_t, 2> cell{};      // Unit.cell_x, Unit.cell_z
    std::array<int16_t, 2> footprint{}; // Unit.footprint_x, Unit.footprint_z
    uint32_t flags{};                   // Unit.flags
    bool object_present{};              // the unit has a movement object (Unit.movement)
    bool owner_object_present{};        // the owner's Player.in_use is nonzero
    uint8_t owner_status{};             // the owner's Player.status
    bool yard_open{};                   // Unit.build_flags bit 2
    std::span<const uint8_t> yard_mask; // UnitDef.yard_map, footprint row-major
    bool spatial_link_locked{};         // Unit.attach_parent is set: the unit rides a parent
    bool bucket_linked{};               // Unit.spatial_bucket is set
    std::optional<std::size_t> bucket;  // the bucket Unit.spatial_bucket names; nullopt off the map
    // Unit.attach_next: the next unit of the bucket chain, or of the parent's
    // children.
    UnitId next_in_bucket{};
    UnitId first_attachment{}; // Unit.attach_first_child; children chain through next_in_bucket
    uint32_t object_tick{};    // the movement object's occupancy change tick
    uint8_t max_slope{};       // UnitDef.max_slope
    uint8_t max_water_slope{}; // UnitDef.max_water_slope
    int16_t max_water_depth{}; // UnitDef.max_water_depth
    int16_t min_water_depth{}; // UnitDef.min_water_depth
    int8_t bm_code{};          // UnitDef.bm_code
};

/// The most plot writes World::written_occupants lists.
inline constexpr std::size_t written_occupant_capacity = 4096;

struct World {
    uint32_t terrain_width{}, terrain_height{}; // Game.map_width, Game.map_height
    std::vector<Plot> plots;
    // The index in `plots` of each plot whose ground or air word was written
    // since the list was last emptied, in the order written, the first
    // written_occupant_count of them; a plot written twice is listed twice.
    // Writes past written_occupant_capacity are not listed and set
    // written_occupants_lost instead. Whoever keeps another copy of the words
    // empties the list.
    std::array<uint32_t, written_occupant_capacity> written_occupants{};
    uint32_t written_occupant_count{};
    bool written_occupants_lost{};
    uint32_t bucket_width{}; // Game.bucket_width
    uint32_t bucket_height{};
    std::vector<Bucket> buckets;
    Bucket outside_bucket; // the off-map bucket
    std::span<Unit> units; // indexed by UnitId
    uint32_t tick{};       // Game.tick
    uint8_t sea_level{};   // Game.sea_level
};
enum class Error {
    none,
    malformed_world,
    invalid_unit_id,
    broken_bucket_chain,
    spatial_bucket_out_of_range,
    invalid_yard_mask
};

struct Host {
    virtual ~Host() = default;
    /// Recomputes the plot height ranges (high_height, low_height) over a cell rectangle.
    ///
    /// @param origin_minus_one first cell of the rectangle, one cell before the footprint origin on each axis
    /// @param footprint_plus_two rectangle size in cells, the footprint plus two on each axis
    virtual void refresh_plot_height_range(
        std::array<int16_t, 2> origin_minus_one, std::array<int16_t, 2> footprint_plus_two
    ) = 0;
    /// Refreshes the movement maps over a changed footprint.
    ///
    /// @param cell footprint origin cell
    /// @param footprint footprint size in cells
    virtual void
    notify_footprint_changed(std::array<int16_t, 2> cell, std::array<int16_t, 2> footprint) = 0;
    /// Refreshes the movement maps after an object-backed unit leaves its footprint.
    ///
    /// @param unit the unit that was removed; its object tick has already advanced
    /// @param old_tick the unit's object tick before removal
    virtual void notify_object_footprint_removed(Unit& unit, uint32_t old_tick) = 0;
};

/// Writes a unit into one layer of a plot, and lists the plot in
/// World::written_occupants.
///
/// An empty layer takes the unit. When the layer holds another unit, the
/// occupant is marked collision_other and the new unit collision_self, and the
/// layer keeps the occupant. An occupant whose owner has status 3 instead takes
/// collision_self, the new unit collision_other, and the new unit replaces it.
///
/// @param[in,out] plot plot to write
/// @param air_layer true for the air layer (Plot.air), false for the ground layer (Plot.ground)
/// @param[in,out] unit unit taking the layer
/// @param[in,out] world world whose unit table holds the occupant
/// @return Error::invalid_unit_id for an occupant outside the unit table, otherwise Error::none
[[nodiscard]] Error occupy(Plot& plot, bool air_layer, Unit& unit, World& world);
/// Files a unit in a spatial bucket, unlinking it from the one it was in.
///
/// A unit already filed in the target is left alone. A unit riding a parent
/// (spatial_link_locked) is not relinked in any chain but still records the
/// target.
///
/// @param[in,out] unit unit to move
/// @param target bucket index, or nullopt for the off-map bucket
/// @param[in,out] world world holding the buckets and units
/// @return Error::spatial_bucket_out_of_range for a bucket index past the grid, Error::broken_bucket_chain when the old chain does not reach the unit, otherwise Error::none
[[nodiscard]] Error move_bucket(Unit& unit, std::optional<std::size_t> target, World& world);
/// Splices a unit out of a bucket's chain and clears its link.
///
/// The chain starts at Bucket.head and links through Unit.next_in_bucket.
///
/// @param[in,out] chain bucket whose chain holds the unit
/// @param[in,out] unit unit to remove
/// @param[in,out] world world whose unit table the chain indexes
/// @return Error::broken_bucket_chain when the chain ends or loops before reaching the unit, otherwise Error::none
[[nodiscard]]
Error bucket_unlink(Bucket& chain, Unit& unit, World& world);
/// Links a unit at the head of a bucket's chain.
///
/// @param[in,out] chain bucket to link into
/// @param[in,out] unit unit to link
void bucket_push_front(Bucket& chain, Unit& unit) noexcept;
/// Registers a unit in the spatial grid and writes its footprint to the plots.
///
/// Stamps an attached object with the world tick and files the unit in the
/// 128-world-unit bucket under its position, or the off-map bucket when its
/// footprint leaves the map (which ends the call). A masked-footprint unit
/// occupies the ground layer of each yard cell whose mask bit matches its yard
/// state (bit 2 open, bit 4 closed), marks mask bit 1 cells plot_claimed, then
/// asks the host to refresh plot heights and the movement maps. Otherwise
/// occupancy kind 1 writes the ground layer and kind 2 the air layer; kind 0
/// only files the unit.
///
/// @param[in,out] unit unit to register; must be world.units[unit.id]
/// @param[in,out] world world to register in
/// @param[in,out] host receives the map refresh requests of a masked footprint
/// @return Error::malformed_world, Error::invalid_unit_id, Error::spatial_bucket_out_of_range, Error::invalid_yard_mask or a bucket/occupy error; Error::none on success
[[nodiscard]] Error register_unit(Unit& unit, World& world, Host& host);
/// Checks whether a unit's yard can take a requested open/closed state.
///
/// Every yard cell active in the requested state must have no ground occupant
/// other than the unit itself.
///
/// @param unit unit whose yard mask is tested
/// @param value requested yard state as the script passed it; any nonzero value tests the open mask
/// @param world world holding the plots
/// @return false for a malformed world or mask, a cell on the top or left map edge, a footprint reaching the far map edge, or a foreign occupant; otherwise true
[[nodiscard]] bool can_change_yard(const Unit& unit, int32_t value, const World& world);
/// Rewrites a marked unit's plot occupancy for its current state.
///
/// Does nothing unless the unit carries collision_self, which it clears. A
/// masked-footprint unit occupies its yard cells active in the current state and
/// clears its own ID from the inactive ones; any other unit re-occupies its
/// ground or air layer over the footprint.
///
/// @param[in,out] unit unit to update; must be world.units[unit.id]
/// @param[in,out] world world holding the plots
/// @return Error::malformed_world for a malformed world or a footprint off the map, Error::invalid_unit_id, Error::invalid_yard_mask or an occupy error; Error::none otherwise
[[nodiscard]] Error update_occupancy(Unit& unit, World& world);
/// Tests whether a mobile unit's footprint may stand at a cell.
///
/// Covers units with a nonzero BM code. A footprint touching or past the far
/// map edge, or with a negative cell, passes only in mode 2. Modes other than 1
/// accept any on-map cell. Mode 1 also requires each footprint plot to be free of
/// blocking features and of ground occupants other than `ignored`, to lie within
/// the unit's water depths below sea level, and to have a height step within
/// max_slope, or within max_water_slope when the plot is under water.
///
/// @param unit unit whose footprint, water depths and slopes are used
/// @param ignored ground occupant that does not block, usually the unit itself
/// @param cell footprint origin cell
/// @param mode 1 walks the plots; 2 accepts any cell, on or off the map, and any
///        other value any on-map cell, without walking the plots
/// @param world world holding the plots
/// @return whether the unit fits, or nullopt for a unit with BM code 0, whose yard-map test lives elsewhere
[[nodiscard]] std::optional<bool> can_occupy(
    const Unit& unit, UnitId ignored, std::array<int16_t, 2> cell, int32_t mode, const World& world
);
/// Tests whether a transport may set a unit down at a world position.
///
/// The footprint is centred on the position. Each footprint cell must be free of
/// blocking features, plot_claimed cells and other ground or air occupants, lie
/// within the unit's water depths below sea level and have a height step within
/// max_slope.
///
/// @param unit unit being unloaded
/// @param world_x signed 16.16 world X of the footprint centre
/// @param world_z signed 16.16 world Z of the footprint centre
/// @param player_sees_cell whether the unloading player can see the cell; when false the plots are not walked
/// @param flies_not_amphibious the type can fly (OA_UNIT_DEF_FLAG_CAN_FLY) and is not amphibious
///        (OA_UNIT_DEF_FLAG_AMPHIBIOUS); raises the depth floor to sea level
/// @param world world holding the plots
/// @return true when the unit fits, false off the map or on any failed cell
/// @quirk A cell the player cannot see is accepted without walking its plots.
[[nodiscard]] bool can_unload_at(
    const Unit& unit,
    int32_t world_x,
    int32_t world_z,
    bool player_sees_cell,
    bool flies_not_amphibious,
    const World& world
);
/// Returns the height a building of this footprint and yard map stands at on a cell.
///
/// The height is the lowest low_height under its yard cells with bit 3 when any
/// has one, otherwise sea level less the waterline, as an 8-bit wrap. Yard bit 4
/// cells are walked but do not change the result; cells past the end of a short
/// yard map count as not level.
///
/// @param footprint_x footprint width in cells
/// @param footprint_z footprint depth in cells
/// @param yard_mask the type's yard map, row-major
/// @param waterline the type's waterline, subtracted from sea level
/// @param cell_x footprint origin column
/// @param cell_z footprint origin row
/// @param world world holding the plots and sea level
/// @return the build height, or 0 when either cell coordinate is not positive or the footprint reaches the far map edge
[[nodiscard]] uint8_t footprint_build_height(
    int16_t footprint_x,
    int16_t footprint_z,
    std::span<const uint8_t> yard_mask,
    int8_t waterline,
    int32_t cell_x,
    int32_t cell_z,
    const World& world
);
/// Tests whether two cell rectangles, each an origin and a size, overlap.
///
/// @param cell first rectangle's origin cell
/// @param size first rectangle's size in cells
/// @param other_cell second rectangle's origin cell
/// @param other_size second rectangle's size in cells
/// @return true when the rectangles share a cell
[[nodiscard]] bool rectangles_overlap(
    std::array<int16_t, 2> cell,
    std::array<int16_t, 2> size,
    std::array<int16_t, 2> other_cell,
    std::array<int16_t, 2> other_size
) noexcept;
/// Sizes the bucket grid to the map and records each bucket's heights and edges.
///
/// Buckets are 128 world units a side, rounded up. Each bucket's high_height is
/// the highest plot high_height in it, at least sea level; area_high_height
/// takes the highest of it and its eight neighbours. Map-edge buckets get their
/// bucket_edge_* bits and the off-map bucket takes every edge bit. The
/// half-resolution height pairs are built by build_altitude_cells.
///
/// @param[in,out] world world whose map size, plots and sea level are read and whose buckets are rebuilt
/// @quirk A map only one bucket wide or deep leaves every area_high_height zero.
void build_buckets(World& world);
/// Returns the bucket a unit is filed in.
///
/// A unit filed nowhere reads the bucket under its position, or the off-map
/// bucket past the map.
///
/// @param world world holding the buckets
/// @param unit unit to look up
/// @return the unit's bucket
[[nodiscard]] const Bucket& unit_bucket(const World& world, const Unit& unit) noexcept;
/// Visits every unit whose footprint overlaps a cell rectangle.
///
/// Walks the 8-cell buckets around the rectangle, one either side of its own,
/// and each unit in them together with the units it carries (first_attachment).
///
/// @param cell rectangle origin cell
/// @param footprint rectangle size in cells
/// @param[in,out] world world holding the buckets and units
/// @param visit callback for each overlapping unit; an error it returns stops the walk
/// @param context passed through to `visit`
/// @return the first error of a visit, Error::broken_bucket_chain for a broken chain, otherwise Error::none
/// @quirk A carried unit is visited whether or not its carrier overlapped.
[[nodiscard]] Error visit_overlapping_units(
    std::array<int16_t, 2> cell,
    std::array<int16_t, 2> footprint,
    World& world,
    Error (*visit)(Unit&, World&, void* context),
    void* context
);
/// Settles a unit's footprint for its current state, as a restored open yard needs.
///
/// Marks the unit collision_self, runs update_occupancy on it and every unit
/// overlapping it, then asks the host to refresh the movement maps.
///
/// @param[in,out] unit unit to settle; must be world.units[unit.id]
/// @param[in,out] world world holding the plots and units
/// @param[in,out] host receives the footprint refresh
/// @return Error::malformed_world, Error::invalid_unit_id or a walk/update error; Error::none on success
[[nodiscard]] Error refresh_footprint_occupancy(Unit& unit, World& world, Host& host);
/// Clears a unit's plot occupancy and wakes the units it collided with.
///
/// Unless the unit is filed in the off-map bucket, clears its own ID from the
/// ground or air layer over its footprint; a masked-footprint unit also clears
/// plot_claimed under its mask bit 1 cells and asks for a plot height refresh.
/// Clears collision_self; if collision_other was set, clears it and runs
/// update_occupancy on every overlapping unit. An object-backed unit then
/// advances its object tick and reports removal to the host; an objectless unit
/// requests a plain footprint refresh.
///
/// @param[in,out] unit unit to clear; must be world.units[unit.id]
/// @param[in,out] world world holding the plots and units
/// @param[in,out] host receives the map refresh requests
/// @return Error::malformed_world, Error::invalid_unit_id, Error::invalid_yard_mask or a walk error; Error::none on success
[[nodiscard]] Error remove_occupancy(Unit& unit, World& world, Host& host);
/// Takes a dying unit off the map.
///
/// Runs remove_occupancy, then, unless the unit rides a parent, unlinks it from
/// its bucket chain; either way it is left in no bucket (bucket_linked cleared).
///
/// @param[in,out] unit unit to remove
/// @param[in,out] world world holding the plots, buckets and units
/// @param[in,out] host receives the map refresh requests
/// @return the remove_occupancy error, Error::spatial_bucket_out_of_range or Error::broken_bucket_chain; Error::none on success
[[nodiscard]] Error remove_unit(Unit& unit, World& world, Host& host);
/// Opens or closes a unit's yard when no foreign unit blocks it.
///
/// Validates with can_change_yard, marks the unit collision_self, stores the
/// yard state (yard_open) and runs update_occupancy, then asks the host to
/// refresh the movement maps over the footprint.
///
/// @param[in,out] unit unit whose yard changes
/// @param value yard state as the script passed it; validation treats any nonzero value as open, the stored state keeps only bit 0
/// @param[in,out] world world holding the plots
/// @param[in,out] host receives the footprint refresh
/// @return true when the yard changed
/// @quirk An even nonzero value validates against the open mask but stores the closed state.
[[nodiscard]] bool change_yard(Unit& unit, int32_t value, World& world, Host& host);
} // namespace oa::sim::spatial_state
