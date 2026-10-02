// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/match_runtime.hpp"
#include "oa/sim/weapon_execution/weapon_launch.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

// Raw words of a unit's economy block (energy then metal accumulators).
std::array<uint32_t, 12>& economy_words(oa::Unit& unit) {
    return *reinterpret_cast<std::array<uint32_t, 12>*>(&unit.economy);
}

using namespace oa;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)

struct Services : sim::match_runtime::OfflineServices {
#define UNEXPECTED(type, name, args)                                                               \
    type name args override {                                                                      \
        throw std::runtime_error("unexpected " #name);                                             \
    }

    void activation_sound(sim::unit_spawn::Slot&, sim::unit_activation::Sound) override {}

    void command_sound(sim::unit_spawn::Slot&, uint32_t) override {}

    UNEXPECTED(void, attachment_notification, (sim::unit_spawn::Slot&, uint32_t))

    void refresh_selected_unit(sim::unit_spawn::Slot&) override {}
    UNEXPECTED(void, emit_sfx, (sim::unit_spawn::Slot&, uint32_t, int32_t))
    UNEXPECTED(void, explode_piece, (sim::unit_spawn::Slot&, uint32_t, int32_t))
    UNEXPECTED(void, attach_unit, (sim::unit_spawn::Slot&, int32_t, int32_t, int32_t))
    UNEXPECTED(void, drop_unit, (sim::unit_spawn::Slot&, int32_t))

    void refresh_plot_height_range(std::array<int16_t, 2>, std::array<int16_t, 2>) override {}

    void notify_object_footprint_removed(oa::sim::spatial_state::Unit&, uint32_t) override {}

    void notify_footprint_changed(std::array<int16_t, 2>, std::array<int16_t, 2>) override {}

#undef UNEXPECTED
};

struct Scenario : sim::scenario::DefinitionHost {
    int32_t integer(std::string_view, int32_t fallback) override { return fallback; }

