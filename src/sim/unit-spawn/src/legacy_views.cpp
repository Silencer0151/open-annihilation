// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_spawn/legacy_views.hpp"

#include <algorithm>
#include <cstdint>

namespace oa::sim::unit_spawn::legacy {
TypeField& TypeField::operator=(sim::simulation_state::UnitType* target) {
    if (!target) {
        ref_ = 0;
        return *this;
    }
    for (std::size_t i = 0; i < types_.size(); ++i)
        if (&types_[i].simulation == target) {
            ref_ = oa::oa_ref_from_index(static_cast<uint32_t>(i));
            return *this;
        }
    ref_ = 0;
    return *this;
}

TypeField::operator sim::simulation_state::UnitType*() const noexcept {
    return ref_ && ref_ <= types_.size() ? &types_[ref_ - 1u].simulation : nullptr;
}
} // namespace oa::sim::unit_spawn::legacy

namespace oa::sim::simulation_state {
Unit::Unit(
    oa::Unit& u, OrderQueue& q, std::span<sim::unit_spawn::Type> types, std::span<Player> players
) noexcept
    : record(u), orders(q), object_present(u.movement), primary(q.primary), secondary(q.secondary),
      position(u.position),
      type(sim::unit_spawn::legacy::Field<oa_ref32>(u, &oa::Unit::def), types),
      owner(sim::unit_spawn::legacy::Field<oa_ref32>(u, &oa::Unit::owner), players),
      script_present(u, &oa::Unit::script),
      type_index(sim::unit_spawn::legacy::as<int16_t>(u.type_index)), squad(u.squad),
      events(u.events), damage_kind(u.damage_kind), health_percent(u.health_percent),
      previous_health_percent(u.previous_health_percent), damage_countdown(u.damage_countdown),
      capture_cooldown(u, &oa::Unit::capture_cooldown), health(u.health),
      state_flags(u.state_flags), flags(u.flags) {
}

Player::Player(oa::Player& p) noexcept
    : record(p), present(p, &oa::Player::in_use), machine_group(p, &oa::Player::machine_group),
      status(p.status), index(p.index) {
}

namespace {
template <std::size_t... I>
std::array<Player, 10> player_views(oa::World& w, std::index_sequence<I...>) {
    return {Player(w.game.players[I])...};
}
} // namespace

World::World(oa::World& w) noexcept
    : record(w), players(player_views(w, std::make_index_sequence<10>{})),
      tick(w.game, &oa::Game::tick), active_units(w.game, &oa::Game::active_unit_count),
      sea_level(w.game.sea_level), run_flag(w.game.session_flags, 1),
      periodic_flag(w.game.periodic_flags, 2),
      periodic_countdown(w.game, &oa::Game::periodic_countdown),
      environment_enabled(w.environment_enabled), environment_damage(w.environment_damage) {
}
} // namespace oa::sim::simulation_state

