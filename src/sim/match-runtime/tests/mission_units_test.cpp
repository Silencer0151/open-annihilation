// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A campaign schema's units created in a match and their InitialMission
// scripts queued through the order-queue insert (issue_order), with script
// lines in the forms the campaign missions use.
#include "oa/sim/match_runtime/mission_unit_binding.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "oa/test/match_services.hpp"

using namespace oa;

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string(#x) + " at line " + std::to_string(__LINE__));    \
    } while (false)

namespace {

using View = sim::match_runtime::Match::OrderRecordView;

// Order-table indices of the kinds the scripts queue.
constexpr uint8_t attack_unit_type = 10;
constexpr uint8_t building_build = 12;
constexpr uint8_t build_weapon = 13;
constexpr uint8_t make_selectable = 24;
constexpr uint8_t patrol = 29;
constexpr uint8_t qpatrol = 31;
constexpr uint8_t vtol_move = 55;
constexpr uint8_t vtol_unload = 65;
constexpr uint8_t wait = 66;
constexpr uint8_t wait_for_attack = 67;
constexpr uint8_t be_carried = 11;

// The preserve, command and order flags after the insert: the descriptor
// bytes, preserve flags bit 0, the command flags' point and target bits kept
// only with a point or target, and command flags bit 0x10 on the last order
// queued.
constexpr uint8_t tail_mark = 0x10;

// Catalogue order (sorted by name), so type ids follow the index.
enum Type : uint16_t {
    armbrawl = 1, // gunship
    armhlt,       // 2x2 structure
    corak,        // ground mobile
    corfmd,       // 2x2 structure
    corlab,       // 4x4 factory
    correap,      // ground mobile
    corvalk,      // air transport
    type_count
};

constexpr int32_t map_cells = 128; // 2048 world units a side
constexpr uint8_t level_ground = 40;
constexpr uint32_t max_damage = 1000;

constexpr int32_t fixed(int32_t whole) {
    return static_cast<int32_t>(static_cast<uint32_t>(whole) << 16);
}

using Services = oa::test::QuietServices;

using Scenario = oa::test::EmptyScenario;

struct Fixture {
    static constexpr std::size_t cells = static_cast<std::size_t>(map_cells) * map_cells;
    static constexpr int32_t sight_cells = map_cells / 2;
    formats::tnt::Map map;
    std::vector<sim::visibility_state::TerrainCell> terrain_values =
        std::vector<sim::visibility_state::TerrainCell>(cells);
    std::array<sim::visibility_state::SightMask, 1> masks{};
    std::vector<sim::visibility_state::AltitudeCell> altitude_cells =
        std::vector<sim::visibility_state::AltitudeCell>(
            static_cast<std::size_t>(sight_cells) * sight_cells
        );
    std::vector<sim::visibility_state::AltitudeSightPattern> altitude_patterns =
        std::vector<sim::visibility_state::AltitudeSightPattern>(6);
    std::shared_ptr<formats::objects3d::Model> model =
        std::make_shared<formats::objects3d::Model>();
    std::shared_ptr<formats::cob::CobProgram> script = std::make_shared<formats::cob::CobProgram>();
    std::array<sim::unit_spawn::LoadedType, type_count> loaded;
    std::array<sim::unit_spawn::Type, type_count> types;
    std::array<data::unit_definitions::UnitDefinition, type_count> defs;
    data::unit_definitions::UnitTargetCategoryMasks target_masks;
    data::unit_definitions::RuntimeDefinitionMetadata metadata;
    std::vector<sim::spatial_state::Plot> plots = std::vector<sim::spatial_state::Plot>(cells);
    std::array<sim::match_runtime::RuntimeTypeFields, type_count> fields{};
    // Every structure cell is a level (0x08) cell of a closed yard (0x04).
    std::array<uint8_t, 16> yard{};
    sim::combat_state::WeaponRegistry weapons;
    Services services;
    Scenario scenario;
    std::unique_ptr<sim::match_runtime::Match> match;

