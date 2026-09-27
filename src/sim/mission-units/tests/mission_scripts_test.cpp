// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// InitialMission lines from the shipped campaign missions run through the
// script parser, with order kinds from the game's mission table.
#include "oa/data/mission_types.hpp"
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

// Sorted mission-table indices of the names the parser queues.
constexpr uint8_t attack_unit_type_kind = 10;
constexpr uint8_t building_build_kind = 12;
constexpr uint8_t build_weapon_kind = 13;
constexpr uint8_t make_selectable_kind = 24;
constexpr uint8_t mobile_build_kind = 25;
constexpr uint8_t self_destruct_fg_kind = 39;
constexpr uint8_t wait_kind = 66;
constexpr uint8_t wait_for_attack_kind = 67;
// The fake order_for answers 100 + the command category.
constexpr uint8_t category_base = 100;

enum Type : uint16_t {
    CORVALK = 1,
    CORAK,
    CORCK,
    CORMEX,
    CORROY,
    CORFMD,
    ARMHLT,
    ARMBRAWL,
    CORLAB,
    CORREAP,
    ARMMEX,
    CORSY,
    CORSUB,
    TYPES
};

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
    Order orders[64]{};
    int count = 0;
    uint32_t next_slot = 1;

    int orders_of(const Unit& unit, const Order** first) const {
        int n = 0;
        *first = nullptr;
        for (int i = 0; i < count; ++i)
            if (orders[i].unit == &unit) {
                if (*first == nullptr)
                    *first = &orders[i];
                ++n;
            }
        return n;
    }
};

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
    // As the match spawns them: every unit, structures included, has a
    // nonzero Unit.movement.
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
        u.flags = OA_UNIT_FLAG_SELECTABLE;
        u.movement = 1;
        return &u;
    };
    // As in the game, only bmcode-1 types get the movement object.
    h.movement_object = [](void* c, const Unit& unit) {
        const UnitDef* def =
            world_unit_def_of(static_cast<const World*>(static_cast<Fake*>(c)->world), &unit);
        return def != nullptr && def->bm_code == 1;
    };
    h.order_for = [](void*, OrderCategory category, Unit&, Unit*, const FixedVec3*) {
        return static_cast<uint8_t>(category_base + static_cast<int>(category));
    };
    h.order_named = [](void*, const char* name) {
        return data::mission_types::index_for_name(name);
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
        if (flags != queue_append || f->count >= 64)
            return;
        f->orders[f->count++] = Order{
            kind, &unit, target, position != nullptr, position ? *position : FixedVec3{}, a, b
        };
    };
    return h;
}

World* make_world() {
    World* w = world_create();
    WorldCapacity cap{16, TYPES, 0};
    if (w == nullptr || !world_alloc_tables(w, &cap))
        std::abort();
    const char* names[TYPES] = {
        "",
        "CORVALK",
        "CORAK",
        "CORCK",
        "CORMEX",
        "CORROY",
        "CORFMD",
        "ARMHLT",
        "ARMBRAWL",
        "CORLAB",
        "CORREAP",
        "ARMMEX",
        "CORSY",
        "CORSUB"
    };
    const bool mobile[TYPES] = {
        false, true, true, true, false, true, false, false, true, false, true, false, false, true
    };
    for (uint16_t i = 1; i < TYPES; ++i) {
        std::strcpy(w->unit_defs[i].name, names[i]);
        w->unit_defs[i].type_id = i;
        w->unit_defs[i].max_damage = 1000;
        w->unit_defs[i].bm_code = mobile[i] ? 1 : 0;
    }
    return w;
}

constexpr int32_t fx(int32_t whole) {
    return static_cast<int32_t>(static_cast<uint32_t>(whole) << 16);
}

bool at(const Order& order, int32_t x, int32_t z) {
    return order.has_position && order.position.x == fx(x) && order.position.z == fx(z);
}

data::campaign::MissionUnit entry(const char* name, const char* ident, const char* script) {
    data::campaign::MissionUnit unit{};
    unit.unit_name = name;
    unit.ident = ident;
    unit.initial_mission = script;
    unit.health_percent = 100;
    unit.player = 2;
    return unit;
}

