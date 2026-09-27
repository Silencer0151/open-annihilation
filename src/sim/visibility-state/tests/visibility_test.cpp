// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/visibility_state.hpp"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
using namespace oa::sim::visibility_state;

namespace {
struct SpeedRecorder final : SpeedHost {
    int speed{-1};

    void set_speed(int32_t terrain_sum) override { speed = terrain_sum; }
};

PlayerSightGrid make_grid(int32_t width, int32_t height, uint8_t viewpoint) {
    PlayerSightGrid grid;
    grid.width = width;
    grid.height = height;
    grid.coverage.resize(static_cast<std::size_t>(width * height));
    grid.player_bits.resize(static_cast<std::size_t>(width * height));
    grid.viewpoint_player = viewpoint;
    return grid;
}

// Coverage of the viewer in the grid, every other player in `others`.
SightContext make_context(
    PlayerSightGrid& grid,
    std::vector<std::vector<uint8_t>>& others,
    std::span<const SightMask> masks,
    uint16_t rules
) {
    SightContext context;
    context.grid = &grid;
    others.assign(OA_PLAYER_COUNT, std::vector<uint8_t>(grid.coverage.size()));
    for (std::size_t player = 0; player < OA_PLAYER_COUNT; ++player)
        context.coverage[player] = player == grid.viewpoint_player
                                       ? std::span<uint8_t>(grid.coverage)
                                       : std::span<uint8_t>(others[player]);
    context.masks = masks;
    context.visibility_flags = rules;
    return context;
}

// The fog and radar passes caught up with the last stamp.
void clear_marks(PlayerSightGrid& grid) {
    grid.game->visibility_flags =
        static_cast<uint8_t>(grid.game->visibility_flags | OA_VISIBILITY_FOG_MASK_CURRENT);
    grid.game->radar_blink_flags =
        static_cast<uint16_t>(grid.game->radar_blink_flags & ~OA_RADAR_MAPPED_DIRTY);
}

// A stamp of the viewpoint's sight left the fog edge mask stale
// (OA_VISIBILITY_FOG_MASK_CURRENT clear) and the mapped radar dirty
// (OA_RADAR_MAPPED_DIRTY set).
bool marked(const PlayerSightGrid& grid) {
    return (grid.game->visibility_flags & OA_VISIBILITY_FOG_MASK_CURRENT) == 0 &&
           (grid.game->radar_blink_flags & OA_RADAR_MAPPED_DIRTY) != 0;
}

void standard_stamps() {
    // A 3x2 mask with its origin one cell in; pixel value 0 is transparent.
    const SightMask masks[2]{
        {3, 2, 1, 1, 0, {1, 0, 1, 1, 1, 0}},
        {1, 1, 0, 0, 0, {1}},
    };
    const auto game = std::make_unique<oa::Game>();
    auto grid = make_grid(4, 3, 2);
    grid.game = game.get();
    clear_marks(grid);
    std::vector<std::vector<uint8_t>> others;
    auto context = make_context(grid, others, masks, update_sight_grid | terrain_mapping);
    CHECK(sight_context_error(context) == nullptr);

    // Sight 160 is band 0 (160/32 - 5). Y below the sea floor is raised to
    // one unit, which adds nothing to Z; X 3 and Z 2 cells, less the mask origin.
    SightStamp stamp{2, 0, 0, 160, 0, 9, 3 * 0x200000, 65536, 2 * 0x200000};
    auto cell = project_sight_cell(stamp, context);
    CHECK(cell.center_x == 2 && cell.center_z == 1 && cell.band == 0);
    // 64 units of height raise the stamp one row; the division truncates.
    stamp.position_y = 64 << 16;
    CHECK(project_sight_cell(stamp, context).center_z == 0);
    stamp.position_y = 63 << 16;
    CHECK(project_sight_cell(stamp, context).center_z == 1);
    stamp.position_x = -(0x200000 - 1);
    CHECK(project_sight_cell(stamp, context).center_x == -1);
    stamp.position_x = 3 * 0x200000;
    stamp.sight_distance = 5000;
    CHECK(project_sight_cell(stamp, context).band == 1);
    stamp.sight_distance = -31;
    CHECK(project_sight_cell(stamp, context).band == 0);
    stamp.sight_distance = 160;

    refresh_area_coverage(stamp, context);
    CHECK(stamp.center_x == 2 && stamp.center_z == 1 && stamp.band == 0);
    // The mask's third column falls off the grid's right edge.
    CHECK(grid.coverage == std::vector<uint8_t>({0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1}));
    CHECK(grid.player_bits[6] == 4 && grid.player_bits[10] == 4 && grid.player_bits[11] == 4);
    CHECK(grid.player_bits[7] == 0 && marked(grid));

    // Adding and removing the same stamp is symmetric, and counts wrap.
    add_area_coverage(stamp, context);
    CHECK(grid.coverage[6] == 2 && grid.coverage[10] == 2 && grid.coverage[11] == 2);
    remove_area_coverage(stamp, context);
    remove_area_coverage(stamp, context);
    CHECK(grid.coverage == std::vector<uint8_t>(12));
    remove_area_coverage(stamp, context);
    CHECK(grid.coverage[6] == 255 && grid.coverage[10] == 255 && grid.coverage[7] == 0);
    add_area_coverage(stamp, context);
    CHECK(grid.coverage == std::vector<uint8_t>(12));
    // Mapped bits are never taken back.
    CHECK(grid.player_bits[6] == 4);

    // The viewer's stamp marks the views stale even off the grid.
    clear_marks(grid);
    SightStamp off_grid = stamp;
    off_grid.center_x = 100;
    off_grid.center_z = 100;
    add_area_coverage(off_grid, context);
    CHECK(marked(grid) && grid.coverage == std::vector<uint8_t>(12));
    // A stamp hanging off the top-left corner keeps its in-grid cells.
    SightStamp corner = stamp;
    corner.center_x = -1;
    corner.center_z = -1;
    add_area_coverage(corner, context);
    CHECK(grid.coverage[0] == 1 && grid.coverage[1] == 0);
    remove_area_coverage(corner, context);

    // Another player's stamp counts in its own grid and leaves the views.
    clear_marks(grid);
    SightStamp enemy = stamp;
    enemy.owner = 5;
    add_area_coverage(enemy, context);
    CHECK(others[5][6] == 1 && others[5][10] == 1 && grid.coverage[6] == 0 && !marked(grid));
    // Mapping sets its own bit; an already mapped cell changes nothing and
    // an enemy never marks the views.
    map_area(enemy, context);
    CHECK(grid.player_bits[6] == (4 | 32) && !marked(grid));
    map_area(stamp, context);
    CHECK(!marked(grid));
    // A cell newly mapped for the viewer marks them.
    grid.player_bits[6] = 32;
    map_area(stamp, context);
    CHECK(grid.player_bits[6] == (4 | 32) && marked(grid));

    // Mapping takes its mask from the sight distance, coverage from the band.
    auto banded = make_grid(4, 3, 2);
    auto band_context = make_context(banded, others, masks, update_sight_grid);
    SightStamp mixed = stamp;
    mixed.band = 1;
    mixed.center_x = 0;
    mixed.center_z = 0;
    add_area_coverage(mixed, band_context);
    map_area(mixed, band_context);
    CHECK(banded.coverage == std::vector<uint8_t>({1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
    CHECK(banded.player_bits[0] == 4 && banded.player_bits[2] == 4 && banded.player_bits[5] == 4);
    // A band past the table stamps nothing.
    mixed.band = 2;
    add_area_coverage(mixed, band_context);
    CHECK(banded.coverage[0] == 1);
}

void moving_stamps() {
    const SightMask masks[1]{{1, 1, 0, 0, 0, {1}}};
    auto grid = make_grid(4, 3, 0);
    std::vector<std::vector<uint8_t>> others;
    auto context = make_context(grid, others, masks, update_sight_grid);
    SightStamp stamp{0, 0, 0, 160, 0, 0, 1 * 0x200000, 65536, 1 * 0x200000};
    refresh_area_coverage(stamp, context);
    CHECK(grid.coverage[5] == 1 && grid.player_bits[5] == 1);
    // In the same cell nothing is restamped.
    stamp.position_x += 0x100000;
    update_area_coverage(stamp, context);
    CHECK(grid.coverage[5] == 1 && stamp.center_x == 1);
    // One cell on: the count moves; mapping needs the mapping rule here.
    stamp.position_x = 2 * 0x200000;
    update_area_coverage(stamp, context);
    CHECK(
        stamp.center_x == 2 && grid.coverage[5] == 0 && grid.coverage[6] == 1 &&
        grid.player_bits[6] == 0
    );
    context.visibility_flags = update_sight_grid | terrain_mapping;
    stamp.position_x = 3 * 0x200000;
    update_area_coverage(stamp, context);
    CHECK(grid.coverage[6] == 0 && grid.coverage[7] == 1 && grid.player_bits[7] == 1);
    // Without line of sight the stamp follows the unit and only maps.
    context.visibility_flags = terrain_mapping;
    stamp.position_z = 2 * 0x200000;
    update_area_coverage(stamp, context);
    CHECK(
        stamp.center_z == 2 && grid.coverage[7] == 1 && grid.coverage[11] == 0 &&
        grid.player_bits[11] == 1
    );
    // A fresh stamp needs line of sight and leaves the band alone without it.
    stamp.band = 7;
    refresh_area_coverage(stamp, context);
    CHECK(stamp.band == 7);
}

void altitude_stamps() {
    std::vector<AltitudeCell> heights(25, AltitudeCell{20, 21});
    AltitudeSightPattern empty_pattern;
    AltitudeSightPattern radius_one{{SightRay{{{{1, 0}}, {{2, 0}}}}}};
    std::vector<AltitudeSightPattern> patterns{radius_one, empty_pattern};
    AltitudeSightData altitude{5, 5, heights, patterns};
    auto grid = make_grid(5, 5, 1);
    std::vector<std::vector<uint8_t>> others;
    auto context = make_context(
        grid, others, {}, update_sight_grid | altitude_sight_algorithm | terrain_mapping
    );
    CHECK(std::string_view(sight_context_error(context)) == "altitude sight data is required");
    context.altitude = &altitude;
    CHECK(sight_context_error(context) == nullptr);

    // Sight 32 selects ray table 1, stored first. The stamp sits at altitude
    // 8 + 2 = 10: X 64 >> 5 = 2, Z (128 - 10/2) >> 5 = 3.
    SightStamp stamp{1, 0, 0, 32, 2, 0, 64 << 16, 8 << 16, 128 << 16};
    refresh_area_coverage(stamp, context);
    CHECK(stamp.center_x == 2 && stamp.center_z == 3 && stamp.band == 10);
    // The first ray cell always clears; the second's top (20 - 10) does not
    // clear twice the first's bottom (21 - 10).
    CHECK(grid.coverage[17] == 1 && grid.coverage[18] == 1 && grid.coverage[19] == 0);
    CHECK(grid.player_bits[17] == 2 && grid.player_bits[18] == 2 && grid.player_bits[19] == 0);

    // Under 6 of altitude in the same cell keeps the stamp.
    stamp.position_y = 13 << 16;
    auto unchanged = grid.coverage;
    update_area_coverage(stamp, context);
    CHECK(grid.coverage == unchanged && stamp.band == 10);
    stamp.position_y = 8 << 16;
    stamp.position_x = 96 << 16;
    update_area_coverage(stamp, context);
    CHECK(
        stamp.center_x == 3 && grid.coverage[17] == 0 && grid.coverage[18] == 1 &&
        grid.coverage[19] == 1
    );
    // Removing mirrors adding.
    remove_area_coverage(stamp, context);
    CHECK(grid.coverage == std::vector<uint8_t>(25));
    add_area_coverage(stamp, context);
    CHECK(grid.coverage[18] == 1 && grid.coverage[19] == 1);

    // Leaving the grid removes the stamp and stores band 0.
    stamp.position_x = 200 << 16;
    update_area_coverage(stamp, context);
    CHECK(stamp.band == 0 && stamp.center_x == 6 && grid.coverage == std::vector<uint8_t>(25));
    // A stamp stored at altitude 0 is never removed.
    SightStamp ground = stamp;
    ground.position_x = 64 << 16;
    ground.position_y = 0;
    ground.position_z = 100 << 16;
    ground.model_height = 0;
    update_area_coverage(ground, context);
    CHECK(ground.band == 0 && grid.coverage[17] == 1 && grid.coverage[18] == 1);
    ground.position_x = 96 << 16;
    update_area_coverage(ground, context);
    CHECK(grid.coverage[17] == 1 && grid.coverage[18] == 2 && grid.coverage[19] == 1);

    // Sight under 32 selects band 0, which has no ray table: only the cell counts.
    auto low = make_grid(5, 5, 1);
    auto low_context = make_context(low, others, {}, update_sight_grid | altitude_sight_algorithm);
    low_context.altitude = &altitude;
    SightStamp short_sight{1, 0, 0, 31, 2, 0, 64 << 16, 8 << 16, 128 << 16};
    refresh_area_coverage(short_sight, low_context);
    CHECK(low.coverage[17] == 1 && low.coverage[18] == 0);
    // Past the table the clamp stops one entry short: the last table (here
    // the empty one) is never read.
    SightStamp long_sight = short_sight;
    long_sight.sight_distance = 1000;
    long_sight.center_x = 0;
    refresh_area_coverage(long_sight, low_context);
    CHECK(low.coverage[17] == 2 && low.coverage[18] == 1);
}

void unit_stamps() {
    const SightMask masks[1]{{1, 1, 0, 0, 0, {1}}};
    auto grid = make_grid(4, 3, 0);
    std::vector<std::vector<uint8_t>> others;
    auto context = make_context(grid, others, masks, update_sight_grid | terrain_mapping);
    context.minimum_height_cell = 3;
    oa::Player owner{};
    owner.index = 4;
    oa::UnitDef def{};
    def.sight_distance = 200;
    def.model_height = 0x00050000;
    oa::Unit unit{};
    unit.position = {2 * 0x200000, 0, 1 * 0x200000};
    unit.sight_center_x = 0x7777;
    unit.sight_band = 9;
    stamp_unit_sight(unit, def, owner, context);
    // The Y floor is four units, which is under one row.
    CHECK(unit.sight_center_x == 2 && unit.sight_center_z == 1 && unit.sight_band == 0);
    CHECK(others[4][6] == 1 && grid.player_bits[6] == 16 && grid.coverage[6] == 0);
    CHECK(unit.position.y == 0);
    unit.position.x = 3 * 0x200000;
    move_unit_sight(unit, def, owner, context);
    CHECK(
        unit.sight_center_x == 3 && others[4][6] == 0 && others[4][7] == 1 &&
        grid.player_bits[7] == 16
    );
    // Death takes the count back and leaves the stamp words and mapped bits.
    unit.position.x = 0;
    clear_unit_sight(unit, def, owner, context);
    CHECK(others[4][7] == 0 && unit.sight_center_x == 3 && grid.player_bits[7] == 16);
}

void remembered_sight() {
    const SightMask masks[1]{{1, 1, 0, 0, 0, {1}}};
    auto grid = make_grid(4, 3, 2);
    std::vector<std::vector<uint8_t>> others;
    auto context = make_context(grid, others, masks, terrain_mapping);
    context.minimum_height_cell = 2;
    EyeballMemory memory;
    memory.slots[0].stamp.center_x = 99;
    const oa::FixedVec3 at{3 * 0x200000, 1, 2 * 0x200000};
    remember_sight(memory, at, 160, 9, 0x20, 0xfffffff0U, context);
    CHECK(memory.count == 0 && memory.slots[0].stamp.center_x == 99);

    context.visibility_flags = update_sight_grid;
    remember_sight(memory, at, 160, 9, 0x20, 0xfffffff0U, context);
    const auto& first = memory.slots[0];
    CHECK(memory.count == 1 && first.stamp.owner == 2 && first.stamp.model_height == 9);
    CHECK(first.stamp.position_y == 3 * 65536 && first.expiry == 0x10);
    CHECK(first.stamp.center_x == 3 && first.stamp.center_z == 2 && first.stamp.band == 0);
    CHECK(grid.coverage[11] == 1 && grid.player_bits[11] == 4);

    remember_sight(memory, {0, 5 * 65536, 0}, 160, 1, 1, 10, context);
    remember_sight(memory, {0x200000, 0, 0}, 160, 1, 90, 10, context);
    CHECK(memory.count == 3 && memory.slots[1].expiry == 11 && memory.slots[2].expiry == 100);
    CHECK(grid.coverage[0] == 1 && grid.coverage[1] == 1);

    // An expiry equal to the tick is still live.
    CHECK(
        !remembered_sight_expired(memory.slots[1], 11) &&
        remembered_sight_expired(memory.slots[1], 12)
    );
    expire_remembered_sight(memory, 11, true, context);
    CHECK(memory.count == 3 && grid.coverage[0] == 1);
    // The first two lapse; the third moves down and the stale copy stays.
    expire_remembered_sight(memory, 50, true, context);
    CHECK(
        memory.count == 1 && memory.slots[0].expiry == 100 && memory.slots[0].stamp.center_x == 1
    );
    CHECK(memory.slots[2].expiry == 100);
    CHECK(grid.coverage == std::vector<uint8_t>({0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
    CHECK(grid.player_bits[11] == 4);

    // Compaction keeps the live records in order.
    EyeballMemory list;
    const uint32_t expiries[5]{5, 100, 3, 200, 7};
    for (uint32_t i = 0; i < 5; ++i) {
        list.slots[i].expiry = expiries[i];
        list.slots[i].stamp.sight_distance = static_cast<int16_t>(i);
    }
    list.count = 5;
    CHECK(compact_remembered_sight(list, 10) == 2);
    CHECK(list.slots[0].stamp.sight_distance == 1 && list.slots[1].stamp.sight_distance == 3);
    CHECK(list.slots[2].stamp.sight_distance == 2 && list.count == 5);
    CHECK(compact_remembered_sight(list, 0) == 5);

    // A full memory, or a corrupt negative count, takes nothing.
    memory.count = eyeball_capacity;
    remember_sight(memory, at, 160, 0, 1, 0, context);
    CHECK(memory.count == eyeball_capacity);
    memory.count = -1;
    remember_sight(memory, at, 160, 0, 1, 0, context);
    CHECK(memory.count == -1);
}

void remembered_altitude_sight() {
    std::vector<AltitudeCell> heights(25, AltitudeCell{20, 21});
    AltitudeSightPattern radius_one{{SightRay{{{{1, 0}}, {{2, 0}}}}}};
    std::vector<AltitudeSightPattern> patterns{radius_one, {}};
    AltitudeSightData altitude{5, 5, heights, patterns};
    auto grid = make_grid(5, 5, 1);
    std::vector<std::vector<uint8_t>> others;
    auto context = make_context(
        grid, others, {}, update_sight_grid | altitude_sight_algorithm | terrain_mapping
    );
    context.altitude = &altitude;
    // A reused record keeps its old cell: altitude 0 + 2 is within 6 of the
    // cleared band there, so nothing is stamped.
    EyeballMemory memory;
    memory.slots[0].stamp.center_x = 2;
    memory.slots[0].stamp.center_z = 3;
    memory.slots[0].stamp.band = 9;
    remember_sight(memory, {64 << 16, 0, 128 << 16}, 32, 2, 60, 10, context);
    CHECK(
        memory.count == 1 && memory.slots[0].stamp.band == 0 &&
        memory.slots[0].stamp.position_y == 65536
    );
    CHECK(memory.slots[0].expiry == 70 && grid.coverage == std::vector<uint8_t>(25));
    remember_sight(memory, {64 << 16, 0, 128 << 16}, 32, 2, 60, 10, context);
    CHECK(memory.count == 2 && memory.slots[1].stamp.band == 3);
    CHECK(
        grid.coverage[17] == 1 && grid.coverage[18] == 1 && grid.coverage[19] == 0 &&
        grid.player_bits[17] == 2
    );
}

void context_errors() {
    const SightMask short_mask[1]{{2, 2, 0, 0, 0, {1}}};
    auto grid = make_grid(4, 3, 0);
    std::vector<std::vector<uint8_t>> others;
    auto context = make_context(grid, others, {}, update_sight_grid);
    CHECK(std::string_view(sight_context_error(context)) == "sight mask table is empty");
    context.masks = short_mask;
    CHECK(std::string_view(sight_context_error(context)) == "sight mask pixels are truncated");
    const SightMask mask[1]{{1, 1, 0, 0, 0, {1}}};
    context.masks = mask;
    std::vector<uint8_t> small(3);
    context.coverage[7] = small;
    CHECK(std::string_view(sight_context_error(context)) == "coverage grid dimensions are invalid");
    context.coverage[7] = {};
    CHECK(sight_context_error(context) == nullptr);
    std::vector<AltitudeCell> heights(12);
    std::vector<AltitudeSightPattern> one{AltitudeSightPattern{}};
    AltitudeSightData altitude{4, 3, heights, one};
    context.visibility_flags = altitude_sight_algorithm;
    context.altitude = &altitude;
    CHECK(
        std::string_view(sight_context_error(context)) ==
        "altitude sight requires at least two table entries"
    );
    AltitudeSightData wrong_size{5, 3, heights, one};
    context.altitude = &wrong_size;
    CHECK(
        std::string_view(sight_context_error(context)) ==
        "altitude and sight grid dimensions differ"
    );
}
} // namespace

int main() {
    std::vector<TerrainCell> cells(9);
    for (unsigned i = 0; i < 9; ++i)
        cells[i].movement_cost = static_cast<uint8_t>(i);
    std::vector<uint8_t> flat_heights(16, 12);
    const auto built_heights = build_altitude_cells(flat_heights, 4, 4, 3);
    CHECK(built_heights.size() == 4);
    for (const auto cell : built_heights)
        CHECK(cell.high_height >= cell.low_height);
    const std::string_view los_line = "2, 1, 2, 3, 4";
    const auto built_pattern = build_altitude_pattern(std::span(&los_line, 1));
    CHECK(
        built_pattern.rays.size() == 4 && built_pattern.rays[0].offsets[0][0] == 1 &&
        built_pattern.rays[0].offsets[0][1] == -2
    );
    CHECK(built_pattern.rays[1].offsets[1][0] == 4 && built_pattern.rays[1].offsets[1][1] == 3);
    const std::string_view signed_los_line = "+1, +2, -3";
    const auto signed_pattern = build_altitude_pattern(std::span(&signed_los_line, 1));
    CHECK(signed_pattern.rays[0].offsets[0][0] == 2 && signed_pattern.rays[0].offsets[0][1] == 3);
    TerrainGrid terrain{3, 3, cells};
    SpeedUnit speed{2.5F, 1, 1, 2, 2, 0, true};
    SpeedRecorder h;
    auto r = initialize_terrain_speed(speed, terrain, &h);
    CHECK(r.updated && r.terrain_sum == 28 && speed.speed == 70 && h.speed == 28);
    std::vector<TerrainCell> huge(256, TerrainCell{255});
    TerrainGrid large{256, 1, huge};
    SpeedUnit wrapped{1.0F, 0, 0, static_cast<int16_t>(256), 1, 0, true};
    r = initialize_terrain_speed(wrapped, large, &h);
    CHECK(r.terrain_sum == 0 && wrapped.speed == 0 && h.speed == 0);
    huge.resize(129);
    large = {129, 1, huge};
    wrapped.footprint_x = 129;
    r = initialize_terrain_speed(wrapped, large, &h);
    CHECK(r.terrain_sum == 33024 && wrapped.speed == -32512 && h.speed == -32512);
    SpeedUnit stopped;
    CHECK(!initialize_terrain_speed(stopped, terrain, &h).updated);
    standard_stamps();
    moving_stamps();
    altitude_stamps();
    unit_stamps();
    remembered_sight();
    remembered_altitude_sight();
    context_errors();
}
