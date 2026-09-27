// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/match_runtime/spawn_bridge.hpp"
#include <cstdint>
#include <stdexcept>

namespace oa::sim::match_runtime {
SpawnBridge::SpawnBridge(
    oa::World& state,
    sim::unit_spawn::Tables& tables,
    sim::unit_spawn::LegacyViews& views,
    std::span<const sim::unit_spawn::LoadedType> loaded,
    UnitValueHost& values,
    Effects& effects,
    SpawnSubsystems& subsystems,
    SharedRandom& random,
    int32_t scale
)
    : state_(state), tables_(tables), views_(views), loaded_(loaded), values_(values),
      effects_(effects), subsystems_(subsystems), random_(random), clock_scale_(scale),
      runtime_(state.unit_slot_count) {
    if (loaded.size() != tables.types.size() || tables.types.size() != state.unit_def_count)
        throw std::invalid_argument("spawn runtime tables do not match world");
}

sim::unit_spawn::Slot* SpawnBridge::create(const sim::unit_spawn::Request& request) {
    auto* previous = active_;
    try {
        auto* result = sim::unit_spawn::create(state_, tables_, request, *this);
        active_ = previous;
        return result ? &slot(*result) : nullptr;
    } catch (...) {
        active_ = previous;
        throw;
    }
}

sim::unit_spawn::StartResult SpawnBridge::start_player(
    uint8_t player,
    const sim::unit_spawn::PlayerSetup& setup,
    std::span<const sim::unit_spawn::StartMarker> markers,
    int32_t index,
    uint8_t local,
    int32_t width,
    int32_t height,
    sim::unit_spawn::StartHost& start
) {
    auto* previous = active_;
    try {
        auto result = sim::unit_spawn::spawn_player_commander(
            state_, tables_, player, setup, markers, index, local, width, height, *this, start
        );
        active_ = previous;
        return result;
    } catch (...) {
        active_ = previous;
        throw;
    }
}

SlotRuntime& SpawnBridge::runtime(sim::unit_spawn::Slot& slot) {
    const auto index = slot.unit_index;
    if (index >= runtime_.size() || &views_.slots()[index] != &slot)
        throw std::out_of_range("spawn runtime slot does not belong to pool");
    return runtime_[index];
}

SlotRuntime& SpawnBridge::runtime(const oa::Unit& unit) {
    return runtime(slot(unit));
}

void SpawnBridge::tick_script(sim::unit_spawn::Slot& slot, uint32_t elapsed) {
    auto& state = runtime(slot);
    if (!state.instance)
        throw std::logic_error("unit model/script runtime is not constructed");
    state.instance->tick(elapsed);
}

SpawnBridge::PendingScript& SpawnBridge::pending(sim::unit_spawn::AssetHandle handle) {
    for (auto& script : scripts_)
        if (reinterpret_cast<sim::unit_spawn::AssetHandle>(script.get()) == handle)
            return *script;
    throw std::out_of_range("script handle is not owned by match");
}

const sim::unit_spawn::LoadedType&
SpawnBridge::asset_model(sim::unit_spawn::AssetHandle handle) const {
    for (const auto& loaded : loaded_)
        if (loaded.model &&
            reinterpret_cast<sim::unit_spawn::AssetHandle>(loaded.model.get()) == handle)
            return loaded;
    throw std::out_of_range("model asset handle is not loaded");
}

std::shared_ptr<const formats::cob::CobProgram>
SpawnBridge::asset_script(sim::unit_spawn::AssetHandle handle) const {
    for (const auto& loaded : loaded_)
        if (loaded.script &&
            reinterpret_cast<sim::unit_spawn::AssetHandle>(loaded.script.get()) == handle)
            return loaded.script;
    throw std::out_of_range("COB asset handle is not loaded");
}

uint32_t SpawnBridge::random_bounded(uint32_t bound) {
    return random_.bounded(bound);
}

void SpawnBridge::init_weapon_target(oa::Unit& unit, uint32_t index) {
    active_ = &slot(unit);
    if (index >= OA_UNIT_WEAPON_COUNT)
        throw std::out_of_range("weapon slot outside the unit");
    auto& weapon = unit.weapons[index];
    weapon.target_a = 0;
    weapon.target_b = OA_UNIT_TARGET_IS_UNIT;
}

void SpawnBridge::reset_weapon_targets(oa::Unit& unit, uint8_t index) {
    if (index == 3) {
        reset_weapon_targets(unit, 0);
        reset_weapon_targets(unit, 1);
        index = 2;
    }
    if (index >= OA_UNIT_WEAPON_COUNT)
        throw std::out_of_range("weapon slot outside the unit");
    auto& weapon = unit.weapons[index];
    if ((weapon.flags & OA_UNIT_WEAPON_ENABLED) && !(weapon.flags & OA_UNIT_WEAPON_RETALIATE)) {
        weapon.flags |= OA_UNIT_WEAPON_RETALIATE;
        subsystems_.stop_weapon(slot(unit), index);
    }
}

void SpawnBridge::init_unit_economy(oa::Unit& unit, uint8_t owner) {
    auto* player = oa::world_player(&state_, owner);
    if (!player)
        throw std::out_of_range("unit economy owner outside ten players");
    unit.economy = {};
    unit.economy.player = oa::world_player_ref_of(&state_, player);
}

void SpawnBridge::assign_squad(oa::Unit& unit, uint32_t group) {
    active_ = &slot(unit);
    subsystems_.assign_squad(*active_, group);
}

sim::unit_spawn::AssetHandle
SpawnBridge::create_model_instance(sim::unit_spawn::AssetHandle model) {
    if (!active_)
        throw std::logic_error("plain model creation outside unit spawn");
    auto loaded = asset_model(model);
    loaded.script.reset();
    loaded.type.cob = 0;
    auto& state = runtime(*active_);
    state.instance = std::make_unique<UnitInstance>(
        loaded, *active_, views_.slots(), state_, values_, effects_, random_, clock_scale_
    );
    ++state.instance_generation;
    return reinterpret_cast<sim::unit_spawn::AssetHandle>(state.instance.get());
}

void SpawnBridge::model_owner(oa::Unit& unit) {
    const auto& state = runtime(unit);
    if (!state.instance ||
        state.instance->model().owner_token() != reinterpret_cast<uintptr_t>(slot(unit).unit))
        throw std::logic_error("plain model owner not bound");
}

sim::unit_spawn::AssetHandle SpawnBridge::allocate_script() {
    scripts_.push_back(std::make_unique<PendingScript>());
    return reinterpret_cast<sim::unit_spawn::AssetHandle>(scripts_.back().get());
}

void SpawnBridge::load_script_state(
    sim::unit_spawn::AssetHandle script, sim::unit_spawn::AssetHandle cob
) {
    pending(script).program = asset_script(cob);
}

sim::unit_spawn::AssetHandle SpawnBridge::create_scripted_model(
    sim::unit_spawn::AssetHandle model, sim::unit_spawn::AssetHandle cob, oa::Unit& unit
) {
    auto loaded = asset_model(model);
    loaded.script = asset_script(cob);
    loaded.type.cob = cob;
    auto& state = runtime(unit);
    state.instance = std::make_unique<UnitInstance>(
        loaded, slot(unit), views_.slots(), state_, values_, effects_, random_, clock_scale_
    );
    ++state.instance_generation;
    return reinterpret_cast<sim::unit_spawn::AssetHandle>(state.instance.get());
}

void SpawnBridge::bind_script_model(
    sim::unit_spawn::AssetHandle script, sim::unit_spawn::AssetHandle model
) {
    auto& record = pending(script);
    for (auto& state : runtime_)
        if (state.instance &&
            reinterpret_cast<sim::unit_spawn::AssetHandle>(state.instance.get()) == model) {
            if (!record.program || !state.instance->script() ||
                &state.instance->script()->program() != record.program.get())
                throw std::logic_error("model and VM COB mismatch");
            record.bound = state.instance.get();
            return;
        }
    throw std::out_of_range("model runtime handle is not owned by match");
}

void SpawnBridge::call_script_create(sim::unit_spawn::AssetHandle script) {
    auto& record = pending(script);
    if (!record.bound)
        throw std::logic_error("Create before model binding");
    record.bound->create();
}

void SpawnBridge::model_reset(oa::Unit& unit) {
    runtime(unit).cached_image_word = 0;
}

void SpawnBridge::initialize_weapons(oa::Unit& unit) {
    subsystems_.initialize_weapons(slot(unit), runtime(unit));
}

void SpawnBridge::initialize_extraction_rate(oa::Unit& unit) {
    subsystems_.initialize_extraction_rate(slot(unit), runtime(unit));
}

sim::unit_spawn::AssetHandle SpawnBridge::create_movement(oa::Unit& unit) {
    return subsystems_.create_movement(slot(unit));
}

void SpawnBridge::fit_spawn_height(oa::Unit& unit) {
    subsystems_.fit_spawn_height(slot(unit));
}

void SpawnBridge::register_occupancy(oa::Unit& unit) {
    subsystems_.register_occupancy(slot(unit));
}

void SpawnBridge::notify_created(oa::Unit& unit) {
    subsystems_.notify_created(slot(unit));
}

void SpawnBridge::notify_finished(oa::Unit& unit) {
    subsystems_.notify_finished(slot(unit));
}

void SpawnBridge::set_activation(oa::Unit& unit, bool mask, bool value) {
    subsystems_.set_activation(slot(unit), static_cast<uint8_t>(mask), value);
}

void SpawnBridge::update_sight(oa::Unit& unit) {
    subsystems_.update_sight(slot(unit));
}

void SpawnBridge::notify_scenario_created(oa::Unit& unit) {
    subsystems_.notify_scenario_created(slot(unit));
}
} // namespace oa::sim::match_runtime
