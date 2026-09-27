// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "match_state.hpp"
#include "oa/sim/ballistics.hpp"
#include "oa/sim/weapon_execution/weapon_launch.hpp"
#include <algorithm>
#include <cstdint>
#include "oa/base/game_math.hpp"
#include "oa/sim/world_environment/wind.hpp"
#include <bit>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace oa::sim::match_runtime {
namespace {
/// Empties the match's shot pool (the "WEAPON ARRAY" block): every record
/// cleared and none in flight.
///
/// @param[in,out] world World whose projectile pool and count are reset.
void clear_projectile_pool(oa::World& world) {
    std::fill_n(world.projectiles, OA_PROJECTILE_CAPACITY, oa::Projectile{});
    world.game.projectile_count = 0;
}

// Sight grid fills: a mapped word holds one bit per player, a coverage byte
// counts the units that see the cell.
constexpr uint16_t mapped_by_nobody = 0x0000;
constexpr uint16_t mapped_by_all = 0xffff;
constexpr uint8_t unseen = 0;
constexpr uint8_t seen_once = 1;

int16_t low(uint32_t word) {
    return std::bit_cast<int16_t>(static_cast<uint16_t>(word));
}

std::array<int32_t, 3> signed_position(const std::array<uint32_t, 3>& p) {
    return {
        std::bit_cast<int32_t>(p[0]), std::bit_cast<int32_t>(p[1]), std::bit_cast<int32_t>(p[2])
    };
}

struct Geometry final : sim::combat_state::SpawnGeometryHost {
    UnitInstance& unit;

    explicit Geometry(UnitInstance& value) : unit(value) {}

    std::array<int32_t, 3> query_weapon_world(uint8_t slot) override {
        return signed_position(unit.query_weapon_world(slot));
    }

    std::array<int32_t, 3> aim_from_world(uint8_t slot) override {
        return signed_position(unit.aim_from_world(slot));
    }

    void set_max_reload_time(int32_t value) override { unit.set_max_reload_time(value); }
};

struct Activation final : sim::unit_activation::Host {
    sim::unit_spawn::Slot& slot;
    UnitInstance* instance;
    OfflineServices& services;
    std::function<void(uint32_t)> wake_observers; // wakes the orders observing the unit
    sim::simulation_state::World& world;
    const MultiplayerHooks& multiplayer;

    Activation(
        sim::unit_spawn::Slot& s,
        UnitInstance* i,
        OfflineServices& h,
        std::function<void(uint32_t)> wake,
        sim::simulation_state::World& w,
        const MultiplayerHooks& m
    )
        : slot(s), instance(i), services(h), wake_observers(std::move(wake)), world(w),
          multiplayer(m) {}

    void script(std::string_view name) override {
        if (instance && instance->script())
            instance->script()->call_no_arguments(name, false);
    }

    void sound(sim::unit_activation::Sound sound) override {
        services.activation_sound(slot, sound);
    }

    void notify_attachments(uint32_t value) override {
        wake_observers(value);
        services.attachment_notification(slot, value);
    }

    void refresh_selected_unit() override { services.refresh_selected_unit(slot); }

    bool owner_simulates_here() override {
        return sim::simulation_state::locally_simulated(*slot.unit);
    }

    void flags_changed(uint16_t unit, uint8_t flags) override {
        if (!world.run_flag)
            return;
        if (!multiplayer.unit_flags_changed)
            throw std::logic_error("unit flag change shared without a multiplayer handler");
        multiplayer.unit_flags_changed(multiplayer.context, unit, flags);
    }
};

struct MatchWindRandom final : sim::world_environment::WindRandomHost {
    SharedRandom& shared;
    uint32_t& lcg;

    MatchWindRandom(SharedRandom& s, uint32_t& c) : shared(s), lcg(c) {}

    uint32_t lcg_rand_15() override {
        lcg = lcg * 214013u + 2531011u;
        return (lcg >> 16) & 0x7fffu;
    }

    uint32_t shared_random(uint32_t bound) override { return shared.bounded(bound); }
};

struct SpeedScript final : sim::visibility_state::SpeedHost {
    ScriptInstance* script;
    float& storage;
    const float& computed;

    SpeedScript(ScriptInstance* value, float& output, const float& input)
        : script(value), storage(output), computed(input) {}

    void set_speed(int32_t value) override {
        storage = computed;
        if (!script)
            throw std::logic_error("SetSpeed has no script");
        script->call("SetSpeed", std::span(&value, 1), false);
    }
};

// A record from the runtime type and its parsed definition, for tables built
// without the FBI loader's records.
void assemble_unit_def(const OfflineInputs& input, std::size_t i, oa::UnitDef& def) {
    sim::unit_spawn::load_unit_def(input.types[i], def);
    // The catalog is sorted by unit name, so the index is the type id.
    def.type_id = static_cast<uint16_t>(i);
    const auto& name = input.loaded[i].unit_name;
    const auto length = std::min(name.size(), sizeof def.unit_name - 1);
    std::copy_n(name.data(), length, def.unit_name);
    def.unit_name[length] = '\0';
    def.corpse = input.fields[i].corpse_feature;
    if (const auto* definition = input.fields[i].definition) {
        def.max_velocity = definition->max_velocity_fixed;
        def.move_rate1 = definition->move_rate1_fixed;
        def.move_rate2 = definition->move_rate2_fixed;
        def.sight_distance = definition->sight_distance;
        def.radar_distance = definition->radar_distance;
        def.sonar_distance = definition->sonar_distance;
        def.min_cloak_distance = definition->min_cloak_distance;
        def.radar_distance_jam = definition->radar_distance_jam;
        def.sonar_distance_jam = definition->sonar_distance_jam;
        def.attack_run_length = definition->attack_run_length;
        def.cruise_alt = definition->cruise_altitude;
        def.worker_time = definition->worker_time;
        def.build_distance = definition->build_distance;
        def.maneuver_leash_length = definition->maneuver_leash_length;
        def.build_cost_metal = static_cast<float>(definition->build_cost_metal);
        def.max_water_depth = definition->max_water_depth;
        def.min_water_depth = definition->min_water_depth;
        def.transport_size = definition->transport_size;
        def.transport_capacity = definition->transport_capacity;
        def.kamikaze_distance = definition->kamikaze_distance;
        def.energy_make = definition->energy_make;
        def.energy_use = definition->energy_use;
        def.metal_make = definition->metal_make;
        def.extracts_metal = definition->extracts_metal;
        def.wind_generator = definition->wind_generator;
        def.tidal_generator = definition->tidal_generator;
        def.cloak_cost = definition->cloak_cost;
        def.cloak_cost_moving = definition->cloak_cost_moving;
        def.energy_storage = definition->energy_storage;
        def.metal_storage = definition->metal_storage;
        def.makes_metal = definition->makes_metal;
        def.build_cost_energy = static_cast<float>(definition->build_cost_energy);
        def.build_time = definition->build_time;
        def.acceleration = definition->acceleration_fixed;
        def.brake_rate = definition->brake_rate_fixed;
        def.bank_scale = definition->bank_scale_fixed;
        def.pitch_scale = definition->pitch_scale_fixed;
        def.turn_rate = definition->turn_rate;
    }
}
} // namespace

