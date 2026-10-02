// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Campaign victory and defeat conditions in the running match: each kind
// registered from a GlobalHeader and driven by unit deaths, captures and the
// local player's 30-tick checks, as the player slot update runs them.
#include "combat_fixture.hpp"
#include "oa/sim/match_runtime/mission_unit_binding.hpp"
#include "oa/base/text.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace oa;
using combat_fixture::Services;

// Six 30-tick checks: the one that starts the countdown and five more.
constexpr uint32_t outcome_ticks = 200;
constexpr uint16_t mobile_type = 1;    // TESTUNIT, bmcode 1
constexpr uint16_t structure_type = 2; // TESTWALL, bmcode 0
constexpr uint8_t local = 0;
constexpr uint8_t enemy = 1;
constexpr const char* victory_cue = "Victory Condition";

// A GlobalHeader of text and integer keys.
struct Header : sim::scenario::DefinitionHost {
    std::vector<std::pair<std::string, std::string>> texts;
    std::vector<std::pair<std::string, int32_t>> integers;

    int32_t integer(std::string_view key, int32_t fallback) override {
        for (const auto& [name, value] : integers)
            if (name == key)
                return value;
        return fallback;
    }

    std::optional<std::string> text(std::string_view key) override {
        for (const auto& [name, value] : texts)
            if (name == key)
                return value;
        return std::nullopt;
    }
};

struct Options {
    Header header;
    // Plot height 60 over sea level 30 from this cell column east.
    std::optional<uint32_t> high_ground_from;
};

// A campaign on a 16x16-cell map with an unarmed mobile type and an unarmed
// structure, the local player 0 against player 1.
struct Fixture {
    formats::tnt::Map map;
    std::vector<sim::visibility_state::TerrainCell> terrain_values =
        std::vector<sim::visibility_state::TerrainCell>(256);
    std::array<sim::visibility_state::SightMask, 1> masks{};
    std::vector<sim::visibility_state::AltitudeCell> altitude_cells =
        std::vector<sim::visibility_state::AltitudeCell>(64);
    std::vector<sim::visibility_state::AltitudeSightPattern> altitude_patterns =
        std::vector<sim::visibility_state::AltitudeSightPattern>(6);
    std::shared_ptr<formats::objects3d::Model> model =
        std::make_shared<formats::objects3d::Model>();
    std::shared_ptr<formats::cob::CobProgram> script = std::make_shared<formats::cob::CobProgram>();
    std::array<sim::unit_spawn::LoadedType, 3> loaded;
    std::array<sim::unit_spawn::Type, 3> types;
    std::array<data::unit_definitions::UnitDefinition, 3> defs;
    data::unit_definitions::UnitTargetCategoryMasks target_masks;
    data::unit_definitions::RuntimeDefinitionMetadata metadata;
    std::vector<sim::spatial_state::Plot> collision_plots =
        std::vector<sim::spatial_state::Plot>(256);
    std::array<sim::match_runtime::RuntimeTypeFields, 3> fields{};
    std::array<uint8_t, 4> yard{4, 4, 4, 4};
    sim::combat_state::WeaponRegistry weapons;
    Services services;
    Header header;
    std::unique_ptr<sim::match_runtime::Match> match;
    std::vector<std::string> sounds;

