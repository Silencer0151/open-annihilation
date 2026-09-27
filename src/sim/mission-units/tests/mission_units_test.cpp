// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/mission_units.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace oa;
using namespace oa::sim::mission_units;

namespace {
int failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

struct Order {
    uint8_t kind;
    const Unit* unit;
    const Unit* target;
    bool has_position;
    FixedVec3 position;
    int32_t a;
    int32_t b;
};

struct Fake {
    World* world = nullptr;
    Order orders[32]{};
    int order_count = 0;
    int fatal = 0;
    int kills = 0;
    int no_units = 0;
    const Unit* carried = nullptr;
    const Unit* carrier = nullptr;
    uint32_t next_slot = 1;
};

// Order kinds: categories map to 100 + category, names to their first letter.
Hooks hooks_for(Fake& f) {
    Hooks h{};
    h.context = &f;
    h.find_def = [](void* c, const char* name) -> const UnitDef* {
        World* w = static_cast<Fake*>(c)->world;
        for (uint32_t i = 1; i < w->unit_def_count; ++i)
            if (std::strcmp(w->unit_defs[i].name, name) == 0)
                return &w->unit_defs[i];
        return nullptr;
    };
    h.type_id = [](void* c, const char* name) -> uint16_t {
        World* w = static_cast<Fake*>(c)->world;
        for (uint32_t i = 1; i < w->unit_def_count; ++i)
            if (std::strcmp(w->unit_defs[i].name, name) == 0)
                return w->unit_defs[i].type_id;
        return 0;
    };
    h.player_active = [](void*, int32_t player) { return player < 2; };
    h.fatal = [](void* c, const char*) { ++static_cast<Fake*>(c)->fatal; };
    h.create_unit = [](void* c,
                       uint8_t player,
                       uint16_t type,
                       const FixedVec3& position,
                       bool,
                       uint32_t,
                       uint16_t) -> Unit* {
        Fake* f = static_cast<Fake*>(c);
        Unit& u = f->world->units[f->next_slot];
        u.id = static_cast<uint16_t>(f->next_slot++);
        u.type_index = type;
        u.def = oa_ref_from_index(type);
        u.owner_index = player;
        u.position = position;
        u.flags = OA_UNIT_FLAG_SELECTABLE | OA_UNIT_FLAG_MOVE_ORDER_MASK;
        u.movement = type == 1 ? 1u : 0u;
        return &u;
    };
    h.order_for = [](void*, OrderCategory category, Unit&, Unit*, const FixedVec3*) {
        return static_cast<uint8_t>(100 + static_cast<int>(category));
    };
    h.order_named = [](void*, const char* name) {
        return static_cast<uint8_t>(name[0] + (name[1] == 'U' ? 1 : 0));
    };
    h.queue_order = [](void* c,
                       uint8_t kind,
                       uint32_t flags,
                       Unit& unit,
                       Unit* target,
                       const FixedVec3* position,
                       int32_t a,
                       int32_t b) {
        Fake* f = static_cast<Fake*>(c);
        if (flags != queue_append || f->order_count >= 32)
            return;
        Order& o = f->orders[f->order_count++];
        o = Order{
            kind, &unit, target, position != nullptr, position ? *position : FixedVec3{}, a, b
        };
    };
    h.carry = [](void* c, Unit& child, Unit& parent, uint8_t piece, uint8_t) {
        if (piece == carry_piece_none) {
            static_cast<Fake*>(c)->carried = &child;
            static_cast<Fake*>(c)->carrier = &parent;
        }
    };
    h.no_mission_units = [](void* c) { ++static_cast<Fake*>(c)->no_units; };
    h.kill = [](void* c, Unit&, uint8_t outcome) {
        if (outcome == kill_outcome_removed)
            ++static_cast<Fake*>(c)->kills;
    };
    return h;
}

World* make_world() {
    World* w = world_create();
    WorldCapacity cap{16, 4, 0};
    if (w == nullptr || !world_alloc_tables(w, &cap))
        std::abort();
    std::strcpy(w->unit_defs[1].name, "ARMCOM");
    w->unit_defs[1].type_id = 1;
    w->unit_defs[1].max_damage = 3000;
    std::strcpy(w->unit_defs[2].name, "ARMFUS");
    w->unit_defs[2].type_id = 2;
    w->unit_defs[2].max_damage = 1000;
    return w;
}

constexpr int32_t fx(int32_t whole) {
    return static_cast<int32_t>(static_cast<uint32_t>(whole) << 16);
}
} // namespace

