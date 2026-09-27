// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Known unit deaths through shattering, with models and textures from the
// game data. A Peewee (ARMPW) killed with severity 26..50 runs `explode torso
// type SHATTER | BITMAP5` while walking; a Core Underwater Metal Extractor
// (CORUWMEX) runs `explode base type SHATTER | EXPLODE_ON_HIT | BITMAP2` on a
// base whose selection primitive is the 3DO file's primitive 57.
#include "oa/sim/unit_effects/effects_offline.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/present/model/model_library.hpp"
#include "oa/test/game_assets.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace oa;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)

namespace {

struct Scenario : sim::scenario::DefinitionHost {
    int32_t integer(std::string_view, int32_t fallback) override { return fallback; }

    std::optional<std::string> text(std::string_view) override { return std::nullopt; }
};

// Records the synced stream as the explode instruction reaches the effects,
// and the explosion table as the debris request leaves them, before the
// tick's explosion step moves anything. A walking unit's movement object is
// given its velocity as the explode starts.
struct Services : sim::match_runtime::OfflineServices,
                  sim::unit_effects::OfflineLifecycle,
                  sim::unit_effects::Sink {
    sim::unit_effects::OfflineEffects* effects{};
    sim::match_runtime::Match* match{};
    std::array<int32_t, 3> walking{};
    bool moving_at_explode{};
    uint32_t stream_at_explode{};
    uint32_t stream_after_shatter{};
    uint32_t explodes{};
    std::vector<sim::effect_particles::ExplosionRecord> records;
    std::vector<sim::effect_particles::ShatterFragment> fragments;

    void activation_sound(sim::unit_spawn::Slot&, sim::unit_activation::Sound) override {}

    void command_sound(sim::unit_spawn::Slot&, uint32_t) override {}

    void attachment_notification(sim::unit_spawn::Slot&, uint32_t) override {}

    void refresh_selected_unit(sim::unit_spawn::Slot&) override {}

    void emit_sfx(sim::unit_spawn::Slot& s, uint32_t p, int32_t e) override {
        effects->emit_sfx(s, p, e);
    }

    void explode_piece(sim::unit_spawn::Slot& s, uint32_t p, int32_t f) override {
        stream_at_explode = match->random_state();
        ++explodes;
        if (auto* mover = match->ground_runtime(s.unit_index); mover != nullptr) {
            mover->movement.velocity = walking;
            moving_at_explode = true;
        }
        effects->explode_piece(s, p, f);
    }

    void attach_unit(sim::unit_spawn::Slot& s, int32_t c, int32_t p, int32_t m) override {
        effects->attach_unit(s, c, p, m);
    }

    void drop_unit(sim::unit_spawn::Slot& s, int32_t c) override { effects->drop_unit(s, c); }

    void refresh_plot_height_range(std::array<int16_t, 2>, std::array<int16_t, 2>) override {}

    void notify_object_footprint_removed(sim::spatial_state::Unit&, uint32_t) override {}

    void notify_footprint_changed(std::array<int16_t, 2>, std::array<int16_t, 2>) override {}

    void effect(const sim::unit_effects::Event& e) override {
        if (e.kind != sim::unit_effects::EventKind::debris_piece)
            return;
        stream_after_shatter = match->random_state();
        const auto& world = match->effects();
        records.assign(world.explosions, world.explosions + world.explosion_count);
        for (const auto& record : records)
            if (record.fragment != sim::effect_particles::no_fragment)
                fragments.push_back(world.fragments[record.fragment]);
    }
};

// The minimal standard generator of the synced stream, replayed from a state.
struct Replay {
    uint32_t state{};