    explicit Fixture(Options options) : header(std::move(options.header)) {
        map.attribute_width = map.attribute_height = 16;
        map.attributes.resize(256);
        if (options.high_ground_from) {
            constexpr uint8_t high = 60;
            map.sea_level = 30;
            for (uint32_t cell = 0; cell < 256; ++cell) {
                if (cell % 16 < *options.high_ground_from)
                    continue;
                map.attributes[cell].height = high;
                collision_plots[cell].low_height = collision_plots[cell].high_height = high;
            }
        }
        masks[0].width = masks[0].height = 1;
        masks[0].pixels.assign(1, 1);
        model->objects.resize(1);
        model->objects[0].name = "root";
        model->objects[0].vertices = {{0, 0, 0}, {0, 20 << 16, 0}};
        script->code = {sim::script_vm::opcode::return_};
        script->scripts = {{"Create", 0}};
        script->entry_points = {0};
        script->piece_names = {"root"};
        const char* names[3] = {"", "TESTUNIT", "TESTWALL"};
        for (uint16_t type = 1; type < 3; ++type) {
            types[type].simulation.flags = OA_UNIT_DEF_FLAG_AVAILABLE;
            types[type].simulation.maximum_health = 1000;
            types[type].model = reinterpret_cast<uintptr_t>(model.get());
            types[type].cob = reinterpret_cast<uintptr_t>(script.get());
            loaded[type].model = model;
            loaded[type].script = script;
            loaded[type].unit_name = names[type];
            defs[type].sight_distance = 32;
            fields[type].definition = &defs[type];
            fields[type].yard_mask = yard;
            fields[type].runtime_metadata = &metadata;
            fields[type].target_masks = &target_masks;
        }
        types[mobile_type].footprint_x = types[mobile_type].footprint_z = 1;
        types[mobile_type].bm_code = 1;
        defs[mobile_type].can_move = true;
        defs[mobile_type].acceleration_fixed = 65536;
        defs[mobile_type].brake_rate_fixed = 65536;
        defs[mobile_type].max_velocity_fixed = 2 * 65536;
        defs[mobile_type].turn_rate = 1024;
        fields[mobile_type].movement_class = 0;
        types[structure_type].footprint_x = types[structure_type].footprint_z = 2;
        types[structure_type].bm_code = 0;
        for (uint16_t type = 1; type < 3; ++type)
            loaded[type].type = types[type];
        sim::match_runtime::OfflineInputs input{
            map,
            loaded,
            types,
            fields,
            weapons,
            terrain_values,
            masks,
            8,
            8,
            8,
            2,
            0,
            30,
            1,
            &header,
            {},
            collision_plots,
            sim::visibility_state::AltitudeSightData{8, 8, altitude_cells, altitude_patterns}
        };
        match = std::make_unique<sim::match_runtime::Match>(input, services);
        match->configure_strategic_environment({0, 0.5F, 0});
        for (uint8_t player = 0; player < 2; ++player) {
            match->simulation().players[player].present = true;
            match->simulation().players[player].status = 1;
        }
        for (uint8_t player = 0; player < 2; ++player) {
            std::array<uint8_t, 10> allies{};
            allies[player] = 1;
            match->configure_player_alliances(player, allies);
            if (player == local)
                match->configure_outcomes(local, allies, true, true);
        }
        match->named_sound = {this, [](void* context, const char* name) {
                                  static_cast<Fixture*>(context)->sounds.emplace_back(name);
                              }};
    }

    sim::unit_spawn::Slot&
    spawn(uint8_t player, uint16_t type, uint32_t x, uint32_t z, bool finished = true) {
        auto* slot = match->create({player, type, {x << 16, 0, z << 16}, finished, 1, 0});
        CHECK(slot && slot->unit);
        return *slot;
    }

    void run(uint32_t ticks) {
        for (uint32_t i = 0; i < ticks; ++i) {
            ++match->simulation().tick;
            match->tick();
        }
    }

    void kill(sim::unit_spawn::Slot& victim) {
        victim.record.damage_kind = static_cast<uint8_t>(sim::match_runtime::DeathKind::weapon);
        match->teardown_dead_unit(victim);
    }

    int cues() const {
        int count = 0;
        for (const auto& sound : sounds)
            count += sound == victory_cue;
        return count;
    }

    sim::scenario::Outcome outcome() const { return match->outcome(); }
};

Options conditions(
    std::vector<std::pair<std::string, std::string>> texts,
    std::vector<std::pair<std::string, int32_t>> integers = {}
) {
    Options options;
    options.header.texts = std::move(texts);
    options.header.integers = std::move(integers);
    return options;
}

// The stored-result query only returns the stored result: an enemy-owned protected type is
// no defeat until one of its kind dies with fewer than two left to players 0
// and 1. A captured copy stands before the captured unit dies, so the capture
// leaves two.
void all_units_killed_of_type_waits_for_the_kill() {
    Fixture f(conditions({{"AllUnitsKilledOfType", "TESTWALL"}}));
    auto& own = f.spawn(local, mobile_type, 32, 32);
    auto& gate = f.spawn(enemy, structure_type, 160, 160);
    f.spawn(enemy, mobile_type, 224, 224);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::ongoing);

    f.match->capture_unit(gate, own);
    f.run(1);
    CHECK(gate.record.type_index == 0);
    sim::unit_spawn::Slot* copy = nullptr;
    for (auto& slot : f.match->world().slots)
        if (slot.unit && slot.record.owner_index == local &&
            slot.record.type_index == structure_type)
            copy = &slot;
    CHECK(copy != nullptr);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::ongoing);

    f.kill(*copy);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::defeat);
    std::cout << "all units killed of type waits for the kill passed\n";
}