Match::State::State(const OfflineInputs& input)
    : world(std::make_unique<oa::World>()),
      units(sim::unit_spawn::offline_pool_size(input.per_player_limit)),
      unit_defs(input.types.size()), projectiles(OA_PROJECTILE_CAPACITY), assets(units.size()),
      orders(units.size()) {
    if (input.types.empty() || input.loaded.size() != input.types.size() ||
        input.fields.size() != input.types.size() ||
        (!input.unit_defs.empty() && input.unit_defs.size() != input.types.size()))
        throw std::invalid_argument("offline runtime type tables differ");
    if (input.viewpoint_player >= 10)
        throw std::out_of_range("offline viewpoint outside ten players");
    load_types(input);
    world->units = units.data();
    world->unit_slot_count = static_cast<uint32_t>(units.size());
    world->unit_defs = unit_defs.data();
    world->unit_def_count = static_cast<uint32_t>(unit_defs.size());
    feature_defs.assign(input.feature_defs.begin(), input.feature_defs.end());
    world->feature_defs = feature_defs.data();
    world->feature_def_count = static_cast<uint32_t>(feature_defs.size());
    world->game.feature_def_count = static_cast<int32_t>(feature_defs.size());
    plots = std::make_unique<oa::MapPlot[]>(
        static_cast<std::size_t>(input.map.attribute_width) * input.map.attribute_height
    );
    world->plots = plots.get();
    placed_features = std::make_unique<sim::feature_runtime::PlacedFeature[]>(
        sim::feature_runtime::slot_capacity
    );
    world->placed_features = reinterpret_cast<uint8_t*>(placed_features.get());
    world->placed_feature_count = static_cast<uint32_t>(sim::feature_runtime::slot_capacity);
    world->projectiles = projectiles.data();
    clear_projectile_pool(*world);
    world->game.viewpoint_player = input.viewpoint_player;
    world->game.difficulty = OA_DIFFICULTY_MEDIUM;
    world->game.tidal_strength = input.tidal_strength;
    // Eleven player records, each owning a zeroed setup block; the eleventh
    // keeps the no-player index.
    for (uint32_t i = 0; i < OA_PLAYER_RECORD_COUNT; ++i)
        oa::world_player_record(world.get(), i)->info = oa::oa_ref_from_index(i);
    world->game.no_player.index = OA_PLAYER_COUNT;
    for (std::size_t i = 0; i < OA_PLAYER_COUNT; ++i) {
        auto& player = world->game.players[i];
        player.index = static_cast<uint8_t>(i);
        player.board_row = static_cast<uint8_t>(i); // kills board rows start in player order
        player.economy = oa::oa_ref_from_index(static_cast<uint32_t>(i));
        player.energy_storage = 1000.0F;
        player.metal_storage = 1000.0F;
    }
    sim::unit_spawn::init_unit_pool(*world, input.per_player_limit);
    tables = {input.types, assets, setups};
    views =
        std::make_unique<sim::unit_spawn::LegacyViews>(*world, input.types, assets, setups, orders);
}

void Match::State::load_types(const OfflineInputs& input) {
    category_masks.clear();
    const auto category = [&](const data::unit_definitions::UnitCategoryMask& mask) {
        category_masks.push_back(&mask);
        return oa::oa_ref_from_index(static_cast<uint32_t>(category_masks.size() - 1));
    };
    for (std::size_t i = 0; i < input.types.size(); ++i) {
        if (!input.unit_defs.empty())
            unit_defs[i] = input.unit_defs[i];
        else
            assemble_unit_def(input, i, unit_defs[i]);
        // The category handles index this match's mask table.
        if (const auto* masks = input.fields[i].target_masks) {
            unit_defs[i].primary_bad_target_category = category(masks->primary_bad);
            unit_defs[i].secondary_bad_target_category = category(masks->secondary_bad);
            unit_defs[i].special_bad_target_category = category(masks->special_bad);
            unit_defs[i].no_chase_category = category(masks->no_chase);
        }
    }
}

void Match::replace_unit_def(std::size_t type, const oa::UnitDef& record) {
    if (type >= state_.unit_defs.size())
        throw std::out_of_range("unit type outside the runtime table");
    auto& def = state_.unit_defs[type];
    const auto primary = def.primary_bad_target_category;
    const auto secondary = def.secondary_bad_target_category;
    const auto special = def.special_bad_target_category;
    const auto no_chase = def.no_chase_category;
    def = record;
    def.primary_bad_target_category = primary;
    def.secondary_bad_target_category = secondary;
    def.special_bad_target_category = special;
    def.no_chase_category = no_chase;
}

