// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "../src/tick_internal.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace oa;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)

namespace {
struct Services : sim::match_runtime::OfflineServices {
    std::vector<uint32_t> speech;

    void command_sound(sim::unit_spawn::Slot&, uint32_t category) override {
        speech.push_back(category);
        caption.clear();
    }

    /// The match's speech hook: a speech with its order's caption.
    static void speak(void* context, sim::unit_spawn::Slot&, uint32_t category, const char* text) {
        auto& services = *static_cast<Services*>(context);
        services.speech.push_back(category);
        services.caption = text;
    }

    std::string caption; // the last speech's caption; empty for none

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

    uint32_t last() const { return speech.empty() ? 0xffffffffu : speech.back(); }
};

struct Scenario : sim::scenario::DefinitionHost {
    int32_t integer(std::string_view, int32_t fallback) override { return fallback; }

    std::optional<std::string> text(std::string_view) override { return std::nullopt; }
};

constexpr uint16_t aircraft_type = 1;
constexpr uint16_t structure_type = 2;

// Runs one dispatch and applies the phase rules the order queue uses for
// results 0 and 1.
uint32_t step(
    sim::match_runtime::Match& match,
    sim::unit_spawn::Slot& slot,
    sim::simulation_state::Order& order,
    uint32_t events
) {
    sim::match_runtime::TickHost host(match);
    const auto result = host.dispatch_mission(match.state(), slot.record, order, events);
    if (result == 0)
        order.phase = 0;
    else if (result == 1)
        ++order.phase;
    return result;
}
} // namespace

