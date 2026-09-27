// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Ships, submarines, hovercraft and a shipyard on a map with deep water, a
// shallow shelf and land, driven through the full match tick.
#include "../src/tick_internal.hpp"
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <memory>
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
// Rows past the bottom band the map load voids around row 24.
constexpr int32_t map_rows = 48;
constexpr uint8_t sea_level = 100;
// Columns 0..17 are 60 deep, 18..20 a shelf 8 deep, 21.. land.
constexpr int32_t shelf_column = 18;
constexpr int32_t land_column = 21;
constexpr uint8_t deep_floor = 40;
constexpr uint8_t shelf_floor = 92;
constexpr uint8_t land_height = 120;

enum Type : uint16_t { ship = 1, submarine, hovercraft, shipyard, picket, tender, type_count };

constexpr int8_t ship_waterline = 3;
constexpr int8_t submarine_waterline = 20;

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

uint8_t floor_at(int32_t column) {
    if (column >= land_column)
        return land_height;
    return column >= shelf_column ? shelf_floor : deep_floor;
}

uint32_t world(int32_t cell) {
    return static_cast<uint32_t>(cell * 16 + 8) << 16;
}

struct Fixture {
    formats::tnt::Map map;
    std::vector<sim::visibility_state::TerrainCell> terrain_values;
    std::array<sim::visibility_state::SightMask, 1> masks{};
    std::shared_ptr<formats::objects3d::Model> model =
        std::make_shared<formats::objects3d::Model>();
    std::shared_ptr<formats::cob::CobProgram> script = std::make_shared<formats::cob::CobProgram>();
    std::shared_ptr<formats::cob::CobProgram> yard_script =
        std::make_shared<formats::cob::CobProgram>();
    std::array<sim::unit_spawn::LoadedType, type_count> loaded;
    std::array<sim::unit_spawn::Type, type_count> types;
    std::array<data::unit_definitions::UnitDefinition, type_count> defs;
    std::array<data::unit_definitions::RuntimeDefinitionMetadata, type_count> metadata;
    std::array<sim::match_runtime::RuntimeTypeFields, type_count> fields{};
    std::vector<uint8_t> yard_cells;
    data::unit_definitions::UnitTargetCategoryMasks target_masks;
    sim::combat_state::WeaponRegistry weapons;
    std::vector<sim::spatial_state::Plot> plots;
    Services services;
    Scenario scenario;
    std::unique_ptr<sim::match_runtime::Match> match;