Match::Match(const OfflineInputs& input, OfflineServices& services)
    : input_(input), services_(services), state_(input), simulation_(state_.views->simulation()),
      world_(state_.views->world()), slots_(state_.views->slots()), units_(state_.views->units()),
      spatial_units_(slots_.size()), weapons_(slots_.size()), movement_(slots_.size()),
      air_drivers_(slots_.size()), mirrored_air_goals_(slots_.size()), terrain_(input.map),
      random_(input.random_seed), lcg_seed_(input.random_seed) {
    target_projection_.resize(slots_.size());
    const auto sighting_capacity = static_cast<uint32_t>(slots_.size());
    sighting_slots_.assign(std::size_t{2} * sightings_.size() * sighting_capacity, 0);
    for (std::size_t player = 0; player < sightings_.size(); ++player) {
        auto& sightings = sightings_[player];
        sightings.seen = &sighting_slots_[2 * player * sighting_capacity];
        sightings.radar = sightings.seen + sighting_capacity;
        sightings.capacity = sighting_capacity;
    }
    type_bounds_.resize(input.types.size());
    for (std::size_t i = 1; i < input.types.size(); ++i) {
        if (!input.loaded[i].model)
            continue;
        const auto& bounds = type_bounds_[i] = formats::objects3d::derive_unit_type_bounds(
            *input.loaded[i].model, input.types[i].footprint_x, input.types[i].footprint_z
        );
        auto& def = state_.unit_defs[i];
        def.bounds_min_x = bounds->bounds_min_x;
        def.bounds_min_y = bounds->bounds_min_y;
        def.bounds_min_z = bounds->bounds_min_z;
        def.bounds_max_x = bounds->bounds_max_x;
        def.model_height = bounds->model_height;
        def.bounds_max_z = bounds->bounds_max_z;
        def.size_x = bounds->size_x;
        def.size_y = bounds->size_y;
        def.size_z = bounds->size_z;
        def.size_radius =
            std::bit_cast<int32_t>(
                std::bit_cast<uint32_t>(bounds->size_z) + std::bit_cast<uint32_t>(bounds->size_x)
            ) /
            3;
    }
    const auto cells =
        static_cast<std::size_t>(input.map.attribute_width) * input.map.attribute_height;
    if (input.terrain_values.size() != cells)
        throw std::invalid_argument("runtime terrain values do not match map");
    if (input.sight_width < 0 || input.sight_height < 0 ||
        static_cast<uint64_t>(input.sight_width) * static_cast<uint32_t>(input.sight_height) >
            16 * 1024 * 1024)
        throw std::invalid_argument("sight grid dimensions exceed bounds");
    if (!input.scenario_definitions)
        throw std::invalid_argument("offline match requires actual scenario definitions");
    sim::scenario::construct(scenario_);
    sim::scenario::register_conditions(scenario_, *input.scenario_definitions);
    scenario_gravity_ = input.scenario_definitions->integer("gravity", 0);
    lava_world_ = input.scenario_definitions->integer("lavaworld", 0);
    // The schema's waterdoesdamage and waterdamage, which the unit tick
    // applies to units at or below sea level.
    state().environment_enabled =
        static_cast<uint32_t>(input.scenario_definitions->integer("waterdoesdamage", 0));
    state().environment_damage = input.scenario_definitions->integer("waterdamage", 0);
    state().game.gravity = sim::ballistics::simulation_gravity(false, 0, scenario_gravity_);
    sim::weapon_execution::store_weapon_defs(input.weapons, state().game.weapon_defs);
    state().game.sea_level = terrain_.sea_level();
    // The map's cell counts and its size in world units.
    state().game.map_width = static_cast<int32_t>(input.map.attribute_width);
    state().game.map_height = static_cast<int32_t>(input.map.attribute_height);
    state().game.map_width_world = state().game.map_width << 4;
    state().game.map_height_world = state().game.map_height << 4;
    // The session's mapping and line-of-sight rules; the fog edge mask
    // starts stale.
    state().game.visibility_flags =
        static_cast<uint8_t>(input.visibility_flags & ~OA_VISIBILITY_FOG_MASK_CURRENT);
    spatial_.terrain_width = input.map.attribute_width;
    spatial_.terrain_height = input.map.attribute_height;
    spatial_.plots.resize(cells);
    spatial_.units = spatial_units_;
    spatial_.sea_level = state().game.sea_level;
    if (!input.collision_plots.empty()) {
        if (input.collision_plots.size() != cells)
            throw std::invalid_argument("collision terrain does not match map");
        for (std::size_t i = 0; i < cells; ++i) {
            spatial_.plots[i].high_height = input.collision_plots[i].high_height;
            spatial_.plots[i].low_height = input.collision_plots[i].low_height;
            spatial_.plots[i].blocking_feature = input.collision_plots[i].blocking_feature;
            spatial_.plots[i].metal_feature = input.collision_plots[i].metal_feature;
            spatial_.plots[i].geo_feature = input.collision_plots[i].geo_feature;
            spatial_.plots[i].indestructible_feature =
                input.collision_plots[i].indestructible_feature;
            spatial_.plots[i].metal = input.collision_plots[i].metal;
            spatial_.plots[i].feature_word = input.collision_plots[i].feature_word;
            spatial_.plots[i].feature_back_x = input.collision_plots[i].feature_back_x;
            spatial_.plots[i].feature_back_z = input.collision_plots[i].feature_back_z;
            spatial_.plots[i].feature_height = input.collision_plots[i].feature_height;
            spatial_.plots[i].feature_footprint_x = input.collision_plots[i].feature_footprint_x;
            spatial_.plots[i].feature_footprint_z = input.collision_plots[i].feature_footprint_z;
        }
    }
    sim::spatial_state::build_buckets(spatial_);
    sight_.width = input.sight_width;
    sight_.height = input.sight_height;
    sight_.viewpoint_player = input.viewpoint_player;
    sight_.game = &state().game;
    const auto sight_cells =
        static_cast<std::size_t>(sight_.width) * static_cast<std::size_t>(sight_.height);
    sight_.coverage.resize(sight_cells);
    sight_.player_bits.resize(sight_cells);
    for (std::size_t i = 0; i < other_player_coverage_.size(); ++i)
        if (i != input.viewpoint_player)
            other_player_coverage_[i].resize(sight_cells);
    // Each player's coverage grid is sight_width by sight_height cells.
    for (auto& player : state().game.players) {
        player.sight_width = static_cast<uint32_t>(sight_.width);
        player.sight_height = static_cast<uint32_t>(sight_.height);
    }
    if (const auto* error = sim::visibility_state::sight_context_error(sight_context()))
        throw std::invalid_argument(error);
    bridge_ = std::make_unique<SpawnBridge>(
        *state_.world,
        state_.tables,
        *state_.views,
        input.loaded,
        static_cast<UnitValueHost&>(*this),
        services_,
        static_cast<SpawnSubsystems&>(*this),
        random_,
        input.clock_scale
    );
    environment_wind_.minimum_strength = input.minimum_wind;
    environment_wind_.maximum_strength = input.maximum_wind;
    if (environment_wind_.maximum_strength < environment_wind_.minimum_strength)
        std::swap(environment_wind_.maximum_strength, environment_wind_.minimum_strength);
    environment_wind_.current_tick = 0;
    // In 3.1c the wind schedule of a session's later matches draws from the
    // LCG stream, since the tick counter still holds the last match's
    // count when the wind is scheduled; a match here always starts at tick 0
    // and draws nothing.
    MatchWindRandom wind_random{random_, lcg_seed_};
    sim::world_environment::initialize_wind(environment_wind_, wind_random);
    effects_ = std::make_unique<sim::effect_particles::EffectWorld>();
    effects_->lava_world = lava_world_ != 0;
    effects_->no_sea_level_trigger =
        input.scenario_definitions->integer("nosealeveltrigger", 0) != 0;
    allocate_flash_tiers();
    sim::effect_particles::init_explosion_tables(*effects_, flash_tiers_, effect_host());
    resolve_effect_sequences();
    place_map_features();
    wind_.changed = environment_wind_.changed != 0;
    wind_.direction = environment_wind_.direction;
    wind_.speed = static_cast<uint32_t>(environment_wind_.strength);
    configure_strategic_environment(
        {environment_wind_.maximum_strength,
         input.tidal_strength,
         environment_wind_.normalized_strength}
    );
    build_movement_maps();
}

