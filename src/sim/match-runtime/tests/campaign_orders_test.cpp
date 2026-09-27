// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The orders InitialMission scripts queue, on the unit classes that queue
// them in the shipped missions: the dispatcher runs the kinds the script
// parser queues by name and the kinds the command resolver gives each class,
// and an attack resolves by the unit's movement object (Unit.movement).
#include "../src/tick_internal.hpp"
#include "oa/data/mission_types.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace oa;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #x);   \
    } while (false)

namespace {
constexpr int32_t map_cells = 32;
constexpr uint16_t tank_type = 1;
constexpr uint16_t factory_type = 2; // bmcode 0: no movement object
constexpr uint16_t tower_type = 3;   // bmcode 0, armed
constexpr uint16_t aircraft_type = 4;
constexpr uint16_t transport_type = 5;
constexpr size_t type_count = 6;

// Mission-table indices of the kinds the command resolver names here.
constexpr uint8_t attack_chase = 6;
constexpr uint8_t attack_no_move = 8;
constexpr uint8_t follow_ground = 18;
constexpr uint8_t ground_unload = 21;
constexpr uint8_t move_ground = 26;
constexpr uint8_t patrol = 29;
constexpr uint8_t qmove = 30;
constexpr uint8_t qpatrol = 31;
constexpr uint8_t suppress = 46;
constexpr uint8_t vtol_follow = 49;
constexpr uint8_t vtol_move = 55;
constexpr uint8_t vtol_patrol = 56;
constexpr uint8_t air_to_ground = 4;

constexpr uint32_t last_result = 9;
// 'd' queues SelfDestructFG with a countdown (first parameter) of 1.
constexpr uint32_t self_destruct_now = 1;

struct Services : sim::match_runtime::OfflineServices {
    void command_sound(sim::unit_spawn::Slot&, uint32_t) override {}

    void activation_sound(sim::unit_spawn::Slot&, sim::unit_activation::Sound) override {}

    void attachment_notification(sim::unit_spawn::Slot&, uint32_t) override {}

    void refresh_selected_unit(sim::unit_spawn::Slot&) override {}

    void emit_sfx(sim::unit_spawn::Slot&, uint32_t, int32_t) override {}

    void explode_piece(sim::unit_spawn::Slot&, uint32_t, int32_t) override {}

    void attach_unit(sim::unit_spawn::Slot&, int32_t, int32_t, int32_t) override {}

    void drop_unit(sim::unit_spawn::Slot&, int32_t) override {}

    void refresh_plot_height_range(std::array<int16_t, 2>, std::array<int16_t, 2>) override {}

    void notify_object_footprint_removed(sim::spatial_state::Unit&, uint32_t) override {}

    void notify_footprint_changed(std::array<int16_t, 2>, std::array<int16_t, 2>) override {}
};

struct Scenario : sim::scenario::DefinitionHost {
    int32_t integer(std::string_view, int32_t fallback) override { return fallback; }

    std::optional<std::string> text(std::string_view) override { return std::nullopt; }
};

// Dispatches the order once and applies the sweep's phase rule for results 0 and 1.
uint32_t step(
    sim::match_runtime::Match& match,
    sim::unit_spawn::Slot& unit,
    sim::simulation_state::Order& order,
    uint32_t events = 0
) {
    sim::match_runtime::TickHost host(match);
    order.wait_events = 0;
    const auto result = host.dispatch_mission(match.state(), unit.record, order, events);
    if (result == 0)
        order.phase = 0;
    else if (result == 1)
        ++order.phase;
    return result;
}

std::array<uint32_t, 3> at(int32_t x, int32_t z) {
    return {static_cast<uint32_t>(x) << 16, 0, static_cast<uint32_t>(z) << 16};
}

sim::ground_orders::Point point(int32_t x, int32_t z) {
    return {x << 16, 0, z << 16};
}

struct Fixture {
    formats::tnt::Map map;
    std::vector<sim::visibility_state::TerrainCell> terrain =
        std::vector<sim::visibility_state::TerrainCell>(map_cells * map_cells);
    std::array<sim::visibility_state::SightMask, 1> masks{};
    std::shared_ptr<formats::objects3d::Model> model =
        std::make_shared<formats::objects3d::Model>();
    std::shared_ptr<formats::cob::CobProgram> script = std::make_shared<formats::cob::CobProgram>();
    std::array<sim::unit_spawn::LoadedType, type_count> loaded;
    std::array<sim::unit_spawn::Type, type_count> types;
    std::array<data::unit_definitions::UnitDefinition, type_count> defs;
    data::unit_definitions::UnitTargetCategoryMasks target_masks;
    data::unit_definitions::RuntimeDefinitionMetadata metadata;
    std::array<sim::match_runtime::RuntimeTypeFields, type_count> fields{};
    std::array<uint8_t, 1> yard{4};
    std::vector<sim::spatial_state::Plot> plots =
        std::vector<sim::spatial_state::Plot>(map_cells * map_cells);
    sim::combat_state::WeaponRegistry weapons;
    Services services;
    Scenario scenario;
    std::unique_ptr<sim::match_runtime::Match> match;