namespace oa::sim::unit_spawn {
Slot::Slot(sim::simulation_state::Unit& view, SlotAssets& assets) noexcept
    : record(view.record), unit(&view), unit_index(view.record.id),
      squad(sim::unit_spawn::legacy::as<uint32_t>(view.record.squad)), flags2(view.record.flags2),
      cleared_on_unfinished_spawn(view.record.cleared_on_unfinished_spawn),
      build_remaining(view.record.build_remaining),
      decloak_until_tick(view.record.decloak_until_tick),
      last_attacker_id(view.record.last_attacker_id),
      bank(sim::unit_spawn::legacy::as<uint16_t>(view.record.bank)), yaw(view.record.heading),
      pitch(sim::unit_spawn::legacy::as<uint16_t>(view.record.pitch)),
      sight_center_x(view.record.sight_center_x), sight_center_z(view.record.sight_center_z),
      bob_phase(view.record.bob_phase), veteran_level(view.record.veteran_level),
      state_flags(view.record.state_flags), build_flags(view.record.build_flags),
      last_attacker_owner(view.record.last_attacker_owner), sight_band(view.record.sight_band),
      attach_piece(view.record.attach_piece), owner_index(view.record.owner_index),
      weapon_slots_initialized(assets.weapon_slots_initialized),
      script_instance(assets.script_instance), model_instance(assets.model_instance),
      movement_object(assets.movement_object) {
}

PlayerRange::PlayerRange(oa::World&, oa::Player& p, PlayerSetupState& setup) noexcept
    : record(p), current_count(p, &oa::Player::unit_count),
      total_created(p, &oa::Player::units_created), setup_side(setup.side),
      setup_color(setup.color), resource_flags(p.resource_flags), energy(p, &oa::Player::energy),
      metal(p, &oa::Player::metal), energy_produced(p, &oa::Player::energy_produced),
      energy_consumed(p, &oa::Player::energy_requested),
      metal_produced(p, &oa::Player::metal_produced),
      metal_consumed(p, &oa::Player::metal_requested), energy_cap(p, &oa::Player::energy_storage),
      metal_cap(p, &oa::Player::metal_storage),
      energy_harvested(p, &oa::Player::energy_produced_total),
      metal_harvested(p, &oa::Player::metal_produced_total),
      cumulative_energy_consumed(p, &oa::Player::energy_requested_total),
      cumulative_metal_consumed(p, &oa::Player::metal_requested_total),
      cumulative_energy_wasted(p, &oa::Player::energy_wasted_total),
      cumulative_metal_wasted(p, &oa::Player::metal_wasted_total),
      shared_energy(p, &oa::Player::shared_energy_storage),
      shared_metal(p, &oa::Player::shared_metal_storage) {
}

namespace {
template <std::size_t... I>
std::array<PlayerRange, 10>
range_views(oa::World& w, std::span<PlayerSetupState> setups, std::index_sequence<I...>) {
    return {PlayerRange(w, w.game.players[I], setups[I])...};
}
} // namespace

World::World(
    oa::World& w,
    sim::simulation_state::World& simulation_view,
    std::span<Type> type_table,
    std::span<PlayerSetupState> setups
)
    : record(w), simulation(&simulation_view), types(type_table),
      players(range_views(w, setups, std::make_index_sequence<10>{})),
      cycle_unit_id(w.game, &oa::Game::cycle_unit_id), per_player_limit(w.game.units_per_player),
      total_slots(w.game, &oa::Game::unit_slot_count), viewpoint_player(w.game.viewpoint_player) {
}

LegacyViews::LegacyViews(
    oa::World& w,
    std::span<Type> types,
    std::span<SlotAssets> assets,
    std::span<PlayerSetupState> setups,
    std::span<sim::simulation_state::OrderQueue> orders
)
    : record_(w), simulation_(w),
      world_(
          w,
          simulation_,
          types,
          setups.size() < OA_PLAYER_COUNT ? std::span<PlayerSetupState>(spare_setups_) : setups
      ) {
    const auto viewed =
        std::min<std::size_t>(w.unit_slot_count, std::min(assets.size(), orders.size()));
    units_.reserve(viewed);
    slots_.reserve(viewed);
    for (uint32_t i = 0; i < viewed; ++i) {
        units_.emplace_back(w.units[i], orders[i], types, std::span(simulation_.players));
        slots_.emplace_back(units_.back(), assets[i]);
    }
    world_.slots = slots_;
    bind_player_ranges();
}

void LegacyViews::bind_player_ranges() {
    for (std::size_t player = 0; player < OA_PLAYER_COUNT; ++player) {
        uint32_t count = 0;
        auto* first = oa::world_player_units(&record_, &record_.game.players[player], &count);
        auto slot = first ? oa::world_unit_slot(&record_, first) : 0u;
        // A range past the viewed slots is viewed as empty.
        if (slot > units_.size() || count > units_.size() - slot) {
            slot = 0;
            count = 0;
        }
        simulation_.players[player].units = std::span(units_).subspan(slot, count);
        world_.players[player].first = slot;
        world_.players[player].count = count;
    }
}

Slot& LegacyViews::slot(const oa::Unit& unit) noexcept {
    const auto index = oa::world_unit_slot(&record_, &unit);
    return index < slots_.size() ? slots_[index] : slots_.front();
}

LegacyWorld::LegacyWorld(std::size_t unit_slots, std::size_t type_count)
    : world_(std::make_unique<oa::World>()), units_(unit_slots), defs_(type_count),
      types_(type_count), assets_(unit_slots), orders_(unit_slots),
      tables_{types_, assets_, setups_} {
    world_->units = units_.data();
    world_->unit_slot_count = static_cast<uint32_t>(unit_slots);
    world_->unit_defs = defs_.data();
    world_->unit_def_count = static_cast<uint32_t>(type_count);
    for (std::size_t i = 0; i < unit_slots; ++i)
        units_[i].id = static_cast<uint16_t>(i);
    load_types();
    views_ = std::make_unique<LegacyViews>(*world_, types_, assets_, setups_, orders_);
}

void LegacyWorld::load_types() {
    for (std::size_t i = 0; i < types_.size(); ++i)
        load_unit_def(types_[i], defs_[i]);
}

void LegacyWorld::range(std::size_t index, std::size_t first, std::size_t count) {
    if (index >= OA_PLAYER_COUNT)
        return;
    auto& player = world_->game.players[index];
    player.first_unit = count ? oa::oa_ref_from_index(static_cast<uint32_t>(first)) : 0u;
    player.last_unit = count ? oa::oa_ref_from_index(static_cast<uint32_t>(first + count - 1)) : 0u;
    views_->bind_player_ranges();
}
} // namespace oa::sim::unit_spawn