const RuntimeTypeFields& Match::fields(sim::unit_spawn::Slot& slot) const {
    for (std::size_t i = 0; i < input_.types.size(); ++i)
        if (slot.unit->type == &input_.types[i].simulation) {
            if (!input_.fields[i].definition)
                throw std::logic_error("runtime type has no FBI definition");
            return input_.fields[i];
        }
    throw std::out_of_range("unit type outside runtime table");
}

sim::unit_spawn::Slot* Match::create(const sim::unit_spawn::Request& request) {
    return bridge_->create(request);
}

sim::unit_spawn::StartResult Match::start_player(
    uint8_t player,
    const sim::unit_spawn::PlayerSetup& setup,
    std::span<const sim::unit_spawn::StartMarker> markers,
    int32_t index,
    int32_t width,
    int32_t height,
    sim::unit_spawn::StartHost& host
) {
    return bridge_->start_player(
        player, setup, markers, index, input_.viewpoint_player, width, height, host
    );
}

UnitInstance* Match::instance(uint16_t index) {
    return bridge_->runtime(slots_.at(index)).instance.get();
}

void Match::share_script_start(uint16_t unit, int16_t function) {
    if (multiplayer.script_started != nullptr)
        multiplayer.script_started(multiplayer.context, unit, function, 0, {});
}

void Match::share_named_script_start(
    uint16_t unit, std::string_view name, uint8_t count, const std::array<uint32_t, 4>& locals
) {
    if (multiplayer.script_started == nullptr)
        return;
    auto* object = instance(unit);
    const auto function =
        object != nullptr && object->script() != nullptr ? object->script()->find(name) : -1;
    multiplayer.script_started(
        multiplayer.context, unit, static_cast<int16_t>(function), count, locals
    );
}

void Match::tick_scripts(uint32_t elapsed) {
    for (auto& slot : slots_)
        if (slot.unit->record.type_index && slot.unit->script_present)
            bridge_->tick_script(slot, elapsed);
}

void Match::refresh_wind() {
    environment_wind_.current_tick = simulation_.tick;
    MatchWindRandom wind_random{random_, lcg_seed_};
    sim::world_environment::refresh_wind(environment_wind_, wind_random);
    sim::world_environment::store_wind_state(state().game, environment_wind_);
    wind_.changed = environment_wind_.changed != 0;
    wind_.direction = environment_wind_.direction;
    wind_.speed = static_cast<uint32_t>(environment_wind_.strength);
}

void Match::tick(sim::simulation_state::Host& host) {
    if (!outcome_view_)
        throw std::logic_error("offline tick requires configured outcome identity and alliances");
    resolve_effect_sequences();
    // The game loop's order: units and their scripts, projectiles, then
    // explosions, so records a script or an impact logs are stepped in the
    // tick that logs them.
    sim::simulation_state::update_units(state(), state_.orders, host);
    mark_profile(OA_PROFILE_UNITS);
    update_projectiles();
    mark_profile(OA_PROFILE_WEAPON);
    sim::effect_particles::tick_explosions(*effects_, state().game, effect_host());
    mark_profile(OA_PROFILE_MISC);
    advance_path_search();
}

void Match::target_cleared(oa::Unit& unit, uint8_t slot) {
    auto* object = instance(unit.id);
    if (object && object->script()) {
        const int32_t argument = slot;
        object->script()->call("TargetCleared", std::span(&argument, 1), false);
    }
}

sim::unit_health::ParalysisHooks Match::weapon_target_hooks() {
    sim::unit_health::ParalysisHooks hooks;
    hooks.context = this;
    hooks.target_cleared = [](void* context, oa::Unit& unit, uint8_t slot) {
        static_cast<Match*>(context)->target_cleared(unit, slot);
    };
    return hooks;
}

void Match::stop_weapon(sim::unit_spawn::Slot& slot, uint32_t index) {
    if (index >= OA_UNIT_WEAPON_COUNT)
        throw std::out_of_range("weapon slot outside the unit");
    (void)sim::unit_health::clear_weapon_target(
        slot.record, static_cast<uint8_t>(index), weapon_target_hooks()
    );
}

void Match::release_tracked_weapons(sim::unit_spawn::Slot& slot, uint8_t index) {
    (void)sim::unit_health::clear_tracked_weapon_targets(slot.record, index, weapon_target_hooks());
}

void Match::assign_squad(sim::unit_spawn::Slot& slot, uint32_t group) {
    const auto owner = slot.unit->owner->record.index;
    if (owner >= groups_.size())
        throw std::out_of_range("unit group owner");
    auto& groups = groups_[owner];
    if (slot.record.squad != -1) {
        auto& old = groups[static_cast<uint32_t>(slot.record.squad)];
        auto found = std::find(old.begin(), old.end(), slot.unit_index);
        if (found != old.end()) {
            *found = old.back();
            old.pop_back();
        }
    }
    if (group != 0xffffffffu)
        groups[group].push_back(slot.unit_index);
    slot.record.squad = static_cast<int32_t>(group);
}