    Fixture() {
        map.attribute_width = map_cells;
        map.attribute_height = map_rows;
        map.sea_level = sea_level;
        map.attributes.resize(map_cells * map_rows);
        plots.resize(map_cells * map_rows);
        for (int32_t z = 0; z < map_rows; ++z)
            for (int32_t x = 0; x < map_cells; ++x) {
                const auto index = static_cast<size_t>(z * map_cells + x);
                map.attributes[index].height = floor_at(x);
                // A cell spans its own corner and the next column's.
                const auto next = floor_at(std::min(x + 1, map_cells - 1));
                plots[index].low_height = std::min(floor_at(x), next);
                plots[index].high_height = std::max(floor_at(x), next);
            }
        terrain_values.resize(map_cells * map_rows);
        // Sight reaches seven 32-unit cells around a unit.
        masks[0].width = masks[0].height = 15;
        masks[0].offset_x = masks[0].offset_z = 7;
        masks[0].pixels.assign(15 * 15, 1);
        // A 16x16 hull 12 high whose first primitive is the ground quad.
        model->objects.resize(1);
        auto& root = model->objects[0];
        root.name = "root";
        constexpr int32_t half = 8 << 16, top = 12 << 16;
        root.vertices = {
            {-half, 0, -half}, {half, 0, -half}, {half, 0, half}, {-half, 0, half}, {0, top, 0}
        };
        root.primitives.resize(1);
        root.primitives[0].vertex_indices = {0, 1, 2, 3};
        root.selection_primitive = 0;
        script->code = {sim::script_vm::opcode::return_};
        script->scripts = {{"Create", 0}};
        script->entry_points = {0};
        script->piece_names = {"root"};
        // Activate opens the yard, then enters the build stance.
        using namespace sim::script_vm;
        yard_script->code = {
            opcode::return_,
            opcode::push_constant,
            18,
            opcode::push_constant,
            1,
            opcode::set_unit_value,
            opcode::push_constant,
            5,
            opcode::push_constant,
            1,
            opcode::set_unit_value,
            opcode::return_
        };
        yard_script->scripts = {{"Create", 0}, {"Activate", 1}};
        yard_script->entry_points = {0, 1};
        yard_script->piece_names = {"root"};

        for (size_t i = 1; i < type_count; ++i) {
            loaded[i].model = model;
            loaded[i].script = i == shipyard ? yard_script : script;
            types[i].simulation.maximum_health = 100;
            types[i].footprint_x = types[i].footprint_z = 2;
            types[i].bm_code = 1;
            types[i].model = reinterpret_cast<uintptr_t>(model.get());
            types[i].cob = reinterpret_cast<uintptr_t>(loaded[i].script.get());
            defs[i].sight_distance = 200;
            defs[i].acceleration_fixed = 65536 / 4;
            defs[i].brake_rate_fixed = 65536 / 4;
            defs[i].max_velocity_fixed = 2 * 65536;
            defs[i].turn_rate = 800;
            defs[i].build_time = 60;
            defs[i].energy_storage = 1000.0F;
            defs[i].metal_storage = 1000.0F;
            metadata[i].footprint_x = metadata[i].footprint_z = 2;
            fields[i].definition = &defs[i];
            fields[i].runtime_metadata = &metadata[i];
            fields[i].target_masks = &target_masks;
        }
        const auto water_class =
            [&](Type type, int8_t waterline, int16_t depth, uint32_t flags, uint8_t handle) {
                types[type].simulation.flags = OA_UNIT_DEF_FLAG_AVAILABLE | flags;
                types[type].simulation.waterline_offset = std::bit_cast<uint8_t>(waterline);
                defs[type].waterline = waterline;
                metadata[type].min_water_depth = depth;
                metadata[type].movement_class_handle = handle;
                fields[type].movement_class = sim::unit_spawn::AssetHandle{handle};
            };
        water_class(ship, ship_waterline, 15, OA_UNIT_DEF_FLAG_FLOATER, 9);
        water_class(submarine, submarine_waterline, 20, OA_UNIT_DEF_FLAG_UPRIGHT, 6);
        water_class(hovercraft, 0, -10000, OA_UNIT_DEF_FLAG_CAN_HOVER, 14);
        // A sonar picket boat, switched on when built.
        water_class(
            picket,
            ship_waterline,
            15,
            OA_UNIT_DEF_FLAG_FLOATER | OA_UNIT_DEF_FLAG_ACTIVATE_WHEN_BUILT,
            9
        );
        defs[picket].sonar_distance = 200;
        defs[picket].activate_when_built = true;
        // A construction ship.
        water_class(
            tender, ship_waterline, 15, OA_UNIT_DEF_FLAG_FLOATER | OA_UNIT_DEF_FLAG_BUILDER, 9
        );
        defs[tender].builder = true;
        defs[tender].worker_time = 300;
        defs[tender].build_distance = 96;
        metadata[hovercraft].max_slope = metadata[hovercraft].bad_slope = 12;
        metadata[hovercraft].max_water_slope = metadata[hovercraft].bad_water_slope = 255;

        // An ARMSY-shaped 4x4 yard: open-yard 'C' cells ringed by 'w' posts.
        types[shipyard].simulation.flags = OA_UNIT_DEF_FLAG_AVAILABLE | OA_UNIT_DEF_FLAG_BUILDER;
        types[shipyard].simulation.waterline_offset = 1;
        types[shipyard].footprint_x = types[shipyard].footprint_z = 4;
        types[shipyard].bm_code = 0;
        defs[shipyard].waterline = 1;
        defs[shipyard].builder = true;
        defs[shipyard].worker_time = 300;
        metadata[shipyard].footprint_x = metadata[shipyard].footprint_z = 4;
        metadata[shipyard].min_water_depth = 30;
        constexpr uint8_t post = 0x37, open_cell = 0x35;
        yard_cells.assign(16, open_cell);
        yard_cells[0] = yard_cells[3] = yard_cells[12] = yard_cells[15] = post;
        fields[shipyard].yard_mask = yard_cells;
        for (size_t i = 1; i < type_count; ++i)
            loaded[i].type = types[i];

        sim::match_runtime::OfflineInputs input{
            map,  loaded, types, fields, weapons,   terrain_values,       masks, 16, 24, 4,
            2,    0,      30,    1,      &scenario, [] { return 1000u; }, plots, {}, 0,  0,
            0.0F, {}
        };
        match = std::make_unique<sim::match_runtime::Match>(input, services);
        match->configure_strategic_environment({0, 0.5f, 0});
        for (uint8_t p = 0; p < 2; ++p) {
            match->simulation().players[p].present = true;
            match->simulation().players[p].status = p == 0 ? 1 : 2;
            std::array<uint8_t, 10> allies{};
            allies[p] = 1;
            match->configure_player_alliances(p, allies);
            auto& player = match->state().game.players[p];
            player.energy = player.metal = 900.0F;
        }
        std::array<uint8_t, 10> local_allies{};
        local_allies[0] = 1;
        match->configure_outcomes(0, local_allies, false);
        constexpr uint32_t mobile = OA_UNIT_DEF_ABILITY_CAN_MOVE | OA_UNIT_DEF_ABILITY_CAN_STOP;
        for (auto type : {ship, submarine, hovercraft, picket, tender})
            match->state().unit_defs[type].abilities = mobile;
    }