    Fixture() {
        map.attribute_width = map.attribute_height = map_cells;
        map.attributes.resize(map_cells * map_cells);
        masks[0].width = masks[0].height = 1;
        masks[0].pixels = {1};
        model->objects.resize(1);
        model->objects[0].name = "root";
        script->code = {sim::script_vm::opcode::return_};
        script->scripts = {{"Create", 0}};
        script->entry_points = {0};
        script->piece_names = {"root"};
        for (size_t i = 1; i < type_count; ++i) {
            loaded[i].model = model;
            loaded[i].script = script;
            types[i].simulation.flags = OA_UNIT_DEF_FLAG_AVAILABLE;
            types[i].simulation.maximum_health = 100;
            types[i].footprint_x = types[i].footprint_z = 1;
            types[i].bm_code = 1;
            types[i].model = reinterpret_cast<uintptr_t>(model.get());
            types[i].cob = reinterpret_cast<uintptr_t>(script.get());
            defs[i].sight_distance = 200;
            defs[i].acceleration_fixed = 65536;
            defs[i].brake_rate_fixed = 65536;
            defs[i].max_velocity_fixed = 2 * 65536;
            defs[i].turn_rate = 1024;
            defs[i].energy_storage = defs[i].metal_storage = 1000.0F;
            defs[i].can_move = defs[i].can_patrol = true;
            fields[i].definition = &defs[i];
            fields[i].yard_mask = yard;
            fields[i].runtime_metadata = &metadata;
            fields[i].target_masks = &target_masks;
            fields[i].movement_class = 0;
        }
        for (const auto armed : {tank_type, tower_type, aircraft_type}) {
            types[armed].simulation.flags |= OA_UNIT_DEF_FLAG_HAS_WEAPONS;
            types[armed].simulation.abilities |= OA_UNIT_DEF_ABILITY_CAN_ATTACK;
            defs[armed].weapon1 = "TESTGUN";
            defs[armed].can_attack = true;
        }
        for (const auto guard : {tank_type, aircraft_type})
            defs[guard].can_guard = true;
        for (const auto structure : {factory_type, tower_type}) {
            types[structure].bm_code = 0;
            fields[structure].movement_class.reset();
        }
        types[factory_type].simulation.flags |= OA_UNIT_DEF_FLAG_BUILDER;
        defs[tower_type].can_move = defs[tower_type].can_patrol = false;
        types[aircraft_type].simulation.flags |= OA_UNIT_DEF_FLAG_CAN_FLY;
        defs[aircraft_type].can_fly = true;
        defs[aircraft_type].cruise_altitude = 60;
        defs[transport_type].can_load = true;
        defs[transport_type].transport_capacity = 1;
        defs[transport_type].transport_size = 2;
        for (size_t i = 1; i < type_count; ++i)
            loaded[i].type = types[i];
        weapons.install_tdf_section(1, "TESTGUN", "0.1");
        weapons.install_target_fields(1, "400", "1", "0", "0", "0", "0", "10", "100", "", "1");
        sim::match_runtime::OfflineInputs input{
            map,
            loaded,
            types,
            fields,
            weapons,
            terrain,
            masks,
            16,
            16,
            12,
            2,
            0,
            30,
            1,
            &scenario,
            {},
            plots,
            {}
        };
        match = std::make_unique<sim::match_runtime::Match>(input, services);
        match->configure_strategic_environment({0, 0.5F, 0});
        for (uint8_t p = 0; p < 2; ++p) {
            match->simulation().players[p].present = true;
            match->simulation().players[p].status = p == 0 ? 1 : 2;
            std::array<uint8_t, 10> allies{};
            allies[p] = 1;
            match->configure_player_alliances(p, allies);
        }
        match->state().game.tick = 100;
    }

    uint32_t tick() const { return match->state().game.tick; }

