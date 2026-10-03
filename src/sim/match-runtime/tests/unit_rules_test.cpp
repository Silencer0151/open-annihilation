// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The unit rules a mod sets, played through a match: the reuse delay of unit
// slots (units.id-reuse-delay), the sea occupy codes (units.water-state-rules),
// building facings (units.build-rotation), the build-site options other rules
// use, the kickout clearing a turned building's site
// (orders.build-site-kickout), and a mobile builder placing a mobile unit as a
// frame on the map (what units.placement-by-builder hands the simulation).
// Each also runs without the rule, where the match plays as 3.1c.
#include "oa/sim/match_runtime.hpp"
#include "oa/sim/world_environment/wind.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "oa/test/match_services.hpp"

using namespace oa;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #x);   \
    } while (false)

namespace {
namespace match_rules = data::match_rules;
namespace runtime = sim::match_runtime;

constexpr int32_t map_cells = 32;
// Rows past the bottom band the map load voids.
constexpr int32_t map_rows = 48;
constexpr uint8_t sea_level = 50;
// Columns 0..9 are sea 40 deep, 10.. land above the sea.
constexpr int32_t land_column = 10;
constexpr uint8_t sea_floor = 10;
constexpr uint8_t land_height = 80;
constexpr uint8_t per_player = 4;

enum Type : uint16_t {
    walker = 1, // a 2x2 mobile unit 12 high
    wader,      // a 2x2 mobile unit 60 high
    builder,    // a mobile constructor
    hall,       // a building 2 wide and 3 deep
    numbered,   // a building 2 wide and 3 deep whose yard cells count 1 to 6
    type_count
};

// The hall's yard, 2 cells a row: [o .] [o o] [. .].
constexpr uint8_t open_cell = 0x2f;
const std::vector<uint8_t> hall_yard{open_cell, 0, open_cell, open_cell, 0, 0};

using Services = oa::test::QuietServices;
using Scenario = oa::test::EmptyScenario;

uint8_t floor_at(int32_t column) {
    return column >= land_column ? land_height : sea_floor;
}

uint32_t world_at(int32_t cell) {
    return static_cast<uint32_t>(cell * 16 + 8) << 16;
}

// The rules, per-type rules and inputs a fixture's match plays by.
struct Setup {
    match_rules::MatchRules rules{};
    std::vector<match_rules::UnitTypeRules> unit_type_rules{};
};

struct Fixture {
    formats::tnt::Map map;
    std::vector<sim::visibility_state::TerrainCell> terrain_values;
    std::array<sim::visibility_state::SightMask, 1> masks{};
    std::shared_ptr<formats::objects3d::Model> short_model =
        std::make_shared<formats::objects3d::Model>();
    std::shared_ptr<formats::objects3d::Model> tall_model =
        std::make_shared<formats::objects3d::Model>();
    std::shared_ptr<formats::cob::CobProgram> script = std::make_shared<formats::cob::CobProgram>();
    std::array<sim::unit_spawn::LoadedType, type_count> loaded;
    std::array<sim::unit_spawn::Type, type_count> types;
    std::array<data::unit_definitions::UnitDefinition, type_count> defs;
    std::array<data::unit_definitions::RuntimeDefinitionMetadata, type_count> metadata;
    std::array<runtime::RuntimeTypeFields, type_count> fields{};
    std::vector<uint8_t> numbered_yard{1, 2, 3, 4, 5, 6};
    data::unit_definitions::UnitTargetCategoryMasks target_masks;
    sim::combat_state::WeaponRegistry weapons;
    std::vector<sim::spatial_state::Plot> plots;
    Services services;
    Scenario scenario;
    std::unique_ptr<runtime::Match> match;