    int32_t next(uint32_t bound) {
        state = state * 0x41a7U - (state / 0x1f31dU) * 0x7fffffffU;
        if (static_cast<int32_t>(state) < 1)
            state += 0x7fffffffU;
        return static_cast<int32_t>(state % bound);
    }
};

// A mobile unit of the model `model_file` whose Killed script runs `explode
// <piece> type <explode_flags>`, killed on a flat 16x16 map at (96, 20, 96)
// while moving at `walking`. `check` gets the match, what the effects
// recorded and the model.
template <class Check>
void kill(
    AssetStore& assets,
    present::model::ModelLibrary& library,
    const char* model_file,
    const char* piece,
    uint32_t explode_flags,
    const std::array<int32_t, 3>& walking,
    Check&& check
) {
    const auto bytes = assets.read(model_file).bytes;
    auto model = std::make_shared<formats::objects3d::Model>(formats::objects3d::load_3do(
        {reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()}
    ));
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
    auto script = std::make_shared<formats::cob::CobProgram>();
    {
        using namespace sim::script_vm;
        script->code = {
            opcode::return_,
            opcode::push_constant,
            explode_flags,
            opcode::explode,
            0,
            opcode::push_constant,
            2,
            opcode::pop_local,
            1,
            opcode::return_
        };
    }
    script->scripts = {{"Create", 0}, {"Killed", 1}};
    script->entry_points = {0, 1};
    script->piece_names = {piece};
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
    std::vector<FeatureDef> features(1);
    features[0].footprint_x = features[0].footprint_z = 1;
    features[0].dead_feature = 0xffff;
    sim::combat_state::WeaponRegistry weapons;
    Scenario scenario;
    Services services;
    services.walking = walking;
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
    // The loader the app installs: the prepared model's primitive order and
    // flag words, with the source quads' vertex lists.
    input.loaded_primitives = [&](const formats::objects3d::Model& loaded_model,
                                  uint32_t object,
                                  std::vector<sim::effect_particles::PiecePrimitive>& out) {
        // The case's one unit type owns the model the loader is asked for.
        CHECK(&loaded_model == model.get());
        const auto& prepared = present::model::prepare_model(library, model).objects[object];
        out.clear();
        for (const auto& primitive : prepared.primitives) {
            const auto& source = loaded_model.objects[object].primitives[primitive.source_index];
            out.push_back(
                {source.color_index,
                 static_cast<uint32_t>(source.vertex_indices.size()),
                 source.vertex_indices.data(),
                 primitive.flags}
            );
        }
        return prepared.skips_first ? 0 : -1;
    };
    sim::match_runtime::Match match(input, services);
    services.match = &match;
    effects.bind(match);
    match.simulation().players[0].present = true;
    match.simulation().players[0].status = 1;
    std::array<uint8_t, 10> allies{};
    allies[0] = 1;
    match.configure_outcomes(0, allies, true);
    match.simulation().tick = 1;
    match.tick();
    auto* victim = match.create({0, 1, {96u << 16, 0, 96u << 16}, true, 1, 0});
    CHECK(victim);
    victim->unit->position[1] = 20u << 16;
    CHECK(match.effects().explosion_count == 0);
    match.apply_damage_event(*victim, nullptr, 30000, 1, 0);
    match.simulation().tick = 2;
    match.tick();
    CHECK(services.explodes == 1 && services.moving_at_explode);
    check(match, services, *model);
}

// Replays the synced stream from the explode: the explode draws six numbers
// for the debris request before the shatter.
Replay stream_at_shatter(const Services& services) {
    Replay replay{services.stream_at_explode};
    for (int32_t i = 0; i < 6; ++i)
        (void)replay.next(2);
    return replay;
}

// The torso's quads by their position in the loaded order; the four at 5,
// 6, 7 and 10 carry the coloured bit in the flag word the loader keeps from
// the 3DO file. Their normals scaled by 512 and truncated, for this piece at
// several stream states.
constexpr uint32_t torso_quads[]{0, 1, 2, 3, 4, 8, 9, 11, 12, 13, 14, 15, 16, 17};
constexpr int32_t torso_leans[][2]{
    {512, 0},
    {0, -512},
    {-512, 0},
    {0, 511},
    {508, 57},
    {0, 97},
    {512, 0},
    {0, 503},
    {501, 19},
    {-501, 19},
    {0, 368},
    {0, -355},
    {0, -495},
    {0, 97}
};

// Fourteen fragments with the flings and leans of the synced stream and
// half the Peewee's walking velocity.
void peewee_torso(AssetStore& assets, present::model::ModelLibrary& library) {
    constexpr std::array<int32_t, 3> walking{0x12345, -0x2001, -0x9876};
    kill(
        assets,
        library,
        "objects3d/armpw.3do",
        "torso",
        0x1 | 0x1000,
        walking,
        [&](sim::match_runtime::Match& match,
            const Services& services,
            const formats::objects3d::Model& model) {
            auto replay = stream_at_shatter(services);
            constexpr size_t fragment_count = std::size(torso_quads);
            CHECK(
                services.records.size() == fragment_count &&
                services.fragments.size() == fragment_count
            );
            const auto gravity = match.state().game.gravity;
            for (size_t k = 0; k < fragment_count; ++k) {
                int32_t draw[8]{};
                for (int32_t i = 0; i < 8; ++i)
                    draw[i] = replay.next(i < 3 ? 160 : i < 6 ? 1600 : 200);
                const auto& record = services.records[k];
                CHECK(record.fragment == static_cast<int16_t>(k));
                CHECK(record.velocity.x == (80 - draw[0]) * 512 + draw[6] * torso_leans[k][0]);
                CHECK(record.velocity.z == (80 - draw[1]) * 512 - draw[7] * torso_leans[k][1]);
                CHECK(record.velocity.y == (80 - draw[2]) * 512 + gravity * 30);
                CHECK(
                    record.spin_rate[0] == 800 - draw[3] && record.spin_rate[1] == 800 - draw[4] &&
                    record.spin_rate[2] == 800 - draw[5]
                );
                // The movement record's velocity, halved with the sign kept.
                CHECK(
                    record.carried.x == 0x91a2 && record.carried.y == -0x1001 &&
                    record.carried.z == -0x4c3b
                );
                CHECK(
                    record.position.x == static_cast<int32_t>(96u << 16) &&
                    record.position.y == static_cast<int32_t>(20u << 16) + 902431 &&
                    record.position.z == static_cast<int32_t>(96u << 16)
                );
                CHECK(!record.explodes && !record.sprite.active());
                CHECK(
                    services.fragments[k].look.primitive == torso_quads[k] &&
                    services.fragments[k].look.model == &model
                );
            }
            CHECK(services.stream_after_shatter == replay.state);
            // BITMAP5 logs its record after the fragments, and the tick then
            // flies them: each is still in the air one step later.
            const auto& world = match.effects();
            CHECK(world.explosion_count == static_cast<int32_t>(fragment_count) + 1);
            CHECK(world.explosions[fragment_count].fragment == sim::effect_particles::no_fragment);
            for (size_t k = 0; k < fragment_count; ++k)
                CHECK(world.explosions[k].velocity.y == services.records[k].velocity.y - gravity);
        }
    );
    std::cout << "ARMPW torso shatter passed\n";
}

// The records built for three of the base's fragments: flung,
// leaned and spun from the synced stream at the shatter.
struct BaseShard {
    size_t record;
    FixedVec3 velocity;
    int32_t rates[3];
};

constexpr BaseShard base_shards[]{
    {0, {-73728, 262570, -28160}, {9, 674, -720}},
    {55, {-45056, 207786, -34304}, {-289, 4, 777}},
    {92, {24064, 283562, -31232}, {-526, -799, -792}}
};

// The loader swaps the selection primitive (file primitive 57) to the front
// and sets the object's selection index to 0: the quad it swapped to position
// 57 shatters, and the selection primitive, coloured by its stray file bit,
// does not. Of 95 primitives, positions 2..94 break off: 93 fragments and
// 744 draws.
void underwater_extractor_base(AssetStore& assets, present::model::ModelLibrary& library) {
    constexpr std::array<int32_t, 3> walking{0x12345, -0x2001, -0x9876};
    kill(
        assets,
        library,
        "objects3d/coruwmex.3do",
        "base",
        0x1 | 0x2 | 0x200,
        walking,
        [&](sim::match_runtime::Match& match,
            const Services& services,
            const formats::objects3d::Model& model) {
            CHECK(model.objects[0].selection_primitive == 57);
            auto replay = stream_at_shatter(services);
            constexpr size_t fragment_count = 93;
            CHECK(
                services.records.size() == fragment_count &&
                services.fragments.size() == fragment_count
            );
            const auto gravity = match.state().game.gravity;
            for (size_t k = 0; k < fragment_count; ++k) {
                int32_t draw[8]{};
                for (int32_t i = 0; i < 8; ++i)
                    draw[i] = replay.next(i < 3 ? 160 : i < 6 ? 1600 : 200);
                const auto& record = services.records[k];
                CHECK(record.fragment == static_cast<int16_t>(k));
                CHECK(services.fragments[k].look.primitive == k + 2);
                CHECK(record.velocity.y == (80 - draw[2]) * 512 + gravity * 30);
                CHECK(
                    record.spin_rate[0] == 800 - draw[3] && record.spin_rate[1] == 800 - draw[4] &&
                    record.spin_rate[2] == 800 - draw[5]
                );
                CHECK(
                    record.carried.x == 0x91a2 && record.carried.y == -0x1001 &&
                    record.carried.z == -0x4c3b
                );
                CHECK(
                    record.position.x == static_cast<int32_t>(96u << 16) &&
                    record.position.y == static_cast<int32_t>(20u << 16) &&
                    record.position.z == static_cast<int32_t>(96u << 16)
                );
                CHECK(record.explodes);
            }
            for (const auto& shard : base_shards) {
                const auto& record = services.records[shard.record];
                CHECK(
                    record.velocity.x == shard.velocity.x &&
                    record.velocity.y == shard.velocity.y && record.velocity.z == shard.velocity.z
                );
                CHECK(
                    record.spin_rate[0] == shard.rates[0] &&
                    record.spin_rate[1] == shard.rates[1] && record.spin_rate[2] == shard.rates[2]
                );
            }
            CHECK(services.stream_after_shatter == replay.state);
        }
    );
    std::cout << "CORUWMEX base shatter passed\n";
}

} // namespace

int main() {
    AssetStore assets = test::require_game_assets("the ARMPW and CORUWMEX shatters");
    try {
        CHECK(
            assets.file_size("objects3d/armpw.3do") != 0 &&
            assets.file_size("objects3d/coruwmex.3do") != 0
        );
        // Each case takes its own library, so the cases stay independent.
        {
            present::model::ModelLibrary library;
            library.textures = present::model::load_texture_library(assets);
            peewee_torso(assets, library);
        }
        {
            present::model::ModelLibrary library;
            library.textures = present::model::load_texture_library(assets);
            underwater_extractor_base(assets, library);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
