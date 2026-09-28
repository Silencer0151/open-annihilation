// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/match_runtime.hpp"

#include <iostream>
#include <stdexcept>

using namespace oa;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)

namespace {
struct Services : sim::match_runtime::OfflineServices {
    void activation_sound(sim::unit_spawn::Slot&, sim::unit_activation::Sound) override {}

    void command_sound(sim::unit_spawn::Slot&, uint32_t) override {}

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

formats::gaf::Sequence sequence_of(size_t frames, uint16_t hold) {
    formats::gaf::Sequence sequence{};
    sequence.frames.resize(frames);
    for (auto& frame : sequence.frames)
        frame.duration = hold;
    return sequence;
}

constexpr int32_t map_cells = 16;
constexpr uint8_t land_height = 60;
constexpr uint32_t sea_level = 20;
constexpr int32_t land_edge = 8; // columns below this are land, the rest water

uint32_t units(int32_t value) {
    return static_cast<uint32_t>(value) << 16;
}

const sim::effect_particles::Layer& smoke_layer(sim::match_runtime::Match& match) {
    return match.effects().layers[sim::effect_particles::layer_smoke];
}

const sim::effect_particles::Emitter& newest(const sim::effect_particles::Layer& layer) {
    return layer.emitters[(layer.head + layer.count - 1) % sim::effect_particles::layer_capacity];
}

struct Art {
    formats::gaf::Sequence smoke_1 = sequence_of(12, 5);
    formats::gaf::Sequence smoke_2 = sequence_of(16, 3);
    formats::gaf::Sequence flamestream = sequence_of(20, 5);
    formats::gaf::Sequence blast = sequence_of(9, 2);
    formats::gaf::Sequence splash = sequence_of(6, 3);
    formats::gaf::Sequence lava = sequence_of(7, 3);