// AC01's ARMGATE is the player's own, held by "o 0 0,w 3600 0,": the script
// clears its selectable bit 0x20 for the hour's wait. The query still returns
// only the stored result, so the gate is lost by its death alone.
void a_script_held_protected_unit_is_no_defeat() {
    Fixture f(conditions({{"AllUnitsKilledOfType", "TESTWALL"}}));
    f.spawn(local, mobile_type, 32, 32);
    f.spawn(enemy, mobile_type, 224, 224);
    data::campaign::MissionUnit entry{};
    entry.unit_name = "TESTWALL";
    entry.initial_mission = "o 0 0,w 3600 0,";
    entry.x = 160 << 16;
    entry.z = 160 << 16;
    entry.health_percent = 100;
    entry.player = local + 1;
    sim::match_runtime::create_mission_units(*f.match, &entry, 1);
    sim::unit_spawn::Slot* gate = nullptr;
    for (auto& slot : f.match->world().slots)
        if (slot.unit && slot.record.owner_index == local &&
            slot.record.type_index == structure_type)
            gate = &slot;
    CHECK(gate != nullptr);
    CHECK((gate->record.flags & OA_UNIT_FLAG_SELECTABLE) == 0);
    f.run(outcome_ticks);
    CHECK((gate->record.flags & OA_UNIT_FLAG_SELECTABLE) == 0);
    CHECK(f.outcome() == sim::scenario::Outcome::ongoing);

    f.kill(*gate);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::defeat);
    std::cout << "a script-held protected unit is no defeat passed\n";
}

// BuildUnitType caches the type as a 16-bit id (Condition.type_index) and wins on a finished
// unit of it among player 0's, announcing once.
void build_unit_type_wins_on_a_finished_unit() {
    Fixture f(conditions({{"BuildUnitType", "TESTUNIT"}}));
    f.spawn(local, structure_type, 32, 32);
    f.spawn(enemy, mobile_type, 224, 224);
    auto& built = f.spawn(local, mobile_type, 96, 96, false);
    CHECK(built.record.build_remaining == 1.0F);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::ongoing && f.cues() == 0);
    built.record.build_remaining = 0.0F;
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::victory);
    CHECK(f.cues() == 1);
    std::cout << "build unit type wins on a finished unit passed\n";
}

// MoveUnitToRadius takes only player 0's units inside the radius
// (for_each_unit_in_radius), and places the map point on the terrain first.
void move_unit_to_radius_takes_local_units() {
    Fixture f(conditions({{"MoveUnitToRadius", "ANYTYPE,64,64,32"}}));
    f.spawn(local, structure_type, 200, 200);
    f.spawn(enemy, mobile_type, 64, 64);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::ongoing);
    f.spawn(local, mobile_type, 80, 72);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::victory && f.cues() == 1);
    std::cout << "move unit to radius takes local units passed\n";
}

// On ground 60 high over the map point the row that projects to it lies half
// the height further on: pixel z 64 is world z 94.
void move_unit_to_radius_point_follows_the_terrain() {
    auto options = conditions({{"MoveUnitToRadius", "TESTUNIT,200,64,8"}});
    options.high_ground_from = 8;
    Fixture f(std::move(options));
    f.spawn(local, structure_type, 32, 200);
    f.spawn(enemy, mobile_type, 32, 32);
    f.spawn(local, mobile_type, 200, 64);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::ongoing);
    f.spawn(local, mobile_type, 200, 94);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::victory);
    std::cout << "move unit to radius point follows the terrain passed\n";
}

// KillAllMobileUnits counts player 1's units that hold a movement object: its
// structures do not keep the condition from being met.
void kill_all_mobile_units_ignores_structures() {
    Fixture f(conditions({}, {{"KillAllMobileUnits", 1}}));
    f.spawn(local, mobile_type, 32, 32);
    f.spawn(enemy, structure_type, 160, 160);
    auto& tank = f.spawn(enemy, mobile_type, 224, 224);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::ongoing);
    f.kill(tank);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::victory && f.cues() == 1);
    std::cout << "kill all mobile units ignores structures passed\n";
}