    sim::unit_spawn::Slot& spawn(uint8_t player, Type type, int32_t cell_x, int32_t cell_z) {
        auto* slot = match->create({player, type, {world(cell_x), 0, world(cell_z)}, true, 1, 0});
        CHECK(slot && slot->unit);
        slot->unit->object_present = true;
        return *slot;
    }

    void run(uint32_t ticks, const std::function<void()>& each = {}) {
        for (uint32_t i = 0; i < ticks; ++i) {
            ++match->state().game.tick;
            match->tick();
            if (each)
                each();
        }
    }

    int32_t cell_x(const sim::unit_spawn::Slot& slot) const { return slot.record.cell_x; }

    int32_t height(const sim::unit_spawn::Slot& slot) const {
        return std::bit_cast<int32_t>(slot.unit->position[1]) >> 16;
    }
};

sim::ground_orders::Point point(int32_t cell_x, int32_t cell_z) {
    return {std::bit_cast<int32_t>(world(cell_x)), 0, std::bit_cast<int32_t>(world(cell_z))};
}

bool near(const sim::unit_spawn::Slot& slot, int32_t cell_x, int32_t cell_z, int32_t cells) {
    return std::abs(slot.record.cell_x - cell_x) <= cells &&
           std::abs(slot.record.cell_z - cell_z) <= cells;
}

void ship_crosses_open_water() {
    Fixture f;
    auto& boat = f.spawn(0, ship, 4, 4);
    f.run(2);
    CHECK(f.height(boat) == sea_level - ship_waterline);
    f.match->issue_ground_move(boat.unit_index, point(4, 26), false);
    bool floated = true;
    f.run(400, [&] { floated = floated && f.height(boat) == sea_level - ship_waterline; });
    CHECK(floated);
    CHECK(near(boat, 4, 26, 2));
}

void idle_ship_leaves_its_berth() {
    Fixture f;
    auto& boat = f.spawn(0, ship, 4, 4);
    f.match->issue_ground_move(boat.unit_index, point(4, 12), false);
    // Arrive, then idle long enough that the ship's own footprint is a
    // settled occupant of the map its next search builds.
    f.run(300);
    CHECK(near(boat, 4, 12, 2));
    f.match->issue_ground_move(boat.unit_index, point(4, 26), false);
    f.run(300);
    CHECK(near(boat, 4, 26, 2));
}