void Match::initialize_weapons(sim::unit_spawn::Slot& slot, SlotRuntime& runtime) {
    if (!runtime.instance)
        throw std::logic_error("combat initialization precedes model");
    const auto& def = *fields(slot).definition;
    Geometry geometry(*runtime.instance);
    auto& weapons = weapons_.at(slot.unit_index);
    for (std::size_t i = 0; i < weapons.slots.size(); ++i)
        weapons.slots[i].flags = slot.record.weapons[i].flags;
    (void)sim::combat_state::initialize_spawn_combat(
        weapons, input_.weapons, {def.weapon1, def.weapon2, def.weapon3}, geometry
    );
    for (std::size_t i = 0; i < weapons.slots.size(); ++i)
        sim::weapon_execution::arm_weapon_slot(
            slot.record.weapons[i], weapons.slots[i], weapons.definitions[i]
        );
}

void Match::initialize_extraction_rate(sim::unit_spawn::Slot& slot, SlotRuntime& runtime) {
    const auto& def = *fields(slot).definition;
    sim::visibility_state::SpeedUnit unit{
        def.extracts_metal,
        slot.record.cell_x,
        slot.record.cell_z,
        slot.record.footprint_x,
        slot.record.footprint_z,
        slot.record.extracted_metal,
        slot.record.script != 0
    };
    SpeedScript script(
        runtime.instance ? runtime.instance->script() : nullptr,
        slot.record.extracted_metal,
        unit.speed
    );
    (void)sim::visibility_state::initialize_terrain_speed(
        unit,
        {static_cast<int32_t>(input_.map.attribute_width),
         static_cast<int32_t>(input_.map.attribute_height),
         input_.terrain_values},
        unit.script_present ? &script : nullptr
    );
    slot.record.extracted_metal = unit.speed;
}

sim::unit_spawn::AssetHandle Match::create_movement(sim::unit_spawn::Slot& slot) {
    const auto& type = fields(slot);
    if (!type.movement_class)
        throw std::logic_error("movement class pointer has not been resolved");
    auto& runtime = movement_.at(slot.unit_index);
    runtime = std::make_unique<sim::ground_orders::GroundRuntime>(
        slot, *type.definition, *type.movement_class
    );
    // A can-fly type's movement object gets the air driver: local, or a
    // mirrored copy for units another player's simulation runs.
    auto& driver = air_drivers_.at(slot.unit_index);
    driver = {};
    mirrored_air_goals_.at(slot.unit_index) = {};
    if (slot.unit->type && (slot.unit->type->flags & OA_UNIT_DEF_FLAG_CAN_FLY) != 0) {
        if (runtime->mirrored_driver)
            sim::air::air_driver_init_mirrored(&driver, &slot.record);
        else
            sim::air::air_driver_init_local(&driver, &slot.record);
    }
    return reinterpret_cast<sim::unit_spawn::AssetHandle>(runtime.get());
}

void Match::fit_spawn_height(sim::unit_spawn::Slot& slot) {
    auto& unit = *slot.unit;
    const auto flags = unit.flags;
    const auto& type = *unit.type;
    constexpr uint32_t dirty = 0x10000, waterline = 0x1000, terrain_height = 0x100000,
                       floating = 0x80000;
    if (!(flags & dirty) && !(type.flags & waterline))
        return;
    unit.flags &= ~dirty;
    if (!unit.object_present || (flags & 3) != 1)
        return;
    if (type.flags & terrain_height) {
        int32_t height;
        if (!(type.flags & waterline))
            height = sample_terrain_height(unit.position[0], unit.position[2]);
        else {
            height = static_cast<int32_t>(simulation_.sea_level) - type.waterline_offset;
            if (height < sample_terrain_height(unit.position[0], unit.position[2]))
                height = sample_terrain_height(unit.position[0], unit.position[2]);
        }
        unit.position[1] = static_cast<uint32_t>(height) << 16;
    } else if (type.flags & floating)
        unit.position[1] =
            (static_cast<uint32_t>(type.waterline_offset) * 0xffffu + simulation_.sea_level) << 16;
    else {
        auto& runtime = movement_.at(slot.unit_index);
        if (!runtime)
            throw std::logic_error("ground fit has no movement object");
        auto* object = instance(slot.unit_index);
        if (!object)
            throw std::logic_error("ground fit has no model object");
        sim::unit_movement::GroundClock clock;
        clock.simulation_tick = simulation_.tick;
        if (type.flags & waterline) {
            if (!input_.uptime_milliseconds)
                throw std::logic_error("floating ground fit requires platform clock");
            for (auto& tick : clock.bob_ticks)
                tick = sim::unit_movement::scaled_bob_tick(
                    input_.uptime_milliseconds(), static_cast<uint32_t>(input_.clock_scale)
                );
        }
        constexpr uint32_t can_fly = 0x800u, can_hover = 0x1000u;
        if (type.flags & can_fly) {
            if (runtime && (runtime->movement.flags & 3) == 2)
                return;
            unit.position[1] =
                static_cast<uint32_t>(sample_terrain_height(unit.position[0], unit.position[2]))
                << 16;
            return;
        }
        (void)runtime->fit_height(terrain_, object->model().model(), clock);
        if ((type.flags & can_hover) != 0) {
            const auto sea = static_cast<uint32_t>(simulation_.sea_level) << 16;
            if (unit.position[1] < sea)
                unit.position[1] = sea;
        }
    }
}

sim::spatial_state::Unit& Match::project_spatial(sim::unit_spawn::Slot& slot) {
    auto& s = spatial_units_.at(slot.unit_index);
    s.id = slot.unit_index;
    s.position = slot.unit->position;
    const auto& unit = slot.record;
    const auto& owner = *oa::world_unit_owner(&state(), &unit);
    s.cell = {unit.cell_x, unit.cell_z};
    s.footprint = {unit.footprint_x, unit.footprint_z};
    // object_present means the unit has a movement object; a building has
    // none, even once finished, and its footprint is always a wall.
    s.object_present = movement_[slot.unit_index] != nullptr;
    s.owner_object_present = owner.in_use != 0;
    s.owner_status = owner.status;
    s.spatial_link_locked = unit.attach_parent != 0;
    s.first_attachment = link_first_child(unit);
    s.next_in_bucket = link_next(unit);
    if (const auto* metadata = fields(slot).runtime_metadata) {
        s.max_slope = metadata->max_slope;
        s.max_water_slope = metadata->max_water_slope;
        s.max_water_depth = metadata->max_water_depth;
        s.min_water_depth = metadata->min_water_depth;
    }
    for (const auto& type : world_.types)
        if (&type.simulation == slot.unit->type) {
            s.bm_code = std::bit_cast<int8_t>(type.bm_code);
            break;
        }
    s.flags = slot.unit->flags;
    s.yard_open = (slot.record.build_flags & 4) != 0;
    s.yard_mask = fields(slot).yard_mask;
    return s;
}

