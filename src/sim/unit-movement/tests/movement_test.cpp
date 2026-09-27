// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_movement/movement.hpp"
#include "oa/sim/unit_movement/terrain.hpp"
#include "oa/base/game_math.hpp"
#include <bit>
#include <cstdlib>
#include <iostream>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "failed: " #x << "\n";                                                    \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
#include <stdexcept>
using namespace oa::sim::unit_movement;

struct Fixture : Host {
    bool allowed = true;
    unsigned calls = 0;

    bool can_occupy(const Unit&, std::array<int16_t, 2>, uint8_t) override {
        calls = calls * 10 + 1;
        return allowed;
    }

    void remove_occupancy(Unit&) override { calls = calls * 10 + 2; }

    void insert_occupancy(Unit&) override { calls = calls * 10 + 3; }

    void update_spatial_membership(Unit&) override { calls = calls * 10 + 4; }
};

int main() {
    Unit u;
    Movement m;
    Fixture h;
    CHECK(sine_scaled(0, 65536) == 0);
    CHECK(cosine_scaled(0, 65536) == 65536);
    u.type.maximum_turn = 100;
    u.heading = 65500;
    turn(u, m, 200);
    CHECK(u.heading == 64 && m.turn == 100 && u.flags == position_dirty);
    u.flags = 0;
    turn(u, m, 0);
    CHECK(u.flags == 0 && m.turn == 0);
    u.type.maximum_speed = 65536;
    u.heading = 0;
    accelerate(u, m, 131072, 0);
    CHECK(m.speed == 65536 && m.velocity[2] == -65536);
    accelerate(u, m, 0, 1);
    CHECK(m.speed == 32768);
    CHECK(braking_distance(65536, 32768) == 65536);
    bool threw = false;
    try {
        (void)braking_distance(1, 0);
    } catch (const std::domain_error&) {
        threw = true;
    }
    CHECK(threw);
    u = {};
    m = {};
    u.type.maximum_speed = 65536;
    u.footprint = {1, 1};
    u.position = {524288, 0, 524288};
    m.velocity = {65536, 0, 0};
    integrate_unattached(u, m, 7, true, h);
    CHECK(u.position[0] == 589824 && m.last_motion_tick == 7 && h.calls == 0);
    m.velocity = {1048576, 0, 0};
    h.allowed = false;
    m.speed = 65536;
    integrate_unattached(u, m, 8, true, h);
    CHECK(h.calls == 1 && u.cell[0] == 0 && u.position[0] == 1048575 && m.speed == 32768);
    h.allowed = true;
    h.calls = 0;
    m.velocity = {1048576, 0, 0};
    integrate_unattached(u, m, 9, true, h);
    CHECK(h.calls == 1234 && u.cell[0] == 1 && u.position[0] == 2097151);
    u = {};
    m = {};
    h.calls = 0;
    u.footprint = {1, 1};
    u.position = {524288, 0, 524288};
    u.flags = 1;
    write_position(u, m, {589824, 16, 524288}, 1, h);
    CHECK(h.calls == 0 && u.position[0] == 589824 && (u.flags & position_dirty) != 0);
    h.calls = 0;
    u.flags = 1;
    write_position(u, m, {0x180000, 0, 524288}, 1, h);
    CHECK(h.calls == 234 && u.cell[0] == 1 && (u.flags & occupancy_mask) == 1);
    const auto toward = oa::base::game_math::direction(0, 1);
    CHECK(facing_toward(0, 0, 0, 0x100000, toward));
    CHECK(!facing_toward(0, 0, 0, 0x100000, static_cast<uint16_t>(toward + 0x8000)));
    auto aimed = aim_velocity(0, 0, 65536);
    CHECK(aimed[0] == 0 && aimed[1] == 0 && aimed[2] == -65536);
    aimed = aim_velocity(0x4000, 0, 65536);
    CHECK(aimed[0] == -65536 && aimed[1] == 0 && aimed[2] == 0);
    aimed = aim_velocity(0, 0x4000, 65536);
    CHECK(aimed[0] == 0 && aimed[1] == 65536 && aimed[2] == 0);
    constexpr uint16_t heading = 0x1234, pitch = 0x0555;
    constexpr Fixed magnitude = -80000;
    aimed = aim_velocity(heading, pitch, magnitude);
    const auto level = cosine_scaled(pitch, magnitude);
    const auto neg = [](Fixed value) {
        return std::bit_cast<Fixed>(0u - static_cast<uint32_t>(value));
    };
    CHECK(aimed[1] == sine_scaled(pitch, magnitude));
    CHECK(aimed[0] == neg(sine_scaled(heading, level)));
    CHECK(aimed[2] == neg(cosine_scaled(heading, level)));
    const std::array<Fixed, 3> origin{0, 0, 0};
    auto angles = aim_angles(origin, {0, 0, 65536});
    CHECK(angles.heading == 0x8000 && angles.pitch == 0);
    angles = aim_angles(origin, {65536, 0, 0});
    CHECK(angles.heading == 0xc000 && angles.pitch == 0);
    angles = aim_angles(origin, {0, 65536, 0});
    CHECK(angles.pitch == 16384);
    angles = aim_angles(origin, {0, 65536, 65536});
    CHECK(angles.heading == 0x8000);
    CHECK(angles.pitch == oa::base::game_math::direction(1, 1));
    // Only the high word of the Y delta is kept, so -1 and -65536 match.
    CHECK(aim_angles(origin, {0, 1, 65536}).pitch == angles.pitch);
    angles = aim_angles({65536, 131072, 0}, {65536, 65536, 65536});
    CHECK(angles.heading == 0x8000);
    CHECK(angles.pitch == oa::base::game_math::direction(-1, 1));

    auto flat = [](uint8_t height) {
        oa::formats::tnt::Map map;
        map.attribute_width = 2;
        map.attribute_height = 2;
        map.attributes.assign(4, {});
        map.sea_level = 90;
        for (auto& attribute : map.attributes)
            attribute.height = height;
        return map;
    };
    const auto below_map = flat(40);
    const auto equal_map = flat(90);
    const auto above_map = flat(140);
    const Terrain below(below_map);
    const Terrain equal(equal_map);
    const Terrain above(above_map);
    CHECK(surface_height(below, 0, 0) == 90);
    CHECK(surface_height(below, 7 * 65536 + 0x8000, 3 * 65536) == 90);
    CHECK(surface_height(below, -1, 0) == 90);
    CHECK(surface_height(equal, 4 * 65536, 4 * 65536) == 90);
    CHECK(surface_height(above, 65536, 2 * 65536) == 140);
}