void ship_stops_at_the_shore() {
    Fixture f;
    auto& boat = f.spawn(0, ship, 8, 12);
    f.match->issue_ground_move(boat.unit_index, point(26, 12), false);
    int32_t furthest = 0;
    f.run(600, [&] { furthest = std::max(furthest, f.cell_x(boat) + boat.record.footprint_x); });
    // The ship's footprint never reaches the shelf, and it ends by the shore.
    CHECK(furthest <= shelf_column);
    CHECK(f.cell_x(boat) >= shelf_column - 5);
    CHECK(f.height(boat) == sea_level - ship_waterline);
}

void submarine_runs_submerged() {
    Fixture f;
    auto& sub = f.spawn(1, submarine, 4, 4);
    f.match->issue_ground_move(sub.unit_index, point(10, 24), false);
    bool submerged = true;
    f.run(400, [&] {
        const auto top = f.height(sub) + (f.match->state().unit_defs[submarine].model_height >> 16);
        submerged = submerged && f.height(sub) == deep_floor && top < sea_level;
    });
    CHECK(submerged);
    CHECK(near(sub, 10, 24, 2));
    // Under the waterline the enemy sees nothing without a sonar contact.
    CHECK(!f.match->unit_visible(0, sub.unit_index));
}

void sonar_finds_the_submarine() {
    Fixture f;
    auto& sub = f.spawn(1, submarine, 4, 4);
    auto& boat = f.spawn(0, picket, 4, 28);
    f.run(2);
    constexpr uint32_t seen = OA_UNIT_FLAG_RADAR_CONTACT | OA_UNIT_FLAG_VIEWPOINT_OWNED;
    CHECK((boat.record.flags & seen) == seen);
    CHECK((sub.record.flags & OA_UNIT_FLAG_VIEWPOINT_OWNED) == 0);
    CHECK(!f.match->unit_visible(0, sub.unit_index));
    f.match->issue_ground_move(boat.unit_index, point(4, 12), false);
    f.run(300);
    CHECK(near(boat, 4, 12, 2));
    // In sonar reach the submerged hull is a contact the viewer can see.
    CHECK((sub.record.flags & OA_UNIT_FLAG_VIEWPOINT_OWNED) != 0);
    CHECK(f.match->unit_visible(0, sub.unit_index));
    // Out of reach again, the contact drops.
    f.match->issue_ground_move(boat.unit_index, point(4, 28), false);
    f.run(300);
    CHECK((sub.record.flags & OA_UNIT_FLAG_VIEWPOINT_OWNED) == 0);
    CHECK(!f.match->unit_visible(0, sub.unit_index));
}

void hovercraft_crosses_the_shore() {
    Fixture f;
    auto& hover = f.spawn(0, hovercraft, 8, 12);
    f.match->issue_ground_move(hover.unit_index, point(26, 12), false);
    // Over water the hull rides the surface, bobbing at most two units under it.
    constexpr int32_t bob = 2;
    bool above_water = true;
    f.run(600, [&] { above_water = above_water && f.height(hover) >= sea_level - bob; });
    CHECK(above_water);
    CHECK(f.height(hover) == land_height);
    CHECK(near(hover, 26, 12, 2));
}

// setSFXoccupy sees the height the previous tick's ground fit
// left, so a hovercraft at the bottom of its bob, just under the surface,
// reports occupancy 1; nothing lifts it back to the surface first.
void hovercraft_dips_under_the_surface() {
    Fixture f;
    auto& hover = f.spawn(0, hovercraft, 8, 12);
    f.match->issue_ground_move(hover.unit_index, point(12, 12), false);
    bool dipped = false;
    bool reported = false;
    f.run(300, [&] {
        dipped = dipped || f.height(hover) < sea_level;
        reported = reported || f.match->runtime_state(hover.unit_index).sfx_occupancy == 1;
    });
    CHECK(dipped && reported);
}