int main() {
    {
        World* w = make_world();
        Fake f;
        f.world = w;
        const Hooks h = hooks_for(f);
        data::campaign::MissionUnit schema[3]{};
        schema[0] = {
            "ARMCOM",
            "hero",
            "w 60, m 250 1000, a ARMFUS",
            fx(10),
            0,
            fx(20),
            0x4000,
            50,
            0,
            0,
            1,
            data::campaign::mission_unit_flag::immunity
        };
        schema[1] = {
            "ARMFUS",
            nullptr,
            "i hero, wa HERO, s, p 7500 200 2",
            fx(30),
            0,
            fx(40),
            0,
            100,
            0,
            0,
            2,
            0
        };
        schema[2] = {"NOPE", nullptr, "m 1 1", 0, 0, 0, 0, 100, 0, 0, 1, 0};
        CHECK(create_mission_units(*w, schema, 3, h));
        Unit& hero = w->units[1];
        Unit& fusion = w->units[2];
        CHECK(hero.owner_index == 0 && fusion.owner_index == 1);
        CHECK(hero.health == 1500 && fusion.health == 1000);
        CHECK(hero.heading == 0x4000);
        CHECK(
            (hero.flags & unit_flag_mission_immune) != 0 &&
            (fusion.flags & unit_flag_mission_immune) == 0
        );
        // Hero: WAIT, move, attack-type, then the implicit MAKESELECTABLE.
        CHECK(f.order_count == 7);
        CHECK(f.orders[0].kind == 'W' && f.orders[0].a == 1800 && f.orders[0].b == 0);
        CHECK(
            f.orders[1].kind == 102 && f.orders[1].position.x == fx(250) &&
            f.orders[1].position.z == fx(1000)
        );
        CHECK(f.orders[2].kind == 'A' + 0 && f.orders[2].a == 2);
        CHECK(f.orders[3].kind == 'M' && f.orders[3].unit == &hero);
        CHECK((hero.flags & OA_UNIT_FLAG_SELECTABLE) == 0);
        // Fusion: carried by hero, waits for attack on hero, explicit MAKESELECTABLE, patrol.
        CHECK(f.carried == &fusion && f.carrier == &hero);
        CHECK(f.orders[4].kind == 'W' && f.orders[4].target == &hero);
        CHECK(f.orders[5].kind == 'M' && f.orders[5].unit == &fusion);
        CHECK(f.orders[6].kind == 109 && f.orders[6].a == 60 && f.orders[6].position.x == fx(7500));
        CHECK(f.fatal == 0 && f.no_units == 0);
        world_destroy(w);
    }
    {
        World* w = make_world();
        Fake f;
        f.world = w;
        const Hooks h = hooks_for(f);
        data::campaign::MissionUnit schema[1]{};
        schema[0] = {
            "ARMCOM", nullptr, "o 1 2,bw 3, b ARMFUS 2 100 200", 0, 0, 0, 0, 100, 0, 0, 5, 0
        };
        CHECK(create_mission_units(*w, schema, 1, h));
        CHECK(f.fatal == 1);
        const Unit& u = w->units[1];
        CHECK(((u.flags & OA_UNIT_FLAG_MOVE_ORDER_MASK) >> OA_UNIT_FLAG_MOVE_ORDER_SHIFT) == 1);
        CHECK(((u.flags & OA_UNIT_FLAG_FIRE_ORDER_MASK) >> OA_UNIT_FLAG_FIRE_ORDER_SHIFT) == 2);
        CHECK(f.order_count == 3);
        CHECK(f.orders[0].kind == 'B' + 1 && f.orders[0].b == 3);
        CHECK(f.orders[1].kind == 'M' && f.orders[1].a == 2 && f.orders[1].b == 2);
        CHECK(f.orders[1].has_position && f.orders[1].position.z == fx(200));
        CHECK(f.orders[2].kind == 'M');
        CHECK(find_script_unit(CreatedUnits{schema, nullptr, 0}, "ARMCOM", nullptr) == nullptr);
        world_destroy(w);
    }
    {
        World* w = make_world();
        Fake f;
        f.world = w;
        const Hooks h = hooks_for(f);
        CHECK(create_mission_units(*w, nullptr, 0, h));
        CHECK(f.no_units == 1);
        w->units[3].type_index = 2;
        w->units[4].type_index = 1;
        w->units[5].type_index = 2;
        kill_units_of_type(*w, 2, h);
        CHECK(f.kills == 2);
        kill_all_units(*w, h);
        CHECK(f.kills == 5);
        world_destroy(w);
    }
    {
        // AC01 [unit12] and [unit13]: the second patrol's numbers do not parse,
        // so it reuses the point the first script left. A movement object
        // decides the build order, not Unit.movement.
        World* w = make_world();
        Fake f;
        f.world = w;
        Hooks h = hooks_for(f);
        h.movement_object = [](void*, const Unit&) { return false; };
        data::campaign::MissionUnit schema[2]{};
        schema[0] = {"ARMCOM", nullptr, "P 502 1223", 0, 0, 0, 0, 100, 0, 0, 1, 0};
        schema[1] = {"ARMCOM", nullptr, "P P 502 1224,b ARMFUS 2", 0, 0, 0, 0, 100, 0, 0, 1, 0};
        CHECK(create_mission_units(*w, schema, 2, h));
        CHECK(f.order_count == 3);
        CHECK(
            f.orders[0].kind == 109 && f.orders[0].position.x == fx(502) &&
            f.orders[0].position.z == fx(1223)
        );
        CHECK(
            f.orders[1].kind == 109 && f.orders[1].position.x == fx(502) &&
            f.orders[1].position.z == fx(1223)
        );
        CHECK(f.orders[1].a == 0 && f.orders[1].unit == &w->units[2]);
        // BUILDINGBUILD without a point; Unit.movement alone would pick MOBILEBUILD.
        CHECK(w->units[2].movement != 0);
        CHECK(
            f.orders[2].kind == 'B' + 1 && !f.orders[2].has_position && f.orders[2].a == 2 &&
            f.orders[2].b == 2
        );
        world_destroy(w);
    }
    {
        data::campaign::MissionUnit schema[3]{};
        schema[0].unit_name = "ARMFUS";
        schema[1].unit_name = "ARMFUS";
        schema[2].ident = "armfus";
        Unit units[3]{};
        Unit* table[3] = {&units[0], nullptr, &units[2]};
        const CreatedUnits created{schema, table, 3};
        CHECK(find_script_unit(created, "armfus", nullptr) == &units[0]);
        CHECK(find_script_unit(created, "armfus", &units[0]) == &units[2]);
        CHECK(find_script_unit(created, "armfus", &units[2]) == nullptr);
        CHECK(find_script_unit(created, "armfus", &units[1]) == nullptr);
    }
    if (failures != 0)
        return 1;
    std::puts("sim-mission-units: ok");
    return 0;
}
