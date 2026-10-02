// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Orders on a structure, which has no movement object (Unit.movement; unit
// creation makes one for bmcode 1 types only): a tower hit by an enemy
// attacks it without moving (command case 3), is paralysed, holds a fire order and
// ends a Standby, and the match ticks through all of it.
#include "../src/tick_internal.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace oa;

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string(#x) + " at line " + std::to_string(__LINE__));    \
    } while (false)

namespace {
constexpr uint16_t tank_type = 1;
constexpr uint16_t tower_type = 2;
constexpr size_t type_count = 3;
constexpr uint32_t map_cells = 16;

constexpr uint8_t weapon_hit = 1;
constexpr uint8_t paralyzer_hit = 2;
// Mission-table indices.
constexpr uint8_t attack_chase = 6;
constexpr uint8_t attack_no_move = 8;
constexpr uint8_t paralyze = 27;
constexpr uint8_t standby = 41;
constexpr uint8_t standing_fire_order = 43;
constexpr uint32_t mission_invalid = 7;
constexpr uint32_t mission_done = 5;
// Unit.flags bits 18..19 and 20..21: a tower's FBI defaults are roam and fire at will.
constexpr uint32_t roam = 2;
constexpr uint32_t fire_at_will = 2;
constexpr uint32_t hold_fire = 0;

struct Services : sim::match_runtime::OfflineServices {
    void activation_sound(sim::unit_spawn::Slot&, sim::unit_activation::Sound) override {}

    void command_sound(sim::unit_spawn::Slot&, uint32_t) override {}

    void refresh_selected_unit(sim::unit_spawn::Slot&) override {}

    void emit_sfx(sim::unit_spawn::Slot&, uint32_t, int32_t) override {}

    void attachment_notification(sim::unit_spawn::Slot&, uint32_t) override {}

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

// Two players on a flat 16x16-cell map; the tank and the tower carry the same
// 400-unit gun, whose script aims at once and counts shots in static 1.
struct Fixture {
    formats::tnt::Map map;
    std::vector<sim::visibility_state::TerrainCell> terrain =
        std::vector<sim::visibility_state::TerrainCell>(256);
    std::array<sim::visibility_state::SightMask, 1> masks{};
    std::vector<sim::spatial_state::Plot> plots = std::vector<sim::spatial_state::Plot>(256);
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
        model->objects[0].vertices = {{0, 0, 0}, {0, 20 << 16, 0}};
        using namespace sim::script_vm;
        script->code = {
            opcode::return_,
            opcode::push_constant,
            1,
            opcode::pop_static,
            0,
            opcode::push_constant,
            1,
            opcode::return_,
            opcode::push_static,
            1,
            opcode::push_constant,
            1,
            opcode::add,
            opcode::pop_static,
            1,
            opcode::return_,
            opcode::return_,
        };
        script->scripts = {{"Create", 0}, {"AimPrimary", 1}, {"FirePrimary", 8}, {"RockUnit", 16}};
        script->entry_points = {0, 1, 8, 16};
        script->header.static_variable_count = 2;
        script->piece_names = {"root"};
        for (size_t i = 1; i < type_count; ++i) {
            types[i].simulation.flags = OA_UNIT_DEF_FLAG_AVAILABLE | OA_UNIT_DEF_FLAG_HAS_WEAPONS;
            types[i].simulation.abilities = OA_UNIT_DEF_ABILITY_CAN_ATTACK;
            types[i].simulation.maximum_health = 1000;
            types[i].footprint_x = types[i].footprint_z = 1;
            types[i].model = reinterpret_cast<uintptr_t>(model.get());
            types[i].cob = reinterpret_cast<uintptr_t>(script.get());
            loaded[i].model = model;
            loaded[i].script = script;
            defs[i].sight_distance = 160;
            defs[i].weapon1 = "TESTGUN";
            defs[i].can_attack = true;
            defs[i].energy_storage = defs[i].metal_storage = 1000.0F;
            fields[i].definition = &defs[i];
            fields[i].yard_mask = yard;
            fields[i].runtime_metadata = &metadata;
            fields[i].target_masks = &target_masks;
        }
        types[tank_type].bm_code = 1;
        defs[tank_type].can_move = defs[tank_type].can_guard = defs[tank_type].can_patrol = true;
        defs[tank_type].acceleration_fixed = defs[tank_type].brake_rate_fixed = 65536;
        defs[tank_type].max_velocity_fixed = 2 * 65536;
        defs[tank_type].turn_rate = 1024;
        fields[tank_type].movement_class = 0;
        types[tower_type].bm_code = 0;
        for (size_t i = 1; i < type_count; ++i)
            loaded[i].type = types[i];
        (void)sim::combat_state::install_weapon_text(
            weapons,
            "[TESTGUN]{id=1; reloadtime=0.1; range=400; lineofsight=1; weaponvelocity=100; "
            "turret=1; [DAMAGE]{default=10;}}"
        );
        sim::match_runtime::OfflineInputs input{
            map,
            loaded,
            types,
            fields,
            weapons,
            terrain,
            masks,
            8,
            8,
            4,
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
        for (uint8_t player = 0; player < 2; ++player) {
            match->simulation().players[player].present = true;
            match->simulation().players[player].status = 1;
            std::array<uint8_t, 10> allies{};
            allies[player] = 1;
            match->configure_player_alliances(player, allies);
            if (player == 0)
                match->configure_outcomes(0, allies, false, false, false);
        }
    }