    std::optional<std::string> text(std::string_view) override { return std::nullopt; }
};

int main() {
    formats::tnt::Map map;
    map.attribute_width = map.attribute_height = 16;
    map.attributes.resize(256);
    std::vector<sim::visibility_state::TerrainCell> terrain_values(256);
    sim::visibility_state::SightMask mask;
    mask.width = mask.height = 1;
    mask.transparent = 0;
    mask.pixels = {1};
    const std::array masks{mask};
    auto model = std::make_shared<formats::objects3d::Model>();
    model->objects.resize(1);
    model->objects[0].name = "root";
    // Model top (UnitDef.model_height) the projectile contact test hits below.
    model->objects[0].vertices = {{0, 0, 0}, {0, 20 << 16, 0}};
    auto script = std::make_shared<formats::cob::CobProgram>();
    using namespace sim::script_vm;
    script->code = {
        opcode::hide,
        0,
        opcode::push_constant,
        1000,
        opcode::sleep,
        opcode::show,
        0,
        opcode::return_,
        opcode::push_constant,
        1,
        opcode::pop_static,
        1,
        opcode::push_constant,
        1,
        opcode::return_,
        opcode::push_constant,
        1,
        opcode::pop_static,
        2,
        opcode::return_,
        opcode::push_constant,
        1,
        opcode::pop_static,
        0,
        opcode::return_
    };
    script->scripts = {{"Create", 0}, {"AimPrimary", 8}, {"FirePrimary", 15}, {"RockUnit", 20}};
    script->entry_points = {0, 8, 15, 20};
    script->header.static_variable_count = 3;
    script->piece_names = {"root"};
    std::array<sim::unit_spawn::LoadedType, 2> loaded;
    loaded[1].model = model;
    loaded[1].script = script;
    std::array<sim::unit_spawn::Type, 2> types;
    types[1].simulation.flags = 0x800000;
    types[1].simulation.maximum_health = 100;
    types[1].footprint_x = types[1].footprint_z = 1;
    types[1].model = reinterpret_cast<uintptr_t>(model.get());
    types[1].cob = reinterpret_cast<uintptr_t>(script.get());
    loaded[1].type = types[1];
    types[1].simulation.default_mission_type = 0;
    data::unit_definitions::UnitDefinition def;
    def.sight_distance = 160;
    def.weapon1 = "TESTGUN";
    data::unit_definitions::UnitTargetCategoryMasks target_masks;
    data::unit_definitions::RuntimeDefinitionMetadata metadata;
    std::vector<sim::spatial_state::Plot> collision_plots(256);
    std::array<sim::match_runtime::RuntimeTypeFields, 2> fields{};
    fields[1].definition = &def;
    const std::array<uint8_t, 1> yard{4};
    fields[1].yard_mask = yard;
    fields[1].runtime_metadata = &metadata;
    fields[1].target_masks = &target_masks;
    sim::combat_state::WeaponRegistry weapons;
    // burst=2 and a burstrate of one tick (0.034 seconds * 30, truncated).
    (void)sim::combat_state::install_weapon_text(
        weapons,
        "[TESTGUN]{id=1; reloadtime=0.1; range=400; lineofsight=1; weaponvelocity=100; turret=1;"
        " unitsonly=1; groundbounce=1; interceptor=1; burst=2; burstrate=0.034;"
        " [DAMAGE]{default=10;}}"
    );
    CHECK(weapons.find("TESTGUN")->flags & sim::combat_state::weapon_ground_skip_flag);
    CHECK(weapons.find("TESTGUN")->flags & sim::combat_state::weapon_ground_bounce_flag);
    CHECK(weapons.find("TESTGUN")->flags & OA_WEAPON_FLAG_INTERCEPTOR);
    CHECK(
        weapons.find("TESTGUN") && weapons.find("TESTGUN")->projectile_velocity ==
                                       static_cast<int32_t>((100.0 * 65536.0) / 30.0)
    );
    CHECK(weapons.find("TESTGUN")->default_damage == 10);
    CHECK(weapons.find("TESTGUN")->flags & sim::combat_state::weapon_line_of_sight_flag);
    CHECK(weapons.find("TESTGUN")->flags & sim::combat_state::weapon_turret_flag);
    CHECK(weapons.find("TESTGUN")->burst == 2 && weapons.find("TESTGUN")->burst_rate_ticks == 1);
    Services services;
    Scenario scenario;
    // Sprite features: a 2x2 rock of 100 hit points and 1x1 trees of 80.
    std::array<FeatureDef, 2> features{};
    for (auto& feature : features) {
        feature.flags = OA_FEATURE_FLAG_SPRITE;
        feature.footprint_x = feature.footprint_z = 1;
        feature.damage = 80;
        feature.dead_feature = sim::feature_runtime::no_feature;
    }
    features[0].footprint_x = features[0].footprint_z = 2;
    features[0].damage = 100;
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
        2,
        2,
        0,
        30,
        1,
        &scenario,
        {},
        collision_plots,
        {}
    };
    input.feature_defs = features;
    sim::match_runtime::Match match(input, services);
    // The match takes UnitDef.model_height from the type's own model, so a
    // shot needs no setup path to supply the top it must pass under.
    CHECK(match.state().unit_defs[1].model_height == 20 << 16);
    match.configure_strategic_environment({0, 0.5f, 0});
    match.simulation().players[0].present = true;
    match.simulation().players[0].status = 1;
    auto* shooter = match.create({0, 1, {64u << 16, 32u << 16, 64u << 16}, true, 1, 0});
    CHECK(shooter && shooter->unit_index == 1);
    match.simulation().players[1].present = true;
    match.simulation().players[1].status = 2;
    auto* target = match.create({1, 1, {80u << 16, 32u << 16, 64u << 16}, true, 1, 0});
    CHECK(target && target->unit_index != 0);
    shooter->unit->object_present = false;
    target->unit->object_present = false;
    shooter->unit->flags |= 0x10000000u;
    target->unit->flags |= 0x10000000u;
    target->unit->health = 1;
    std::array<uint8_t, 10> allies{};
    allies[0] = 1;
    match.configure_outcomes(0, allies, true);
    auto& gun = shooter->record.weapons[0];
    CHECK(gun.flags & OA_UNIT_WEAPON_ENABLED);
    CHECK(oa::world_weapon_def(&match.state(), gun.def) == &match.state().game.weapon_defs[1]);
    gun.target_a = static_cast<int16_t>(target->unit_index);
    gun.target_b = OA_UNIT_TARGET_IS_UNIT;
    match.simulation().tick = 1;
    match.tick();
    // The weapon tick queues AimPrimary; the unit's script tick runs it to its
    // return, which readies the slot (UnitWeapon.aim_ready), and the turret shot fires next tick.
    CHECK(gun.flags & OA_UNIT_WEAPON_AIMED);
    CHECK(gun.aim_ready == 1);
    CHECK(match.projectiles().empty() && !(shooter->unit->events & 0x400));
    auto* instance = match.instance(shooter->unit_index);
    CHECK(instance && instance->script());
    CHECK(instance->script()->vm().static_value(1) == 1);
    CHECK(instance->script()->vm().static_value(2) == 0);
    ++match.simulation().tick;
    match.tick();
    // The shot spends the aim: the slot aims again before the next one.
    CHECK(!(gun.flags & OA_UNIT_WEAPON_AIMED) && gun.aim_ready == 0);
    CHECK(shooter->unit->events & 0x400);
    CHECK(instance->script()->vm().static_value(2) == 1);
    CHECK(instance->script()->vm().static_value(0) == 1);
    CHECK(match.projectiles().size() == 1);
    CHECK(match.projectiles()[0].burst_remaining == 2);
    CHECK(match.projectiles()[0].burst_tick == match.simulation().tick);
    CHECK(match.projectiles()[0].target_unit == oa::oa_unit_ref_from_slot(target->unit_index));
    CHECK(match.projectiles()[0].source == oa::world_unit_ref(&match.state(), &shooter->record));
    CHECK(shooter->record.decloak_until_tick == match.simulation().tick + 600u);
    ++match.simulation().tick;
    match.tick();
    CHECK(match.projectiles().size() == 2);
    const auto& burst_parent = match.projectiles()[0];
    const auto& burst_child = match.projectiles()[1];
    const auto same = [](const oa::FixedVec3& a, const oa::FixedVec3& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    };
    CHECK(burst_parent.burst_remaining == 1);
    CHECK(burst_parent.burst_tick == match.simulation().tick);
    CHECK((burst_parent.flags & OA_PROJECTILE_FLAG_RETIRED) == 0);
    CHECK(burst_child.burst_remaining == 0);
    CHECK(burst_child.created_tick == burst_parent.created_tick);
    CHECK(burst_child.burst_tick == match.simulation().tick);
    CHECK(burst_child.def == burst_parent.def);
    CHECK(burst_child.lifetime_tick > match.simulation().tick);
    CHECK(same(burst_child.velocity, burst_parent.velocity));
    CHECK(burst_child.source == burst_parent.source);
    CHECK(same(burst_child.origin, burst_parent.origin));
    CHECK(same(burst_child.target, burst_parent.target));
    CHECK(burst_child.heading == burst_parent.heading);
    CHECK(burst_child.pitch == burst_parent.pitch);
    CHECK(burst_child.speed == burst_parent.speed);
    CHECK(burst_child.target_unit == burst_parent.target_unit);
    CHECK(burst_child.owner_index == burst_parent.owner_index);
    CHECK(burst_child.query_piece == burst_parent.query_piece);
    std::cout << "projectile burst child passed\n";
    for (int step = 0; step < 24; ++step) {
        ++match.simulation().tick;
        match.tick();
    }
    CHECK(target->unit->record.type_index == 0);
    CHECK((target->unit->flags & 0x10000000u) == 0);
    CHECK(match.world().players[1].current_count == 0);
    CHECK(oa::world_player(&match.state(), 1)->losses == 1);
    CHECK(oa::world_player(&match.state(), 0)->kills == 1);
    std::cout << "match live-target weapon tick passed\n";