    void type(
        Type index,
        const char* name,
        uint8_t bm_code,
        int16_t footprint,
        uint32_t flags,
        uint32_t abilities
    ) {
        auto& t = types[index];
        t.simulation.flags = OA_UNIT_DEF_FLAG_AVAILABLE | flags;
        t.simulation.abilities = abilities;
        t.simulation.maximum_health = max_damage;
        t.footprint_x = t.footprint_z = footprint;
        t.bm_code = bm_code;
        t.model = reinterpret_cast<uintptr_t>(model.get());
        t.cob = reinterpret_cast<uintptr_t>(script.get());
        loaded[index].model = model;
        loaded[index].script = script;
        loaded[index].type = t;
        loaded[index].unit_name = name;
        fields[index].definition = &defs[index];
        const auto yard_cells =
            static_cast<std::size_t>(footprint) * static_cast<std::size_t>(footprint);
        fields[index].yard_mask = std::span<const uint8_t>(yard.data(), yard_cells);
        fields[index].runtime_metadata = &metadata;
        fields[index].target_masks = &target_masks;
        fields[index].movement_class = 0;
    }

    Fixture() {
        yard.fill(0x0c);
        map.attribute_width = map.attribute_height = map_cells;
        map.attributes.resize(cells);
        for (auto& plot : plots)
            plot.low_height = plot.high_height = level_ground;
        masks[0].width = masks[0].height = 1;
        masks[0].pixels.assign(1, 1);
        model->objects.resize(1);
        model->objects[0].name = "root";
        // Create returns at once.
        script->code = {sim::script_vm::opcode::return_};
        script->scripts = {{"Create", 0}};
        script->entry_points = {0};
        script->piece_names = {"root"};
        constexpr uint32_t mobile = OA_UNIT_DEF_ABILITY_CAN_MOVE | OA_UNIT_DEF_ABILITY_CAN_PATROL |
                                    OA_UNIT_DEF_ABILITY_CAN_GUARD | OA_UNIT_DEF_ABILITY_CAN_ATTACK;
        type(armbrawl, "ARMBRAWL", 1, 3, OA_UNIT_DEF_FLAG_CAN_FLY, mobile);
        type(armhlt, "ARMHLT", 0, 2, 0, OA_UNIT_DEF_ABILITY_CAN_ATTACK);
        type(corak, "CORAK", 1, 2, 0, mobile);
        type(corfmd, "CORFMD", 0, 2, 0, 0);
        type(corlab, "CORLAB", 0, 4, OA_UNIT_DEF_FLAG_BUILDER, OA_UNIT_DEF_ABILITY_CAN_PATROL);
        type(correap, "CORREAP", 1, 3, 0, mobile);
        type(
            corvalk,
            "CORVALK",
            1,
            3,
            OA_UNIT_DEF_FLAG_CAN_FLY,
            OA_UNIT_DEF_ABILITY_CAN_MOVE | OA_UNIT_DEF_ABILITY_CAN_PATROL |
                OA_UNIT_DEF_ABILITY_CAN_LOAD
        );
        sim::match_runtime::OfflineInputs input{
            map,
            loaded,
            types,
            fields,
            weapons,
            terrain_values,
            masks,
            sight_cells,
            sight_cells,
            16,
            2,
            0,
            30,
            1,
            &scenario,
            {},
            plots,
            sim::visibility_state::AltitudeSightData{
                sight_cells, sight_cells, altitude_cells, altitude_patterns
            }
        };
        match = std::make_unique<sim::match_runtime::Match>(input, services);
        for (uint8_t player = 0; player < 2; ++player) {
            match->simulation().players[player].present = true;
            match->simulation().players[player].status = 1;
        }
    }

    // The `nth` live unit of a type, in slot order.
    const Unit& unit(Type type, std::size_t nth = 0) const {
        for (uint32_t slot = 1; slot < match->state().unit_slot_count; ++slot) {
            const auto& candidate = match->state().units[slot];
            if (candidate.type_index == type && nth-- == 0)
                return candidate;
        }
        throw std::runtime_error("no such unit");
    }