int main() {
    formats::tnt::Map map;
    map.attribute_width = map.attribute_height = 16;
    map.attributes.resize(256);
    std::vector<sim::visibility_state::TerrainCell> terrain_values(256);
    sim::visibility_state::SightMask mask;
    mask.width = mask.height = 1;
    mask.pixels = {1};
    const std::array masks{mask};
    auto model = std::make_shared<formats::objects3d::Model>();
    model->objects.resize(1);
    model->objects[0].name = "root";
    auto script = std::make_shared<formats::cob::CobProgram>();
    script->code = {sim::script_vm::opcode::return_};
    script->scripts = {{"Create", 0}};
    script->entry_points = {0};
    script->piece_names = {"root"};
    std::array<sim::unit_spawn::LoadedType, 3> loaded;
    std::array<sim::unit_spawn::Type, 3> types;
    std::array<data::unit_definitions::UnitDefinition, 3> defs;
    for (size_t i = 1; i < 3; ++i) {
        loaded[i].model = model;
        loaded[i].script = script;
        types[i].simulation.flags = 0x800000;
        types[i].simulation.maximum_health = 100;
        types[i].footprint_x = types[i].footprint_z = 1;
        types[i].model = reinterpret_cast<uintptr_t>(model.get());
        types[i].cob = reinterpret_cast<uintptr_t>(script.get());
        defs[i].sight_distance = 160;
        defs[i].acceleration_fixed = 65536;
        defs[i].brake_rate_fixed = 65536;
        defs[i].max_velocity_fixed = 2 * 65536;
        defs[i].turn_rate = 1024;
        defs[i].energy_storage = 1000.0F;
        defs[i].metal_storage = 1000.0F;
        defs[i].build_cost_energy = 100;
        defs[i].build_cost_metal = 100;
        defs[i].build_time = 100;
    }
    types[aircraft_type].simulation.flags |= OA_UNIT_DEF_FLAG_CAN_FLY | OA_UNIT_DEF_FLAG_BUILDER;
    types[aircraft_type].simulation.abilities =
        OA_UNIT_DEF_ABILITY_CAN_MOVE | OA_UNIT_DEF_ABILITY_CAN_PATROL |
        OA_UNIT_DEF_ABILITY_CAN_REPAIR | OA_UNIT_DEF_ABILITY_CAN_RECLAMATE;
    types[aircraft_type].bm_code = 1;
    defs[aircraft_type].builder = true;
    defs[aircraft_type].can_fly = true;
    defs[aircraft_type].worker_time = 300;
    defs[aircraft_type].build_distance = 64;
    defs[aircraft_type].cruise_altitude = 50;
    for (size_t i = 1; i < 3; ++i)
        loaded[i].type = types[i];
    data::unit_definitions::UnitTargetCategoryMasks target_masks;
    data::unit_definitions::RuntimeDefinitionMetadata metadata;
    std::vector<sim::spatial_state::Plot> collision_plots(256);
    constexpr size_t feature_plot = 5 * 16 + 5;
    collision_plots[feature_plot].feature_word = 0;
    std::array<FeatureDef, 1> feature_defs{};
    feature_defs[0].footprint_x = feature_defs[0].footprint_z = 1;
    feature_defs[0].energy = 20.0F;
    feature_defs[0].metal = 40.0F;
    feature_defs[0].flags = OA_FEATURE_FLAG_RECLAIMABLE | OA_FEATURE_FLAG_AUTO_RECLAIMABLE;
    // No featuredead, featureburnt or featurereclamate remnants.
    feature_defs[0].dead_feature = sim::feature_runtime::no_feature;
    feature_defs[0].burnt_feature = sim::feature_runtime::no_feature;
    feature_defs[0].reclamate_feature = sim::feature_runtime::no_feature;
    std::array<sim::match_runtime::RuntimeTypeFields, 3> fields{};
    const std::array<uint8_t, 1> yard{4};
    for (size_t i = 1; i < 3; ++i) {
        fields[i].definition = &defs[i];
        fields[i].yard_mask = yard;
        fields[i].runtime_metadata = &metadata;
        fields[i].target_masks = &target_masks;
    }
    fields[aircraft_type].movement_class = 0;
    sim::combat_state::WeaponRegistry weapons;
    Services services;
    Scenario scenario;
    sim::match_runtime::OfflineInputs input{
        map, loaded, types, fields,    weapons, terrain_values,  masks, 8, 8, 2,    2,
        0,   30,     1,     &scenario, {},      collision_plots, {},    0, 0, 0.0F, feature_defs
    };
    sim::match_runtime::Match match(input, services);
    match.set_speech_hooks({&services, &Services::speak});
    match.reload_unit_defs();
    for (uint8_t player = 0; player < 2; ++player) {
        match.simulation().players[player].present = true;
        match.simulation().players[player].status = 1;
    }
    auto& p0 = match.state().game.players[0];
    p0.alliance[0] = 1;
    p0.energy = 1000.0F;
    p0.metal = 1000.0F;
    match.simulation().tick = 1;

    auto* builder = match.create({0, aircraft_type, {64u << 16, 0, 64u << 16}, true, 1, 0});
    CHECK(builder && builder->record.movement != 0 && match.ground_runtime(builder->unit_index));
    const auto builder_id = builder->unit_index;
    auto& queue = match.orders(builder_id);

    // MobileBuild resolves to the VTOL mission for an aircraft.
    const sim::ground_orders::Point site{160 << 16, 0, 160 << 16};
    auto& build = match.issue_mobile_build(builder_id, structure_type, site, false);
    CHECK(build.kind == sim::match_runtime::vtol_mobile_build_kind);
    CHECK(step(match, *builder, build, 0) == 1 && services.last() == 5);
    CHECK(services.caption == "Building");
    CHECK(step(match, *builder, build, 0) == 1);
    CHECK(build.wait_events == 0xe0);
    // The approach is an air goal reached anywhere within builddistance.
    const auto* approach = match.air_driver(builder_id)->goal;
    CHECK(approach && (approach->flags & sim::air::goal_arrival_radius) != 0);
    CHECK(approach->arrival_radius == 64);
    CHECK(match.ground_runtime(builder_id)->navigation.goal == nullptr);
    CHECK(step(match, *builder, build, sim::ground_orders::path_failed_event) == 8);
    services.speech.clear();
    CHECK(step(match, *builder, build, 0) == 1 && services.last() == 9);
    CHECK(services.caption == "Starting construction");
    sim::unit_spawn::Slot* frame = nullptr;
    for (auto& slot : match.world().slots)
        if (slot.unit && slot.record.type_index == structure_type && slot.unit_index != builder_id)
            frame = &slot;
    CHECK(frame && frame->record.build_remaining == 1.0F);
    CHECK(
        match.orders(frame->unit_index).primary &&
        match.orders(frame->unit_index).primary->kind == sim::match_runtime::get_built_kind
    );
    CHECK(step(match, *builder, build, 0) == 2 && build.phase == 3);
    CHECK((build.wait_events & 0xb) == 0xb);
    // On every 150th tick the builder moves to hover builddistance from the
    // frame, 0x2492 short of its bearing to it, and holds that bearing.
    const auto saved_tick = match.state().game.tick;
    match.state().game.tick = 150;
    CHECK(step(match, *builder, build, 0) == 2 && build.phase == 3);
    match.state().game.tick = saved_tick;
    const auto bearing = static_cast<uint16_t>(
        base::game_math::direction(
            builder->record.position.x - frame->record.position.x,
            builder->record.position.z - frame->record.position.z
        ) -
        0x2492
    );
    const auto* hover = match.air_driver(builder_id)->goal;
    CHECK(hover && (hover->flags & sim::air::goal_fixed_bearing) != 0 && hover->bearing == bearing);
    CHECK((hover->flags & (sim::air::goal_arrival_radius | sim::air::goal_fixed_altitude)) == 0);
    frame->record.build_remaining = 0.0F;
    CHECK(step(match, *builder, build, 0) == 1 && build.phase == 4);
    CHECK(step(match, *builder, build, 0) == 1 && build.phase == 5);
    CHECK(step(match, *builder, build, 0) == 5 && services.last() == 8);
    CHECK(services.caption == "Building complete");
    services.speech.clear();
    CHECK(step(match, *builder, build, 0x8) == 8 && services.last() == 7);
    CHECK(services.caption == "Construction terminated");
    CHECK(step(match, *builder, build, 0x2) == 5);

    // HelpBuild needs a build list, then hovers and adds worker time.
    frame->record.build_remaining = 0.5F;
    auto& help = match.issue_help_build(builder_id, frame->unit_index, false);
    CHECK(help.kind == sim::match_runtime::vtol_help_build_kind);
    CHECK(step(match, *builder, help, 0) == 7);
    match.state().unit_defs[aircraft_type].build_ids = 1;
    help.phase = 0;
    CHECK(step(match, *builder, help, 0) == 1 && help.phase == 1);
    CHECK(step(match, *builder, help, 0) == 1 && help.wait_events == 0xe0);
    CHECK(step(match, *builder, help, 0) == 1 && help.phase == 3);
    CHECK(step(match, *builder, help, 0) == 2);
    frame->record.build_remaining = 0.0F;
    CHECK(step(match, *builder, help, 0) == 5);

    // RepairUnit: a damaged, landed ally is repaired until full.
    frame->record.health = 40;
    frame->record.flags = (frame->record.flags & ~3u) | 1u;
    auto& repair = match.issue_repair(builder_id, frame->unit_index, false);
    CHECK(repair.kind == sim::match_runtime::vtol_repair_unit_kind);
    CHECK(step(match, *builder, repair, 0) == 1);
    CHECK(step(match, *builder, repair, 0) == 1 && repair.wait_events == 0xe8);
    // Repair approaches at cruisealt over the land at the patient.
    const auto* over_patient = match.air_driver(builder_id)->goal;
    CHECK(over_patient && (over_patient->flags & sim::air::goal_fixed_altitude) != 0);
    CHECK(over_patient->altitude == 50 && over_patient->point.y == 50 << 16);
    CHECK(step(match, *builder, repair, 0x40) == 8);
    CHECK(step(match, *builder, repair, 0) == 2 && (repair.wait_events & 9) == 9);
    frame->record.health = 100;
    CHECK(step(match, *builder, repair, 0) == 1 && repair.phase == 3);
    services.speech.clear();
    CHECK(step(match, *builder, repair, 0) == 5 && services.last() == 10);
    CHECK(services.caption == "Unit repaired");
    frame->record.flags = (frame->record.flags & ~3u) | 2u;
    services.speech.clear();
    CHECK(step(match, *builder, repair, 0) == 5 && services.last() == 7);
    CHECK(services.caption == "Repairs unsuccessful.");
    frame->record.flags = (frame->record.flags & ~3u) | 1u;

    // ReclaimUnit on an enemy: approach, then bite while in build range.
    auto* enemy = match.create({1, structure_type, {72u << 16, 0, 64u << 16}, true, 1, 0});
    CHECK(enemy);
    auto& reclaim = match.issue_reclaim(builder_id, enemy->unit_index, false);
    CHECK(reclaim.kind == sim::match_runtime::vtol_reclaim_unit_kind);
    CHECK(step(match, *builder, reclaim, 0) == 1);
    services.speech.clear();
    CHECK(step(match, *builder, reclaim, 0) == 1 && services.last() == 11);
    CHECK(services.caption.empty());
    // A plain point goal: no radius, altitude or bearing.
    CHECK(match.air_driver(builder_id)->goal->flags == sim::air::goal_terrain_altitude);
    CHECK((reclaim.wait_events & 0x100e8) == 0x100e8);
    CHECK(step(match, *builder, reclaim, 0x40) == 9);
    CHECK(step(match, *builder, reclaim, 0) == 2 && (reclaim.wait_events & 0x10009) == 0x10009);
    enemy->record.position.x = 400 << 16;
    CHECK(step(match, *builder, reclaim, 0) == 0 && reclaim.phase == 0);
    CHECK(step(match, *builder, reclaim, 0x10000) == 5);

    // VTOL_Reclaim fails where there is no feature.
    auto& bare = match.insert_ground_order(builder_id, sim::match_runtime::vtol_reclaim_kind, site);
    services.speech.clear();
    CHECK(step(match, *builder, bare, 0) == 8 && services.last() == 7);
    CHECK(services.caption == "Reclamation failed");

    // RepairPatrol queues its return leg, then sends the aircraft to repair
    // a damaged ally in sight.
    match.stop_orders(builder_id);
    auto& patrol =
        match.insert_ground_order(builder_id, sim::match_runtime::vtol_repair_patrol_kind, site);
    CHECK(step(match, *builder, patrol, 0) == 1);
    CHECK(
        queue.primary == &patrol && patrol.next &&
        patrol.next->kind == sim::match_runtime::vtol_repair_patrol_kind
    );
    CHECK(step(match, *builder, patrol, sim::ground_orders::arrived_event) == 6);
    frame->record.health = 50;
    builder->record.flags = (builder->record.flags & ~OA_UNIT_FLAG_MOVE_ORDER_MASK) | 0x80000u;
    CHECK(step(match, *builder, patrol, 0) == 6);
    CHECK((match.air_driver(builder_id)->goal->flags & sim::air::goal_fixed_altitude) != 0);
    CHECK(match.air_driver(builder_id)->goal->altitude == 50);
    CHECK(queue.primary && queue.primary->kind == sim::match_runtime::vtol_repair_unit_kind);
    CHECK(queue.primary->next == &patrol);
    CHECK(step(match, *builder, patrol, 0x8) == 0 && (patrol.wait_events & 1));

    // With nothing to repair and metal low, the patrol turns to the metal
    // feature nearby, which VTOL_Reclaim then consumes.
    match.stop_orders(builder_id);
    frame->record.health = 100;
    p0.metal = 100.0F;
    auto& sweep =
        match.insert_ground_order(builder_id, sim::match_runtime::vtol_repair_patrol_kind, site);
    sweep.phase = 1;
    CHECK(step(match, *builder, sweep, 0) == 3 && sweep.wait_events == 0);
    CHECK(match.air_driver(builder_id)->goal == nullptr);
    auto* salvage = queue.primary;
    CHECK(
        salvage && salvage->kind == sim::match_runtime::vtol_reclaim_kind && salvage->next == &sweep
    );
    CHECK(step(match, *builder, *salvage, 0) == 1);
    CHECK(step(match, *builder, *salvage, 0) == 1 && salvage->wait_events == 0xe0);
    services.speech.clear();
    CHECK(step(match, *builder, *salvage, 0) == 1 && services.last() == 11);
    uint32_t bites = 0;
    while (step(match, *builder, *salvage, 0) == 2)
        ++bites;
    CHECK(bites == 29 && salvage->phase == 4);
    CHECK(builder->record.decloak_until_tick == match.state().game.tick + 300);
    CHECK(step(match, *builder, *salvage, 0) == 5);
    CHECK(match.spatial().plots[feature_plot].feature_word == sim::spatial_state::no_feature);

    // A frame destroyed before it is finished wakes its VTOL_MobileBuild
    // with the target-lost event, and the order ends without touching the
    // dead unit. The finished frame leaves first, freeing player 0's second
    // slot for the new one.
    match.stop_orders(builder_id);
    match.kill_unit(
        frame->unit_index, static_cast<uint8_t>(sim::match_runtime::DeathKind::dismissed)
    );
    const sim::ground_orders::Point second_site{224 << 16, 0, 224 << 16};
    auto& doomed = match.issue_mobile_build(builder_id, structure_type, second_site, false);
    CHECK(step(match, *builder, doomed, 0) == 1);
    CHECK(step(match, *builder, doomed, 0) == 1);
    services.speech.clear();
    CHECK(step(match, *builder, doomed, 0) == 1 && services.caption == "Starting construction");
    sim::unit_spawn::Slot* doomed_frame = nullptr;
    for (auto& slot : match.world().slots)
        if (slot.unit && slot.record.type_index == structure_type &&
            slot.record.build_remaining == 1.0F)
            doomed_frame = &slot;
    CHECK(doomed_frame);
    match.kill_unit(
        doomed_frame->unit_index, static_cast<uint8_t>(sim::match_runtime::DeathKind::weapon)
    );
    CHECK(doomed_frame->record.type_index == 0);
    CHECK((doomed.raised_events & 0x8) != 0);
    services.speech.clear();
    CHECK(step(match, *builder, doomed, doomed.raised_events) == 8 && services.last() == 7);
    CHECK(services.caption == "Construction terminated");

    // Every order given against a unit watches it the same way: when the
    // unit dies, each is woken with the target-lost event.
    match.stop_orders(builder_id);
    auto* victim = match.create({0, structure_type, {300u << 16, 0, 300u << 16}, false, 1, 0});
    CHECK(victim);
    const auto victim_id = victim->unit_index;
    std::vector<const sim::simulation_state::Order*> watching{
        &match.issue_help_build(builder_id, victim_id, false),
        &match.issue_repair(builder_id, victim_id, true),
        &match.issue_reclaim(builder_id, victim_id, true),
        &match.issue_capture(builder_id, victim_id, true),
        &match.issue_guard(builder_id, victim_id, true),
    };
    CHECK(match.issue_attack(builder_id, victim_id, false, true));
    const auto* queued_attack = queue.primary;
    while (queued_attack && queued_attack->next)
        queued_attack = queued_attack->next;
    CHECK(queued_attack && queued_attack != watching.back());
    watching.push_back(queued_attack);
    match.kill_unit(victim_id, static_cast<uint8_t>(sim::match_runtime::DeathKind::weapon));
    for (const auto* order : watching)
        CHECK((order->raised_events & 0x8) != 0);
    services.speech.clear();
    CHECK(step(match, *builder, *queue.primary, queue.primary->raised_events) == 8);
    CHECK(services.caption == "Construction terminated by hostile action");

    std::cout << "match-missions-vtol-build: ok\n";
    return 0;
}