// A failed coordinate read keeps the point read last, across units too.
void carried_points() {
    World* w = make_world();
    Fake f;
    f.world = w;
    data::campaign::MissionUnit schema[4] = {
        entry("CORVALK", "TRANSPORT5", "w 3500,m 670 2701,u,m 3115 256"),         // AC13
        entry("CORAK", nullptr, "P P 502 1224"),                                  // AC01
        entry("CORCK", nullptr, "w 600,b CORMEX 5084 2750"),                      // AC05
        entry("CORROY", nullptr, "w 1050, m 1557, a ARMMEX, m 444 2483, w 750,"), // AC06
    };
    CHECK(create_mission_units(*w, schema, 4, hooks_for(f)));
    const Order* o = nullptr;

    // Seconds become ticks; the bare unload reuses the move's point.
    CHECK(f.orders_of(w->units[1], &o) == 5);
    CHECK(o[0].kind == wait_kind && o[0].a == 3500 * 30 && o[0].b == 0 && !o[0].has_position);
    CHECK(o[1].kind == category_base + 2 && at(o[1], 670, 2701));
    CHECK(o[2].kind == category_base + 5 && at(o[2], 670, 2701));
    CHECK(o[3].kind == category_base + 2 && at(o[3], 3115, 256));
    CHECK(o[4].kind == make_selectable_kind && o[4].target == nullptr);
    CHECK((w->units[1].flags & OA_UNIT_FLAG_SELECTABLE) == 0);

    // "P P 502 1224" reads no number: the patrol goes to the previous
    // script's last point and, being terminal, adds no MakeSelectable.
    CHECK(f.orders_of(w->units[2], &o) == 1);
    CHECK(o[0].kind == category_base + 9 && at(o[0], 3115, 256) && o[0].a == 0);
    CHECK((w->units[2].flags & OA_UNIT_FLAG_SELECTABLE) == 0);

    // "b CORMEX 5084 2750" misses its count: 5084 is the count, 2750 the x,
    // and z comes from the previous script.
    CHECK(f.orders_of(w->units[3], &o) == 3);
    CHECK(
        o[1].kind == mobile_build_kind && o[1].a == CORMEX && o[1].b == 5084 && at(o[1], 2750, 256)
    );

    // " m 1557" keeps z; a type name after 'a' queues AttackUType.
    CHECK(f.orders_of(w->units[4], &o) == 6);
    CHECK(o[1].kind == category_base + 2 && at(o[1], 1557, 256));
    CHECK(o[2].kind == attack_unit_type_kind && o[2].a == ARMMEX && !o[2].has_position);
    CHECK(o[3].kind == category_base + 2 && at(o[3], 444, 2483));
    CHECK(o[4].kind == wait_kind && o[4].a == 750 * 30 && o[5].kind == make_selectable_kind);
    world_destroy(w);
}

void named_orders() {
    World* w = make_world();
    Fake f;
    f.world = w;
    data::campaign::MissionUnit schema[4] = {
        entry("CORFMD", nullptr, "bw 2,"),    // EXP1AC12
        entry("ARMHLT", "HLTOWER2", nullptr), // CC23
        entry("ARMBRAWL", nullptr, "wa HLTOWER2,a CORREAP,CORINT,CORFUS,CORESTOR,CORCOM"), // CC23
        entry("CORLAB", nullptr, "w 700,b CORAK,w 300,b CORAK 1"),                         // AC02
    };
    CHECK(create_mission_units(*w, schema, 4, hooks_for(f)));
    const Order* o = nullptr;

    // BuildWeapon for weapon slot 0; it does not count as a scripted order,
    // so the silo stays selectable and gets no MakeSelectable.
    CHECK(f.orders_of(w->units[1], &o) == 1);
    CHECK(o[0].kind == build_weapon_kind && o[0].a == 0 && o[0].b == 2);
    CHECK((w->units[1].flags & OA_UNIT_FLAG_SELECTABLE) != 0);

    // WaitForAttack on the Ident; tokens that start with no command letter
    // are skipped.
    CHECK(f.orders_of(w->units[3], &o) == 3);
    CHECK(o[0].kind == wait_for_attack_kind && o[0].target == &w->units[2]);
    CHECK(o[1].kind == attack_unit_type_kind && o[1].a == CORREAP);
    CHECK(o[2].kind == make_selectable_kind);

    // A structure builds with BuildingBuild and no point, whatever
    // Unit.movement says; a missing count is one.
    CHECK(f.orders_of(w->units[4], &o) == 5);
    CHECK(o[0].kind == wait_kind && o[0].a == 700 * 30);
    CHECK(o[1].kind == building_build_kind && o[1].a == CORAK && o[1].b == 1 && !o[1].has_position);
    CHECK(o[3].kind == building_build_kind && o[3].b == 1);
    CHECK(o[4].kind == make_selectable_kind);
    world_destroy(w);
}