void Match::prepare_spatial_state() {
    auto& world = state();
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        auto& projected = spatial_units_[i];
        const auto& unit = world.units[i];
        projected.flags = unit.flags;
        projected.next_in_bucket = link_next(unit);
        projected.first_attachment = link_first_child(unit);
        if (const auto* owner = oa::world_unit_owner(&world, &unit)) {
            projected.owner_object_present = owner->in_use != 0;
            projected.owner_status = owner->status;
        }
    }
}

void Match::synchronize_spatial_state() {
    auto& world = state();
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        auto& unit = world.units[i];
        unit.flags = spatial_units_[i].flags;
        if (!unit.attach_parent)
            unit.attach_next = oa::oa_unit_ref_from_slot(spatial_units_[i].next_in_bucket);
    }
}

const formats::objects3d::UnitTypeBounds*
Match::bounds_for(const sim::simulation_state::Unit& unit) const {
    for (std::size_t i = 0; i < input_.types.size(); ++i)
        if (unit.type == &input_.types[i].simulation && type_bounds_[i])
            return &*type_bounds_[i];
    return nullptr;
}

void Match::place_unit(
    sim::unit_spawn::Slot& slot, int32_t x, int32_t y, int32_t z, uint8_t layer
) {
    const auto cell_for = [](int32_t world, int16_t footprint) {
        const auto biased =
            static_cast<uint32_t>(world) - static_cast<uint32_t>(footprint) * 0x80000u + 0x80000u;
        return static_cast<int16_t>(std::bit_cast<int32_t>(biased) >> 20);
    };
    const auto cell_x = cell_for(x, slot.record.footprint_x);
    const auto cell_z = cell_for(z, slot.record.footprint_z);
    const std::array position{
        static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(z)
    };
    if (cell_x == slot.record.cell_x && cell_z == slot.record.cell_z &&
        (slot.unit->flags & 3u) == layer) {
        slot.unit->position = position;
        slot.unit->flags |= OA_UNIT_FLAG_POSITION_DIRTY;
        return;
    }
    prepare_spatial_state();
    auto& projected = project_spatial(slot);
    spatial_.tick = simulation_.tick;
    const auto removed = sim::spatial_state::remove_occupancy(projected, spatial_, map_listeners_);
    synchronize_spatial_state();
    if (removed != sim::spatial_state::Error::none)
        throw std::runtime_error("occupancy removal rejected spatial state");
    slot.unit->position = position;
    slot.unit->flags = (slot.unit->flags & ~3u) | (layer & 3u);
    slot.record.cell_x = cell_x;
    slot.record.cell_z = cell_z;
    if (auto* ground = ground_runtime(slot.unit_index))
        ground->project_slot();
    register_occupancy(slot);
    slot.unit->flags |= OA_UNIT_FLAG_POSITION_DIRTY;
    update_moving_sight(slot);
}

void Match::refresh_restored_footprint(uint16_t index) {
    auto& slot = slots_.at(index);
    prepare_spatial_state();
    auto& projected = project_spatial(slot);
    spatial_.tick = simulation_.tick;
    const auto result =
        sim::spatial_state::refresh_footprint_occupancy(projected, spatial_, map_listeners_);
    synchronize_spatial_state();
    if (result != sim::spatial_state::Error::none)
        throw std::runtime_error("restored footprint refresh rejected spatial state");
}

void Match::register_occupancy(sim::unit_spawn::Slot& slot) {
    // Collision can change flags on previously inserted units as well as this unit.
    prepare_spatial_state();
    auto& s = project_spatial(slot);
    spatial_.tick = simulation_.tick;
    const auto result = sim::spatial_state::register_unit(s, spatial_, map_listeners_);
    if (slot.unit->object_present && movement_[slot.unit_index])
        movement_[slot.unit_index]->occupancy_changed_tick = s.object_tick;
    if (result != sim::spatial_state::Error::none)
        throw std::runtime_error(
            "unit spatial registration rejected branch/state: " +
            std::to_string(static_cast<int>(result))
        );
    synchronize_spatial_state();
}

void Match::notify_created(sim::unit_spawn::Slot& slot) {
    // Nothing is shared while the run flag (Game.session_flags bit 0) is
    // clear.
    if (!simulation_.run_flag)
        return;
    if (multiplayer.unit_created) {
        multiplayer.unit_created(multiplayer.context, slot.unit_index);
        return;
    }
    throw std::logic_error("unit creation shared without a multiplayer handler");
}

void Match::notify_finished(sim::unit_spawn::Slot&) {
    // A shared completion names the builder, which is not known here; the
    // other players take it from the unit's per-tick update.
    const bool shared = multiplayer.local_player_ticked != nullptr;
    if (simulation_.run_flag && !shared)
        throw std::logic_error("unit completion shared without a multiplayer handler");
}

void Match::set_activation(sim::unit_spawn::Slot& slot, uint8_t mask, bool enabled) {
    auto wake = [this, &slot](uint32_t event) { wake_target_observers(*slot.unit, event); };
    Activation host(slot, instance(slot.unit_index), services_, wake, simulation_, multiplayer);
    sim::unit_activation::change(slot.record.state_flags, slot.unit_index, mask, enabled, host);
}

std::span<const uint8_t> Match::player_coverage(uint8_t owner) const {
    if (owner >= other_player_coverage_.size())
        throw std::out_of_range("coverage owner outside player table");
    return owner == input_.viewpoint_player
               ? std::span<const uint8_t>(sight_.coverage)
               : std::span<const uint8_t>(other_player_coverage_[owner]);
}

