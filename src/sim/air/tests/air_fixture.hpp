// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Test world and AirHost shared by the air goal, driver and flight tests.

#include "oa/sim/air/driver.hpp"
#include "oa/sim/air/host.hpp"

#include <cstdio>

namespace air_test {

inline int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

using namespace oa;
using namespace oa::sim::air;

/// Returns a whole number of world units as a 16.16 fixed-point coordinate.
constexpr oa_fixed world(int32_t units) {
    return static_cast<oa_fixed>(static_cast<uint32_t>(units) << 16);
}

// A small world with one flying def (index 1) and one ground def (index 2),
// and a host whose terrain and off-map answers the tests set.
struct Fixture {
    World* state{};
    AirDriver drivers[8]{};
    int32_t terrain{10};
    bool outside{};
    AirHost host{};

    /// Creates the world, its two unit types and seven units of the flying type,
    /// and points the host's terrain and off-map answers at this fixture.
    Fixture() {
        state = world_create();
        WorldCapacity capacity{8, 4, 1};
        world_alloc_tables(state, &capacity);
        state->game.sea_level = 5;
        state->game.map_pixel_width = 2048;
        state->game.map_pixel_height = 1024;
        state->game.tick = 100;
        UnitDef& flyer = state->unit_defs[0];
        flyer.flags = OA_UNIT_DEF_FLAG_CAN_FLY;
        flyer.cruise_alt = 80;
        flyer.turn_rate = 800;
        flyer.acceleration = 0x2000;
        flyer.max_velocity = 0x80000;
        flyer.brake_rate = 0x60000;
        flyer.bank_scale = 0x10000;
        flyer.pitch_scale = 0x10000;
        UnitDef& tank = state->unit_defs[1];
        tank.flags = 0;
        for (uint32_t slot = 1; slot < 8; ++slot) {
            Unit& unit = state->units[slot];
            unit.id = static_cast<uint16_t>(slot);
            unit.type_index = 1;
            unit.def = oa_ref_from_index(0);
            unit.movement = 1;
            unit.owner_index = 0;
        }
        host.context = this;
        host.world = state;
        host.bucket_height = [](void*, const Unit*) -> uint8_t { return 20; };
        host.outside_map = [](void* context, const Unit*) { return self(context).outside; };
        host.terrain_height = [](void* context, oa_fixed, oa_fixed) {
            return self(context).terrain;
        };
    }

    /// Destroys the world.
    ~Fixture() { world_destroy(state); }

    /// Returns the fixture a host callback's context points at.
    static Fixture& self(void* context) { return *static_cast<Fixture*>(context); }

    /// Returns the unit in a slot.
    Unit& unit(uint32_t slot) { return state->units[slot]; }
};

/// Prints whether a test's checks passed.
///
/// @param name the test's name, for the message
/// @return 0 when every check passed, else 1
inline int finish(const char* name) {
    if (failures != 0) {
        std::fprintf(stderr, "%d %s checks failed\n", failures, name);
        return 1;
    }
    std::printf("%s: all checks passed\n", name);
    return 0;
}

} // namespace air_test