    sim::unit_spawn::Slot& spawn(uint8_t player, uint16_t type, int32_t x, int32_t z) {
        auto* slot = match->create({player, type, at(x, z), true, 1, 0});
        CHECK(slot && slot->unit);
        return *slot;
    }
};

// Command case 3 against a unit: a structure has no movement object, so
// it attacks without moving.
void attack_resolution() {
    Fixture f;
    auto& tank = f.spawn(1, tank_type, 100, 100);
    auto& tower = f.spawn(1, tower_type, 200, 100);
    auto& enemy = f.spawn(0, tank_type, 150, 150);
    CHECK(!tower.unit->object_present && !f.match->ground_runtime(tower.unit_index));
    CHECK(f.match->issue_attack(tank.unit_index, enemy.unit_index, true));
    CHECK(f.match->orders(tank.unit_index).primary->kind == attack_chase);
    CHECK(f.match->issue_attack(tower.unit_index, enemy.unit_index, true));
    CHECK(f.match->orders(tower.unit_index).primary->kind == attack_no_move);
}

// The kinds the command resolver gives the parser's move, attack, unload, guard and
// patrol commands on each class reach a handler; a structure's move and
// patrol are the queued orders for its products.
void chosen_kinds_dispatch() {
    Fixture f;
    auto& tank = f.spawn(1, tank_type, 100, 100);
    auto& other_tank = f.spawn(1, tank_type, 120, 100);
    auto& factory = f.spawn(1, factory_type, 160, 160);
    auto& aircraft = f.spawn(1, aircraft_type, 100, 200);
    auto& transport = f.spawn(1, transport_type, 60, 60);

    const struct {
        sim::unit_spawn::Slot& unit;
        uint8_t kind;
    } placed[] = {
        {tank, move_ground},
        {tank, patrol},
        {tank, suppress},
        {aircraft, vtol_move},
        {aircraft, vtol_patrol},
        {aircraft, air_to_ground},
        {transport, ground_unload},
    };

    for (const auto& c : placed) {
        auto& order = f.match->insert_ground_order(c.unit.unit_index, c.kind, point(40, 40));
        CHECK(step(*f.match, c.unit, order) <= last_result);
        f.match->stop_orders(c.unit.unit_index);
    }
    for (auto* guard : {&tank, &aircraft}) {
        auto& order = f.match->issue_guard(guard->unit_index, other_tank.unit_index, false);
        CHECK(order.kind == (guard == &tank ? follow_ground : vtol_follow));
        CHECK(step(*f.match, *guard, order) <= last_result);
        f.match->stop_orders(guard->unit_index);
    }
    // QMove and QPatrol wait a minute, then pass the order on.
    for (const auto kind : {qmove, qpatrol}) {
        auto& order = f.match->insert_ground_order(factory.unit_index, kind, point(40, 40));
        CHECK(step(*f.match, factory, order) == 6 && order.wake_tick == f.tick() + 0x3c);
        f.match->stop_orders(factory.unit_index);
    }
}

// The kinds the parser queues by name, with the results their handlers
// give in 3.1c.
void named_kinds_dispatch() {
    Fixture f;
    auto& tank = f.spawn(1, tank_type, 100, 100);
    auto& factory = f.spawn(1, factory_type, 160, 160);
    auto& transport = f.spawn(1, transport_type, 60, 60);
    const auto kind = [](const char* name) { return data::mission_types::index_for_name(name); };

    // MakeSelectable clears the unselectable bit, sets bit 0x20, finishes.
    factory.record.flags =
        (factory.record.flags | OA_UNIT_FLAG_NOT_SELECTABLE) & ~OA_UNIT_FLAG_SELECTABLE;
    auto& selectable = f.match->insert_ground_order(factory.unit_index, kind("MAKESELECTABLE"));
    CHECK(step(*f.match, factory, selectable) == 5);
    CHECK(
        !(factory.record.flags & OA_UNIT_FLAG_NOT_SELECTABLE) &&
        (factory.record.flags & OA_UNIT_FLAG_SELECTABLE)
    );
    f.match->stop_orders(factory.unit_index);

    // Wait with no search radius: "w 20" waits 600 ticks, then ends.
    auto& wait =
        f.match->insert_ground_order(factory.unit_index, kind("WAIT"), std::nullopt, 20 * 30);
    CHECK(
        step(*f.match, factory, wait) == 1 && wait.wake_tick == f.tick() + 600 &&
        (wait.wait_events & 1)
    );
    CHECK(step(*f.match, factory, wait) == 5);
    f.match->stop_orders(factory.unit_index);

    // WaitForAttack with nothing to watch finishes at once.
    auto& watch = f.match->insert_ground_order(factory.unit_index, kind("WAITFORATTACK"));
    CHECK(step(*f.match, factory, watch) == 5);
    f.match->stop_orders(factory.unit_index);

    // AttackUType phase 0 waits 1 to 0x5a ticks; a type that cannot attack fails.
    auto& hunt = f.match->insert_ground_order(tank.unit_index, kind("ATTACKUTYPE"));
    CHECK(step(*f.match, tank, hunt) == 1);
    CHECK(hunt.wake_tick > f.tick() && hunt.wake_tick <= f.tick() + 0x5a);
    f.match->stop_orders(tank.unit_index);
    auto& unarmed = f.match->insert_ground_order(transport.unit_index, kind("ATTACKUTYPE"));
    CHECK(step(*f.match, transport, unarmed) == 7);
    f.match->stop_orders(transport.unit_index);

    // SelfDestruct with a countdown of 1: the unit blows up at once.
    auto& destruct = f.match->insert_ground_order(
        tank.unit_index, kind("SELFDESTRUCTFG"), std::nullopt, self_destruct_now
    );
    CHECK(step(*f.match, tank, destruct) == 5);
    CHECK(tank.record.flags & OA_UNIT_FLAG_DEATH_PENDING);
}
} // namespace

int main() {
    try {
        attack_resolution();
        chosen_kinds_dispatch();
        named_kinds_dispatch();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "campaign orders passed\n";
    return 0;
}
