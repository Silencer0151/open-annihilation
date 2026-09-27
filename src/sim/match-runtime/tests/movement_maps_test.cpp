// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Persistent movement-class maps (build, footprint refresh, object removal,
// search projection) and the player slot's single 30-tick deadline.
#include "oa/sim/match_runtime.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string(#x) + " at line " + std::to_string(__LINE__));    \
    } while (false)

using namespace oa;

namespace {
constexpr uint32_t map_cells = 32;
constexpr uint8_t ground_height = 40;
constexpr uint16_t tank = 1, kbot = 2, building = 3, type_count = 4;
constexpr sim::unit_spawn::AssetHandle tank_class = 1, kbot_class = 2;
// Blocking sprite features: a 2x2 rock and a 1x3 wall.
constexpr uint16_t rock = 0, wall_feature = 1;

struct Services : sim::match_runtime::OfflineServices {
    uint32_t footprint_changes{}, object_removals{};

    void activation_sound(sim::unit_spawn::Slot&, sim::unit_activation::Sound) override {}

    void command_sound(sim::unit_spawn::Slot&, uint32_t) override {}

    void refresh_selected_unit(sim::unit_spawn::Slot&) override {}

    void emit_sfx(sim::unit_spawn::Slot&, uint32_t, int32_t) override {}

    void attachment_notification(sim::unit_spawn::Slot&, uint32_t) override {}

    void explode_piece(sim::unit_spawn::Slot&, uint32_t, int32_t) override {}

    void attach_unit(sim::unit_spawn::Slot&, int32_t, int32_t, int32_t) override {}

    void drop_unit(sim::unit_spawn::Slot&, int32_t) override {}

    void refresh_plot_height_range(std::array<int16_t, 2>, std::array<int16_t, 2>) override {}

    void notify_object_footprint_removed(sim::spatial_state::Unit&, uint32_t) override {
        ++object_removals;
    }

    void notify_footprint_changed(std::array<int16_t, 2>, std::array<int16_t, 2>) override {
        ++footprint_changes;
    }
};

struct Scenario : sim::scenario::DefinitionHost {
    int32_t integer(std::string_view, int32_t fallback) override { return fallback; }

    std::optional<std::string> text(std::string_view) override { return std::nullopt; }
};

// A flat 32x32-cell land map with a two-cell tank class, a one-cell kbot
// class and a 3x3 building. `wall` marks blocking-feature cells present from
// the start.
struct Fixture {
    formats::tnt::Map map;
    std::vector<sim::visibility_state::TerrainCell> terrain_values =
        std::vector<sim::visibility_state::TerrainCell>(map_cells * map_cells);
    std::array<sim::visibility_state::SightMask, 1> masks{};
    std::shared_ptr<formats::objects3d::Model> model =
        std::make_shared<formats::objects3d::Model>();
    std::shared_ptr<formats::cob::CobProgram> script = std::make_shared<formats::cob::CobProgram>();
    std::array<sim::unit_spawn::LoadedType, type_count> loaded;
    std::array<sim::unit_spawn::Type, type_count> types;
    std::array<data::unit_definitions::UnitDefinition, type_count> defs;
    std::array<data::unit_definitions::RuntimeDefinitionMetadata, type_count> metadata;
    data::unit_definitions::UnitTargetCategoryMasks target_masks;
    std::vector<sim::spatial_state::Plot> plots =
        std::vector<sim::spatial_state::Plot>(map_cells * map_cells);
    std::array<sim::match_runtime::RuntimeTypeFields, type_count> fields{};
    std::array<FeatureDef, 2> features{};
    std::vector<uint8_t> building_yard = std::vector<uint8_t>(9, 0x2f);
    sim::combat_state::WeaponRegistry weapons;
    Services services;
    Scenario scenario;
    std::unique_ptr<sim::match_runtime::Match> match;

