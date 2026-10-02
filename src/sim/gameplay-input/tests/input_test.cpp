// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/gameplay_input/input.hpp"
#include "oa/test/check.hpp"
#include <array>
#include <bit>
#include <limits>
#include <span>

int main() {
    using namespace oa::sim::gameplay_input;
    const std::array<ScreenPoint, 4> clockwise{{{0, 0}, {0, 10}, {10, 10}, {10, 0}}};
    OA_CHECK(inside_clockwise(clockwise, {5, 5}));
    OA_CHECK(!inside_clockwise(clockwise, {0, 5}));
    OA_CHECK(!inside_clockwise(clockwise, {12, 5}));
    OA_CHECK(!inside_clockwise(std::span(clockwise).first(2), {5, 5}));
    // The edge test multiplies in 32 bits: the first edge's 0x10000 * 0x8000
    // wraps negative, so the centre of this square is rejected.
    const std::array<ScreenPoint, 4> wide{{{0, 0}, {0, 0x10000}, {0x10000, 0x10000}, {0x10000, 0}}};
    OA_CHECK(!inside_clockwise(wide, {0x8000, 0x8000}));
    const std::array<ScreenPoint, 4> half{{{0, 0}, {0, 0x8000}, {0x8000, 0x8000}, {0x8000, 0}}};
    OA_CHECK(inside_clockwise(half, {0x4000, 0x4000}));
    const oa::formats::objects3d::FixedVector3 local{2 * 65536, 4 * 65536, -3 * 65536},
        world{100 * 65536, 20 * 65536, 200 * 65536};
    const auto p = project(local, world, {50, 75});
    OA_CHECK(p.x == 180 && p.y == 148);
    oa::formats::objects3d::Model model;
    model.objects.resize(1);
    model.objects[0].vertices = {
        {-10 * 65536, 0, -10 * 65536},
        {-10 * 65536, 0, 10 * 65536},
        {10 * 65536, 0, 10 * 65536},
        {10 * 65536, 0, -10 * 65536}
    };
    const PickUnit unit{7, {100 * 65536, 0, 200 * 65536}, {}, &model};
    OA_CHECK(hits_root_bounds(unit, {50, 75}, {178, 157}));
    OA_CHECK(!hits_root_bounds(unit, {50, 75}, {200, 157}));
    oa::formats::tnt::Map map;
    map.attribute_width = 4;
    map.attribute_height = 8;
    map.sea_level = 0;
    map.attributes.resize(32);
    for (auto& attribute : map.attributes)
        attribute.height = 20;
    const oa::sim::unit_movement::Terrain terrain(map);
    const auto ground = terrain_intersection(terrain, 32, 40, 64, 128);
    OA_CHECK(ground.x == 32 * 65536 && ground.y == 20 * 65536 && ground.z == 50 * 65536);
    OA_CHECK(command_binds_cursor_unit(0));
    OA_CHECK(command_binds_cursor_unit(1));
    OA_CHECK(command_binds_cursor_unit(2));
    OA_CHECK(command_binds_cursor_unit(3));
    OA_CHECK(!command_binds_cursor_unit(unload_command));
    OA_CHECK(!command_binds_cursor_unit(stop_command));
    OA_CHECK(!command_binds_cursor_unit(build_command));
    OA_CHECK(command_binds_cursor_unit(0xff));
    return oa::test::check_exit_status();
}
