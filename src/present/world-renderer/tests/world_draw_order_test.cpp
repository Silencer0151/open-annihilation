// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The order the battlefield draws features and units in: lying features
// first, then row by row the ground units and the standing features of the
// row, and the units off the ground last.
#include "oa/present/world_renderer/world_draw_order.hpp"

#include "oa/core/unit.h"

#include <cstdio>
#include <vector>

namespace wr = oa::present::world_renderer;

namespace {

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

constexpr uint32_t on_ground = 1;
constexpr uint32_t in_air = 2;
constexpr uint32_t carried = 0;
constexpr int8_t tree_height = 30;
constexpr int8_t rubble_height = 5;

[[nodiscard]] int32_t fixed(int32_t pixels) {
    return static_cast<int32_t>(static_cast<uint32_t>(pixels) << 16);
}

[[nodiscard]] wr::UnitDrawSite unit_at(int32_t z, uint32_t occupancy = on_ground) {
    return {fixed(z), occupancy | OA_UNIT_FLAG_LIVE};
}

[[nodiscard]] wr::BattlefieldDrawPlan plan(
    const std::vector<wr::FeatureDrawSite>& features,
    const std::vector<wr::UnitDrawSite>& units,
    int32_t camera_y = 0
) {
    wr::BattlefieldDrawPlan result;
    wr::plan_battlefield_draws(features, units, camera_y, result);
    return result;
}

[[nodiscard]] bool is(const wr::BattlefieldDraw& draw, wr::DrawnThing kind, uint32_t index) {
    return draw.kind == kind && draw.index == index;
}

// A tree in plot row 10 (map pixels 160 to 175) covers a unit a row behind it.
void tree_covers_unit_behind_it() {
    const auto order = plan({{10, 10, tree_height}}, {unit_at(150)});
    CHECK(order.ground.size() == 2);
    CHECK(is(order.ground[0], wr::DrawnThing::unit, 0));
    CHECK(is(order.ground[1], wr::DrawnThing::feature, 0));
}

// A unit standing in the tree's own plot row is covered by it too.
void tree_covers_unit_in_its_row() {
    const auto order = plan({{10, 10, tree_height}}, {unit_at(175)});
    CHECK(order.ground.size() == 2);
    CHECK(is(order.ground[0], wr::DrawnThing::unit, 0));
    CHECK(is(order.ground[1], wr::DrawnThing::feature, 0));
}

// A unit a row in front of the tree covers it.
void unit_in_front_covers_tree() {
    const auto order = plan({{10, 10, tree_height}}, {unit_at(176)});
    CHECK(order.ground.size() == 2);
    CHECK(is(order.ground[0], wr::DrawnThing::feature, 0));
    CHECK(is(order.ground[1], wr::DrawnThing::unit, 0));
}

// Rows are counted from the camera: with the view 14 pixels into a plot, a
// unit 2 pixels into row 11 still draws with row 10, under that row's tree.
void rows_count_from_the_camera() {
    CHECK(wr::unit_draw_row(fixed(176), 0) == 11);
    CHECK(wr::unit_draw_row(fixed(176), 14) == 10);
    CHECK(wr::unit_draw_row(fixed(190), 14) == 11);
    const auto order = plan({{10, 10, tree_height}}, {unit_at(176)}, 14);
    CHECK(is(order.ground[0], wr::DrawnThing::unit, 0));
    CHECK(is(order.ground[1], wr::DrawnThing::feature, 0));
}

// Above the camera the count truncates toward zero, so the first row above
// the view draws with the camera's own row.
void rows_above_the_camera_truncate() {
    CHECK(wr::unit_draw_row(fixed(30), 40) == 2);
    CHECK(wr::unit_draw_row(fixed(20), 40) == 1);
    CHECK(wr::unit_draw_row(fixed(-5), 0) == 0);
    CHECK(wr::unit_draw_row(fixed(-16), 0) == -1);
}

// A camera above the map counts its rows on from the map's: the rows a
// camera three rows further down counts, three fewer.
void rows_above_the_map_run_on() {
    for (const int32_t unit : {-30, -5, 18, 176})
        CHECK(wr::unit_draw_row(fixed(unit), -40) == wr::unit_draw_row(fixed(unit + 48), 8) - 3);
    CHECK(wr::unit_draw_row(fixed(-30), -40) == -3);
}

// Features below the standing height lie under every unit, row by row and
// left to right within a row, and are not in the ground pass.
void low_features_lie_under_every_unit() {
    const auto order =
        plan({{4, 12, rubble_height}, {2, 12, rubble_height}, {9, 3, rubble_height}}, {unit_at(0)});
    CHECK(order.lying_features == (std::vector<uint32_t>{2, 1, 0}));
    CHECK(order.ground.size() == 1);
    CHECK(is(order.ground[0], wr::DrawnThing::unit, 0));
    CHECK(!wr::feature_stands(9));
    CHECK(wr::feature_stands(10));
    CHECK(wr::feature_stands(-1));
}

// A row draws its units in the order given, then its standing features left
// to right; the next row follows.
void a_row_draws_units_then_features() {
    const auto order = plan(
        {{7, 5, tree_height}, {3, 5, tree_height}, {1, 6, tree_height}},
        {unit_at(100), unit_at(85), unit_at(81)}
    );
    CHECK(order.ground.size() == 6);
    CHECK(is(order.ground[0], wr::DrawnThing::unit, 1));
    CHECK(is(order.ground[1], wr::DrawnThing::unit, 2));
    CHECK(is(order.ground[2], wr::DrawnThing::feature, 1));
    CHECK(is(order.ground[3], wr::DrawnThing::feature, 0));
    CHECK(is(order.ground[4], wr::DrawnThing::unit, 0));
    CHECK(is(order.ground[5], wr::DrawnThing::feature, 2));
}

// Aircraft and carried units draw after the ground pass, row by row, even
// when they are behind a tree.
void units_off_the_ground_draw_last() {
    const auto order = plan(
        {{10, 10, tree_height}},
        {unit_at(300, in_air), unit_at(150, in_air), unit_at(150), unit_at(40, carried)}
    );
    CHECK(order.raised_units == (std::vector<uint32_t>{3, 1, 0}));
    CHECK(order.ground.size() == 2);
    CHECK(is(order.ground[0], wr::DrawnThing::unit, 2));
    CHECK(is(order.ground[1], wr::DrawnThing::feature, 0));
}

} // namespace

int main() {
    tree_covers_unit_behind_it();
    tree_covers_unit_in_its_row();
    unit_in_front_covers_tree();
    rows_count_from_the_camera();
    rows_above_the_camera_truncate();
    rows_above_the_map_run_on();
    low_features_lie_under_every_unit();
    a_row_draws_units_then_features();
    units_off_the_ground_draw_last();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