    // Area blast feature square. The shot sits on the 2x2 rock's origin cell;
    // its continuation cells lead to the same origin, so the blast damages it
    // once. The tree one radius away is equal, not inside. unitsonly leaves
    // the next tree alone. The far tree stays above the bottom band the map
    // load hides, where no feature can be placed.
    constexpr int width = 16;
    auto& world = match.state();
    world.game.console_flags |= OA_CONSOLE_FLAG_TREE_DEATH;
    const auto place = [&](uint16_t feature, int x, int z) {
        const auto index = static_cast<std::size_t>(z * width + x);
        (void)sim::feature_runtime::place_feature(
            world,
            match.feature_host(),
            index,
            feature,
            nullptr,
            nullptr,
            sim::feature_runtime::no_player
        );
        CHECK(world.plots[index].feature == feature);
        return index;
    };
    const auto origin = place(0, 2, 2);
    CHECK(
        world.plots[static_cast<std::size_t>(3 * width + 3)].feature ==
        sim::feature_runtime::feature_continuation
    );
    const auto edge = place(1, 4, 2);
    const auto far = place(1, 10, 4);
    const auto skipped = place(1, 6, 6);
    constexpr uint8_t blast_index = 2;
    CHECK(
        sim::combat_state::install_weapon_text(
            weapons, "[TESTBLAST]{id=2; areaofeffect=64; [DAMAGE]{default=25;}}"
        ) == 1
    );
    std::copy(
        weapons.records().begin(),
        weapons.records().end(),
        std::begin(match.state().game.weapon_defs)
    );
    oa::Projectile shot{};
    shot.def = oa::oa_ref_from_index(blast_index);
    shot.position = {40 << 16, 0, 40 << 16};
    match.detonate(shot, nullptr);
    CHECK(shot.flags & OA_PROJECTILE_FLAG_RETIRED);
    CHECK(world.plots[origin].feature_record == 25);
    CHECK(world.plots[edge].feature_record == 0);
    CHECK(world.plots[far].feature_record == 0);
    // unitsonly: the blast passes over the ground's features.
    CHECK(
        sim::combat_state::install_weapon_text(
            weapons, "[TESTBLAST]{id=2; areaofeffect=64; unitsonly=1; [DAMAGE]{default=25;}}"
        ) == 1
    );
    std::copy(
        weapons.records().begin(),
        weapons.records().end(),
        std::begin(match.state().game.weapon_defs)
    );
    shot.position = {104 << 16, 0, 104 << 16};
    match.detonate(shot, nullptr);
    CHECK(world.plots[skipped].feature_record == 0);
    std::cout << "area blast feature damage passed\n";