    std::vector<View> queue(const Unit& unit, bool secondary = false) const {
        std::vector<View> records(16);
        records.resize(match->queue_records(unit.id, secondary, records.data(), records.size()));
        return records;
    }
};

data::campaign::MissionUnit entry(
    const char* name,
    const char* ident,
    const char* script,
    int32_t x,
    int32_t z,
    uint8_t player = 2
) {
    data::campaign::MissionUnit unit{};
    unit.unit_name = name;
    unit.ident = ident;
    unit.initial_mission = script;
    unit.x = fixed(x);
    unit.z = fixed(z);
    unit.health_percent = 100;
    unit.player = player;
    return unit;
}

bool at(const View& view, int32_t x, int32_t z) {
    return view.point[0] == fixed(x) && view.point[1] == 0 && view.point[2] == fixed(z);
}

bool selectable(const Unit& unit) {
    return (unit.flags & OA_UNIT_FLAG_SELECTABLE) != 0;
}

// AC01 [unit12] "P 502 1223" then [unit13] "P P 502 1224": the second
// patrol's numbers do not parse, so it reuses the first script's point
// (the point the previous parse left in place) with no dwell. A patrol ends
// the script, so no MakeSelectable follows.
void ac01_patrols_reuse_the_last_point() {
    Fixture f;
    const std::array schema{
        entry("CORAK", nullptr, "P 502 1223", 368, 223),
        entry("CORAK", nullptr, "P P 502 1224", 416, 287)
    };
    sim::match_runtime::create_mission_units(
        *f.match, schema.data(), static_cast<int32_t>(schema.size())
    );
    for (std::size_t index = 0; index < schema.size(); ++index) {
        const auto& unit = f.unit(corak, index);
        CHECK(!selectable(unit));
        const auto queue = f.queue(unit);
        CHECK(queue.size() == 1);
        CHECK(queue[0].kind == patrol && at(queue[0], 502, 1223) && queue[0].parameter_1 == 0);
        // Patrol descriptor 0x412, preserve flags bit 0 and the tail mark.
        CHECK(queue[0].preserve_flags == 0x13 && queue[0].command_flags == (0x04 | tail_mark));
        CHECK(queue[0].flags == 0 && queue[0].target == 0);
    }
    std::cout << "AC01 patrols reuse the last point passed\n";
}

// AC13 CORVALK "w 3500,m 670 2701,u,m 3115 256": a wait of 3500 seconds,
// aircraft moves and an unload at the move's point, then MakeSelectable.
void ac13_transport_script() {
    Fixture f;
    const std::array schema{
        entry("CORVALK", "TRANSPORT5", "w 3500,m 670 2701,u,m 3115 256", 1500, 256)
    };
    sim::match_runtime::create_mission_units(*f.match, schema.data(), 1);
    const auto& unit = f.unit(corvalk);
    CHECK(!selectable(unit));
    const auto queue = f.queue(unit);
    CHECK(queue.size() == 5);
    CHECK(queue[0].kind == wait && queue[0].parameter_1 == 3500 * 30 && queue[0].parameter_2 == 0);
    CHECK(queue[0].preserve_flags == 0x05 && queue[0].command_flags == 0);
    CHECK(queue[1].kind == vtol_move && at(queue[1], 670, 2701));
    CHECK(queue[1].preserve_flags == 0x03 && queue[1].command_flags == 0x04);
    CHECK(queue[2].kind == vtol_unload && at(queue[2], 670, 2701));
    CHECK(queue[2].preserve_flags == 0x01 && queue[2].command_flags == 0x04);
    CHECK(queue[3].kind == vtol_move && at(queue[3], 3115, 256));
    CHECK(queue[4].kind == make_selectable && queue[4].command_flags == tail_mark);
    CHECK(f.queue(unit, true).empty());
    std::cout << "AC13 transport script passed\n";
}

// EXP1AC12 CORFMD "bw 2,": BuildWeapon for two rounds on the secondary
// queue. It does not count as an order, so the unit stays selectable.
void exp1ac12_build_weapon() {
    Fixture f;
    const std::array schema{entry("CORFMD", nullptr, "bw 2,", 1520, 736)};
    sim::match_runtime::create_mission_units(*f.match, schema.data(), 1);
    const auto& unit = f.unit(corfmd);
    CHECK(selectable(unit));
    CHECK(f.queue(unit).empty());
    const auto secondary = f.queue(unit, true);
    CHECK(secondary.size() == 1);
    CHECK(
        secondary[0].kind == build_weapon && secondary[0].parameter_1 == 0 &&
        secondary[0].parameter_2 == 2
    );
    // Descriptor 0x000c0140: kept queue, secondary queue; no tail mark.
    CHECK(secondary[0].preserve_flags == 0x41 && secondary[0].command_flags == 0x01);
    CHECK(secondary[0].flags == 0x0c);
    std::cout << "EXP1AC12 build weapon passed\n";
}

// CC23 ARMBRAWL "wa HLTOWER2,a CORREAP,CORINT,...": waits for an attack on
// the ARMHLT named HLTOWER2, then attacks CORREAPs; the bare type names are
// not commands.
void cc23_wait_then_attack_type() {
    Fixture f;
    const std::array schema{
        entry("ARMHLT", "HLTOWER2", nullptr, 1040, 1200),
        entry("ARMBRAWL", nullptr, "wa HLTOWER2,a CORREAP,CORINT,CORFUS,CORESTOR,CORCOM", 1100, 370)
    };
    sim::match_runtime::create_mission_units(*f.match, schema.data(), 2);
    const auto& tower = f.unit(armhlt);
    const auto& gunship = f.unit(armbrawl);
    CHECK(selectable(tower) && f.queue(tower).empty());
    CHECK(!selectable(gunship));
    const auto queue = f.queue(gunship);
    CHECK(queue.size() == 3);
    CHECK(queue[0].kind == wait_for_attack && queue[0].target == tower.id);
    CHECK(queue[0].preserve_flags == 0x05 && queue[0].command_flags == 0x02);
    CHECK(
        queue[1].kind == attack_unit_type && queue[1].parameter_1 == correap &&
        queue[1].parameter_2 == 0
    );
    CHECK(queue[2].kind == make_selectable);
    std::cout << "CC23 wait then attack type passed\n";
}

// A factory has no movement object: its patrol is QPatrol and its builds
// BuildingBuild.
void factory_orders_follow_the_movement_object() {
    Fixture f;
    const std::array schema{entry("CORLAB", nullptr, "p 900 800,b CORAK 3 100 200", 1000, 600)};
    sim::match_runtime::create_mission_units(*f.match, schema.data(), 1);
    const auto& lab = f.unit(corlab);
    CHECK(lab.movement == 0 && f.match->ground_runtime(lab.id) == nullptr);
    const auto queue = f.queue(lab);
    CHECK(queue.size() == 2);
    CHECK(queue[0].kind == qpatrol && at(queue[0], 900, 800));
    CHECK(
        queue[1].kind == building_build && queue[1].parameter_1 == corak &&
        queue[1].parameter_2 == 3
    );
    // BuildingBuild carries no point: descriptor 0x0010010c.
    CHECK(queue[1].preserve_flags == 0x0d && queue[1].command_flags == (0x01 | tail_mark));
    CHECK(queue[1].flags == 0x10);
    std::cout << "factory orders follow the movement object passed\n";
}

// Immunity sets Unit flag 0x8000, health follows HealthPercentage of the
// type's maxdamage, the heading is the schema angle, and a structure's site
// snaps to its footprint grid at the height of its level cells.
void placement_fields() {
    Fixture f;
    auto immune = entry("CORAK", nullptr, nullptr, 101, 99);
    immune.flags =
        data::campaign::mission_unit_flag::immunity | data::campaign::mission_unit_flag::ai_ignore;
    immune.health_percent = 37;
    immune.angle = 0x4000;
    const std::array schema{immune, entry("CORLAB", nullptr, nullptr, 1000, 600, 1)};
    sim::match_runtime::create_mission_units(*f.match, schema.data(), 2);
    const auto& tank = f.unit(corak);
    CHECK((tank.flags & OA_UNIT_FLAG_NOT_SELECTABLE) != 0 && selectable(tank));
    CHECK(tank.health == static_cast<int16_t>(max_damage * 37 / 100));
    CHECK(tank.heading == 0x4000);
    CHECK(tank.position.x == fixed(101) && tank.position.z == fixed(99));
    CHECK(tank.owner_index == 1);
    // 4x4 footprint: cell (x - 4 * 8 + 8) / 16 = 61 and 36, centred at
    // (4 + 2 * cell) * 8 = 1008 and 608.
    const auto& lab = f.unit(corlab);
    CHECK((lab.flags & OA_UNIT_FLAG_NOT_SELECTABLE) == 0);
    CHECK(lab.health == static_cast<int16_t>(max_damage));
    CHECK(lab.position.x == fixed(1008) && lab.position.z == fixed(608));
    CHECK(lab.position.y == fixed(level_ground));
    CHECK(lab.owner_index == 0);
    std::cout << "placement fields passed\n";
}

// An empty schema turns the mission's conditions off; a unit for an inactive
// player slot raises the game's fatal report.
void empty_schema_and_inactive_player() {
    {
        Fixture f;
        CHECK(f.match->scenario_controller().enabled == 1);
        sim::match_runtime::create_mission_units(*f.match, nullptr, 0);
        CHECK(f.match->scenario_controller().enabled == 0);
    }
    Fixture f;
    const std::array schema{entry("CORAK", nullptr, nullptr, 100, 100, 3)};
    CHECK(!sim::match_runtime::create_mission_units(*f.match, schema.data(), 1));
    CHECK(
        f.match->fault() != nullptr &&
        std::string(f.match->fault()) == "Player number 3 invalid for unit CORAK"
    );
    CHECK(f.match->scenario_controller().enabled == 1);
    std::cout << "empty schema and inactive player passed\n";
}

} // namespace