bool Match::unit_visible(uint8_t player, uint16_t index) const {
    if (player >= simulation_.players.size())
        throw std::out_of_range("visibility owner outside player table");
    const auto& slot = slots_.at(index);
    const auto& unit = *slot.unit;
    if (unit.owner == &simulation_.players[player])
        return true;
    if (slot.record.state_flags & 4)
        return false;
    const formats::objects3d::UnitTypeBounds* bounds = nullptr;
    for (std::size_t i = 0; i < input_.types.size(); ++i)
        if (unit.type == &input_.types[i].simulation && type_bounds_[i]) {
            bounds = &*type_bounds_[i];
            break;
        }
    if (!bounds)
        throw std::logic_error("visibility query requires resolved unit bounds");
    auto point = unit.position;
    point[0] += static_cast<uint32_t>(bounds->bounds_min_x);
    point[1] += static_cast<uint32_t>(bounds->model_height);
    point[2] += static_cast<uint32_t>(bounds->bounds_min_z);
    if (!(unit.flags & 0x200u) &&
        std::bit_cast<int32_t>(point[1]) < static_cast<int32_t>(simulation_.sea_level) * 65536)
        return false;
    if (point_visible(player, point))
        return true;
    point[0] += static_cast<uint32_t>(bounds->size_x);
    if (point_visible(player, point))
        return true;
    point[2] += static_cast<uint32_t>(bounds->size_z);
    point[1] -= static_cast<uint32_t>(bounds->size_y);
    if (point_visible(player, point))
        return true;
    point[0] -= static_cast<uint32_t>(bounds->size_x);
    return point_visible(player, point);
}

namespace {
// The sight cell under a 16.16 point, raised by half its height, or nothing
// past the grid (a negative cell fails the unsigned test).
std::optional<std::size_t> sight_cell(
    const sim::visibility_state::PlayerSightGrid& sight, const std::array<uint32_t, 3>& position
) {
    const auto x = static_cast<int32_t>(low(position[0] >> 16)) >> 5;
    const auto z = (static_cast<int32_t>(low(position[2] >> 16)) -
                    (static_cast<int32_t>(low(position[1] >> 16)) >> 1)) >>
                   5;
    if (static_cast<uint32_t>(x) >= static_cast<uint32_t>(sight.width) ||
        static_cast<uint32_t>(z) >= static_cast<uint32_t>(sight.height))
        return std::nullopt;
    return static_cast<std::size_t>(z) * static_cast<std::size_t>(sight.width) +
           static_cast<std::size_t>(x);
}
} // namespace

bool Match::point_visible(uint8_t player, const std::array<uint32_t, 3>& position) const {
    const auto coverage = player_coverage(player);
    if ((state().game.visibility_flags & sim::visibility_state::update_sight_grid) == 0)
        return point_mapped(position);
    const auto index = sight_cell(sight_, position);
    return index && coverage[*index] != 0;
}

bool Match::cell_or_footprint_corner_visible(
    uint8_t player,
    int16_t cell_x,
    int16_t cell_z,
    int16_t footprint_x,
    int16_t footprint_z,
    int16_t height
) const {
    constexpr int32_t units_per_cell = 16;
    // The whole-unit words go in the high halves; the fractions are zero.
    const auto point = [&](int32_t x, int32_t z) {
        return std::array<uint32_t, 3>{
            static_cast<uint32_t>(static_cast<uint16_t>(x)) << 16U,
            static_cast<uint32_t>(static_cast<uint16_t>(height)) << 16U,
            static_cast<uint32_t>(static_cast<uint16_t>(z)) << 16U
        };
    };
    const auto x = cell_x * units_per_cell;
    const auto z = cell_z * units_per_cell;
    return point_visible(player, point(x, z)) ||
           point_visible(
               player, point(x + footprint_x * units_per_cell, z + footprint_z * units_per_cell)
           );
}

bool Match::point_mapped(const std::array<uint32_t, 3>& position) const {
    const auto index = sight_cell(sight_, position);
    if (!index)
        return false;
    // Deliberately the viewpoint bit, not the queried player's bit.
    return (sight_.player_bits[*index] & (1u << sight_.viewpoint_player)) != 0;
}

sim::visibility_state::SightContext Match::sight_context() {
    sim::visibility_state::SightContext context{};
    context.grid = &sight_;
    for (std::size_t player = 0; player < other_player_coverage_.size(); ++player)
        context.coverage[player] = player == input_.viewpoint_player
                                       ? std::span<uint8_t>(sight_.coverage)
                                       : std::span<uint8_t>(other_player_coverage_[player]);
    context.masks = input_.sight_masks;
    context.altitude = input_.altitude_sight ? &*input_.altitude_sight : nullptr;
    context.visibility_flags = state().game.visibility_flags;
    context.minimum_height_cell = simulation_.sea_level;
    return context;
}

void Match::update_sight(sim::unit_spawn::Slot& slot) {
    auto& world = state();
    const auto* def = oa::world_unit_def_of(&world, &slot.record);
    const auto* owner = oa::world_unit_owner(&world, &slot.record);
    if (def == nullptr || owner == nullptr)
        throw std::logic_error("sight stamp requires the unit's type and owner");
    auto context = sight_context();
    sim::visibility_state::stamp_unit_sight(slot.record, *def, *owner, context);
}

void Match::update_moving_sight(sim::unit_spawn::Slot& slot) {
    auto& world = state();
    const auto* def = oa::world_unit_def_of(&world, &slot.record);
    const auto* owner = oa::world_unit_owner(&world, &slot.record);
    if (def == nullptr || owner == nullptr)
        throw std::logic_error("sight stamp requires the unit's type and owner");
    auto context = sight_context();
    sim::visibility_state::move_unit_sight(slot.record, *def, *owner, context);
}

void Match::update_player_sight(const oa::Player& player) {
    auto& world = state();
    uint32_t count = 0;
    const oa::Unit* first = oa::world_player_units(&world, &player, &count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto index = oa::world_unit_slot(&world, &first[i]);
        if ((first[i].flags & OA_UNIT_FLAG_LIVE) != 0 && index < slots_.size())
            update_moving_sight(slots_[index]);
    }
}