    // Kind-5 reclaim credit: the reclaimer's metal accumulator gains
    // (1 - buildfraction) * metal cost; the economy tick then settles
    // the accumulator into the metal store. Human owners take the full amount;
    // easy computers half; a finished build fraction credits 0.
    {
        const auto killer = shooter->unit_index;
        def.build_cost_metal = 100.0F;
        def.energy_storage = 1000.0F;
        def.metal_storage = 1000.0F;
        match.reload_unit_defs();

        struct CreditCase {
            float fraction;
            float human;
            float computer;
        };

        const CreditCase cases[3] = {
            {0.0F, 100.0F, 50.0F}, {0.25F, 75.0F, 37.5F}, {1.0F, 0.0F, 0.0F}
        };
        for (const auto& c : cases) {
            auto* victim = match.create({0, 1, {96u << 16, 32u << 16, 96u << 16}, true, 1, 0});
            CHECK(victim && victim->unit_index != 0);
            victim->unit->flags |= 0x10000000u;
            victim->unit->record.damage_kind =
                static_cast<uint8_t>(sim::match_runtime::DeathKind::reclaim);
            victim->record.last_attacker_id = killer;
            victim->record.build_remaining = c.fraction;
            match.world().players[0].energy = 200.0F;
            match.world().players[0].metal = 50.0F;
            match.simulation().players[0].status = 1;
            const auto human_accumulator =
                std::bit_cast<float>(economy_words(match.state().units[killer])[6]);
            match.teardown_dead_unit(*victim);
            CHECK(match.world().players[0].metal == 50.0F);
            CHECK(
                std::bit_cast<float>(economy_words(match.state().units[killer])[6]) ==
                human_accumulator + c.human
            );
            match.update_player_economy(0);
            CHECK(match.world().players[0].metal == 50.0F + c.human);
            CHECK(std::bit_cast<float>(economy_words(match.state().units[killer])[6]) == 0.0F);

            victim = match.create({0, 1, {96u << 16, 32u << 16, 96u << 16}, true, 1, 0});
            CHECK(victim && victim->unit_index != 0);
            victim->unit->flags |= 0x10000000u;
            victim->unit->record.damage_kind =
                static_cast<uint8_t>(sim::match_runtime::DeathKind::reclaim);
            victim->record.last_attacker_id = killer;
            victim->record.build_remaining = c.fraction;
            match.world().players[0].energy = 200.0F;
            match.world().players[0].metal = 50.0F;
            match.simulation().players[0].status = 2;
            match.set_difficulty(0);
            const auto computer_accumulator =
                std::bit_cast<float>(economy_words(match.state().units[killer])[6]);
            match.teardown_dead_unit(*victim);
            CHECK(match.world().players[0].metal == 50.0F);
            CHECK(
                std::bit_cast<float>(economy_words(match.state().units[killer])[6]) ==
                computer_accumulator + c.computer
            );
            match.update_player_economy(0);
            CHECK(match.world().players[0].metal == 50.0F + c.computer);
            CHECK(std::bit_cast<float>(economy_words(match.state().units[killer])[6]) == 0.0F);
        }
        match.set_difficulty(1);
        match.simulation().players[0].status = 1;
        def.build_cost_metal = 0.0F;
        def.energy_storage = 0.0F;
        def.metal_storage = 0.0F;
        // Any other death kind skips the statistics and runs the shared
        // teardown tail.
        auto* unknown = match.create({0, 1, {96u << 16, 32u << 16, 96u << 16}, true, 1, 0});
        CHECK(unknown && unknown->unit_index != 0);
        unknown->unit->flags |= 0x10000000u;
        unknown->unit->record.damage_kind = 0xe;
        const auto losses = oa::world_player(&match.state(), 1)->losses;
        match.teardown_dead_unit(*unknown);
        CHECK(unknown->unit->record.type_index == 0);
        CHECK(oa::world_player(&match.state(), 1)->losses == losses);
    }
    std::cout << "kill handler reclaim credit passed\n";
}