// A campaign unit's "i <name>" boards it into the named unit, in its hold
// (no piece) with its movement layer 0, with no check of the carrier's size or
// capacity: the Valkyrie here can carry nothing, yet takes both.
void boarding_ignores_the_carrier_capacity() {
    Fixture f;
    const std::array schema{
        entry("CORVALK", "TRANSPORT5", nullptr, 1500, 256),
        entry("CORREAP", nullptr, "i TRANSPORT5", 1400, 256),
        entry("CORAK", nullptr, "i TRANSPORT5", 1300, 256)
    };
    sim::match_runtime::create_mission_units(*f.match, schema.data(), 3);
    const auto& carrier = f.unit(corvalk);
    for (const auto type : {correap, corak}) {
        const auto& aboard = f.unit(type);
        CHECK(oa::oa_unit_slot_from_ref(aboard.attach_parent) == carrier.id);
        CHECK(static_cast<int8_t>(aboard.attach_piece) == -1);
        const auto* movement = f.match->ground_runtime(aboard.id);
        CHECK(movement && (movement->movement.flags & 3) == 0);
        const auto queue = f.queue(aboard);
        CHECK(!queue.empty() && queue[0].kind == be_carried);
    }
    CHECK(f.match->loaded_child_count(carrier.id) == 2);
    std::cout << "boarding ignores the carrier capacity passed\n";
}

int main() {
    try {
        ac01_patrols_reuse_the_last_point();
        ac13_transport_script();
        exp1ac12_build_weapon();
        cc23_wait_then_attack_type();
        factory_orders_follow_the_movement_object();
        placement_fields();
        empty_schema_and_inactive_player();
        boarding_ignores_the_carrier_capacity();
    } catch (const std::exception& error) {
        std::cerr << "match-mission-units: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
