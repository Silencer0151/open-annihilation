// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/core/game_state.h"
#include "oa/core/player.h"
#include "oa/core/unit.h"
#include "oa/core/unit_def.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace oa::sim::visibility_state {
inline constexpr uint16_t terrain_mapping = 0x0001;
inline constexpr uint16_t update_sight_grid = 0x0002;
inline constexpr uint16_t altitude_sight_algorithm = 0x0004;

struct TerrainCell {
    uint8_t movement_cost{};
};

struct TerrainGrid {
    int32_t width{}, height{};
    std::span<const TerrainCell> cells;
    /// Returns the cell at a grid position.
    ///
    /// @param x cell column
    /// @param z cell row
    /// @return the cell, or null off the grid or past the end of `cells`
    [[nodiscard]] const TerrainCell* at(int32_t x, int32_t z) const noexcept;
};

struct SpeedUnit {
    float type_speed{};
    int16_t grid_x{}, grid_z{}, footprint_x{}, footprint_z{};
    float speed{};
    bool script_present{};
};

struct SpeedResult {
    uint16_t terrain_sum{};
    bool updated{};
};

struct SpeedHost {
    virtual ~SpeedHost() = default;
    /// Passes the unit's terrain sum to its script through SetSpeed.
    ///
    /// @param terrain_sum the footprint sum read as a signed 16-bit value
    virtual void set_speed(int32_t terrain_sum) = 0;
};

/// Sets a metal extractor's rate at creation from the metal under its footprint.
///
/// With a positive type rate (UnitDef.extracts_metal), MapPlot.metal plus one of
/// every footprint cell is summed in a 16-bit word; the unit's rate
/// (Unit.extracted_metal) is the type rate times that sum read as signed, which
/// the script also receives through SetSpeed.
///
/// @param[in,out] unit extractor whose speed is set
/// @param terrain metal grid the footprint is summed over; cells off the grid are skipped
/// @param script SetSpeed receiver for a unit with a script; null skips the call
/// @return the unsigned sum and whether the rate was set; nothing for a non-positive type rate
[[nodiscard]] SpeedResult
initialize_terrain_speed(SpeedUnit& unit, const TerrainGrid& terrain, SpeedHost* script);

struct PlayerSightGrid {
    int32_t width{}, height{};
    std::vector<uint8_t> coverage;
    std::vector<uint16_t> player_bits;
    uint8_t viewpoint_player{};
    // Game block whose fog-mask and mapped-radar bits a change to the
    // viewpoint player's sight marks; none leaves them.
    oa::Game* game{};
};

struct SightMask {
    uint16_t width{}, height{};
    int16_t offset_x{}, offset_z{};
    uint8_t transparent{};
    std::vector<uint8_t> pixels;
};

struct AltitudeCell {
    uint8_t high_height{}, low_height{};
};

struct SightRay {
    std::vector<std::array<int16_t, 2>> offsets;
};

struct AltitudeSightPattern {
    std::vector<SightRay> rays;
    const char* error{}; // why build_altitude_pattern refused the lines, or null
};

struct AltitudeSightData {
    int32_t width{}, height{};
    std::span<const AltitudeCell> cells;
    // gamedata/los.tdf TABLE1 is stored at index 0 and selected for logical band
    // 1. Band 0 selects no table: a sight under 32 stamps only its own cell.
    std::span<const AltitudeSightPattern> patterns;
};

/// Projects the 16-unit terrain-height lattice into the paired 32-unit height grid altitude sight reads.
///
/// Each height also counts, scaled, in the cells of the sight row it is drawn
/// in (z * 16 - height / 2 world units). Each cell's high and low are then
/// pulled a third of the way toward each other and raised to the minimum height.
///
/// @param terrain_heights plot heights, row-major
/// @param terrain_width lattice width in plots
/// @param terrain_height lattice height in plots
/// @param minimum_height lowest high and low height of a cell, the sea-level height
/// @return half-width by half-height cells, row-major; none when a dimension is
///         negative or past 32767, or the heights do not fill the lattice
[[nodiscard]] std::vector<AltitudeCell> build_altitude_cells(
    std::span<const uint8_t> terrain_heights,
    int32_t terrain_width,
    int32_t terrain_height,
    uint8_t minimum_height
);
/// Builds a TABLE%d pattern from its ordered line1..lineN values in gamedata/los.tdf.
///
/// Each line is a point count and x,y pairs, stored as four rays turned a
/// quarter each: north (x,-y), east (y,x), south (-x,y) and west (-y,-x). A
/// missing line is an empty ray.
///
/// @param lines the table's line values in order
/// @return rays grouped by direction: all north rays first, then east, south and
///         west; no rays and an error for more than 8191 lines, a value that is not
///         a 32-bit integer, or a line with fewer pairs than its count
[[nodiscard]] AltitudeSightPattern build_altitude_pattern(std::span<const std::string_view> lines);