void Match::reset_sight_buffers(bool refill_mapped) {
    auto& game = state().game;
    if (refill_mapped) {
        const bool mapping = (game.visibility_flags & sim::visibility_state::terrain_mapping) != 0;
        std::fill(
            sight_.player_bits.begin(),
            sight_.player_bits.end(),
            mapping ? mapped_by_nobody : mapped_by_all
        );
    }
    const bool line_of_sight =
        (game.visibility_flags & sim::visibility_state::update_sight_grid) != 0;
    for (uint8_t index = 0; index < OA_PLAYER_COUNT; ++index) {
        if (!sim::simulation_state::player_slot_active(index, game.players[index]))
            continue;
        auto& coverage =
            index == input_.viewpoint_player ? sight_.coverage : other_player_coverage_[index];
        std::fill(coverage.begin(), coverage.end(), line_of_sight ? unseen : seen_once);
    }
    for (uint32_t slot = 1; slot < state().unit_slot_count; ++slot)
        if (state().units[slot].type_index != 0)
            update_sight(slots_[slot]);
    game.visibility_flags =
        static_cast<uint8_t>(game.visibility_flags & ~OA_VISIBILITY_FOG_MASK_CURRENT);
    game.radar_blink_flags = static_cast<uint16_t>(game.radar_blink_flags | OA_RADAR_MAPPED_DIRTY);
}

void Match::notify_scenario_created(sim::unit_spawn::Slot& slot) {
    sim::scenario::notify_unit_created(scenario_, {reinterpret_cast<uintptr_t>(slot.unit)});
}

std::array<uint32_t, 3> Match::piece_world_position(sim::unit_spawn::Slot& slot, uint32_t piece) {
    auto* unit = instance(slot.unit_index);
    if (!unit)
        throw std::logic_error("piece query before instance");
    return unit->piece_world(piece);
}

uint16_t Match::direction_to(uint32_t x, uint32_t z) {
    return base::game_math::direction(std::bit_cast<int32_t>(x), std::bit_cast<int32_t>(z));
}

uint32_t Match::distance(uint32_t x, uint32_t z) {
    return base::game_math::distance(std::bit_cast<int32_t>(x), std::bit_cast<int32_t>(z));
}

int32_t Match::sample_terrain_height(uint32_t x, uint32_t z) {
    return terrain_.height(std::bit_cast<int32_t>(x), std::bit_cast<int32_t>(z));
}

std::vector<Match::NanoLaser> Match::nano_lasers() const {
    std::vector<NanoLaser> beams;
    auto add = [&](const std::array<uint32_t, 3>& from, const sim::simulation_state::Unit& target) {
        const formats::objects3d::UnitTypeBounds* bounds = nullptr;
        for (std::size_t i = 0; i < input_.types.size(); ++i)
            if (target.type == &input_.types[i].simulation && type_bounds_[i]) {
                bounds = &*type_bounds_[i];
                break;
            }
        std::array<int32_t, 3> min_p{
            std::bit_cast<int32_t>(target.position[0]),
            std::bit_cast<int32_t>(target.position[1]),
            std::bit_cast<int32_t>(target.position[2])
        };
        std::array<int32_t, 3> max_p = min_p;
        if (bounds) {
            min_p[0] += bounds->bounds_min_x;
            min_p[1] += bounds->bounds_min_y;
            min_p[2] += bounds->bounds_min_z;
            max_p[0] += bounds->bounds_max_x;
            max_p[1] += bounds->model_height;
            max_p[2] += bounds->bounds_max_z;
        }
        NanoLaser beam;
        beam.from = from;
        for (int i = 0; i < 3; ++i) {
            const auto delta =
                max_p[static_cast<std::size_t>(i)] - min_p[static_cast<std::size_t>(i)];
            // Origin = min + 4/11 delta, extent = 3/11 delta.
            beam.to_origin[static_cast<std::size_t>(i)] =
                (delta * 4) / 11 + min_p[static_cast<std::size_t>(i)];
            beam.to_extent[static_cast<std::size_t>(i)] = (delta * 3) / 11;
        }
        beams.push_back(beam);
    };
    auto nano_from = [&](const sim::unit_spawn::Slot& builder) -> std::array<uint32_t, 3> {
        try {
            auto* inst = const_cast<Match*>(this)->instance(builder.unit_index);
            if (inst)
                return inst->query_nano_world();
        } catch (const std::exception&) {
        }
        return builder.unit->position;
    };
    for (const auto& entry : orders_) {
        if (!entry || !entry->unit || !entry->construction.target)
            continue;
        const sim::unit_spawn::Slot* builder = nullptr;
        for (const auto& slot : slots_)
            if (slot.unit == entry->unit) {
                builder = &slot;
                break;
            }
        if (entry->order.kind == get_built_kind) {
            const sim::unit_spawn::Slot* worker = nullptr;
            for (const auto& slot : slots_)
                if (slot.unit == entry->construction.target) {
                    worker = &slot;
                    break;
                }
            add(worker ? nano_from(*worker) : entry->construction.target->position, *entry->unit);
            continue;
        }
        add(builder ? nano_from(*builder) : entry->unit->position, *entry->construction.target);
    }
    for (const auto& slot : world_.slots) {
        if (slot.unit == nullptr || slot.record.build_remaining == 0.0F)
            continue;
        for (const auto& builder : world_.slots) {
            if (builder.unit == nullptr || builder.unit_index == slot.unit_index)
                continue;
            if (builder.record.owner_index != slot.record.owner_index ||
                (builder.record.build_flags & 1) == 0)
                continue;
            add(nano_from(builder), *slot.unit);
        }
    }
    return beams;
}

void Match::set_yard_open(sim::unit_spawn::Slot& slot, int32_t value) {
    prepare_spatial_state();
    auto& unit = project_spatial(slot);
    (void)sim::spatial_state::change_yard(unit, value, spatial_, map_listeners_);
    for (std::size_t i = 0; i < slots_.size(); ++i)
        state().units[i].flags = spatial_units_[i].flags;
    slot.record.build_flags =
        static_cast<uint8_t>((slot.record.build_flags & ~4u) | (unit.yard_open ? 4u : 0u));
}

uint32_t Match::loaded_child_count(uint16_t index) const {
    const auto& unit = match_unit(*this, index);
    return static_cast<uint32_t>(sim::unit_spawn::attached_child_count(state(), unit));
}
} // namespace oa::sim::match_runtime