void shipyard_builds_a_ship_on_water() {
    Fixture f;
    CHECK(f.match->building_site_clear(shipyard, 6, 6, 0));
    CHECK(!f.match->building_site_clear(shipyard, land_column + 1, 6, 0));
    CHECK(!f.match->building_site_clear(shipyard, shelf_column - 1, 6, 0));
    auto* yard = f.match->create({0, shipyard, {128u << 16, 0, 128u << 16}, true, 1, 0});
    CHECK(yard && yard->unit);
    yard->unit->object_present = true;
    f.match->issue_building_build(yard->unit_index, ship, 1, false);
    sim::unit_spawn::Slot* built = nullptr;
    f.run(300, [&] {
        if (built)
            return;
        for (auto& slot : f.match->world().slots)
            if (slot.unit && slot.record.type_index == ship)
                built = &slot;
    });
    CHECK(built);
    CHECK(built->record.build_remaining == 0.0F && built->record.attach_parent == 0);
    CHECK(f.height(*built) == sea_level - ship_waterline);
    CHECK(f.cell_x(*built) + built->record.footprint_x <= shelf_column);
}

// A shipyard stopped while a ship stands unfinished on its pad cancels the
// nanoframe (BuildingBuild): the damage path deals it 30000 of kind 9 from the yard,
// scaled by the frame's veteran level, (25 - 12 / 5) * 30000 * 4 / 100.
void stopped_shipyard_cancels_the_nanoframe() {
    constexpr uint8_t cancelled = static_cast<uint8_t>(sim::match_runtime::DeathKind::cancelled);
    Fixture f;
    auto* yard = f.match->create({0, shipyard, {128u << 16, 0, 128u << 16}, true, 1, 0});
    CHECK(yard && yard->unit);
    yard->unit->object_present = true;
    f.match->issue_building_build(yard->unit_index, ship, 1, false);
    bool stopped = false;
    f.run(300, [&] {
        if (stopped)
            return;
        for (auto& slot : f.match->world().slots) {
            if (!slot.unit || slot.record.type_index != ship ||
                slot.record.build_remaining == 0.0F || slot.unit->health <= 0)
                continue;
            slot.record.veteran_level = 12;
            const auto before = slot.unit->health;
            f.match->stop_orders(yard->unit_index);
            stopped = true;
            CHECK((slot.unit->flags & OA_UNIT_FLAG_DEATH_PENDING) != 0);
            CHECK(slot.unit->health == static_cast<int16_t>(before - 27600));
            CHECK(slot.record.damage_kind == cancelled);
            CHECK(
                slot.record.last_attacker_id == yard->unit_index &&
                slot.record.last_attacker_owner == 0
            );
            return;
        }
    });
    CHECK(stopped);
}

void tender_floats_a_shipyard() {
    Fixture f;
    auto& builder = f.spawn(0, tender, 4, 16);
    // The yard's 4x4 footprint on cells 8..11 by 14..17, all deep water.
    const sim::ground_orders::Point site{160 << 16, 0, 256 << 16};
    f.match->issue_mobile_build(builder.unit_index, shipyard, site, false);
    sim::unit_spawn::Slot* yard = nullptr;
    f.run(400, [&] {
        for (auto& slot : f.match->world().slots)
            if (slot.unit && slot.record.type_index == shipyard)
                yard = &slot;
    });
    CHECK(yard);
    CHECK(yard->record.build_remaining == 0.0F);
    CHECK(yard->record.cell_x == 8 && yard->record.cell_z == 14);
    // No yard cell levels the site, so the yard floats at its waterline.
    CHECK(f.height(*yard) == sea_level - 1);
    CHECK(f.height(builder) == sea_level - ship_waterline);
}

constexpr uint32_t radar_seen = OA_UNIT_FLAG_RADAR_CONTACT;
constexpr uint32_t sonar_seen = OA_UNIT_FLAG_VIEWPOINT_OWNED;
constexpr uint32_t contact_bits = radar_seen | sonar_seen | OA_UNIT_FLAG_JAMMED;

uint32_t contacts(const sim::unit_spawn::Slot& slot) {
    return slot.record.flags & contact_bits;
}