// EXP1AC01 medium CORSY "600,b CORROY 1,w 200,b CORSUB 1": the leading
// number names no command, so the shipyard builds from tick 0 and only its
// later 'w 200' waits (6000 ticks).
void bare_number_is_not_a_wait() {
    World* w = make_world();
    Fake f;
    f.world = w;
    data::campaign::MissionUnit schema[1] = {
        entry("CORSY", nullptr, "600,b CORROY 1,w 200,b CORSUB 1")
    };
    CHECK(create_mission_units(*w, schema, 1, hooks_for(f)));
    const Order* o = nullptr;
    CHECK(f.orders_of(w->units[1], &o) == 4);
    CHECK(o[0].kind == building_build_kind && o[0].a == CORROY && o[0].b == 1);
    CHECK(o[1].kind == wait_kind && o[1].a == 200 * 30 && o[1].b == 0);
    CHECK(o[2].kind == building_build_kind && o[2].a == CORSUB && o[2].b == 1);
    CHECK(o[3].kind == make_selectable_kind);
    CHECK((w->units[1].flags & OA_UNIT_FLAG_SELECTABLE) == 0);
    world_destroy(w);
}

// 'wa' without a known Ident watches the unit itself; 'd' is terminal.
void self_orders() {
    World* w = make_world();
    Fake f;
    f.world = w;
    data::campaign::MissionUnit schema[2] = {
        entry("ARMHLT", nullptr, "wa NOBODY"),
        entry("CORAK", nullptr, "w 5,d"),
    };
    CHECK(create_mission_units(*w, schema, 2, hooks_for(f)));
    const Order* o = nullptr;
    CHECK(f.orders_of(w->units[1], &o) == 2);
    CHECK(o[0].kind == wait_for_attack_kind && o[0].target == &w->units[1]);
    CHECK(o[1].kind == make_selectable_kind);
    CHECK(f.orders_of(w->units[2], &o) == 2);
    CHECK(o[1].kind == self_destruct_fg_kind && o[1].a == 1);
    world_destroy(w);
}
} // namespace

int main() {
    // The parser's names resolve to the kinds the match dispatches.
    CHECK(data::mission_types::index_for_name(order_attack_type) == attack_unit_type_kind);
    CHECK(data::mission_types::index_for_name(order_building_build) == building_build_kind);
    CHECK(data::mission_types::index_for_name(order_build_weapon) == build_weapon_kind);
    CHECK(data::mission_types::index_for_name(order_make_selectable) == make_selectable_kind);
    CHECK(data::mission_types::index_for_name(order_mobile_build) == mobile_build_kind);
    CHECK(data::mission_types::index_for_name(order_self_destruct) == self_destruct_fg_kind);
    CHECK(data::mission_types::index_for_name(order_wait) == wait_kind);
    CHECK(data::mission_types::index_for_name(order_wait_for_attack) == wait_for_attack_kind);
    carried_points();
    named_orders();
    bare_number_is_not_a_wait();
    self_orders();
    if (failures != 0)
        return 1;
    std::puts("sim-mission-scripts: ok");
    return 0;
}