// The record every sight stamp is added, moved and removed through. A unit's
// stamp words (Unit.sight_center_x, sight_center_z and sight_band) are copied
// in here and written back.
struct SightStamp {
    uint8_t owner{}; // Player.index
    int16_t center_x{}, center_z{};
    int16_t sight_distance{};
    uint8_t model_height{}; // whole units of UnitDef.model_height, as a byte
    uint8_t band{};         // mask band, or the stamped altitude under altitude sight
    int32_t position_x{}, position_y{}, position_z{}; // Y no lower than one unit over sea level
};

struct SightProjection {
    int32_t center_x{}, center_z{};
    int32_t band{};
};

// The remembered-sight table: 20 records, each a stamp and the tick it
// expires; count holds the live ones.
inline constexpr int32_t eyeball_capacity = 0x14;

struct EyeballSlot {
    SightStamp stamp{};
    uint32_t expiry{};
};

struct EyeballMemory {
    int32_t count{};
    EyeballSlot slots[eyeball_capacity]{};
};

// The state the stamp routines read and write: the game's visibility
// rules and sea level (Game.visibility_flags and Game.sea_level), the stamp
// tables, each player's coverage bytes (Player.coverage_grid) and the shared
// mapped words.
struct SightContext {
    PlayerSightGrid* grid{};
    std::span<uint8_t> coverage[OA_PLAYER_COUNT]{}; // by Player.index
    std::span<const SightMask> masks{};             // the standard sight masks, by band
    const AltitudeSightData* altitude{};            // altitude heights and los.tdf rays
    uint16_t visibility_flags{};
    uint8_t minimum_height_cell{};
};

/// Checks that stamps can go through a context.
///
/// Every grid must hold the sight cells, the rules' tables must be present and
/// whole, and the altitude tables must match the sight grid within the game's
/// signed-word counts.
///
/// @param context context to check
/// @return why stamps cannot go through it, or null when they can
[[nodiscard]] const char* sight_context_error(const SightContext& context) noexcept;

/// Returns the standard stamp's cell and mask band for a descriptor's position.
///
/// The band is sight/32 - 5 within the mask table; X and Z divide toward zero,
/// Z is raised by Y/64, and the band's mask offset is taken off both.
///
/// @param stamp descriptor whose position and sight distance are read
/// @param context context holding the mask table
/// @return the stamp cell and band; the stored ones when there are no masks
[[nodiscard]] SightProjection
project_sight_cell(const SightStamp& stamp, const SightContext& context) noexcept;
/// Counts the stamp once more in each cell of the owner's coverage it sees.
///
/// A viewpoint player's stamp marks the fog and radar stale before anything is
/// counted.
///
/// @param stamp descriptor to add, at its stored cell and band
/// @param[in,out] context coverage grids and tables
void add_area_coverage(const SightStamp& stamp, SightContext& context) noexcept;
/// Counts the stamp once less in each cell of the owner's coverage it saw.
///
/// @param stamp descriptor to remove, at its stored cell and band
/// @param[in,out] context coverage grids and tables
/// @quirk The byte counts wrap.
void remove_area_coverage(const SightStamp& stamp, SightContext& context) noexcept;
/// Sets the owner's mapped bit in each cell the stamp sees.
///
/// A new bit for the viewpoint player marks the fog and radar stale.
///
/// @param stamp descriptor to map
/// @param[in,out] context sight grid and tables
/// @quirk The standard stamp takes its mask from the sight distance, not from the stored band.
void map_area(const SightStamp& stamp, SightContext& context) noexcept;
/// Gives one player every cell another has mapped, as sharing a map does:
/// the receiver's mapped bit is set in each cell where the sharer's is.
///
/// A new bit for the viewpoint player marks the fog and radar stale.
///
/// @param[in,out] grid sight grid whose mapped words change
/// @param from player whose mapped cells are shared, 0..15
/// @param to player receiving them, 0..15
void share_mapped_cells(PlayerSightGrid& grid, uint8_t from, uint8_t to) noexcept;
/// Moves a stamp to its descriptor's position when the cell or band changed.
///
/// The stamp is removed and added again under the line-of-sight rule, then
/// mapped under the mapping rule.
///
/// @param[in,out] stamp descriptor; its cell and band are updated
/// @param[in,out] context coverage grids and tables
/// @quirk Altitude sight keeps a stamp whose altitude moved by less than 6 in the same cell, and never removes a stamp stored at altitude 0.
void update_area_coverage(SightStamp& stamp, SightContext& context) noexcept;
/// Stamps a descriptor afresh under the line-of-sight rule.
///
/// @param[in,out] stamp descriptor; its cell and band are recomputed
/// @param[in,out] context coverage grids and tables
/// @quirk The standard stamp is mapped whatever the mapping rule.
void refresh_area_coverage(SightStamp& stamp, SightContext& context) noexcept;

