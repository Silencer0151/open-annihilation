// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_movement/terrain.hpp"
#include <cstdlib>
#include <iostream>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "failed: " #x << "\n";                                                    \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
using namespace oa::sim::unit_movement;

int main() {
    CHECK(scaled_bob_tick(1000, 30) == 30);
    CHECK(scaled_bob_tick(0xffffffffu, 30) == 4294967);
    oa::formats::tnt::Map map;
    map.attribute_width = 3;
    map.attribute_height = 3;
    map.attributes.resize(9);
    map.sea_level = 100;
    for (std::size_t z = 0; z < 3; ++z)
        for (std::size_t x = 0; x < 3; ++x)
            map.attributes[z * 3 + x].height = static_cast<uint8_t>(x * 16 + z * 32);
    Terrain terrain(map);
    CHECK(terrain.height(8 * 65536, 8 * 65536) == 24);
    CHECK(terrain.height(-1, 0) == -1);
    CHECK(terrain.height(32 * 65536, 0) == -1);
    GroundPose p;
    p.position = {16 * 65536, 123, 16 * 65536};
    GroundQuad quad{
        {{-4 * 65536, 4 * 65536},
         {4 * 65536, 4 * 65536},
         {4 * 65536, -4 * 65536},
         {-4 * 65536, -4 * 65536}}
    };
    CHECK(fit_ground(terrain, quad, p, {}, {}));
    CHECK((uint32_t(p.position[1]) & 65535) == 123);
    CHECK(p.position[1] >> 16 == 48);
    CHECK(p.pitch > 0 && p.roll < 0);
    auto copy = p;
    CHECK(!fit_ground(terrain, std::nullopt, p, {}, {}));
    CHECK(p.position == copy.position);
    p.position[0] = 0;
    CHECK(!fit_ground(terrain, quad, p, {}, {}));
    CHECK(p.position[1] == copy.position[1]);
    oa::formats::objects3d::Model model;
    CHECK(!ground_quad(model));
    model.objects.resize(1);
    auto& root = model.objects[0];
    root.selection_primitive = 0;
    root.primitives.resize(1);
    root.primitives[0].vertex_indices = {0, 1, 2, 3};
    root.vertices = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {10, 11, 12}};
    auto fitted = ground_quad(model);
    CHECK(fitted && (*fitted)[0][0] == -1 && (*fitted)[3][1] == -12);
    root.primitives[0].vertex_indices.pop_back();
    CHECK(!ground_quad(model));
    // A grid smaller than 2x2 gives an empty view.
    oa::formats::tnt::Map tiny;
    tiny.attribute_width = 1;
    tiny.attribute_height = 1;
    tiny.attributes.resize(1);
    const Terrain empty(tiny);
    CHECK(!terrain_grid_valid(tiny) && !empty.holds_grid());
    CHECK(empty.height(0, 0) == -1 && empty.sea_level() == 0);
}