    /// Builds the match.
    ///
    /// @param setup the rules it plays by
    explicit Fixture(const Setup& setup = {}) {
        map.attribute_width = map_cells;
        map.attribute_height = map_rows;
        map.sea_level = sea_level;
        map.attributes.resize(map_cells * map_rows);
        plots.resize(map_cells * map_rows);
        for (int32_t z = 0; z < map_rows; ++z)
            for (int32_t x = 0; x < map_cells; ++x) {
                const auto index = static_cast<size_t>(z * map_cells + x);
                map.attributes[index].height = floor_at(x);
                const auto next = floor_at(std::min(x + 1, map_cells - 1));
                plots[index].low_height = std::min(floor_at(x), next);
                plots[index].high_height = std::max(floor_at(x), next);
            }
        terrain_values.resize(map_cells * map_rows);
        masks[0].width = masks[0].height = 15;
        masks[0].offset_x = masks[0].offset_z = 7;
        masks[0].pixels.assign(15 * 15, 1);
        const auto hull = [](formats::objects3d::Model& model, int32_t height) {
            model.objects.resize(1);
            auto& root = model.objects[0];
            root.name = "root";
            constexpr int32_t half = 8 << 16;
            root.vertices = {
                {-half, 0, -half},
                {half, 0, -half},
                {half, 0, half},
                {-half, 0, half},
                {0, height << 16, 0}
            };
            root.primitives.resize(1);
            root.primitives[0].vertex_indices = {0, 1, 2, 3};
            root.selection_primitive = 0;
        };
        hull(*short_model, 12);
        hull(*tall_model, 60);
        script->code = {sim::script_vm::opcode::return_};
        script->scripts = {{"Create", 0}};
        script->entry_points = {0};
        script->piece_names = {"root"};
        for (size_t i = 1; i < type_count; ++i) {
            const auto& model = i == wader ? tall_model : short_model;
            loaded[i].model = model;
            loaded[i].script = script;
            types[i].simulation.flags = OA_UNIT_DEF_FLAG_AVAILABLE;
            types[i].simulation.maximum_health = 100;
            types[i].footprint_x = types[i].footprint_z = 2;
            types[i].bm_code = 1;
            types[i].model = reinterpret_cast<uintptr_t>(model.get());
            types[i].cob = reinterpret_cast<uintptr_t>(script.get());
            defs[i].sight_distance = 200;
            defs[i].acceleration_fixed = 65536 / 4;
            defs[i].brake_rate_fixed = 65536 / 4;
            defs[i].max_velocity_fixed = 2 * 65536;
            defs[i].turn_rate = 800;
            defs[i].build_time = 60;
            defs[i].energy_storage = 1000.0F;
            defs[i].metal_storage = 1000.0F;
            metadata[i].footprint_x = metadata[i].footprint_z = 2;
            metadata[i].movement_class_handle = uint8_t{1};
            fields[i].definition = &defs[i];
            fields[i].runtime_metadata = &metadata[i];
            fields[i].target_masks = &target_masks;
            fields[i].movement_class = sim::unit_spawn::AssetHandle{1};
        }
        types[builder].simulation.flags |= OA_UNIT_DEF_FLAG_BUILDER;
        defs[builder].builder = true;
        defs[builder].worker_time = 300;
        defs[builder].build_distance = 96;
        for (const auto building : {hall, numbered}) {
            types[building].footprint_x = 2;
            types[building].footprint_z = 3;
            types[building].bm_code = 0;
            metadata[building].footprint_x = 2;
            metadata[building].footprint_z = 3;
            metadata[building].movement_class_handle.reset();
            fields[building].movement_class = sim::unit_spawn::AssetHandle{0};
        }
        fields[hall].yard_mask = hall_yard;
        fields[numbered].yard_mask = numbered_yard;
        for (size_t i = 1; i < type_count; ++i)
            loaded[i].type = types[i];

        runtime::OfflineInputs input{
            map,  loaded, types, fields, weapons,   terrain_values,       masks, 16, 24, per_player,
            2,    0,      30,    1,      &scenario, [] { return 1000U; }, plots, {}, 0,  0,
            0.0F, {}
        };
        input.rules = setup.rules;
        input.unit_type_rules = setup.unit_type_rules;
        match = std::make_unique<runtime::Match>(input, services);
        CHECK(match->fault() == nullptr);
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
        for (auto type : {walker, wader, builder})
            match->state().unit_defs[type].abilities = mobile;
    }