    explicit Fixture(const std::vector<std::array<int32_t, 2>>& wall = {}) {
        map.attribute_width = map.attribute_height = map_cells;
        map.sea_level = 20;
        map.attributes.resize(map_cells * map_cells);
        for (auto& attribute : map.attributes)
            attribute.height = ground_height;
        for (auto& plot : plots)
            plot.low_height = plot.high_height = ground_height;
        for (const auto& [x, z] : wall)
            plots[static_cast<std::size_t>(z) * map_cells + static_cast<std::size_t>(x)]
                .blocking_feature = true;
        masks[0].width = masks[0].height = 1;
        masks[0].pixels = {1};
        model->objects.resize(1);
        model->objects[0].name = "root";
        // A model 20 units high (UnitDef.model_height).
        model->objects[0].vertices = {{0, 0, 0}, {0, 20 << 16, 0}};
        script->code = {sim::script_vm::opcode::return_};
        script->scripts = {{"Create", 0}};
        script->entry_points = {0};
        script->piece_names = {"root"};
        const std::array<int16_t, type_count> footprint{0, 2, 1, 3};
        for (uint16_t i = 1; i < type_count; ++i) {
            types[i].simulation.flags = OA_UNIT_DEF_FLAG_AVAILABLE;
            types[i].simulation.maximum_health = 100;
            types[i].footprint_x = types[i].footprint_z = footprint[i];
            types[i].bm_code = i == building ? 0 : 1;
            types[i].model = reinterpret_cast<uintptr_t>(model.get());
            types[i].cob = reinterpret_cast<uintptr_t>(script.get());
            loaded[i].model = model;
            loaded[i].script = script;
            loaded[i].type = types[i];
            loaded[i].unit_name = "UNIT" + std::to_string(i);
            defs[i].sight_distance = 64;
            defs[i].acceleration_fixed = 65536;
            defs[i].brake_rate_fixed = 65536;
            defs[i].max_velocity_fixed = 2 * 65536;
            defs[i].turn_rate = 1024;
            metadata[i].footprint_x = metadata[i].footprint_z = footprint[i];
            fields[i].definition = &defs[i];
            fields[i].runtime_metadata = &metadata[i];
            fields[i].target_masks = &target_masks;
            fields[i].movement_class = sim::unit_spawn::AssetHandle{0};
        }
        fields[tank].movement_class = tank_class;
        fields[kbot].movement_class = kbot_class;
        fields[building].yard_mask = building_yard;
        for (auto& feature : features) {
            feature.flags = OA_FEATURE_FLAG_SPRITE | OA_FEATURE_FLAG_BLOCKING;
            feature.dead_feature = sim::feature_runtime::no_feature;
        }
        features[rock].footprint_x = features[rock].footprint_z = 2;
        features[wall_feature].footprint_x = 1;
        features[wall_feature].footprint_z = 3;
        sim::match_runtime::OfflineInputs input{
            map,
            loaded,
            types,
            fields,
            weapons,
            terrain_values,
            masks,
            12,
            12,
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
        match = std::make_unique<sim::match_runtime::Match>(input, services);
        match->configure_strategic_environment({0, 0.5F, 0});
        for (uint8_t player = 0; player < 2; ++player) {
            match->simulation().players[player].present = true;
            match->simulation().players[player].status = 1;
            std::array<uint8_t, 10> allies{};
            allies[player] = 1;
            match->configure_player_alliances(player, allies);
            if (player == 0)
                match->configure_outcomes(0, allies, false);
        }
    }

    // A finished unit whose footprint origin is the given cell.
    sim::unit_spawn::Slot& spawn(uint16_t type, int32_t cell_x, int32_t cell_z) {
        const auto centre = [&](int32_t cell) {
            return static_cast<uint32_t>(cell * 16 + types[type].footprint_x * 8) << 16;
        };
        auto* slot = match->create({0, type, {centre(cell_x), 0, centre(cell_z)}, true, 1, 0});
        CHECK(slot && slot->unit);
        CHECK(slot->record.cell_x == cell_x && slot->record.cell_z == cell_z);
        return *slot;
    }

    // A feature placed or cleared through the feature runtime.
    void place(uint16_t feature, int32_t cell_x, int32_t cell_z) {
        (void)sim::feature_runtime::place_feature(
            match->state(),
            match->feature_host(),
            plot(cell_x, cell_z),
            feature,
            nullptr,
            nullptr,
            sim::feature_runtime::no_player
        );
        CHECK(match->state().plots[plot(cell_x, cell_z)].feature == feature);
    }

    void clear(int32_t cell_x, int32_t cell_z) {
        CHECK(
            sim::feature_runtime::clear_plot_feature(
                match->state(), match->feature_host(), plot(cell_x, cell_z), false
            )
        );
    }

    static std::size_t plot(int32_t cell_x, int32_t cell_z) {
        return static_cast<std::size_t>(cell_z) * map_cells + static_cast<std::size_t>(cell_x);
    }

    const sim::ground_orders::MovementMap& class_map(sim::unit_spawn::AssetHandle handle) const {
        const auto* map = match->movement_map(handle);
        CHECK(map != nullptr);
        return *map;
    }
};

// Expected class of every anchor in [first, last] of both axes around a
// square block of blocked cells: 0 where the class footprint overlaps the
// block, 1 where only its one-cell rim touches it, 3 beyond.
void check_block(
    const sim::ground_orders::MovementMap& map,
    int32_t origin,
    int32_t size,
    int32_t footprint,
    int32_t first,
    int32_t last
) {
    const auto overlaps = [&](int32_t anchor, int32_t reach) {
        return anchor + footprint + reach > origin && anchor - reach < origin + size;
    };
    for (auto z = first; z <= last; ++z)
        for (auto x = first; x <= last; ++x) {
            const uint8_t expected = overlaps(x, 0) && overlaps(z, 0)   ? 0
                                     : overlaps(x, 1) && overlaps(z, 1) ? 1
                                                                        : 3;
            const auto actual = map.cell(static_cast<uint32_t>(x), static_cast<uint32_t>(z));
            if (actual != expected)
                throw std::runtime_error(
                    "cell " + std::to_string(x) + "," + std::to_string(z) + " is " +
                    std::to_string(actual) + ", expected " + std::to_string(expected)
                );
        }
}

void check_clear(const sim::ground_orders::MovementMap& map, int32_t first, int32_t last) {
    for (auto z = first; z <= last; ++z)
        for (auto x = first; x <= last; ++x)
            CHECK(map.cell(static_cast<uint32_t>(x), static_cast<uint32_t>(z)) == 3);
}

// build_movement_maps builds a map for each class a type names and none for
// class-less types; a building registered mid-match walls its footprint in
// every class's map through the footprint refresh, and its death (no
// movement object) clears it again through the same refresh.
void building_walls_every_class_until_removed() {
    Fixture f;
    CHECK(f.match->movement_map(sim::unit_spawn::AssetHandle{0}) == nullptr);
    CHECK(f.class_map(tank_class).footprint_x() == 2 && f.class_map(kbot_class).footprint_x() == 1);
    CHECK(
        f.class_map(tank_class).width() == map_cells &&
        f.class_map(tank_class).height() == map_cells
    );
    check_clear(f.class_map(tank_class), 6, 15);
    check_clear(f.class_map(kbot_class), 6, 15);

    f.match->simulation().tick = 100;
    const auto changes = f.services.footprint_changes;
    auto& site = f.spawn(building, 10, 10);
    CHECK(f.services.footprint_changes == changes + 1);
    // The rectangle reaches the anchors just past the far edge: the tank's
    // rim at 13 touches column 12.
    check_block(f.class_map(tank_class), 10, 3, 2, 6, 15);
    check_block(f.class_map(kbot_class), 10, 3, 1, 6, 15);
    CHECK(f.class_map(tank_class).cell(13, 13) == 1 && f.class_map(tank_class).cell(9, 9) == 0);

    f.match->simulation().tick = 130;
    site.unit->record.damage_kind = static_cast<uint8_t>(sim::match_runtime::DeathKind::weapon);
    f.match->teardown_dead_unit(site);
    CHECK(f.match->spatial().plots[10 * map_cells + 10].ground == sim::spatial_state::no_unit);
    check_clear(f.class_map(tank_class), 6, 15);
    check_clear(f.class_map(kbot_class), 6, 15);
}

// Feature placement and removal refresh every class. The checked anchors
// stay below the top rows the map load voids.
void features_refresh_every_class() {
    Fixture f;
    const auto changes = f.services.footprint_changes;
    f.place(rock, 6, 6);
    CHECK(f.services.footprint_changes == changes + 1);
    CHECK(f.match->spatial().plots[Fixture::plot(7, 7)].blocking_feature);
    check_block(f.class_map(tank_class), 6, 2, 2, 3, 11);
    check_block(f.class_map(kbot_class), 6, 2, 1, 3, 11);
    f.clear(7, 7);
    CHECK(f.services.footprint_changes == changes + 2);
    CHECK(!f.match->spatial().plots[Fixture::plot(7, 7)].blocking_feature);
    check_clear(f.class_map(tank_class), 3, 11);
    check_clear(f.class_map(kbot_class), 3, 11);
}

struct PlotClassifier final : sim::ground_orders::MovementMapSampler {
    sim::spatial_state::CellClassifier movement{};
    const sim::spatial_state::World* world{};