    const formats::gaf::Sequence* find(std::string_view archive, std::string_view entry) const {
        if (archive == "boom")
            return entry == "blast"    ? &blast
                   : entry == "splash" ? &splash
                   : entry == "lava"   ? &lava
                                       : nullptr;
        if (entry == "smoke 1")
            return &smoke_1;
        if (entry == "smoke 2")
            return &smoke_2;
        if (entry == "flamestream")
            return &flamestream;
        return nullptr;
    }
};

// Weapon impacts (detonation): land art with the small flash and a smoke
// column, endsmoke puffs, water and lava art, and nosealeveltrigger seas.
void weapon_explosions(sim::match_runtime::Match& match, const Art& art) {
    auto& weapon = match.state().game.weapon_defs[1];
    const auto flags = weapon.flags;
    auto& world = match.effects();
    const std::array<uint32_t, 3> on_land{units(40), units(land_height), units(40)};
    const std::array<uint32_t, 3> in_water{units(200), units(0), units(40)};
    const auto layered = smoke_layer(match).count;

    CHECK(match.spawn_weapon_explosion(weapon, on_land, false));
    CHECK(world.explosion_count == 1 && world.explosions[0].sprite.sequence == &art.blast);
    CHECK(world.explosions[0].flash.sequence == world.flash_tiers[0]);
    CHECK(smoke_layer(match).count == layered + 1);
    const auto& column = newest(smoke_layer(match));
    CHECK(column.interval == 7 && column.deadline == match.state().game.tick + 15);

    weapon.flags = flags | OA_WEAPON_FLAG_END_SMOKE;
    CHECK(match.spawn_weapon_explosion(weapon, on_land, false));
    CHECK(world.explosion_count == 1);
    CHECK(smoke_layer(match).count == layered + 2 && newest(smoke_layer(match)).interval == 1);
    weapon.flags = flags;

    CHECK(match.spawn_weapon_explosion(weapon, in_water, false));
    CHECK(world.explosion_count == 2 && world.explosions[1].sprite.sequence == &art.splash);
    CHECK(smoke_layer(match).count == layered + 2);
    CHECK(match.spawn_weapon_explosion(weapon, in_water, true));
    CHECK(world.explosion_count == 3 && world.explosions[2].sprite.sequence == &art.blast);
    CHECK(smoke_layer(match).count == layered + 2);

    world.lava_world = true;
    CHECK(match.spawn_weapon_explosion(weapon, in_water, false));
    CHECK(world.explosion_count == 4 && world.explosions[3].sprite.sequence == &art.lava);
    world.lava_world = false;

    world.no_sea_level_trigger = true;
    CHECK(!match.spawn_weapon_explosion(weapon, in_water, false));
    CHECK(match.spawn_weapon_explosion(weapon, in_water, true));
    CHECK(world.explosion_count == 5);
    world.no_sea_level_trigger = false;
    world.explosion_count = 0;
    std::cout << "detonation impact effects passed\n";
}

// spawn_corpse leaves the corpse where the unit stood and smokes it on land for
// 900 ticks. Under water a 3DO corpse sinks and does not smoke, a sprite one
// (which gets no record) still smokes, and a dismissed unit never smokes.
void wreck_smoke(sim::match_runtime::Match& match) {
    auto& world = match.state();
    auto& corpse = world.feature_defs[0];
    auto* on_land = match.create({0, 1, {units(40), units(land_height), units(40)}, true, 1, 0});
    auto* sunk = match.create({0, 1, {units(200), units(0), units(40)}, true, 1, 0});
    CHECK(on_land && sunk);
    const auto land_plot =
        static_cast<size_t>(on_land->record.cell_z * map_cells + on_land->record.cell_x);
    const auto sea_plot =
        static_cast<size_t>(sunk->record.cell_z * map_cells + sunk->record.cell_x);
    const auto before = smoke_layer(match).count;
    match.spawn_corpse(*on_land, 1, false);
    CHECK(smoke_layer(match).count == before);
    CHECK(world.plots[land_plot].feature == 0 && match.wrecks().back().feature == 0);
    const auto* record =
        sim::feature_runtime::feature_record(world, world.plots[land_plot].feature_record);
    CHECK(record != nullptr && record->model.position.x == on_land->record.position.x);
    CHECK(record->orientation[1] == static_cast<int16_t>(on_land->record.heading));
    match.spawn_corpse(*on_land, 1, true);
    CHECK(smoke_layer(match).count == before + 1);
    const auto& column = newest(smoke_layer(match));
    CHECK(column.interval == 15 && column.deadline == match.state().game.tick + 900);
    match.spawn_corpse(*sunk, 1, true);
    CHECK(smoke_layer(match).count == before + 1);
    const auto* sinking =
        sim::feature_runtime::feature_record(world, world.plots[sea_plot].feature_record);
    CHECK(
        sinking != nullptr && sinking->model.velocity.y == sim::feature_runtime::wreck_sink_speed
    );
    corpse.flags = OA_FEATURE_FLAG_SPRITE;
    match.spawn_corpse(*sunk, 1, true);
    CHECK(smoke_layer(match).count == before + 2);
    corpse.flags = 0;
    // Wreck level 2 follows featuredead, which this corpse lacks.
    const auto wrecks = match.wrecks().size();
    match.spawn_corpse(*on_land, 2, true);
    CHECK(smoke_layer(match).count == before + 2 && match.wrecks().size() == wrecks);
    std::cout << "corpse and wreck smoke passed\n";
}

// Reclaim, VTOL_Reclaim and Resurrect: the feature box spans the footprint
// from the ground at the origin cell up by the feature's height; the nano
// emitter keeps the middle 3/11 of each axis.
void feature_sprays(sim::match_runtime::Match& match, const FeatureDef& feature) {
    auto* worker = match.create({0, 1, {units(24), units(land_height), units(24)}, true, 1, 0});
    CHECK(worker);
    const auto& layer = match.effects().layers[sim::effect_particles::layer_nano];
    const auto before = layer.count;
    match.spray_nano_feature(*worker, 2, 3, feature, true);
    CHECK(layer.count == before + 1);
    const auto& inward = newest(layer);
    const int32_t low_x = 2 << 20, low_y = land_height << 16, low_z = 3 << 20;
    const int32_t span_x = feature.footprint_x << 20, span_y = feature.height << 16;
    const int32_t span_z = feature.footprint_z << 20;
    CHECK(
        inward.origin.x == low_x + span_x * 4 / 11 &&
        inward.origin_extent.x == span_x * 7 / 11 - span_x * 4 / 11
    );
    CHECK(inward.origin.y == low_y + span_y * 4 / 11 && inward.origin.z == low_z + span_z * 4 / 11);
    CHECK(
        inward.target_extent.x == 0 &&
        inward.target.x == static_cast<int32_t>(worker->unit->position[0])
    );
    match.spray_nano_feature(*worker, 2, 3, feature, false);
    const auto& outward = newest(layer);
    CHECK(outward.target.x == inward.origin.x && outward.origin_extent.x == 0);
    std::cout << "feature nano sprays passed\n";
}

// The Fire path puffs startsmoke at the muzzle, a smoke-trail shot puffs
// every smokedelay ticks, a shot that drops into the sea splashes, and an
// inert shot still trails.
void fired_shots(sim::match_runtime::Match& match, const Art& art) {
    auto* shooter = match.create({0, 1, {units(150), units(80), units(40)}, true, 1, 0});
    auto* target = match.create({1, 1, {units(200), units(sea_level + 4), units(40)}, true, 1, 0});
    CHECK(shooter && target);
    shooter->unit->object_present = false;
    target->unit->object_present = false;
    shooter->unit->flags |= 0x10000000u;
    target->unit->flags |= 0x10000000u;
    auto& slot = shooter->record.weapons[0];
    slot.target_a = static_cast<int16_t>(target->unit_index);
    slot.target_b = OA_UNIT_TARGET_IS_UNIT;
    auto& world = match.effects();
    world.explosion_count = 0;
    const uint32_t start = match.simulation().tick;
    int32_t launched_at = -1;
    int32_t trail_puffs = 0;
    int32_t entry_splashes = 0;
    int32_t impact_splashes = 0;
    for (int32_t step = 1; step <= 40; ++step) {
        const auto logged = world.explosion_count;
        match.simulation().tick = start + static_cast<uint32_t>(step);
        match.tick();
        const auto& layer = smoke_layer(match);
        const auto& last = newest(layer);
        const bool fresh =
            layer.count != 0 && last.interval == 1 && last.deadline == match.simulation().tick;
        if (launched_at < 0) {
            if (match.projectiles().empty())
                continue;
            launched_at = step;
            CHECK(fresh && last.frame_cap == 3 && last.hold == 30);
            // With its target gone the shot flies on down into the sea.
            match.apply_damage_event(*target, nullptr, 30000, 1, 0);
            continue;
        }
        for (int32_t i = logged; i < world.explosion_count; ++i)
            if (world.explosions[i].sprite.sequence == &art.splash)
                ++(match.projectiles().empty() ? impact_splashes : entry_splashes);
        if (fresh && last.hold == 7) {
            CHECK((step - launched_at) % 2 == 1);
            ++trail_puffs;
        }
    }
    CHECK(launched_at > 0);
    CHECK(trail_puffs >= 2);
    // The shot splashes as it drops into the sea, then goes off on the water
    // with the same art a tick later.
    CHECK(entry_splashes == 1 && impact_splashes == 1);
    std::cout << "startsmoke, smoke trail and water entry passed\n";
}
} // namespace

int main() {
    formats::tnt::Map map;
    map.attribute_width = map.attribute_height = map_cells;
    map.attributes.resize(map_cells * map_cells);
    map.sea_level = sea_level;
    std::vector<sim::spatial_state::Plot> plots(map_cells * map_cells);
    for (int32_t z = 0; z < map_cells; ++z)
        for (int32_t x = 0; x < map_cells; ++x) {
            const auto height = x < land_edge ? land_height : uint8_t{0};
            map.attributes[static_cast<size_t>(z * map_cells + x)].height = height;
            plots[static_cast<size_t>(z * map_cells + x)].high_height = height;
            plots[static_cast<size_t>(z * map_cells + x)].low_height = height;
        }
    std::vector<sim::visibility_state::TerrainCell> terrain(map_cells * map_cells);
    sim::visibility_state::SightMask mask;
    mask.width = mask.height = 1;
    mask.pixels = {1};
    const std::array masks{mask};
    auto model = std::make_shared<formats::objects3d::Model>();
    model->objects.resize(1);
    model->objects[0].name = "root";
    auto script = std::make_shared<formats::cob::CobProgram>();
    using namespace sim::script_vm;
    script->code = {
        opcode::return_,
        opcode::push_constant,
        1,
        opcode::pop_static,
        0,
        opcode::return_,
        opcode::push_constant,
        1,
        opcode::pop_static,
        1,
        opcode::return_
    };
    script->scripts = {{"Create", 0}, {"AimPrimary", 1}, {"FirePrimary", 6}};
    script->entry_points = {0, 1, 6};
    script->header.static_variable_count = 2;
    script->piece_names = {"root"};
    std::array<sim::unit_spawn::LoadedType, 2> loaded{};
    loaded[1].model = model;
    loaded[1].script = script;
    std::array<sim::unit_spawn::Type, 2> types{};
    types[1].simulation.flags = 0x800000;
    types[1].simulation.maximum_health = 100;
    types[1].footprint_x = types[1].footprint_z = 1;
    types[1].model = reinterpret_cast<uintptr_t>(model.get());
    types[1].cob = reinterpret_cast<uintptr_t>(script.get());
    loaded[1].type = types[1];
    data::unit_definitions::UnitDefinition def;
    def.sight_distance = 160;
    def.weapon1 = "TESTGUN";
    data::unit_definitions::UnitTargetCategoryMasks target_masks;
    data::unit_definitions::RuntimeDefinitionMetadata metadata;
    const std::array<uint8_t, 1> yard{4};
    std::array<sim::match_runtime::RuntimeTypeFields, 2> fields{};
    fields[1].definition = &def;
    fields[1].yard_mask = yard;
    fields[1].runtime_metadata = &metadata;
    fields[1].target_masks = &target_masks;
    fields[1].corpse_feature = 0;
    std::vector<FeatureDef> features(1);
    features[0].footprint_x = 2;
    features[0].footprint_z = 3;
    features[0].height = 11;
    features[0].dead_feature = 0xffff;
    sim::combat_state::WeaponRegistry weapons;
    weapons.install_tdf_section(1, "TESTGUN", "0.1");
    weapons.install_target_fields(1, "400", "1", "0", "0", "0", "0", "10", "100", "-60", "1");
    weapons.install_explosion_sprites(1, "boom", "blast", "boom", "splash", "boom", "lava");
    auto& gun = const_cast<sim::combat_state::WeaponDefinition&>(*weapons.find("TESTGUN"));
    gun.flags |=
        sim::combat_state::weapon_start_smoke_flag | sim::combat_state::weapon_smoke_trail_flag;
    gun.smoke_delay_ticks = 2;
    Art art;
    Services services;
    Scenario scenario;
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
        plots,
        {}
    };
    input.feature_defs = features;
    input.effect_sequence = [&art](std::string_view archive, std::string_view entry) {
        return art.find(archive, entry);
    };
    sim::match_runtime::Match match(input, services);
    CHECK(
        match.effects().fx[static_cast<uint32_t>(sim::effect_particles::Fx::smoke_1)] ==
        &art.smoke_1
    );
    match.configure_strategic_environment({0, 0.5F, 0});
    match.simulation().players[0].present = true;
    match.simulation().players[0].status = 1;
    match.simulation().players[1].present = true;
    match.simulation().players[1].status = 2;
    std::array<uint8_t, 10> allies{};
    allies[0] = 1;
    match.configure_outcomes(0, allies, true);
    std::array<uint8_t, 10> enemy_allies{};
    enemy_allies[1] = 1;
    match.configure_player_alliances(1, enemy_allies);
    match.simulation().tick = 1;
    match.tick();

    weapon_explosions(match, art);
    wreck_smoke(match);
    feature_sprays(match, features[0]);
    fired_shots(match, art);
    return 0;
}