// Places a unit at a height without moving it out of its spatial bucket.
void set_height(sim::unit_spawn::Slot& slot, int32_t y) {
    slot.record.position.y = static_cast<int32_t>(static_cast<uint32_t>(y) << 16);
}

void switch_on(sim::unit_spawn::Slot& slot) {
    slot.record.state_flags |= OA_UNIT_STATE_ACTIVE;
}

// A cloaked unit is skipped by the scan's line-of-sight pass, which keeps
// these checks on radar, sonar and jammers.
void out_of_sight(std::initializer_list<sim::unit_spawn::Slot*> slots) {
    for (auto* slot : slots)
        slot->record.state_flags |= OA_UNIT_STATE_CLOAKED;
}

void contact_scan_waits_for_the_deadline() {
    Fixture f;
    auto& sub = f.spawn(1, submarine, 4, 4);
    f.spawn(0, picket, 4, 8);
    f.match->state().unit_defs[picket].energy_make = 6.0F;
    auto& viewer = f.match->state().game.players[0];
    // The first scan and settlement run on tick 1 against a deadline of 0.
    f.run(1);
    CHECK(viewer.next_economy_tick == 30);
    // Ticks 2..29 neither scan nor settle.
    sub.record.flags &= ~contact_bits;
    const auto energy = viewer.energy;
    f.run(28);
    CHECK(f.match->state().game.tick == 29);
    CHECK(contacts(sub) == 0);
    CHECK(viewer.energy == energy);
    f.run(1);
    CHECK(viewer.next_economy_tick == 60);
    // Sonar finds the hull, and the picket's line of sight adds radar.
    CHECK(contacts(sub) == (radar_seen | sonar_seen));
    CHECK(viewer.energy > energy);
}

void scan_needs_two_players() {
    Fixture f;
    auto& sub = f.spawn(1, submarine, 4, 4);
    auto& boat = f.spawn(0, picket, 4, 8);
    switch_on(boat);
    set_height(sub, deep_floor);
    out_of_sight({&sub});
    sub.record.flags &= ~contact_bits;
    boat.record.flags &= ~contact_bits;
    f.match->state().game.player_count = 1;
    f.match->scan_contacts();
    CHECK(contacts(sub) == 0 && contacts(boat) == 0);
    f.match->state().game.player_count = 2;
    f.match->scan_contacts();
    CHECK(contacts(sub) == sonar_seen);
    CHECK(contacts(boat) == (radar_seen | sonar_seen));
}

void radar_reach_grows_with_altitude() {
    Fixture f;
    auto& defs = f.match->state().unit_defs[picket];
    defs.radar_distance = 100;
    defs.sonar_distance = 0;
    auto& tower = f.spawn(0, picket, 2, 4);
    switch_on(tower);
    set_height(tower, 50);
    // 144, 192 and 208 units away; radar 100 at altitude 50 reaches 200.
    auto& beyond_walk = f.spawn(1, ship, 11, 4);
    auto& inside = f.spawn(1, ship, 14, 4);
    auto& outside = f.spawn(1, ship, 15, 4);
    // Above the surface, so the wider sonar walk adds no sonar contact.
    for (auto* slot : {&beyond_walk, &inside, &outside})
        set_height(*slot, sea_level + 1);
    out_of_sight({&beyond_walk, &inside, &outside});
    // The walk covers only the plain range, the larger of radar and sonar.
    f.match->scan_contacts();
    CHECK(contacts(beyond_walk) == 0 && contacts(inside) == 0);
    defs.sonar_distance = 250;
    f.match->scan_contacts();
    CHECK(contacts(beyond_walk) == radar_seen);
    CHECK(contacts(inside) == radar_seen);
    CHECK(contacts(outside) == 0);
}

void switched_off_sonar_sees_nothing() {
    Fixture f;
    auto& sub = f.spawn(1, submarine, 4, 4);
    auto& boat = f.spawn(0, picket, 4, 8);
    set_height(sub, deep_floor);
    out_of_sight({&sub});
    boat.record.state_flags &= ~OA_UNIT_STATE_ACTIVE;
    f.match->scan_contacts();
    CHECK(contacts(sub) == 0);
    CHECK(contacts(boat) == (radar_seen | sonar_seen));
}

