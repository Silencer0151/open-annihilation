// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_effects/effects_offline.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
using namespace oa;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)

struct Scenario : sim::scenario::DefinitionHost {
    int32_t integer(std::string_view, int32_t fallback) override { return fallback; }

    std::optional<std::string> text(std::string_view) override { return std::nullopt; }
};

struct Services : sim::match_runtime::OfflineServices,
                  sim::unit_effects::OfflineLifecycle,
                  sim::unit_effects::Sink {
    sim::unit_effects::OfflineEffects* effects{};
    uint32_t events{};

    void activation_sound(sim::unit_spawn::Slot&, sim::unit_activation::Sound) override {}

    void command_sound(sim::unit_spawn::Slot&, uint32_t) override {}

    void attachment_notification(sim::unit_spawn::Slot&, uint32_t) override {}

    void refresh_selected_unit(sim::unit_spawn::Slot&) override {}

    void emit_sfx(sim::unit_spawn::Slot& s, uint32_t p, int32_t e) override {
        effects->emit_sfx(s, p, e);
    }

    void explode_piece(sim::unit_spawn::Slot& s, uint32_t p, int32_t f) override {
        effects->explode_piece(s, p, f);
    }

    void attach_unit(sim::unit_spawn::Slot& s, int32_t c, int32_t p, int32_t m) override {
        effects->attach_unit(s, c, p, m);
    }

    void drop_unit(sim::unit_spawn::Slot& s, int32_t c) override { effects->drop_unit(s, c); }

    void refresh_plot_height_range(std::array<int16_t, 2>, std::array<int16_t, 2>) override {}

    void notify_object_footprint_removed(sim::spatial_state::Unit&, uint32_t) override {}

    void notify_footprint_changed(std::array<int16_t, 2>, std::array<int16_t, 2>) override {}

    void effect(const sim::unit_effects::Event&) override { ++events; }
};

oa::formats::gaf::Sequence effect_sequence(size_t frames, uint16_t hold) {
    oa::formats::gaf::Sequence sequence{};
    sequence.repeat_flags = 1;
    sequence.frames.resize(frames);
    for (auto& frame : sequence.frames)
        frame.duration = hold;
    return sequence;
}

