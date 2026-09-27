// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Test world and recording AirHost shared by the air handler tests.

#include "oa/sim/air/driver.hpp"
#include "oa/sim/air/host.hpp"
#include "oa/sim/air/orders.hpp"

#include <cstdio>
#include <cstring>
#include <initializer_list>

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

constexpr oa_fixed world(int32_t units) {
    return static_cast<oa_fixed>(static_cast<uint32_t>(units) << 16);
}

// A small world with one flying def (index 1), one ground def (index 2) and
// a repair pad def (index 3), plus a scripted host that records its calls.
struct Fixture {
    World* state{};
    AirDriver drivers[8]{};
    uint8_t layers[8]{};
    uint32_t random_values[16]{};
    uint32_t random_count{};
    uint32_t random_next{};
    uint32_t random_limits[16]{};
    int32_t terrain{10};
    bool outside{};
    bool can_land{true};
    uint32_t speeches{};
    uint32_t last_speech{};
    uint32_t weapon_resets{};
    uint32_t weapon_enables{};
    uint32_t layer_changes{};
    char pushed[32]{};
    Unit* pushed_target{};
    bool pushed_append{};
    FixedVec3 pushed_point{};
    Unit* aimed_unit{};
    FixedVec3 aimed_point{};
    uint32_t aims{};
    FixedVec3 velocities[8]{};
    uint32_t scripts{};
    AirHost host{};

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
        UnitDef& pad = state->unit_defs[2];
        pad.flags = OA_UNIT_DEF_FLAG_BUILDER | OA_UNIT_DEF_FLAG_IS_AIRBASE;
        state->game.players[0].first_unit = oa_unit_ref_from_slot(1);
        state->game.players[0].last_unit = oa_unit_ref_from_slot(6);
        for (uint32_t slot = 1; slot < 8; ++slot) {
            Unit& unit = state->units[slot];
            unit.id = static_cast<uint16_t>(slot);
            unit.type_index = 1;
            unit.def = oa_ref_from_index(0);
            unit.movement = 1;
            unit.owner_index = 0;
            layers[slot] = layer_ground;
        }
        host.context = this;
        host.world = state;
        host.bucket_height = [](void*, const Unit*) -> uint8_t { return 20; };
        host.outside_map = [](void* context, const Unit*) { return self(context).outside; };
        host.terrain_height = [](void* context, oa_fixed, oa_fixed) {
            return self(context).terrain;
        };
        host.driver = [](void* context, Unit* unit) -> AirDriver* {
            return &self(context).drivers[unit->id];
        };
        host.movement_layer = [](void* context, const Unit* unit) -> uint8_t {
            return self(context).layers[unit->id];
        };
        host.set_movement_layer = [](void* context, Unit* unit, uint8_t layer) {
            self(context).layers[unit->id] = layer;
            ++self(context).layer_changes;
        };
        host.random = [](void* context, uint32_t limit) {
            Fixture& f = self(context);
            f.random_limits[f.random_next % 16] = limit;
            const uint32_t value =
                f.random_next < f.random_count ? f.random_values[f.random_next] : 0;
            ++f.random_next;
            return value % (limit == 0 ? 1 : limit);
        };
        host.speech = [](void* context, Unit*, uint32_t category, const char*) {
            ++self(context).speeches;
            self(context).last_speech = category;
        };
        host.reset_weapons = [](void* context, Unit*, uint8_t) { ++self(context).weapon_resets; };
        host.enable_weapons = [](void* context, Unit*, uint8_t) { ++self(context).weapon_enables; };
        host.script_start = [](void* context, Unit*, const char*, int32_t, bool) {
            ++self(context).scripts;
        };
        host.push_order = [](void* context,
                             Unit*,
                             bool append,
                             const char* mission,
                             Unit* target,
                             const FixedVec3* point,
                             int32_t,
                             int32_t,
                             int32_t) {
            Fixture& f = self(context);
            std::strncpy(f.pushed, mission, sizeof(f.pushed) - 1);
            f.pushed_target = target;
            f.pushed_append = append;
            f.pushed_point = point != nullptr ? *point : FixedVec3{};
        };
        host.aim_at_unit = [](void* context, Unit*, Unit* target, uint8_t) {
            ++self(context).aims;
            self(context).aimed_unit = target;
        };
        host.aim_at_point = [](void* context, Unit*, const FixedVec3* point, uint8_t) {
            ++self(context).aims;
            self(context).aimed_point = *point;
        };
        host.movement_velocity = [](void* context, const Unit* unit) {
            return self(context).velocities[unit->id];
        };
        host.can_land_at = [](void* context, Unit*, const FixedVec3*) {
            return self(context).can_land;
        };
    }

    ~Fixture() { world_destroy(state); }

    static Fixture& self(void* context) { return *static_cast<Fixture*>(context); }

    Unit& unit(uint32_t slot) { return state->units[slot]; }

    void randoms(std::initializer_list<uint32_t> values) {
        random_count = 0;
        random_next = 0;
        for (auto value : values)
            random_values[random_count++] = value;
    }
};

struct OrderFixture {
    uint32_t events{};
    AirGoal goal{};
    AirOrder order{};

    explicit OrderFixture(Unit* unit) {
        order.unit = unit;
        order.events = &events;
        order.goal = &goal;
    }
};

inline int finish(const char* name) {
    if (failures != 0) {
        std::fprintf(stderr, "%d %s checks failed\n", failures, name);
        return 1;
    }
    std::printf("%s: all checks passed\n", name);
    return 0;
}

} // namespace air_test