void jammers_clear_contacts() {
    Fixture f;
    f.match->state().unit_defs[picket].radar_distance = 200;
    f.match->state().unit_defs[tender].radar_distance_jam = 40;
    f.match->state().unit_defs[tender].sonar_distance_jam = 40;
    auto& boat = f.spawn(0, picket, 4, 4);
    auto& escort = f.spawn(0, ship, 8, 4);
    auto& jammer = f.spawn(1, tender, 8, 6);
    auto& shielded = f.spawn(1, ship, 7, 4);
    auto& exposed = f.spawn(1, ship, 4, 8);
    switch_on(boat);
    switch_on(jammer);
    for (auto* slot : {&boat, &escort, &jammer, &shielded, &exposed})
        set_height(*slot, sea_level - ship_waterline);
    out_of_sight({&escort, &shielded, &exposed});
    f.match->scan_contacts();
    // The jammer hides its own side and blinds the viewer's units near it.
    CHECK(contacts(shielded) == OA_UNIT_FLAG_JAMMED);
    CHECK(contacts(escort) == OA_UNIT_FLAG_JAMMED);
    CHECK(contacts(exposed) == (radar_seen | sonar_seen));
}

void line_of_sight_is_a_radar_contact() {
    Fixture f;
    f.spawn(0, ship, 4, 4);
    auto& seen = f.spawn(1, ship, 6, 4);
    auto& cloaked = f.spawn(1, ship, 4, 6);
    auto& diver = f.spawn(1, submarine, 6, 6);
    auto& distant = f.spawn(1, ship, 4, 28);
    cloaked.record.state_flags |= OA_UNIT_STATE_CLOAKED;
    f.run(1);
    CHECK(contacts(seen) == radar_seen);
    CHECK(contacts(cloaked) == 0);
    CHECK(contacts(distant) == 0);
    // A hull in sight but under the surface is a radar contact the viewer
    // still cannot see without sonar.
    CHECK(contacts(diver) == radar_seen);
    CHECK(!f.match->unit_visible(0, diver.unit_index));
}

void shared_radar_and_watchers() {
    Fixture f;
    auto& sub = f.spawn(1, submarine, 20, 20);
    set_height(sub, deep_floor);
    auto& game = f.match->state().game;
    for (uint8_t p = 0; p < 2; ++p)
        game.players[p].info = oa_ref_from_index(p);
    f.match->scan_contacts();
    CHECK(contacts(sub) == 0);
    // An ally sharing its radar shows all its units.
    game.players[1].alliance[0] = 1;
    f.match->state().player_info[1].role = 0x40;
    f.match->scan_contacts();
    CHECK(contacts(sub) == (radar_seen | sonar_seen));
    // An alliance without the shared-radar role hides them again.
    f.match->state().player_info[1].role = 0;
    f.match->scan_contacts();
    CHECK(contacts(sub) == 0);
    // A watching viewer sees everything.
    f.match->state().player_info[0].options = OA_SETUP_OPTION_WATCHER;
    f.match->scan_contacts();
    CHECK(contacts(sub) == (radar_seen | sonar_seen));
}
} // namespace

int main() {
    try {
        ship_crosses_open_water();
        idle_ship_leaves_its_berth();
        ship_stops_at_the_shore();
        submarine_runs_submerged();
        sonar_finds_the_submarine();
        hovercraft_crosses_the_shore();
        hovercraft_dips_under_the_surface();
        shipyard_builds_a_ship_on_water();
        stopped_shipyard_cancels_the_nanoframe();
        tender_floats_a_shipyard();
        contact_scan_waits_for_the_deadline();
        scan_needs_two_players();
        radar_reach_grows_with_altitude();
        switched_off_sonar_sees_nothing();
        jammers_clear_contacts();
        line_of_sight_is_a_radar_contact();
        shared_radar_and_watchers();
    } catch (const std::exception& error) {
        std::cerr << "naval: " << error.what() << '\n';
        return 1;
    }
    std::cout << "naval passed\n";
    return 0;
}
