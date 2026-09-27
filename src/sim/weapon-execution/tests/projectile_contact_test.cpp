// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/weapon_execution/projectile_contact.hpp"

#include "oa/core/unit_def.h"
#include "oa/core/weapon_def.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace oa;
using namespace oa::sim::weapon_execution;

namespace {
int failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

constexpr int32_t fx(int32_t whole) {
    return static_cast<int32_t>(static_cast<uint32_t>(whole) << 16);
}

constexpr oa_ref32 plain_def = 1;
constexpr oa_ref32 units_only_def = 2;
constexpr oa_ref32 bounce_def = 3;
constexpr oa_ref32 water_def = 4;
constexpr int32_t map_side = 4;

struct Fixture {
    World* w{};
    MapPlot plots[map_side * map_side]{};

    Fixture() {
        w = world_create();
        WorldCapacity cap{8, 4, 4};
        if (w == nullptr || !world_alloc_tables(w, &cap))
            std::abort();
        w->game.map_width = map_side;
        w->game.map_height = map_side;
        w->game.feature_def_count = 4;
        w->plots = plots;
        for (auto& plot : plots) {
            plot.feature = 0xffff;
            plot.high_height = 30;
            plot.low_height = 10;
        }
        w->game.weapon_defs[plain_def - 1].area_of_effect = 16;
        w->game.weapon_defs[units_only_def - 1].flags = OA_WEAPON_FLAG_UNITS_ONLY;
        w->game.weapon_defs[bounce_def - 1].flags = OA_WEAPON_FLAG_GROUND_BOUNCE;
        w->game.weapon_defs[water_def - 1].flags = OA_WEAPON_FLAG_WATER_WEAPON;
        UnitDef& def = w->unit_defs[1];
        def.model_height = fx(20);
        def.bounds_min_y = fx(5);
        w->feature_defs[1].height = 40;
        w->feature_defs[3].height = 40;
        // Unit 1 (player 0) on the ground of cell (1, 1); unit 2 (player 0) in the air there.
        for (uint32_t slot = 1; slot <= 2; ++slot) {
            Unit& unit = w->units[slot];
            unit.owner_index = 0;
            unit.def = 2;
            unit.type_index = 1;
            unit.id = static_cast<uint16_t>(slot);
            unit.position = {fx(24), fx(slot == 1 ? 10 : 100), fx(24)};
        }
    }

    ~Fixture() {
        w->plots = nullptr;
        world_destroy(w);
    }

    MapPlot& plot(int32_t x, int32_t z) { return plots[z * map_side + x]; }