    /// Creates a finished unit.
    ///
    /// @param player owning player
    /// @param type unit type
    /// @param cell_x cell column
    /// @param cell_z cell row
    /// @param facing quarter turns from south
    /// @return the unit's slot, or null when no slot is free
    sim::unit_spawn::Slot*
    create(uint8_t player, Type type, int32_t cell_x, int32_t cell_z, uint8_t facing = 0) {
        sim::unit_spawn::Request request;
        request.player = player;
        request.type = type;
        request.position = {
            world_at(cell_x), static_cast<uint32_t>(floor_at(cell_x)) << 16, world_at(cell_z)
        };
        request.finished = true;
        request.state = sim::unit_spawn::ground_occupancy_state;
        request.facing = facing;
        auto* slot = match->create(request);
        if (slot != nullptr && slot->unit != nullptr && types[type].bm_code != 0)
            slot->unit->object_present = true;
        return slot;
    }

    /// Creates a finished unit that must find a slot.
    sim::unit_spawn::Slot& spawn(uint8_t player, Type type, int32_t cell_x, int32_t cell_z) {
        auto* slot = create(player, type, cell_x, cell_z);
        CHECK(slot && slot->unit);
        return *slot;
    }

    void run(uint32_t ticks, const std::function<void()>& each = {}) {
        for (uint32_t i = 0; i < ticks; ++i) {
            ++match->state().game.tick;
            match->tick();
            CHECK(match->fault() == nullptr);
            if (each)
                each();
        }
    }

    /// Returns the ground occupant of a plot.
    ///
    /// @param cell_x cell column
    /// @param cell_z cell row
    /// @return the unit slot, or no_unit
    uint16_t occupant(int32_t cell_x, int32_t cell_z) {
        return match->spatial().plots[static_cast<size_t>(cell_z * map_cells + cell_x)].ground;
    }