// KillAllOfType counts player 1's units of the type, the dying one included.
void kill_all_of_type_counts_the_enemy_type() {
    Fixture f(conditions({{"KillAllOfType", "TESTWALL"}}));
    f.spawn(local, mobile_type, 32, 32);
    f.spawn(local, structure_type, 64, 200);
    auto& first = f.spawn(enemy, structure_type, 160, 160);
    auto& second = f.spawn(enemy, structure_type, 200, 96);
    f.spawn(enemy, mobile_type, 224, 224);
    f.kill(first);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::ongoing);
    f.kill(second);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::victory && f.cues() == 1);
    std::cout << "kill all of type counts the enemy type passed\n";
}

// The stored-result query only returns the stored result: player 1 units of the type that
// their InitialMission scripts hold (0x20 clear, as CC17's two ARMCARRY) are
// no victory until they die, and KillAllOfType takes no player 0 death even when
// one of the type is left to player 1.
void kill_all_of_type_waits_for_held_units() {
    Fixture f(conditions({{"KillAllOfType", "TESTWALL"}}));
    f.spawn(local, mobile_type, 32, 32);
    auto& own_wall = f.spawn(local, structure_type, 64, 200);
    auto& first = f.spawn(enemy, structure_type, 160, 160);
    auto& second = f.spawn(enemy, structure_type, 200, 96);
    f.spawn(enemy, mobile_type, 224, 224);
    for (auto* held : {&first, &second})
        held->record.flags &= ~OA_UNIT_FLAG_SELECTABLE;
    f.run(5 * outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::ongoing && f.cues() == 0);
    f.kill(first);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::ongoing && f.cues() == 0);
    f.kill(own_wall);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::ongoing && f.cues() == 0);
    f.kill(second);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::victory && f.cues() == 1);
    std::cout << "kill all of type waits for held units passed\n";
}

// KillUnitType counts player 1's kills of the type down from Condition.kills_left.
void kill_unit_type_counts_enemy_kills() {
    Fixture f(conditions({{"KillUnitType", "TESTWALL,2"}}));
    f.spawn(local, mobile_type, 32, 32);
    auto& own_wall = f.spawn(local, structure_type, 64, 200);
    auto& first = f.spawn(enemy, structure_type, 160, 160);
    auto& second = f.spawn(enemy, structure_type, 200, 96);
    f.spawn(enemy, mobile_type, 224, 224);
    f.kill(own_wall);
    f.kill(first);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::ongoing);
    f.kill(second);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::victory);
    std::cout << "kill unit type counts enemy kills passed\n";
}

// KillEnemyCommander and CommanderKilled name a commander by the side its
// owner plays.
void commanders_are_named_by_their_side() {
    const auto bind_sides = [](Fixture& f) {
        auto& world = f.match->state();
        world.player_info[0].side = 0;
        world.player_info[1].side = 1;
        oa::base::text::copy_terminated(world.game.sides[0].commander, "TESTUNIT");
        oa::base::text::copy_terminated(world.game.sides[1].commander, "TESTWALL");
    };
    {
        Fixture f(conditions({}, {{"KillEnemyCommander", 1}}));
        bind_sides(f);
        f.spawn(local, mobile_type, 32, 32);
        auto& tank = f.spawn(enemy, mobile_type, 224, 224);
        auto& commander = f.spawn(enemy, structure_type, 160, 160);
        f.kill(tank);
        f.run(outcome_ticks);
        CHECK(f.outcome() == sim::scenario::Outcome::ongoing);
        f.kill(commander);
        f.run(outcome_ticks);
        CHECK(f.outcome() == sim::scenario::Outcome::victory && f.cues() == 1);
    }
    {
        Fixture f(conditions({}, {{"CommanderKilled", 1}}));
        bind_sides(f);
        auto& commander = f.spawn(local, mobile_type, 32, 32);
        auto& wall = f.spawn(local, structure_type, 64, 200);
        f.spawn(enemy, mobile_type, 224, 224);
        f.kill(wall);
        f.run(outcome_ticks);
        CHECK(f.outcome() == sim::scenario::Outcome::ongoing);
        f.kill(commander);
        f.run(outcome_ticks);
        CHECK(f.outcome() == sim::scenario::Outcome::defeat && f.cues() == 0);
    }
    std::cout << "commanders are named by their side passed\n";
}

