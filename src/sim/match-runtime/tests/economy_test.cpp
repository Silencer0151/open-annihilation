// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/match_runtime.hpp"
#include "oa/sim/unit_health.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "oa/test/match_services.hpp"

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

// Economy tick branch coverage: four players select the energy and metal
// computer credit scales, and the unit set exercises inactive,
// storage-full, storage-free, and negative-income branches. Every player
// economy output field is asserted after each tick.
struct Services : oa::test::QuietServices {
    std::vector<std::string> calls;

    void activation_sound(sim::unit_spawn::Slot&, sim::unit_activation::Sound) override {
        calls.push_back("sound");
    }

    void attachment_notification(sim::unit_spawn::Slot&, uint32_t) override {
        calls.push_back("notify");
    }

    void refresh_selected_unit(sim::unit_spawn::Slot&) override { calls.push_back("refresh"); }
};

using Scenario = oa::test::EmptyScenario;

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
        0,
        opcode::return_,
        opcode::push_constant,
        1,
        opcode::push_constant,
        1,
        opcode::set_unit_value,
        opcode::return_
    };
    script->scripts = {{"Create", 0}, {"Activate", 8}, {"Enable", 13}};
    script->entry_points = {0, 8, 13};
    script->header.static_variable_count = 1;
    script->piece_names = {"root"};
    constexpr std::size_t type_count = 8;
    std::array<sim::unit_spawn::LoadedType, type_count> loaded;
    for (std::size_t i = 1; i < type_count; ++i) {
        loaded[i].model = model;
        loaded[i].script = script;
    }
    std::array<sim::unit_spawn::Type, type_count> types;
    for (std::size_t i = 1; i < type_count; ++i) {
        types[i].simulation.flags = 0x800000;
        types[i].simulation.maximum_health = 100;
        types[i].footprint_x = types[i].footprint_z = 1;
        types[i].bm_code = 0;
        types[i].model = reinterpret_cast<uintptr_t>(model.get());
        types[i].cob = reinterpret_cast<uintptr_t>(script.get());
        loaded[i].type = types[i];
    }
    std::array<data::unit_definitions::UnitDefinition, type_count> defs;
    for (auto& definition : defs) {
        definition.sight_distance = 160;
        definition.acceleration_fixed = 65536;
        definition.brake_rate_fixed = 65536;
        definition.max_velocity_fixed = 2 * 65536;
        definition.turn_rate = 1024;
    }
    // Type 1 (unit A): human mobile, negative EnergyUse, cloak costs.
    defs[1].energy_use = -20.0F;
    defs[1].energy_storage = 1000.0F;
    defs[1].metal_storage = 1000.0F;
    defs[1].cloak_cost = 7.5F;
    defs[1].cloak_cost_moving = 9.5F;
    // Type 2 (unit B): easy computer building, MakesMetal, storage not full.
    defs[2].energy_use = 10.0F;
    defs[2].makes_metal = 4;
    defs[2].energy_storage = 1000.0F;
    defs[2].metal_storage = 1000.0F;
    // Type 3 (unit C): medium computer building with make totals.
    defs[3].energy_make = 600.0F;
    defs[3].energy_use = 10.0F;
    defs[3].makes_metal = 4;
    defs[3].energy_storage = 1000.0F;
    defs[3].metal_storage = 1000.0F;
    // Type 4 (unit D): medium wind generator, no storage of its own.
    defs[4].wind_generator = 4.0F;
    // Type 5 (unit E): hard inactive building with make totals.
    defs[5].energy_make = 300.0F;
    defs[5].energy_use = 10.0F;
    defs[5].makes_metal = 4;
    defs[5].energy_storage = 1000.0F;
    defs[5].metal_storage = 1000.0F;
    // Type 6 (unit F): hard metal extractor.
    defs[6].extracts_metal = 3.0F;
    // Type 7 (unit G): hard tidal generator.
    defs[7].tidal_generator = 5.0F;
    data::unit_definitions::UnitTargetCategoryMasks target_masks;
    data::unit_definitions::RuntimeDefinitionMetadata metadata;
    std::vector<sim::spatial_state::Plot> collision_plots(256);
    std::array<sim::match_runtime::RuntimeTypeFields, type_count> fields{};
    const std::array<uint8_t, 1> yard{4};
    for (std::size_t i = 1; i < type_count; ++i) {
        fields[i].definition = &defs[i];
        fields[i].yard_mask = yard;
        fields[i].runtime_metadata = &metadata;
        fields[i].target_masks = &target_masks;
    }
    sim::combat_state::WeaponRegistry weapons;
    Services services;
    Scenario scenario;
    // minimum == maximum wind pins the wind sample: normalized strength is
    // 1500 / 5000. Tidal strength is the fixed Game.tidal_strength value.
    sim::match_runtime::OfflineInputs input{
        map, loaded, types, fields,    weapons, terrain_values,  masks, 8,    8,    4,   2,
        0,   30,     1,     &scenario, {},      collision_plots, {},    1500, 1500, 0.5f
    };
    sim::match_runtime::Match match(input, services);
    match.simulation().tick = 1;
    match.refresh_wind();
    for (std::size_t player = 0; player < 4; ++player) {
        match.simulation().players[player].present = true;
        match.simulation().players[player].status = player == 0 ? 1 : 2;
    }
    auto* a = match.create({0, 1, {16u << 16, 0, 16u << 16}, true, 1, 0});
    auto* b = match.create({1, 2, {32u << 16, 0, 32u << 16}, true, 1, 0});
    auto* c = match.create({2, 3, {48u << 16, 0, 48u << 16}, true, 1, 0});
    auto* d = match.create({2, 4, {64u << 16, 0, 64u << 16}, true, 1, 0});
    auto* e = match.create({3, 5, {80u << 16, 0, 80u << 16}, true, 1, 0});
    auto* f = match.create({3, 6, {96u << 16, 0, 96u << 16}, true, 1, 0});
    auto* g = match.create({3, 7, {112u << 16, 0, 112u << 16}, true, 1, 0});
    CHECK(a && b && c && d && e && f && g);
    // Unit A: human mobile with cloak running and moving-rate bits. Units B..G
    // keep the create-time building flag; B..D, F and G are active, E is not.
    // D, F and G stay unfinished so their storage is not credited: the income
    // branches still run before the finished test.
    a->unit->flags = (a->unit->flags & ~OA_UNIT_FLAG_BUILDING) | 0x800u | 0x4u;
    a->record.state_flags |= 1;
    b->record.state_flags |= 1;
    c->record.state_flags |= 1;
    d->record.state_flags |= 1;
    f->record.state_flags |= 1;
    g->record.state_flags |= 1;
    d->record.build_remaining = 1.0F;
    f->record.build_remaining = 1.0F;
    g->record.build_remaining = 1.0F;

    // Player 0: human, negative EnergyUse production and cloak upkeep paid
    // from the store through the energy payment.
    match.set_difficulty(1);
    auto& p0 = match.world().players[0];
    p0.energy = 200.0F;
    p0.metal = 200.0F;
    match.update_player_economy(0);
    CHECK(p0.energy_cap == 1000.0F && p0.metal_cap == 1000.0F);
    CHECK(p0.energy_produced == 20.0F && p0.energy_consumed == 9.0F);
    CHECK(p0.metal_produced == 0.0F && p0.metal_consumed == 0.0F);
    CHECK(p0.energy == 211.0F && p0.metal == 200.0F);
    CHECK(p0.energy_harvested == 20.0 && p0.metal_harvested == 0.0);
    CHECK(p0.cumulative_energy_consumed == 9.0 && p0.cumulative_metal_consumed == 0.0);
    CHECK(p0.cumulative_energy_wasted == 0.0 && p0.cumulative_metal_wasted == 0.0);
    CHECK((a->record.state_flags & 4) != 0);
    {
        const auto& words = economy_words(a->record);
        CHECK(std::bit_cast<float>(words[4]) == 20.0F && std::bit_cast<float>(words[5]) == 9.0F);
        CHECK(std::bit_cast<float>(words[0]) == 0.0F && std::bit_cast<float>(words[3]) == 0.0F);
    }

    // Player 1: easy computer (scale 0.5), storage not full, MakesMetal credit.
    match.set_difficulty(0);
    auto& p1 = match.world().players[1];
    p1.energy = 200.0F;
    p1.metal = 200.0F;
    match.update_player_economy(1);
    CHECK(p1.energy_cap == 1000.0F && p1.metal_cap == 1000.0F);
    CHECK(p1.energy_produced == 0.0F && p1.energy_consumed == 10.0F);
    CHECK(p1.metal_produced == 2.0F && p1.metal_consumed == 0.0F);
    CHECK(p1.energy == 190.0F && p1.metal == 202.0F);
    CHECK(p1.energy_harvested == 0.0 && p1.metal_harvested == 2.0);
    CHECK(p1.cumulative_energy_consumed == 10.0 && p1.cumulative_metal_consumed == 0.0);
    CHECK(p1.cumulative_energy_wasted == 0.0 && p1.cumulative_metal_wasted == 0.0);
    {
        const auto& words = economy_words(b->record);
        CHECK(std::bit_cast<float>(words[5]) == 10.0F && std::bit_cast<float>(words[10]) == 2.0F);
        CHECK(std::bit_cast<float>(words[4]) == 0.0F && std::bit_cast<float>(words[3]) == 0.0F);
    }

    // Player 2: medium computer (scale 0.7), storage full with waste. Unit C
    // makes energy and metal; unit D is a wind generator.
    match.set_difficulty(1);
    auto& p2 = match.world().players[2];
    p2.energy = 1000.0F;
    p2.metal = 1000.0F;
    {
        const auto wind = sim::unit_health::scale_computer_credit(4.0F * 0.3F, 2, 1);
        const auto produced = sim::unit_health::scale_computer_credit(600.0F, 2, 1) + wind;
        match.update_player_economy(2);
        CHECK(p2.energy_cap == 1000.0F && p2.metal_cap == 1000.0F);
        CHECK(p2.energy_produced == produced && p2.energy_consumed == 10.0F);
        CHECK(p2.metal_produced == 2.8F && p2.metal_consumed == 0.0F);
        CHECK(p2.energy == 1000.0F && p2.metal == 1000.0F);
        CHECK(p2.energy_harvested == static_cast<double>(produced));
        CHECK(p2.metal_harvested == static_cast<double>(2.8F));
        CHECK(p2.cumulative_energy_consumed == 10.0 && p2.cumulative_metal_consumed == 0.0);
        CHECK(
            p2.cumulative_energy_wasted ==
            static_cast<double>(((produced + 1000.0F) - 10.0F) - 1000.0F)
        );
        CHECK(p2.cumulative_metal_wasted == static_cast<double>((2.8F + 1000.0F) - 1000.0F));
        const auto& cw = economy_words(c->record);
        CHECK(std::bit_cast<float>(cw[4]) == 420.0F && std::bit_cast<float>(cw[5]) == 10.0F);
        CHECK(std::bit_cast<float>(cw[10]) == 2.8F && std::bit_cast<float>(cw[3]) == 0.0F);
        const auto& dw = economy_words(d->record);
        CHECK(std::bit_cast<float>(dw[4]) == wind);
    }

    // Player 3: hard computer (scale 1.0). Unit E is inactive, unit F extracts
    // metal, unit G is a tidal generator.
    match.set_difficulty(2);
    f->record.extracted_metal = 30.0F;
    auto& p3 = match.world().players[3];
    p3.energy = 200.0F;
    p3.metal = 200.0F;
    match.update_player_economy(3);
    CHECK(p3.energy_cap == 1000.0F && p3.metal_cap == 1000.0F);
    CHECK(p3.energy_produced == 302.5F && p3.energy_consumed == 0.0F);
    CHECK(p3.metal_produced == 30.0F && p3.metal_consumed == 0.0F);
    CHECK(p3.energy == 502.5F && p3.metal == 230.0F);
    CHECK(p3.energy_harvested == 302.5 && p3.metal_harvested == 30.0);
    CHECK(p3.cumulative_energy_consumed == 0.0 && p3.cumulative_metal_consumed == 0.0);
    CHECK(p3.cumulative_energy_wasted == 0.0 && p3.cumulative_metal_wasted == 0.0);
    {
        const auto& ew = economy_words(e->record);
        CHECK(std::bit_cast<float>(ew[4]) == 300.0F && std::bit_cast<float>(ew[5]) == 0.0F);
        CHECK(std::bit_cast<float>(ew[10]) == 0.0F);
        const auto& fw = economy_words(f->record);
        CHECK(std::bit_cast<float>(fw[10]) == 30.0F);
        const auto& gw = economy_words(g->record);
        CHECK(std::bit_cast<float>(gw[4]) == 2.5F);
    }
    // Only the human cloak activation changed unit state through set_activation.
    CHECK(
        (b->record.state_flags & 4) == 0 && (c->record.state_flags & 4) == 0 &&
        (e->record.state_flags & 4) == 0
    );

    // A lone commander (EnergyMake 25, MetalMake 1, no storage of its own)
    // building an ARMMEX (521 energy, 50 metal, BuildTime 1800) at WorkerTime
    // 300. The start grants the 1000/1000 start storage and seeds the stores;
    // each tick the build step debits cost * (300 / 30) / 1800, and every 30
    // ticks the economy tick settles, so the rates read as per-second amounts.
    defs[1] = defs[0];
    defs[1].energy_make = 25.0F;
    defs[1].metal_make = 1.0F;
    sim::match_runtime::Match lone(input, services);
    lone.simulation().players[0].present = true;
    lone.simulation().players[0].status = 1;
    auto* commander = lone.create({0, 1, {16u << 16, 0, 16u << 16}, true, 1, 0});
    CHECK(commander && commander->record.build_remaining == 0.0F);
    auto& owner = lone.state().game.players[0];
    sim::unit_spawn::grant_start_storage(owner, 1000, 1000);
    owner.energy = 1000.0F;
    owner.metal = 1000.0F;
    constexpr float build_rate = static_cast<float>(300u / 30u);
    float remaining = 1.0F;
    const auto build_window = [&] {
        float energy_spent = 0.0F, metal_spent = 0.0F;
        for (int tick = 0; tick < 30; ++tick) {
            const auto next = remaining - build_rate / 1800.0F;
            const auto delta = remaining - next;
            remaining = next;
            auto& accounts = commander->record.economy;
            sim::unit_health::EconomyDebit energy{
                accounts.energy.requested, accounts.energy.accepted, accounts.energy.gate
            };
            sim::unit_health::EconomyDebit metal{
                accounts.metal.requested, accounts.metal.accepted, accounts.metal.gate
            };
            CHECK(sim::unit_health::debit_resources(energy, metal, delta * 521.0F, delta * 50.0F));
            accounts.energy.requested = energy.requested;
            accounts.energy.accepted = energy.accepted;
            accounts.metal.requested = metal.requested;
            accounts.metal.accepted = metal.accepted;
            energy_spent = energy.requested;
            metal_spent = metal.requested;
        }
        lone.update_player_economy(0);
        return std::array<float, 2>{energy_spent, metal_spent};
    };
    const auto first = build_window();
    CHECK(std::fabs(first[0] - 521.0F / 6.0F) < 0.01F);
    CHECK(std::fabs(first[1] - 50.0F / 6.0F) < 0.01F);
    CHECK(owner.energy_storage == 1000.0F && owner.metal_storage == 1000.0F);
    CHECK(owner.energy_produced == 25.0F && owner.metal_produced == 1.0F);
    CHECK(owner.energy_requested == first[0] && owner.metal_requested == first[1]);
    CHECK(owner.energy == (25.0F + 1000.0F) - first[0]);
    CHECK(owner.metal == (1.0F + 1000.0F) - first[1]);
    CHECK(owner.energy_wasted_total == 0.0 && owner.metal_wasted_total == 0.0);
    CHECK(commander->record.economy.energy.last_produced == 25.0F);
    CHECK(commander->record.economy.energy.last_requested == first[0]);
    CHECK(commander->record.economy.metal.last_produced == 1.0F);
    CHECK(commander->record.economy.metal.last_requested == first[1]);
    CHECK(commander->record.economy.energy.requested == 0.0F);
    const auto energy_after_first = owner.energy;
    const auto metal_after_first = owner.metal;
    const auto second = build_window();
    CHECK(owner.energy == (25.0F + energy_after_first) - second[0]);
    CHECK(owner.metal == (1.0F + metal_after_first) - second[1]);
    CHECK(std::fabs(owner.energy - (1000.0F - 2.0F * (521.0F / 6.0F - 25.0F))) < 0.05F);
    CHECK(std::fabs(owner.metal - (1000.0F - 2.0F * (50.0F / 6.0F - 1.0F))) < 0.05F);
    std::cout << "economy tick branch coverage passed\n";
}
