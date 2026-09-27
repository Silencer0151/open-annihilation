// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/ground_orders/movement_map.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace oa::sim::ground_orders;

namespace {
int failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

struct GridSampler : MovementMapSampler {
    uint32_t width{}, height{};
    std::vector<uint8_t> cells;

    uint8_t classify_cell(int32_t x, int32_t z) override {
        if (x < 0 || z < 0 || static_cast<uint32_t>(x) >= width ||
            static_cast<uint32_t>(z) >= height)
            return 0;
        return cells[static_cast<std::size_t>(z) * width + static_cast<uint32_t>(x)];
    }
};

GridSampler clear_grid(uint32_t width, uint32_t height) {
    GridSampler sampler;
    sampler.width = width;
    sampler.height = height;
    sampler.cells.assign(static_cast<std::size_t>(width) * height, 3);
    return sampler;
}

bool matches(const MovementMap& map, const std::vector<uint8_t>& expected) {
    bool same = true;
    for (uint32_t z = 0; z < map.height(); ++z)
        for (uint32_t x = 0; x < map.width(); ++x)
            if (map.cell(x, z) != expected[static_cast<std::size_t>(z) * map.width() + x]) {
                std::fprintf(
                    stderr,
                    "cell (%u,%u) = %u, expected %u\n",
                    x,
                    z,
                    map.cell(x, z),
                    expected[static_cast<std::size_t>(z) * map.width() + x]
                );
                same = false;
            }
    return same;
}

// An open map keeps class 3 inside; the outer ring borders the zero padding.
void test_open_map_keeps_interior_clear() {
    auto sampler = clear_grid(7, 7);
    MovementMap map(7, 7, 1, 1, sampler);
    map.rebuild();
    std::vector<uint8_t> expected(49, 3);
    for (uint32_t i = 0; i < 7; ++i)
        expected[i] = expected[42 + i] = expected[i * 7] = expected[i * 7 + 6] = 1;
    CHECK(matches(map, expected));
}

// The grid packs sixteen rows per word: a 17-row map keeps its last row in a
// second band of words, and a map with a zero side has no grid at all.
void test_grid_bands_of_sixteen_rows() {
    auto sampler = clear_grid(3, 17);
    MovementMap map(3, 17, 1, 1, sampler);
    map.rebuild();
    CHECK(map.cell(1, 0) == 1 && map.cell(1, 15) == 3 && map.cell(1, 16) == 1);
    auto empty = clear_grid(0, 5);
    MovementMap none(0, 5, 1, 1, empty);
    CHECK(none.width() == 0 && none.height() == 5 && none.cell(0, 0) == 0);
}

// A blocked cell drops its eight neighbours to class 1 across both passes.
void test_blocked_cell_degrades_neighbours() {
    auto sampler = clear_grid(7, 7);
    sampler.cells[3 * 7 + 3] = 0;
    MovementMap map(7, 7, 1, 1, sampler);
    map.rebuild();
    std::vector<uint8_t> expected(49, 3);
    for (uint32_t z = 0; z < 7; ++z)
        for (uint32_t x = 0; x < 7; ++x) {
            const bool ring = x == 0 || x == 6 || z == 0 || z == 6;
            const bool near = x >= 2 && x <= 4 && z >= 2 && z <= 4;
            expected[z * 7 + x] = ring || near ? 1 : 3;
        }
    expected[3 * 7 + 3] = 0;
    CHECK(matches(map, expected));
}

// A 2x2 footprint takes the window minimum: the last column and row cannot
// hold the footprint, and the columns and rows before them border the padding.
void test_footprint_window_minimum() {
    auto sampler = clear_grid(6, 4);
    MovementMap map(6, 4, 2, 2, sampler);
    map.rebuild();
    const std::vector<uint8_t> expected{
        1, 1, 1, 1, 1, 0, //
        1, 3, 3, 3, 1, 0, //
        1, 1, 1, 1, 1, 0, //
        0, 0, 0, 0, 0, 0, //
    };
    CHECK(matches(map, expected));
}

// The running minimum is rescanned only when the cell leaving a window was the
// minimum: with a 2-wide window over [3,1,3,2,3,3,3,3] the minimum follows the
// band, and the first clear window after it still touches class 2 on its left.
void test_running_minimum_rescans() {
    auto sampler = clear_grid(8, 3);
    const uint8_t band[8] = {3, 1, 3, 2, 3, 3, 3, 3};
    for (uint32_t x = 0; x < 8; ++x)
        sampler.cells[8 + x] = band[x];
    MovementMap map(8, 3, 2, 1, sampler);
    map.rebuild();
    const std::vector<uint8_t> middle{1, 1, 2, 2, 1, 3, 1, 0};
    for (uint32_t x = 0; x < 8; ++x)
        CHECK(map.cell(x, 1) == middle[x]);
    // The clear rows above and below only lose their padding-bordered ends.
    const std::vector<uint8_t> clear_row{1, 1, 1, 1, 1, 1, 1, 0};
    for (uint32_t x = 0; x < 8; ++x) {
        CHECK(map.cell(x, 0) == clear_row[x]);
        CHECK(map.cell(x, 2) == clear_row[x]);
    }
}

// rebuild reads the single-plot class; refresh keeps reading the footprint class.
void test_plot_sampler_is_separate() {
    struct SplitSampler : MovementMapSampler {
        uint8_t classify_cell(int32_t, int32_t) override { return 1; }

        uint8_t classify_plot(int32_t, int32_t) override { return 3; }
    } sampler;

    MovementMap map(3, 3, 1, 1, sampler);
    map.rebuild();
    CHECK(map.cell(1, 1) == 3);
    CHECK(map.cell(0, 0) == 1);
    map.refresh({0, 2u | (2u << 16)});
    CHECK(map.cell(1, 1) == 1);
}

// prepare_search moves the projection to 30 ticks before now. The searching unit's
// tick reads as now while its rectangle is reclassified (only when the old
// projection held it as a wall) and is restored afterwards; live units whose
// tick fell in [old, new) are reclassified, dead or other units are not.
void test_prepare_search_moves_projection() {
    struct TickSampler : MovementMapSampler {
        const uint32_t* searcher_tick{};
        uint32_t seen_tick{};
        unsigned samples{};

        uint8_t classify_cell(int32_t, int32_t) override {
            ++samples;
            seen_tick = *searcher_tick;
            return 3;
        }
    } sampler;

    uint32_t searcher_tick = 5;
    sampler.searcher_tick = &searcher_tick;
    const auto at = [](uint32_t x, uint32_t z) { return x | z << 16; };
    const OccupancyRectangle active{at(1, 1), at(1, 1)};
    const OccupancyChange units[] = {
        {{at(4, 4), at(1, 1)}, 10, true},
        {{at(7, 7), at(1, 1)}, 80, true},
        {{at(7, 1), at(1, 1)}, 20, false},
    };
    MovementMap map(10, 10, 1, 1, sampler);
    // A one-cell rectangle under a one-cell class refreshes a 3x3 block.
    map.prepare_search(active, searcher_tick, units, 100);
    CHECK(map.projection_tick() == 70 && sampler.samples == 9 && sampler.seen_tick == 100);
    CHECK(searcher_tick == 5);
    map.prepare_search(active, searcher_tick, units, 200);
    CHECK(map.projection_tick() == 170 && sampler.samples == 27 && sampler.seen_tick == 200);
    CHECK(searcher_tick == 5);
    // An unchanged projection skips the unit list but still frees the searcher.
    map.prepare_search(active, searcher_tick, units, 200);
    CHECK(sampler.samples == 36);
    MovementMap early(10, 10, 1, 1, sampler);
    early.prepare_search(active, searcher_tick, units, 30);
    CHECK(early.projection_tick() == 0 && sampler.samples == 36);
    early.prepare_search(active, searcher_tick, units, 31);
    CHECK(early.projection_tick() == 1 && sampler.samples == 36);
}

// prepare_search and the job start pass the unit's cell and footprint unchanged: the
// extent is the footprint, so a refresh reaches the anchor just past the far edge.
void test_occupancy_rectangle_carries_footprint() {
    oa::Unit unit{};
    unit.cell_x = 5;
    unit.cell_z = -2;
    unit.footprint_x = 3;
    unit.footprint_z = 2;
    const auto rectangle = occupancy_rectangle(unit);
    CHECK(rectangle.origin == (5u | 0xfffeu << 16) && rectangle.extent == (3u | 2u << 16));
    auto sampler = clear_grid(12, 12);
    MovementMap map(12, 12, 2, 2, sampler);
    unit.cell_z = 4;
    sampler.cells[4 * 12 + 7] = 0;
    map.refresh(occupancy_rectangle(unit));
    // Anchors 3..8 by 2..6, from the class's two cells before the unit through
    // the cell after its footprint; the unbuilt map keeps 0 elsewhere.
    CHECK(map.cell(8, 6) == 3 && map.cell(3, 2) == 3 && map.cell(7, 4) == 0);
    CHECK(map.cell(9, 4) == 0 && map.cell(2, 4) == 0 && map.cell(5, 7) == 0);
}

} // namespace

int main() {
    test_open_map_keeps_interior_clear();
    test_grid_bands_of_sixteen_rows();
    test_blocked_cell_degrades_neighbours();
    test_footprint_window_minimum();
    test_running_minimum_rescans();
    test_plot_sampler_is_separate();
    test_prepare_search_moves_projection();
    test_occupancy_rectangle_carries_footprint();
    if (failures)
        std::fprintf(stderr, "%d movement map checks failed\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