/// Stamps a unit's sight afresh, as creation and the grid rebuild do.
///
/// @param[in,out] unit unit whose stamp words are rewritten
/// @param def the unit's type, for its sight distance and model height
/// @param owner the unit's owner
/// @param[in,out] context coverage grids and tables
void stamp_unit_sight(
    oa::Unit& unit, const oa::UnitDef& def, const oa::Player& owner, SightContext& context
) noexcept;
/// Moves a unit's sight stamp to where the unit now is.
///
/// @param[in,out] unit unit whose stamp words are updated
/// @param def the unit's type, for its sight distance and model height
/// @param owner the unit's owner
/// @param[in,out] context coverage grids and tables
void move_unit_sight(
    oa::Unit& unit, const oa::UnitDef& def, const oa::Player& owner, SightContext& context
) noexcept;
/// Takes a dying unit's stamp out of its owner's coverage.
///
/// The unit keeps its stamp words and the mapped bits stay.
///
/// @param unit dying unit
/// @param def the unit's type
/// @param owner the unit's owner
/// @param[in,out] context coverage grids and tables
void clear_unit_sight(
    const oa::Unit& unit, const oa::UnitDef& def, const oa::Player& owner, SightContext& context
) noexcept;

/// Keeps the viewer seeing around a point for a while.
///
/// Appends a remembered-sight record owned by the viewpoint player and stamps it,
/// only under the line-of-sight rule and while fewer than 20 are live.
///
/// @param[in,out] memory the remembered-sight records
/// @param position signed 16.16 point to keep seeing; Y is raised to one unit over sea level
/// @param sight_distance sight distance in world units
/// @param model_height whole-unit model height
/// @param duration ticks to remember
/// @param tick current game tick
/// @param[in,out] context coverage grids and tables
/// @quirk A reused record keeps its previous occupant's cell until the stamp rewrites it; the expiry is the wrapping sum of tick and duration.
void remember_sight(
    EyeballMemory& memory,
    const oa::FixedVec3& position,
    int16_t sight_distance,
    uint8_t model_height,
    int32_t duration,
    uint32_t tick,
    SightContext& context
) noexcept;
/// Tests whether a remembered stamp's expiry tick is behind the world tick.
///
/// @param slot remembered record
/// @param tick current game tick
/// @return true when expiry < tick
[[nodiscard]] bool remembered_sight_expired(const EyeballSlot& slot, uint32_t tick) noexcept;
/// Drops the expired records and closes the gaps in order.
///
/// @param[in,out] memory the remembered-sight records; its count is not updated
/// @param tick current game tick
/// @return the number of records left
/// @quirk Records past the new count keep their stale contents.
int32_t compact_remembered_sight(EyeballMemory& memory, uint32_t tick) noexcept;
/// Lets remembered sight lapse.
///
/// Takes each expired record's stamp out of the viewer's coverage, then compacts
/// the memory when one expired or `always_compact` is set.
///
/// @param[in,out] memory the remembered-sight records
/// @param tick current game tick
/// @param always_compact compact even when nothing expired
/// @param[in,out] context coverage grids and tables
void expire_remembered_sight(
    EyeballMemory& memory, uint32_t tick, bool always_compact, SightContext& context
) noexcept;
} // namespace oa::sim::visibility_state