    sim::unit_spawn::Slot&
    spawn(uint8_t player, uint16_t type, uint32_t x, uint32_t z, uint32_t move_order) {
        auto* slot = match->create({player, type, {x << 16, 32u << 16, z << 16}, true, 1, 0});
        CHECK(slot && slot->unit);
        slot->unit->flags =
            (slot->unit->flags & ~(OA_UNIT_FLAG_MOVE_ORDER_MASK | OA_UNIT_FLAG_FIRE_ORDER_MASK)) |
            (move_order << OA_UNIT_FLAG_MOVE_ORDER_SHIFT) |
            (fire_at_will << OA_UNIT_FLAG_FIRE_ORDER_SHIFT);
        return *slot;
    }

    sim::unit_spawn::Slot& tower(uint8_t player, uint32_t x, uint32_t z) {
        auto& slot = spawn(player, tower_type, x, z, roam);
        CHECK(!slot.unit->object_present && !match->ground_runtime(slot.unit_index));
        return slot;
    }

    void run(uint32_t ticks) {
        for (uint32_t i = 0; i < ticks; ++i) {
            ++match->simulation().tick;
            match->tick();
        }
    }

    int32_t shots_from(const sim::unit_spawn::Slot& slot) {
        auto* instance = match->instance(slot.unit_index);
        CHECK(instance && instance->script());
        return instance->script()->vm().static_value(1).value_or(-1);
    }

    uint32_t dispatch(
        sim::unit_spawn::Slot& unit, sim::simulation_state::Order& order, uint32_t events = 0
    ) {
        sim::match_runtime::TickHost host(*match);
        return host.dispatch_mission(match->state(), unit.record, order, events);
    }
};

bool head_is(const sim::unit_spawn::Slot& slot, uint8_t kind) {
    return slot.unit->primary != nullptr && slot.unit->primary->kind == kind;
}

// CC14/CC21 (ARMLLT) and EXP1CC12 (CORRL): the retaliation
// attack resolves by Unit.movement, so the tower gets Attack_NoMove where a tank
// gets Attack_Chase, and Attack_NoMove runs without a movement object.
void tower_retaliates_without_moving() {
    Fixture f;
    auto& tower = f.tower(0, 64, 64);
    auto& tank = f.spawn(1, tank_type, 160, 64, 1);
    // Attack_Chase is rejected at phase 0 when Unit.movement is null.
    auto& chase = f.match->insert_ground_order(
        tower.unit_index, attack_chase, std::nullopt, 0, tank.unit_index
    );
    CHECK(f.dispatch(tower, chase) == mission_invalid);
    f.match->stop_orders(tower.unit_index);
    f.run(1);
    f.match->apply_damage_event(tower, &tank, 1, weapon_hit, 0);
    CHECK(head_is(tower, attack_no_move));
    f.run(30);
    CHECK(f.shots_from(tower) > 0);

    auto& chaser = f.spawn(0, tank_type, 64, 96, 1);
    f.match->apply_damage_event(chaser, &tank, 1, weapon_hit, 0);
    CHECK(head_is(chaser, attack_chase));
    std::cout << "tower retaliates without moving passed\n";
}

// Paralyze touches the movement object only to release an order goal, and
// a structure's orders hold none.
void tower_is_paralysed() {
    Fixture f;
    auto& tower = f.tower(0, 64, 64);
    auto& tank = f.spawn(1, tank_type, 160, 64, 1);
    f.run(1);
    f.match->apply_damage_event(tower, &tank, 30, paralyzer_hit, 0);
    CHECK(head_is(tower, paralyze));
    f.run(1);
    CHECK(tower.record.state_flags & sim::unit_health::paralyzed_state_flag);
    f.run(60);
    CHECK(!(tower.record.state_flags & sim::unit_health::paralyzed_state_flag));
    std::cout << "tower is paralysed passed\n";
}

// Standby ends at phase 0 when Unit.movement is null; the fire order only
// rewrites the unit and weapon flags.
void tower_standby_and_fire_order() {
    Fixture f;
    auto& tower = f.tower(0, 64, 64);
    auto& idle = f.match->insert_ground_order(tower.unit_index, standby);
    CHECK(f.dispatch(tower, idle) == mission_invalid);
    f.match->stop_orders(tower.unit_index);

    auto& hold = f.match->insert_ground_order(
        tower.unit_index, standing_fire_order, std::nullopt, hold_fire
    );
    CHECK(f.dispatch(tower, hold) == mission_done);
    CHECK(
        ((tower.unit->flags & OA_UNIT_FLAG_FIRE_ORDER_MASK) >> OA_UNIT_FLAG_FIRE_ORDER_SHIFT) ==
        hold_fire
    );
    std::cout << "tower standby and fire order passed\n";
}
} // namespace

int main() {
    try {
        tower_retaliates_without_moving();
        tower_is_paralysed();
        tower_standby_and_fire_order();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