    Projectile& shot(uint8_t owner, oa_ref32 def, int32_t x, int32_t y, int32_t z) {
        Projectile& record = w->projectiles[w->game.projectile_count++];
        std::memset(&record, 0, sizeof record);
        record.owner_index = owner;
        record.def = def;
        record.position = {fx(x), fx(y), fx(z)};
        record.feature_cell_x = -1;
        record.feature_cell_z = -1;
        return record;
    }
};

void map_edges_and_intercept() {
    Fixture f;
    Projectile& off = f.shot(1, plain_def, -1, 100, 8);
    CHECK(projectile_map_contact(*f.w, off, false).kind == ContactKind::off_map);
    Projectile& no_def = f.shot(1, 0, 8, 100, 8);
    CHECK(projectile_map_contact(*f.w, no_def, false).kind == ContactKind::off_map);

    Projectile& nuke = f.shot(1, plain_def, 8, 100, 8);
    Projectile& interceptor = f.shot(0, plain_def, 8, 100 + 15, 8);
    interceptor.intercept_target = static_cast<oa_ref32>(&nuke - f.w->projectiles) + 1;
    auto contact = projectile_map_contact(*f.w, interceptor, false);
    CHECK(contact.intercepted && contact.kind == ContactKind::none);
    CHECK(interceptor.plot_height == 20);
    interceptor.position.y = fx(100 + 16);
    contact = projectile_map_contact(*f.w, interceptor, false);
    CHECK(!contact.intercepted && contact.kind == ContactKind::none);
    // A dead link never intercepts.
    interceptor.intercept_target = 250;
    CHECK(!projectile_map_contact(*f.w, interceptor, false).intercepted);
}

void occupants() {
    Fixture f;
    f.plot(1, 1).ground_unit = 1;
    f.plot(1, 1).air_unit = 2;
    Projectile& shot = f.shot(1, plain_def, 24, 25, 24);
    auto contact = projectile_map_contact(*f.w, shot, false);
    CHECK(contact.kind == ContactKind::unit && contact.unit == 2);
    shot.owner_index = 0;
    CHECK(projectile_map_contact(*f.w, shot, false).kind == ContactKind::none);
    shot.owner_index = 1;
    shot.position.y = fx(30);
    CHECK(projectile_map_contact(*f.w, shot, false).kind == ContactKind::none);

    shot.position.y = fx(110);
    contact = projectile_map_contact(*f.w, shot, false);
    CHECK(contact.kind == ContactKind::unit && contact.unit == 3);
    shot.position.y = fx(105);
    CHECK(projectile_map_contact(*f.w, shot, false).unit == 3);
    shot.position.y = fx(120);
    CHECK(projectile_map_contact(*f.w, shot, false).unit == 3);
    shot.position.y = fx(104);
    CHECK(projectile_map_contact(*f.w, shot, false).kind == ContactKind::none);
    shot.position.y = fx(121);
    CHECK(projectile_map_contact(*f.w, shot, false).kind == ContactKind::none);
    // A ground occupant is tested before the air one and blocks at any height below its top.
    shot.position.y = fx(-5);
    CHECK(projectile_map_contact(*f.w, shot, false).unit == 2);
}

void features() {
    Fixture f;
    f.plot(2, 2).feature = 1;
    Projectile& shot = f.shot(1, plain_def, 40, 45, 40);
    auto contact = projectile_map_contact(*f.w, shot, false);
    CHECK(contact.kind == ContactKind::feature);
    CHECK(shot.feature_cell_x == 2 && shot.feature_cell_z == 2);
    // The same cell again falls through to the ground and water tests.
    CHECK(projectile_map_contact(*f.w, shot, false).kind == ContactKind::none);
    shot.position.y = fx(50);
    shot.feature_cell_x = -1;
    CHECK(projectile_map_contact(*f.w, shot, false).kind == ContactKind::none);
    CHECK(shot.feature_cell_x == -1);

    // A continuation cell one column right of the origin resolves the same feature.
    f.plot(3, 2).feature = 0xfffe;
    f.plot(3, 2).feature_record = 0x0100;
    Projectile& second = f.shot(1, plain_def, 56, 45, 40);
    CHECK(projectile_map_contact(*f.w, second, false).kind == ContactKind::feature);
    CHECK(second.feature_cell_x == 3 && second.feature_cell_z == 2);
    // Rows back beyond the first plot is no feature.
    f.plot(3, 2).feature_record = 0x0009;
    second.feature_cell_x = -1;
    CHECK(projectile_map_contact(*f.w, second, false).kind == ContactKind::none);

    f.plot(2, 2).feature = 0xfffb;
    shot.position.y = fx(45);
    shot.feature_cell_x = -1;
    CHECK(projectile_map_contact(*f.w, shot, false).kind == ContactKind::none);
    f.plot(2, 2).feature = 3;
    f.w->game.feature_def_count = 3;
    CHECK(projectile_map_contact(*f.w, shot, false).kind == ContactKind::none);
    f.w->game.feature_def_count = 4;
    CHECK(projectile_map_contact(*f.w, shot, false).kind == ContactKind::feature);

    Projectile& piercing = f.shot(1, units_only_def, 40, 45, 40);
    CHECK(projectile_map_contact(*f.w, piercing, false).kind == ContactKind::none);
    CHECK(piercing.feature_cell_x == -1);
}

void ground_and_water() {
    Fixture f;
    Projectile& shot = f.shot(1, plain_def, 8, 5, 8);
    CHECK(projectile_map_contact(*f.w, shot, false).kind == ContactKind::ground);
    shot.position.y = fx(10);
    CHECK(projectile_map_contact(*f.w, shot, false).kind == ContactKind::none);

    Projectile& bouncer = f.shot(1, bounce_def, 8, 5, 8);
    bouncer.velocity.y = -0x40000;
    CHECK(projectile_map_contact(*f.w, bouncer, false).kind == ContactKind::bounce);
    CHECK(bouncer.velocity.y == 0x10000);

    Projectile& piercing = f.shot(1, units_only_def, 8, 5, 8);
    CHECK(projectile_map_contact(*f.w, piercing, false).kind == ContactKind::none);

    f.w->game.sea_level = 30;
    shot.position.y = fx(20);
    CHECK(projectile_map_contact(*f.w, shot, false).kind == ContactKind::water);
    CHECK(projectile_map_contact(*f.w, shot, true).kind == ContactKind::none);
    shot.position.y = fx(30);
    CHECK(projectile_map_contact(*f.w, shot, false).kind == ContactKind::none);
    Projectile& torpedo = f.shot(1, water_def, 8, 20, 8);
    CHECK(projectile_map_contact(*f.w, torpedo, false).kind == ContactKind::none);
    // Below ground wins over below sea level.
    shot.position.y = fx(5);
    CHECK(projectile_map_contact(*f.w, shot, false).kind == ContactKind::ground);
}
} // namespace

int main() {
    map_edges_and_intercept();
    occupants();
    features();
    ground_and_water();
    if (failures != 0) {
        std::fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    std::puts("projectile contact tests passed");
    return 0;
}