    uint8_t classify_cell(int32_t x, int32_t z) override {
        return sim::spatial_state::classify_movement_cell(movement, x, z, *world);
    }

    uint8_t classify_plot(int32_t x, int32_t z) override {
        return sim::spatial_state::classify_movement_plot(movement, x, z, *world);
    }
};

std::vector<sim::ground_orders::RoutePoint> search(
    sim::simulation_state::Unit& unit,
    sim::ground_orders::MovementMap& map,
    const sim::ground_orders::SearchBegin& begin,
    sim::ground_orders::Goal& goal
) {
    static const std::vector<uint16_t> all_seen(64 * 64, 0xffff);
    sim::ground_orders::Navigation navigation;
    sim::ground_orders::SearchWorker worker;
    auto result = worker.begin({&unit, &navigation, &goal, &map}, begin, all_seen);
    for (int slice = 0; slice < 64 && result == sim::ground_orders::SearchAdvance::in_progress;
         ++slice)
        result = worker.advance_slice();
    CHECK(result == sim::ground_orders::SearchAdvance::succeeded);
    return worker.route();
}

// On a map that only changes through the refresh triggers, the job's
// persistent class map after the search projection holds what the whole-map
// build makes at job time with the searcher's own tick set to now, so
// the search returns the same route. A tank settled for more than 30 ticks
// is a wall; a kbot that moved within 30 ticks is not.
void persistent_search_matches_the_job_build() {
    std::vector<std::array<int32_t, 2>> wall;
    for (int32_t z = 4; z <= 15; ++z)
        wall.push_back({8, z});
    Fixture f(wall);
    auto& searcher = f.spawn(kbot, 3, 11);
    f.match->simulation().tick = 5;
    auto& settled = f.spawn(tank, 13, 10);
    f.match->simulation().tick = 150;
    auto& site = f.spawn(building, 14, 15);
    (void)site;
    f.place(wall_feature, 12, 5);
    f.match->simulation().tick = 195;
    auto& walker = f.spawn(kbot, 16, 8);
    constexpr uint32_t now = 200;
    f.match->simulation().tick = now;

    auto* job_unit = searcher.unit;
    sim::ground_orders::SearchBegin begin;
    begin.current_tick = now;
    sim::ground_orders::MovementMap* persistent = nullptr;
    CHECK(f.match->prepare_search_job(searcher, begin, persistent));
    CHECK(persistent == f.match->movement_map(kbot_class));
    CHECK(begin.occupancy.size() == 2 && begin.unit_changed_tick != nullptr);
    CHECK(*begin.unit_changed_tick == 0);
    sim::ground_orders::Goal goal;
    goal.cell = {20, 11};
    const auto route = search(*job_unit, *persistent, begin, goal);
    CHECK(persistent->projection_tick() == now - 30 && *begin.unit_changed_tick == 0);
    CHECK(persistent->cell(13, 10) == 0 && persistent->cell(14, 11) == 0);
    CHECK(persistent->cell(16, 8) == 3 && persistent->cell(3, 11) == 3);

    PlotClassifier classifier;
    classifier.world = &f.match->spatial();
    classifier.movement.footprint_x = classifier.movement.footprint_z = 1;
    classifier.movement.max_water_depth = f.metadata[kbot].max_water_depth;
    classifier.movement.min_water_depth = f.metadata[kbot].min_water_depth;
    classifier.movement.max_land_slope = f.metadata[kbot].max_slope;
    classifier.movement.bad_land_slope = f.metadata[kbot].bad_slope;
    classifier.movement.max_water_slope = f.metadata[kbot].max_water_slope;
    classifier.movement.bad_water_slope = f.metadata[kbot].bad_water_slope;
    classifier.movement.occupancy_before_tick = now - 30;
    sim::ground_orders::MovementMap job_map(map_cells, map_cells, 1, 1, classifier);
    auto& own_tick = f.match->spatial().units[searcher.unit_index].object_tick;
    const auto saved = own_tick;
    own_tick = now;
    job_map.rebuild();
    own_tick = saved;
    for (uint32_t z = 0; z < map_cells; ++z)
        for (uint32_t x = 0; x < map_cells; ++x)
            CHECK(persistent->cell(x, z) == job_map.cell(x, z));

    sim::ground_orders::SearchBegin job_begin;
    job_begin.current_tick = now;
    job_begin.world_width = job_begin.world_height = map_cells;
    CHECK(search(*job_unit, job_map, job_begin, goal) == route);
    CHECK(route.size() > 2);
    (void)settled;
    (void)walker;
}

// The player slot update advances each slot's Player.next_economy_tick deadline
// once when it comes due; the local slot's outcome checks run under that same
// deadline and no other.
void outcome_checks_follow_the_slot_deadline() {
    Fixture f;
    f.spawn(tank, 4, 4);
    auto& local = f.match->state().game.players[0];
    auto& other = f.match->state().game.players[1];
    const auto run_to = [&](uint32_t tick) {
        while (f.match->simulation().tick < tick) {
            ++f.match->simulation().tick;
            f.match->tick();
        }
    };
    f.match->simulation().tick = 39;
    local.next_economy_tick = 45;
    other.next_economy_tick = 42;
    run_to(44);
    CHECK(f.match->outcome_state().countdown == -1);
    CHECK(local.next_economy_tick == 45 && other.next_economy_tick == 72);
    run_to(45);
    CHECK(f.match->outcome_state().countdown == 4 && local.next_economy_tick == 75);
    run_to(74);
    CHECK(f.match->outcome_state().countdown == 4 && other.next_economy_tick == 102);
    run_to(75);
    CHECK(f.match->outcome_state().countdown == 3 && local.next_economy_tick == 105);
}
} // namespace

int main() {
    building_walls_every_class_until_removed();
    features_refresh_every_class();
    persistent_search_matches_the_job_build();
    outcome_checks_follow_the_slot_deadline();
    return 0;
}