    /// Checks that a unit holds exactly the plots a yard's open cells cover.
    ///
    /// @param slot the unit
    /// @param width the yard's width in cells
    /// @param yard the yard, row by row
    /// @return true when it does
    bool
    occupies(const sim::unit_spawn::Slot& slot, int32_t width, const std::vector<uint8_t>& yard) {
        const auto depth = static_cast<int32_t>(yard.size()) / width;
        for (int32_t row = -1; row <= depth; ++row)
            for (int32_t column = -1; column <= width; ++column) {
                const bool inside = row >= 0 && row < depth && column >= 0 && column < width;
                const bool held = occupant(slot.record.cell_x + column, slot.record.cell_z + row) ==
                                  slot.unit_index;
                const bool expected =
                    inside && yard[static_cast<size_t>(row * width + column)] != 0;
                if (held != expected)
                    return false;
            }
        return true;
    }
};

/// Returns a match's rules with the slot reuse delay on.
///
/// @param ticks its delay
/// @return the setup
Setup reuse_delay(int32_t ticks) {
    Setup setup;
    setup.rules.units.id_reuse_delay.enabled = true;
    setup.rules.units.id_reuse_delay.ticks = ticks;
    return setup;
}

/// Returns a match's rules with building rotation on and the hall, the
/// numbered building and the walker listing south, east and west.
///
/// @return the setup
Setup rotation() {
    Setup setup;
    setup.rules.units.build_rotation.enabled = true;
    setup.unit_type_rules.resize(type_count);
    constexpr uint8_t south_east_west = match_rules::build_facing::south |
                                        match_rules::build_facing::east |
                                        match_rules::build_facing::west;
    setup.unit_type_rules[hall].build_facings = south_east_west;
    setup.unit_type_rules[numbered].build_facings = south_east_west;
    setup.unit_type_rules[walker].build_facings = south_east_west;
    return setup;
}

// units.id-reuse-delay through the match: a death leaves its place waiting,
// and the waiting ticks are rule state the digest and saves carry.
void slot_reuse_waits_in_the_match() {
    {
        Fixture f;
        f.match->state().game.tick = 5;
        auto& first = f.spawn(0, walker, 14, 4);
        CHECK(first.unit_index == 1);
        f.match->teardown_dead_unit(first);
        CHECK(f.spawn(0, walker, 14, 4).unit_index == 1);
        CHECK(f.match->rule_state().count == 0);
    }
    {
        // A delay of 0 keeps no state.
        Fixture f(reuse_delay(0));
        CHECK(f.match->rule_state().count == 0);
    }
    for (const int32_t delay : {150, 30}) {
        Fixture f(reuse_delay(delay));
        CHECK(f.match->rule_state().count == 1);
        const auto* table = runtime::find_rule_state(f.match->rule_state(), "unit-slot-reuse");
        CHECK(table != nullptr);
        const auto empty_digest = f.match->fold_rule_state(0);
        f.match->state().game.tick = 5;
        auto& first = f.spawn(0, walker, 14, 4);
        f.match->teardown_dead_unit(first);
        CHECK(f.spawn(0, walker, 14, 4).unit_index == 2);
        CHECK(f.match->fold_rule_state(0) != empty_digest);
        // The ticks are saved as they are and restored whole.
        const auto saved = table->bytes(table->context);
        CHECK(saved.size() == size_t{OA_PLAYER_COUNT} * per_player * sizeof(int32_t));
        int32_t first_tick = 0;
        std::memcpy(&first_tick, saved.data(), sizeof first_tick);
        CHECK(first_tick == 5 + delay);
        const std::vector<uint8_t> copy(saved.begin(), saved.end());
        const auto digest = f.match->fold_rule_state(0);
        std::vector<uint8_t> cleared(copy.size(), 0);
        CHECK(table->restore(table->context, cleared));
        CHECK(f.match->fold_rule_state(0) == empty_digest);
        CHECK(table->restore(table->context, copy));
        CHECK(f.match->fold_rule_state(0) == digest);
        CHECK(!table->restore(table->context, std::span(copy).first(4)));
        f.match->state().game.tick = static_cast<uint32_t>(5 + delay);
        CHECK(f.spawn(0, walker, 14, 4).unit_index == 1);
    }
}

// units.water-state-rules: a unit created under the sea starts submerged,
// and the reordered checks give a deep unit with a tall model its waterline
// code.
void water_state_rules_in_the_match() {
    for (const bool rule : {false, true}) {
        Setup setup;
        setup.rules.units.water_state_rules.enabled = rule;
        setup.rules.units.water_state_rules.start_submerged = rule;
        setup.rules.units.water_state_rules.rules =
            rule ? match_rules::UnitsWaterStateRulesRules::reordered
                 : match_rules::UnitsWaterStateRulesRules::base;
        Fixture f(setup);
        CHECK(f.match->rule_state().count == 0);
        // Model top 22 under the sea at 50; on land, above it.
        const auto& deep = f.spawn(0, walker, 4, 4);
        const auto& dry = f.spawn(0, walker, 20, 4);
        CHECK(deep.record.last_occupy_code[0] == (rule ? 3 : 0));
        CHECK(dry.record.last_occupy_code[0] == 0);
        // A 60-high model 40 under the sea: its top is above the sea, its
        // waterline below.
        const auto& wading = f.spawn(0, wader, 6, 8);
        CHECK(wading.record.last_occupy_code[0] == 0);
        f.run(1);
        const auto code = sim::world_environment::sea_occupy(wading.record);
        CHECK(code == (rule ? sim::world_environment::sea_occupy_waterline : 0));
    }
}

// units.build-rotation: which facings a type takes, its turned yards and
// footprints, and the facing a building keeps from its heading.
void buildings_turn_with_the_rule() {
    {
        // Without the rule every building faces south.
        Setup setup;
        setup.unit_type_rules.resize(type_count);
        setup.unit_type_rules[hall].build_facings = 0xf;
        Fixture f(setup);
        for (uint8_t facing = 0; facing < 4; ++facing)
            CHECK(f.match->build_facing(hall, facing) == 0);
        CHECK(f.match->build_facings(hall) == match_rules::build_facing::south);
        CHECK(
            (std::vector<uint8_t>(
                 f.match->build_yard(hall, 1).begin(), f.match->build_yard(hall, 1).end()
             ) == hall_yard)
        );
    }
    Fixture f(rotation());
    // The facings the type lists, never north here, and only for buildings.
    CHECK(f.match->build_facing(hall, 1) == 1 && f.match->build_facing(hall, 3) == 3);
    CHECK(f.match->build_facing(hall, 2) == 0 && f.match->build_facing(hall, 5) == 1);
    CHECK(f.match->build_facing(walker, 1) == 0);
    // Every facing the cursor offers: the listed ones the type may take, and
    // south alone for a unit that moves, whatever it lists.
    CHECK(
        f.match->build_facings(hall) ==
        (match_rules::build_facing::south | match_rules::build_facing::east |
         match_rules::build_facing::west)
    );
    CHECK(f.match->build_facings(walker) == match_rules::build_facing::south);
    // The yard turned: east [2 4 6][1 3 5], west [5 3 1][6 4 2], north
    // (not allowed) as south.
    const auto yard = [&](uint8_t facing) {
        const auto cells = f.match->build_yard(numbered, facing);
        return std::vector<uint8_t>(cells.begin(), cells.end());
    };
    CHECK((yard(1) == std::vector<uint8_t>{2, 4, 6, 1, 3, 5}));
    CHECK((yard(3) == std::vector<uint8_t>{5, 3, 1, 6, 4, 2}));
    CHECK((yard(2) == std::vector<uint8_t>{1, 2, 3, 4, 5, 6}));
    // A heading's facing: south spans 0x6000..0x9fff, east 0xa000..0xdfff.
    CHECK(f.match->build_facing_of_heading(hall, 0x8000) == 0);
    CHECK(f.match->build_facing_of_heading(hall, 0x9fff) == 0);
    CHECK(f.match->build_facing_of_heading(hall, 0xa000) == 1);
    CHECK(f.match->build_facing_of_heading(hall, 0x2000) == 3);
    CHECK(f.match->build_facing_of_heading(hall, 0x0000) == 0); // north is not allowed

    // Turned east the hall's yard holds cell (21,20) at site (20,20), which
    // facing south it leaves empty: a unit standing there refuses the site
    // facing east only.
    {
        auto& blocker = f.spawn(1, walker, 21, 19);
        CHECK(f.occupant(21, 20) == blocker.unit_index);
        CHECK(f.match->building_site_clear(hall, 20, 20, 0));
        CHECK(!f.match->building_site_clear(hall, 20, 20, 0, {1}));
        CHECK(f.match->site_clear_for(hall, 20, 20, 0, 1, 0));
        CHECK(!f.match->site_clear_for(hall, 20, 20, 0, 1, 1));
        CHECK(f.match->footprint_height(hall, 20, 20, 1) == land_height);
        f.match->teardown_dead_unit(blocker);
    }

    // A hall placed facing east holds the turned yard's plots, [. o .][o o .],
    // keeps its facing in its heading, and frees them when it dies.
    auto* east = f.create(0, hall, 20, 20, 1);
    CHECK(east && east->unit);
    CHECK(east->record.footprint_x == 3 && east->record.footprint_z == 2);
    CHECK(f.match->unit_build_facing(east->record) == 1);
    CHECK(f.occupies(*east, 3, {0, open_cell, 0, open_cell, open_cell, 0}));
    // Facing west, [. o o][. o .]; facing south, the yard as it is.
    auto* west = f.create(0, hall, 20, 26, 3);
    CHECK(west && f.occupies(*west, 3, {0, open_cell, open_cell, 0, open_cell, 0}));
    auto* south = f.create(0, hall, 26, 20, 0);
    CHECK(south && f.match->unit_build_facing(south->record) == 0);
    CHECK(f.occupies(*south, 2, hall_yard));
    // A building handed to another player keeps its facing.
    f.match->transfer_unit(east->unit_index, 1);
    sim::unit_spawn::Slot* handed = nullptr;
    for (auto& slot : f.match->world().slots)
        if (slot.unit && slot.record.type_index == hall && slot.record.owner_index == 1)
            handed = &slot;
    CHECK(handed != nullptr);
    CHECK(handed->record.footprint_x == 3 && f.match->unit_build_facing(handed->record) == 1);
    // Once the old unit has gone, the copy holds the turned yard's plots.
    f.run(2);
    CHECK(f.occupies(*handed, 3, {0, open_cell, 0, open_cell, open_cell, 0}));
    f.match->teardown_dead_unit(*handed);
    CHECK(f.occupies(*handed, 3, {0, 0, 0, 0, 0, 0}));
}

// A builder builds a building in the facing its order is given, whether
// the order is given with it or set after.
void builder_builds_in_the_order_facing() {
    for (const bool rule : {false, true})
        for (const bool given_with_order : {false, true}) {
            Fixture f(rule ? rotation() : Setup{});
            auto& constructor = f.spawn(0, builder, 16, 20);
            const sim::ground_orders::Point site{
                std::bit_cast<int32_t>(world_at(20)), 0, std::bit_cast<int32_t>(world_at(20))
            };
            if (given_with_order) {
                (void)f.match->issue_mobile_build(constructor.unit_index, hall, site, false, 1);
            } else {
                auto& order =
                    f.match->issue_mobile_build(constructor.unit_index, hall, site, false);
                f.match->set_build_facing(order, 1);
            }
            sim::unit_spawn::Slot* built = nullptr;
            f.run(300, [&] {
                for (auto& slot : f.match->world().slots)
                    if (slot.unit && slot.record.type_index == hall)
                        built = &slot;
            });
            CHECK(built != nullptr && built->record.build_remaining == 0.0F);
            CHECK(f.match->unit_build_facing(built->record) == (rule ? 1 : 0));
            CHECK(built->record.footprint_x == (rule ? 3 : 2));
            if (rule)
                CHECK(f.occupies(*built, 3, {0, open_cell, 0, open_cell, open_cell, 0}));
            else
                CHECK(f.occupies(*built, 2, hall_yard));
        }
}

// The build-site options: a player's own units with a movement object do not
// refuse a site when the test lets them, while buildings, other players'
// units and the default test still do.
void site_lets_own_mobile_units_through() {
    Fixture f;
    // The hall's yard covers cells (20,20), (20,21) and (21,21) at site (20,20).
    auto& own = f.spawn(0, walker, 20, 21);
    CHECK(f.occupant(20, 21) == own.unit_index || f.occupant(21, 21) == own.unit_index);
    CHECK(!f.match->building_site_clear(hall, 20, 20, 0));
    runtime::BuildSiteOptions own_units{};
    own_units.own_units_player = 0;
    CHECK(f.match->building_site_clear(hall, 20, 20, 0, own_units));
    runtime::BuildSiteOptions other_units{};
    other_units.own_units_player = 1;
    CHECK(!f.match->building_site_clear(hall, 20, 20, 0, other_units));
    // An own building still refuses the site.
    auto* standing = f.create(0, hall, 26, 26);
    CHECK(standing != nullptr);
    CHECK(!f.match->building_site_clear(hall, 26, 26, 0, own_units));
}

// A mobile builder ordered to build a mobile unit at a site places it there
// as a frame and builds it into a unit with a movement object, as
// units.placement-by-builder's build-menu clicks have it do.
void mobile_unit_built_at_a_site() {
    Fixture f;
    auto& constructor = f.spawn(0, builder, 16, 20);
    const sim::ground_orders::Point site{
        std::bit_cast<int32_t>(world_at(20)), 0, std::bit_cast<int32_t>(world_at(22))
    };
    f.match->issue_mobile_build(constructor.unit_index, walker, site, false);
    sim::unit_spawn::Slot* built = nullptr;
    std::array<int16_t, 2> placed{-1, -1};
    f.run(300, [&] {
        for (auto& slot : f.match->world().slots)
            if (slot.unit && slot.record.type_index == walker) {
                if (built == nullptr)
                    placed = {slot.record.cell_x, slot.record.cell_z};
                built = &slot;
            }
    });
    CHECK(built != nullptr && built->record.build_remaining == 0.0F);
    CHECK(built->record.owner_index == 0 && built->record.movement != 0);
    // The frame went where the order put it.
    CHECK(placed[0] == 20 && placed[1] == 22);
}

// orders.build-site-kickout kickout under units.build-rotation: a ground
// builder whose building is turned east clears the turned footprint's cells,
// and leaves alone a unit on the cells only the footprint facing south
// would cover.
void kickout_clears_the_turned_footprint() {
    Setup setup = rotation();
    setup.rules.orders.build_site_kickout.enabled = true;
    setup.rules.orders.build_site_kickout.kickout = true;
    setup.rules.orders.build_site_kickout.retry_limit = 20;
    Fixture f(setup);
    // A walker may stop on the flat ground the kickout sends it to.
    f.match->state().unit_defs[walker].max_slope = 10;
    auto& constructor = f.spawn(0, builder, 16, 20);
    // The hall turned east at site (20,20) covers cells 19..21 by 20..21.
    auto& on_site = f.spawn(0, walker, 19, 20);
    auto& beside = f.spawn(0, walker, 19, 22);
    CHECK(f.occupant(19, 21) == on_site.unit_index && f.occupant(20, 22) == beside.unit_index);
    const sim::ground_orders::Point site{
        std::bit_cast<int32_t>(world_at(20)), 0, std::bit_cast<int32_t>(world_at(20))
    };
    (void)f.match->issue_mobile_build(constructor.unit_index, hall, site, false, 1);
    sim::unit_spawn::Slot* built = nullptr;
    f.run(400, [&] {
        for (auto& slot : f.match->world().slots)
            if (slot.unit && slot.record.type_index == hall)
                built = &slot;
    });
    CHECK(built != nullptr && built->record.build_remaining == 0.0F);
    CHECK(f.match->unit_build_facing(built->record) == 1);
    // The unit on the site moved off it; the one beside it never moved.
    CHECK(
        on_site.record.cell_z > 21 || on_site.record.cell_z < 19 || on_site.record.cell_x > 21 ||
        on_site.record.cell_x < 18
    );
    CHECK(beside.record.cell_x == 19 && beside.record.cell_z == 22);
    CHECK(f.occupant(19, 22) == beside.unit_index && f.occupant(20, 23) == beside.unit_index);
}

} // namespace

int main() {
    slot_reuse_waits_in_the_match();
    water_state_rules_in_the_match();
    buildings_turn_with_the_rule();
    builder_builds_in_the_order_facing();
    site_lets_own_mobile_units_through();
    mobile_unit_built_at_a_site();
    kickout_clears_the_turned_footprint();
    std::cout << "match unit rules tests passed\n";
}