int main() {
    formats::tnt::Map map;
    map.attribute_width = map.attribute_height = 16;
    map.attributes.resize(256);
    for (auto& cell : map.attributes)
        cell.height = 10;
    std::vector<sim::visibility_state::TerrainCell> terrain(256);
    sim::visibility_state::SightMask mask;
    mask.width = mask.height = 1;
    mask.pixels = {1};
    const std::array masks{mask};
    auto model = std::make_shared<formats::objects3d::Model>();
    model->objects.resize(1);
    model->objects[0].name = "root";
    auto script = std::make_shared<formats::cob::CobProgram>();
    // Killed(severity, corpsetype): explode the root piece with EXPLODE_ON_HIT |
    // FALL | SMOKE | FIRE | BITMAP1, emit a light puff, leave corpse type 1.
    constexpr int32_t killed_explode = 0x2 | 0x4 | 0x8 | 0x10 | 0x100;
    constexpr int32_t light_puff_sfx = 0x101;
    {
        using namespace sim::script_vm;
        script->code = {
            opcode::return_,
            opcode::push_constant,
            killed_explode,
            opcode::explode,
            0,
            opcode::push_constant,
            light_puff_sfx,
            opcode::emit_sfx,
            0,
            opcode::push_constant,
            1,
            opcode::pop_local,
            1,
            opcode::return_
        };
    }
    script->scripts = {{"Create", 0}, {"Killed", 1}};
    script->entry_points = {0, 1};
    script->piece_names = {"root"};
    std::array<sim::unit_spawn::LoadedType, 2> loaded{};
    loaded[1].model = model;
    loaded[1].script = script;
    std::array<sim::unit_spawn::Type, 2> types{};
    types[1].simulation.flags = OA_UNIT_DEF_FLAG_AVAILABLE;
    types[1].simulation.maximum_health = 100;
    types[1].footprint_x = types[1].footprint_z = 1;
    types[1].bm_code = 1;
    types[1].model = reinterpret_cast<uintptr_t>(model.get());
    types[1].cob = reinterpret_cast<uintptr_t>(script.get());
    loaded[1].type = types[1];
    data::unit_definitions::UnitDefinition def;
    def.sight_distance = 160;
    const std::array<uint8_t, 1> yard{4};
    std::array<sim::match_runtime::RuntimeTypeFields, 2> fields{};
    fields[1].definition = &def;
    fields[1].yard_mask = yard;
    fields[1].movement_class = 0;
    data::unit_definitions::UnitTargetCategoryMasks target_masks;
    data::unit_definitions::RuntimeDefinitionMetadata metadata;
    fields[1].runtime_metadata = &metadata;
    fields[1].target_masks = &target_masks;
    fields[1].corpse_feature = 0;
    std::vector<FeatureDef> features(1);
    features[0].footprint_x = features[0].footprint_z = 1;
    features[0].dead_feature = 0xffff;
    def.explode_as = "UNITBLAST";
    sim::combat_state::WeaponRegistry weapons;
    weapons.install_tdf_section(uint8_t{7}, "UNITBLAST", "");
    weapons.install_explosion_sprites(7, "boom", "blast");
    // FX.GAF frame counts and holds of the stock entries this test reaches.
    auto smoke_1 = effect_sequence(12, 5);
    auto smoke_2 = effect_sequence(16, 3);
    auto flamestream = effect_sequence(20, 5);
    auto explosion = effect_sequence(23, 2);
    auto h2oboom2 = effect_sequence(14, 3);
    auto blast = effect_sequence(9, 2);
    Scenario scenario;
    Services services;
    sim::unit_effects::OfflineEffects effects(services, services);
    services.effects = &effects;
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
        8,
        2,
        0,
        30,
        1,
        &scenario,
        {},
        {},
        std::nullopt
    };
    input.feature_defs = features;
    input.effect_sequence = [&](std::string_view archive,
                                std::string_view entry) -> const formats::gaf::Sequence* {
        if (archive == "boom")
            return entry == "blast" ? &blast : nullptr;
        if (entry == "smoke 1")
            return &smoke_1;
        if (entry == "smoke 2")
            return &smoke_2;
        if (entry == "flamestream")
            return &flamestream;
        if (entry == "explosion")
            return &explosion;
        if (entry == "h2oboom2")
            return &h2oboom2;
        return nullptr;
    };
    sim::match_runtime::Match match(input, services);
    effects.bind(match);
    match.simulation().players[0].present = true;
    match.simulation().players[0].status = 1;
    auto* parent = match.create({0, 1, {32u << 16, 0, 32u << 16}, true, 1, 0});
    auto* first = match.create({0, 1, {48u << 16, 0, 32u << 16}, true, 1, 0});
    auto* second = match.create({0, 1, {64u << 16, 0, 32u << 16}, true, 1, 0});
    CHECK(parent);
    CHECK(first);
    CHECK(second);
    CHECK(match.world().slots.size() > 4);
    effects.attach_unit(*parent, first->unit_index, -1, 1);
    effects.attach_unit(*parent, 0x10000 | second->unit_index, 0, 2);
    const auto first_child = [&](const oa::sim::unit_spawn::Slot& slot) {
        return oa::oa_unit_slot_from_ref(slot.record.attach_first_child);
    };
    CHECK(first_child(*parent) == second->unit_index);
    CHECK(oa::oa_unit_slot_from_ref(second->record.attach_next) == first->unit_index);
    CHECK((first->unit->flags & 0x20000U) != 0);
    CHECK(static_cast<int8_t>(second->record.attach_piece) == 0);
    effects.drop_unit(*first, second->unit_index);
    CHECK(oa::oa_unit_slot_from_ref(second->record.attach_parent) == parent->unit_index);
    match.prepare_spatial_state();
    auto& carried = match.project_spatial_slot(*second);
    CHECK(carried.spatial_link_locked);
    const std::optional<size_t> preserved =
        match.spatial().buckets.size() > 1 ? std::optional<size_t>{1} : std::nullopt;
    CHECK(
        sim::spatial_state::move_bucket(carried, preserved, match.spatial()) ==
        sim::spatial_state::Error::none
    );
    CHECK(carried.bucket_linked && carried.bucket == preserved);
    effects.drop_unit(*parent, second->unit_index);
    CHECK(first_child(*parent) == first->unit_index);
    CHECK(match.spatial().units[second->unit_index].bucket == preserved);
    CHECK(match.spatial().units[second->unit_index].bucket_linked);
    CHECK((first->unit->flags & 0x20000U) != 0);
    CHECK(services.events == 0);
    std::cout << "offline effects attachment composition passed\n";

    // A unit killed above sea level. Killing it runs its Killed script, which
    // explodes the root piece (debris, and a bitmap record with the large
    // flash and a smoke column) and emits a light puff; then the unit
    // detonates the explodeas weapon (its art with the small flash and a
    // smoke column), and the corpse smokes for 900 ticks.
    std::array<uint8_t, 10> allies{};
    allies[0] = 1;
    match.configure_outcomes(0, allies, true);
    match.simulation().tick = 1;
    match.tick();
    auto* victim = match.create({0, 1, {96u << 16, 0, 96u << 16}, true, 1, 0});
    CHECK(victim);
    victim->unit->position[1] = 20u << 16;
    const auto& world = match.effects();
    const auto& smoke_layer = world.layers[sim::effect_particles::layer_smoke];
    const auto smoke_before = smoke_layer.count;
    const auto smoke_at = [&](uint16_t age) -> const sim::effect_particles::Emitter& {
        return smoke_layer.emitters
            [(smoke_layer.head + smoke_layer.count - 1 - age) %
             sim::effect_particles::layer_capacity];
    };
    CHECK(world.explosion_count == 0 && match.particle_count() == 0);
    const auto* victim_model = &match.instance(victim->unit_index)->model().model();
    match.apply_damage_event(*victim, nullptr, 30000, 1, 0);
    match.simulation().tick = 2;
    match.tick();
    CHECK(victim->unit->record.type_index == 0);
    CHECK(world.explosion_count == 2);
    CHECK(
        world.explosions[0].sprite.sequence == &explosion &&
        world.explosions[0].flash.sequence == world.flash_tiers[2]
    );
    CHECK(
        world.explosions[1].sprite.sequence == &blast &&
        world.explosions[1].flash.sequence == world.flash_tiers[0]
    );
    // The tick steps explosions after the unit pass, so records the death
    // logged have already spent one tick of their first frame.
    CHECK(
        world.explosions[0].sprite.frame_index == 0 &&
        world.explosions[0].sprite.remaining_ticks == 1
    );
    // At the tick's end the debris piece, smoking and burning, starts its
    // puff and then its spark.
    CHECK(smoke_layer.count == smoke_before + 6);
    CHECK(smoke_at(5).interval == 7 && smoke_at(5).deadline == 2 + 15);
    CHECK(smoke_at(4).interval == 1 && smoke_at(4).deadline == 2);
    CHECK(smoke_at(3).interval == 7 && smoke_at(3).deadline == 2 + 15);
    CHECK(smoke_at(2).interval == 15 && smoke_at(2).deadline == 2 + 900);
    CHECK(smoke_at(1).interval == 1 && smoke_at(1).deadline == 2);
    const sim::effect_particles::DebrisPiece* debris = nullptr;
    for (const auto& piece : world.debris)
        if (piece.live)
            debris = &piece;
    CHECK(
        debris && debris->unit == victim->unit_index && debris->lifetime == 900 - 1 &&
        debris->flags ==
            (sim::effect_particles::debris_fire | sim::effect_particles::debris_smoke |
             sim::effect_particles::debris_fall | sim::effect_particles::debris_explode_on_landing)
    );
    // The debris copy of the piece keeps the object, so the piece still draws once
    // its unit is gone.
    CHECK(debris->model == victim_model && debris->object < victim_model->objects.size());
    // The debris falls to the ground and, asked to explode on landing, logs
    // the explosion entry with the small flash.
    int32_t landed_after = 0;
    for (uint32_t tick = 3; tick < 200 && debris->live; ++tick) {
        match.simulation().tick = tick;
        match.tick();
        landed_after = static_cast<int32_t>(tick);
    }
    CHECK(!debris->live && landed_after > 3);
    bool landing_logged = false;
    for (int32_t i = 0; i < world.explosion_count; ++i)
        landing_logged =
            landing_logged || (world.explosions[i].sprite.sequence == &explosion &&
                               world.explosions[i].flash.sequence == world.flash_tiers[0]);
    CHECK(landing_logged);
    std::cout << "offline effects death particle set passed\n";
}