// UnitTypePasses and AnyUnitPasses read the unit's own cell (Unit.cell_x and cell_z), structures
// included; UnitTypePasses takes player 0's units, AnyUnitPasses player 1's.
void passing_units_are_read_from_their_cells() {
    {
        Fixture f(conditions({{"UnitTypePassesX", "TESTWALL,64"}}));
        f.spawn(local, mobile_type, 200, 32);
        f.spawn(enemy, mobile_type, 224, 224);
        f.spawn(enemy, structure_type, 64, 160);
        f.run(outcome_ticks);
        CHECK(f.outcome() == sim::scenario::Outcome::ongoing);
        auto& wall = f.spawn(local, structure_type, 64, 96);
        CHECK(std::abs(wall.record.cell_x - 4) <= 2);
        f.run(outcome_ticks);
        CHECK(f.outcome() == sim::scenario::Outcome::victory && f.cues() == 1);
    }
    {
        Fixture f(conditions({}, {{"AnyUnitPassesX", 64}}));
        f.spawn(local, mobile_type, 200, 32);
        f.spawn(local, structure_type, 64, 200);
        f.spawn(enemy, mobile_type, 224, 224);
        f.run(outcome_ticks);
        CHECK(f.outcome() == sim::scenario::Outcome::ongoing);
        f.spawn(enemy, structure_type, 64, 96);
        f.run(outcome_ticks);
        CHECK(f.outcome() == sim::scenario::Outcome::defeat);
    }
    std::cout << "passing units are read from their cells passed\n";
}

// The victory and defeat checks test nothing while the controller is disabled.
void a_disabled_controller_decides_nothing() {
    for (const bool disabled : {false, true}) {
        Fixture f(conditions({}));
        f.spawn(local, mobile_type, 32, 32);
        if (disabled)
            f.match->disable_scenario();
        f.run(outcome_ticks);
        CHECK(
            f.outcome() ==
            (disabled ? sim::scenario::Outcome::ongoing : sim::scenario::Outcome::victory)
        );
    }
    std::cout << "a disabled controller decides nothing passed\n";
}

// The player slot update settles no economy once the outcome countdown runs.
void the_countdown_stops_the_economy() {
    constexpr float untouched = -1.0F;
    Fixture f(conditions({}));
    f.spawn(local, mobile_type, 32, 32);
    auto& player = f.match->state().game.players[local];
    f.run(1);
    CHECK(f.match->outcome_state().countdown == 4);
    player.metal_storage = untouched;
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::victory);
    CHECK(player.metal_storage == untouched);
    std::cout << "the countdown stops the economy passed\n";
}

// AllUnitsKilled asks unit_selectable, so a unit carried by a transport
// that is not an air base no longer counts as alive.
void carried_units_do_not_count_as_alive() {
    Fixture f(conditions({}));
    auto& carried = f.spawn(local, mobile_type, 32, 32);
    auto& carrier = f.spawn(enemy, mobile_type, 36, 36);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::ongoing);
    f.match->set_carry_link(carried.unit_index, carrier.unit_index, -1, 1);
    CHECK(carried.record.attach_parent != 0);
    f.run(outcome_ticks);
    CHECK(f.outcome() == sim::scenario::Outcome::defeat);
    std::cout << "carried units do not count as alive passed\n";
}

} // namespace

int main() {
    try {
        all_units_killed_of_type_waits_for_the_kill();
        a_script_held_protected_unit_is_no_defeat();
        build_unit_type_wins_on_a_finished_unit();
        move_unit_to_radius_takes_local_units();
        move_unit_to_radius_point_follows_the_terrain();
        kill_all_mobile_units_ignores_structures();
        kill_all_of_type_counts_the_enemy_type();
        kill_all_of_type_waits_for_held_units();
        kill_unit_type_counts_enemy_kills();
        commanders_are_named_by_their_side();
        passing_units_are_read_from_their_cells();
        a_disabled_controller_decides_nothing();
        the_countdown_stops_the_economy();
        carried_units_do_not_count_as_alive();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
