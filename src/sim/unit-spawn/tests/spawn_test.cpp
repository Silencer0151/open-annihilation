// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "fixture.hpp"
#include <cstdint>
#include <limits>
#include <vector>

struct SwitchFixture : Fixture {
    oa::Unit* unit{};
    oa::oa_ref32 replacement{};

    uint32_t random_bounded(uint32_t) override {
        unit->def = replacement;
        return 7;
    }
};

struct StartFixture : StartHost {
    unsigned lookup{}, missing{}, camera{};
    int32_t x{}, z{};

    uint16_t commander_type_for_side(uint8_t side) override {
        CHECK(side == 1);
        ++lookup;
        return 1;
    }

    void report_missing_start_position(int32_t) override { ++missing; }

    void set_camera_position(int32_t px, int32_t pz, uint32_t flags) override {
        CHECK(flags == 0);
        ++camera;
        x = px;
        z = pz;
    }
};

int main() {
    SpawnWorld sw(4, 3);
    auto& w = *sw;
    auto& units = sw.units;
    sw.range(0, 1, 3);
    for (auto& unit : units)
        unit.owner = oa::oa_ref_from_index(0);
    sw.player(0).in_use = 1;
    sw.player(0).status = 1;
    auto& t = sw.types[1];
    t.simulation.flags = 0x1840000;
    t.simulation.maximum_health = 1000;
    t.footprint_x = 2;
    t.footprint_z = 3;
    t.build_angle = 100;
    t.model = 10;
    sw.load_types();
    Request r;
    r.type = 1;
    r.finished = true;
    r.state = ground_occupancy_state;
    r.position = {40u << 16, 12u << 16, 80u << 16};
    Fixture h;
    auto* unit = create(w, sw.tables, r, h);
    const auto words = [](const oa::Unit& u) {
        return std::array<uint32_t, 3>{
            static_cast<uint32_t>(u.position.x),
            static_cast<uint32_t>(u.position.y),
            static_cast<uint32_t>(u.position.z)
        };
    };
    CHECK(unit == &units[1]);
    CHECK(units[1].def == oa::oa_ref_from_index(1) && units[1].health == 1000);
    CHECK(words(units[1]) == r.position && units[1].type_index == 1);
    // Only a bmcode 1 unit gets Unit.movement (its movement object); a structure's model does not
    // set it.
    CHECK(sw.assets[1].model_instance == 100 && units[1].movement == 0);
    CHECK(unit->cell_x == 2 && unit->cell_z == 4);
    CHECK(unit->heading == static_cast<uint16_t>(7 - 0x8000 - 50));
    CHECK(units[1].damage_kind == 7 && (units[1].flags & OA_UNIT_FLAG_DEATH_PENDING));
    CHECK(sw.player(0).unit_count == 1 && sw.player(0).units_created == 1);
    CHECK(h.calls == std::vector<std::string>({"rng100",     "resetA0", "resetB0",    "resetA1",
                                               "resetB1",    "resetA2", "resetB2",    "economy",
                                               "rng65536",   "spatial", "plain",      "owner",
                                               "modelreset", "weapons", "visibility", "height",
                                               "register",   "notify",  "finished",   "activate",
                                               "finalize",   "scenario"}));
    h.calls.clear();
    t.player_limit = 1;
    sw.load_types();
    CHECK(create(w, sw.tables, r, h) == nullptr && h.calls.empty());
    t.player_limit = -1;
    sw.load_types();
    r.requested_slot = 1;
    CHECK(create(w, sw.tables, r, h) == nullptr && h.calls.empty());
    r.requested_slot = 3;
    t.cob = 20;
    t.bm_code = 1;
    sw.load_types();
    r.finished = false;
    unit = create(w, sw.tables, r, h);
    CHECK(unit == &units[3] && sw.assets[3].script_instance == 200);
    CHECK(sw.assets[3].model_instance == 300);
    CHECK(units[3].script && units[3].movement && units[3].health == 0);
    CHECK(unit->build_remaining == 1.0F && unit->heading == 100);
    CHECK(
        h.calls ==
        std::vector<std::string>({"rng100",     "resetA0", "resetB0",     "resetA1",  "resetB1",
                                  "resetA2",    "resetB2", "economy",     "rng65536", "spatial",
                                  "allocate",   "load",    "scriptmodel", "bind",     "Create",
                                  "modelreset", "weapons", "visibility",  "movement", "height",
                                  "register",   "notify",  "finalize",    "scenario"})
    );
    h.calls.clear();
    r.type = 0;
    CHECK(!create(w, sw.tables, r, h));
    r.type = 2;
    CHECK(!create(w, sw.tables, r, h));
    CHECK(h.calls.empty());
    StartFixture start;
    std::array<StartMarker, 3> markers = {
        StartMarker{0, 1, 2, 3}, StartMarker{1, 1, -30, 100}, StartMarker{1, 1, 80, 80}
    };
    CHECK(count_start_positions(markers) == 2);
    std::array<uint32_t, 3> pos = {9, 8, 7};
    CHECK(!start_position(markers, 99, pos));
    CHECK(pos[0] == 9);
    CHECK(start_position(markers, 1, pos));
    CHECK(pos[0] == 0xffe20000u && pos[1] == 0 && pos[2] == 100u * 65536);
    PlayerSetup setup;
    setup.side = 1;
    setup.metal = 100;
    setup.energy = 400;
    auto started =
        spawn_player_commander(w, sw.tables, 0, setup, markers, 1, 0, 640, 480, h, start);
    CHECK(started.position_found && started.unit == &units[2]);
    CHECK(start.camera == 1 && start.x == -350 && start.z == -140);
    CHECK(
        sw.player(0).shared_energy_storage == 400.0f && sw.player(0).shared_metal_storage == 200.0f
    );
    CHECK((sw.player(0).resource_flags & 1) != 0);
    CHECK(sw.player(0).energy == 0.0f && sw.player(0).metal == 0.0f);
    CHECK(sw.setups[0].side == 1);
    auto absent =
        spawn_player_commander(w, sw.tables, 0, setup, markers, 99, 0, 640, 480, h, start);
    CHECK(!absent.position_found && !absent.unit && start.missing == 1 && start.camera == 1);
    {
        SpawnWorld switched(2, 2);
        auto& changed = switched.units[1];
        changed.owner = oa::oa_ref_from_index(0);
        SwitchFixture callback;
        callback.unit = &changed;
        callback.replacement = oa::oa_ref_from_index(1);
        switched.types[0].build_angle = 100;
        switched.types[1].build_angle = 200;
        switched.types[1].gui_page_count = 2;
        switched.load_types();
        Request numeric;
        initialize_numeric(*switched, changed, numeric, callback);
        CHECK(changed.heading == static_cast<uint16_t>(7 - 0x8000 - 100));
        CHECK(
            (changed.flags & (OA_UNIT_FLAG_BUILD_MENU | OA_UNIT_FLAG_BUILD_PAGE_MASK)) == 0xc00000
        );
    }
    {
        // A new unit takes its type's standing orders over whatever the slot
        // held: type 0 as ARMCOM.FBI (StandingMoveOrder=0, StandingFireOrder=2)
        // holds position and fires at will; type 1, with neither key, roams.
        SpawnWorld standing(2, 2);
        auto& reused = standing.units[1];
        reused.owner = oa::oa_ref_from_index(0);
        standing.types[0].simulation.flags = 0u | (2u << OA_UNIT_DEF_FLAG_FIRE_ORDER_SHIFT);
        standing.types[1].simulation.flags = 2u | (2u << OA_UNIT_DEF_FLAG_FIRE_ORDER_SHIFT);
        standing.load_types();
        const auto move_order = [&] {
            return (reused.flags & OA_UNIT_FLAG_MOVE_ORDER_MASK) >> OA_UNIT_FLAG_MOVE_ORDER_SHIFT;
        };
        const auto fire_order = [&] {
            return (reused.flags & OA_UNIT_FLAG_FIRE_ORDER_MASK) >> OA_UNIT_FLAG_FIRE_ORDER_SHIFT;
        };
        Fixture host;
        Request commander;
        commander.type = 0;
        reused.flags =
            (1u << OA_UNIT_FLAG_MOVE_ORDER_SHIFT) | (0u << OA_UNIT_FLAG_FIRE_ORDER_SHIFT);
        initialize_numeric(*standing, reused, commander, host);
        CHECK(move_order() == 0 && fire_order() == 2);
        Request defaults;
        defaults.type = 1;
        initialize_numeric(*standing, reused, defaults, host);
        CHECK(move_order() == 2 && fire_order() == 2);
    }
    {
        // The whole flag words a spawn leaves, from a slot with every bit set
        // and from a cleared one. Type 0 is a structure with every type flag
        // and two build pages, owned by the viewpoint player; type 1 a mobile
        // unit with no type flags and one page, owned by another player.
        SpawnWorld pinned(2, 2);
        auto& slot = pinned.units[1];
        slot.owner = oa::oa_ref_from_index(0);
        pinned.player(0).index = 0;
        (*pinned).game.viewpoint_player = 0;
        pinned.types[0].simulation.flags = 0xffffffffu;
        pinned.types[0].bm_code = 0;
        pinned.types[0].gui_page_count = 2;
        pinned.types[1].simulation.flags = 0;
        pinned.types[1].bm_code = 1;
        pinned.types[1].gui_page_count = 1;
        pinned.load_types();
        Fixture host;
        Request structure;
        structure.type = 0;
        Request mobile;
        mobile.type = 1;
        const auto spawn = [&](const Request& request, uint32_t flags, uint32_t flags2) {
            slot.flags = flags;
            slot.flags2 = flags2;
            initialize_numeric(*pinned, slot, request, host);
        };
        // Every spawn leaves these set: live, in the ground layer, selectable
        // and with its position marked changed.
        constexpr uint32_t spawned = OA_UNIT_FLAG_LIVE | ground_occupancy_state |
                                     OA_UNIT_FLAG_SELECTABLE | OA_UNIT_FLAG_POSITION_DIRTY;
        // A slot's own bits that a spawn keeps.
        constexpr uint32_t kept = OA_UNIT_FLAG_CYCLE_VISITED | OA_UNIT_FLAG_CYCLE_SKIP |
                                  OA_UNIT_FLAG_CLOAK_LOCKED | OA_UNIT_FLAG_CONSTRUCTION_DIRTY |
                                  OA_UNIT_FLAG_NOT_SELECTABLE;
        // What the structure type and its owner give: weapons, an air base, a
        // building, standing orders 3 and 3, the build menu at page 1, a
        // running cloak and the viewpoint player's ownership.
        constexpr uint32_t from_structure =
            OA_UNIT_FLAG_HAS_WEAPONS | OA_UNIT_FLAG_AIR_BASE | OA_UNIT_FLAG_BUILDING |
            OA_UNIT_FLAG_MOVE_ORDER_MASK | OA_UNIT_FLAG_FIRE_ORDER_MASK | OA_UNIT_FLAG_BUILD_MENU |
            (1u << OA_UNIT_FLAG_BUILD_PAGE_SHIFT) | OA_UNIT_FLAG_CLOAK_RUNNING |
            OA_UNIT_FLAG_VIEWPOINT_OWNED;
        spawn(structure, 0xffffffffu, 0xffffffffu);
        CHECK(slot.flags == (spawned | kept | from_structure) && slot.flags2 == 0xffffffffu);
        spawn(structure, 0, 0);
        CHECK(slot.flags == (spawned | from_structure) && slot.flags2 == OA_UNIT_FLAG2_Z_BUFFER);
        (*pinned).game.viewpoint_player = 1;
        spawn(mobile, 0xffffffffu, 0xffffffffu);
        CHECK(slot.flags == (spawned | kept) && slot.flags2 == ~OA_UNIT_FLAG2_Z_BUFFER);
        spawn(mobile, 0, 0);
        CHECK(slot.flags == spawned && slot.flags2 == 0);
    }
    {
        CHECK(unit_pool_size(0) == 0 && unit_pool_size(6553) == 65531);
        CHECK(unit_pool_size(6554) == 0 && unit_pool_size(5) == 51);
        SpawnWorld pooled(unit_pool_size(5), 1);
        for (std::size_t i = 0; i < 10; ++i)
            pooled.player(i).index = static_cast<uint8_t>(i);
        (*pooled).game.periodic_flags = 2;
        init_unit_pool(*pooled, 5);
        auto& pool_units = pooled.units;
        uint32_t count = 0;
        auto* last_range = oa::world_player_units(&*pooled, &pooled.player(9), &count);
        CHECK(
            (*pooled).game.unit_slot_count == 51 && !((*pooled).game.periodic_flags & 2) &&
            !pool_units[0].owner && pool_units[0].owner_index == 255
        );
        CHECK(last_range == &pool_units[46] && count == 5);
        // The pool setup stores the ids of the range ends in Player.base_unit_id and last_unit_id.
        CHECK(pooled.player(0).base_unit_id == 1 && pooled.player(0).last_unit_id == 5);
        CHECK(pooled.player(9).base_unit_id == 46 && pooled.player(9).last_unit_id == 50);
        CHECK(
            oa::world_unit_owner(&*pooled, &pool_units[50]) == &pooled.player(9) &&
            pool_units[50].id == 50 && pool_units[50].owner_index == 9 && pool_units[50].squad == -1
        );
        CHECK(pool_units[50].def == oa::oa_ref_from_index(0));
    }
    {
        SpawnWorld linked(4, 1);
        auto& l = linked.units;
        const auto ref = [&](std::size_t i) { return oa::world_unit_ref(&*linked, &l[i]); };
        CHECK(attached_child_count(*linked, l[0]) == 0);
        l[0].attach_first_child = ref(1);
        l[1].attach_parent = ref(0);
        l[1].attach_next = ref(2);
        l[2].attach_parent = ref(0);
        l[2].attach_next = ref(3);
        l[3].attach_parent = ref(1);
        CHECK(attached_child_count(*linked, l[0]) == 2);
        l[2].attach_next = ref(1);
        // The cycle is counted to its 65535th link, each one a child of l[0].
        CHECK(attached_child_count(*linked, l[0]) == 65535);
    }
    {
        std::vector<oa::sim::spatial_state::Plot> plots(4);
        plots[0].high_height = 10;
        plots[0].low_height = 4;
        plots[1].high_height = 5;
        plots[1].low_height = 2;
        plots[3].high_height = 255;
        plots[3].low_height = 255;
        const auto at = [](int16_t x, int16_t z, uint32_t y = 0) {
            return std::array<uint32_t, 3>{
                static_cast<uint32_t>(static_cast<int32_t>(x)) << 16,
                y,
                static_cast<uint32_t>(static_cast<int32_t>(z)) << 16
            };
        };
        CHECK(average_plot_height(at(0, 0), plots, 2, 2) == 7);
        CHECK(average_plot_height(at(15, 15), plots, 2, 2) == 7);
        CHECK(average_plot_height(at(16, 0), plots, 2, 2) == 3);
        CHECK(average_plot_height(at(31, 15), plots, 2, 2) == 3);
        CHECK(average_plot_height(at(16, 16), plots, 2, 2) == 255);
        CHECK(average_plot_height(at(0, 0, 0xffffffffu), plots, 2, 2) == 7);
        auto fractional = at(0, 0);
        fractional[0] |= 0xffffu;
        fractional[2] |= 0xabcdu;
        CHECK(average_plot_height(fractional, plots, 2, 2) == 7);
        // Toward-zero division keeps -1..-15 on cell 0; an arithmetic shift would miss the map.
        CHECK(average_plot_height(at(-1, -15), plots, 2, 2) == 7);
        CHECK(average_plot_height(at(-16, 0), plots, 2, 2) == -1);
        CHECK(average_plot_height(at(-17, 0), plots, 2, 2) == -1);
        CHECK(average_plot_height(at(32, 0), plots, 2, 2) == -1);
        CHECK(average_plot_height(at(0, 32), plots, 2, 2) == -1);
        plots[0].high_height = 0;
        plots[0].low_height = 0;
        CHECK(average_plot_height(at(0, 0), plots, 2, 2) == 0);
        std::vector<oa::sim::spatial_state::Plot> short_map(1);
        short_map[0].high_height = 8;
        short_map[0].low_height = 8;
        CHECK(average_plot_height(at(16, 0), short_map, 2, 2) == -1);
    }
    // Paying for both: compare each store, then debit both. A short
    // store leaves both stores and both requested words unchanged.
    {
        oa::Player economy{};
        economy.energy = 5.0F;
        economy.metal = 3.0F;
        float energy_requested = 1.0F;
        float metal_requested = 2.0F;
        CHECK(!player_pay_resources(economy, energy_requested, metal_requested, 6.0F, 1.0F));
        CHECK(
            economy.energy == 5.0F && economy.metal == 3.0F && energy_requested == 1.0F &&
            metal_requested == 2.0F
        );
        CHECK(!player_pay_resources(economy, energy_requested, metal_requested, 2.0F, 4.0F));
        CHECK(
            economy.energy == 5.0F && economy.metal == 3.0F && energy_requested == 1.0F &&
            metal_requested == 2.0F
        );
        CHECK(player_pay_resources(economy, energy_requested, metal_requested, 2.0F, 1.0F));
        CHECK(
            economy.energy == 3.0F && economy.metal == 2.0F && energy_requested == 3.0F &&
            metal_requested == 3.0F
        );
        CHECK(player_pay_energy(economy, energy_requested, 3.0F));
        CHECK(economy.energy == 0.0F && energy_requested == 6.0F);
        CHECK(!player_pay_energy(economy, energy_requested, 0.01F));
        CHECK(economy.energy == 0.0F && energy_requested == 6.0F);
        CHECK(player_pay_metal(economy, metal_requested, 2.0F));
        CHECK(economy.metal == 0.0F && metal_requested == 5.0F);
        CHECK(!player_pay_metal(economy, metal_requested, 0.01F));
        CHECK(economy.metal == 0.0F && metal_requested == 5.0F);
        // The payment keeps the game's comparisons: an unordered
        // (NaN) cost fails like "store < cost" and neither store is debited.
        economy.energy = 5.0F;
        economy.metal = 3.0F;
        energy_requested = 0.0F;
        metal_requested = 0.0F;
        CHECK(!player_pay_resources(
            economy,
            energy_requested,
            metal_requested,
            std::numeric_limits<float>::quiet_NaN(),
            1.0F
        ));
        CHECK(
            economy.energy == 5.0F && economy.metal == 3.0F && energy_requested == 0.0F &&
            metal_requested == 0.0F
        );
    }
    std::cout << "unit spawn tests passed\n";
}
