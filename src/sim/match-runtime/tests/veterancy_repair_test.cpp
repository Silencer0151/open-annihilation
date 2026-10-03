// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The veterancy and repair rules in a running match: a shot between veterans
// scaled by each side's own type, a repair order and natural regeneration at
// the exact rate, and the heal remainders as rule state the digest and saves
// carry. Each scenario also runs at 3.1c's rules, which it must keep.
#include "match_tick_access.hpp"
#include "combat_fixture.hpp"

#include "oa/sim/match_runtime/construction_orders.hpp"
#include "oa/sim/match_runtime/rule_state.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace {

using namespace combat_fixture;
namespace match_rules = oa::data::match_rules;
namespace runtime = oa::sim::match_runtime;

/// Returns options with veterancy.model on at its defaults and the fixture's
/// type given thresholds of its own: 10, 20, 30, 40 and 50 kills.
///
/// @return the options
Options veterancy_options() {
    Options options{};
    auto& model = options.rules.veterancy.model;
    model.enabled = true;
    model.level_source = match_rules::VeterancyModelLevelSource::thresholds;
    model.damage_taken_cap = 25;
    model.damage_dealt_cap = std::nullopt;
    model.reload_cap = 16;
    model.lead_after = match_rules::VeterancyModelLeadAfter::first_threshold;
    model.capture_level = match_rules::VeterancyModelCaptureLevel::extended;
    options.unit_type_rules.resize(2);
    options.unit_type_rules[1].veterancy_thresholds =
        match_rules::FixedList<uint16_t, match_rules::max_veterancy_thresholds>{10, 20, 30, 40, 50};
    return options;
}

/// Returns options with repair.rate exact-remainder and the self-heal rule at
/// their defaults.
///
/// @return the options
Options repair_options() {
    Options options{};
    options.rules.repair.rate.enabled = true;
    options.rules.repair.rate.mode = match_rules::RepairRateMode::exact_remainder;
    auto& heal = options.rules.repair.healtime_self_heal;
    heal.enabled = true;
    heal.skip_under_construction = true;
    heal.cadence = match_rules::RepairHealtimeSelfHealCadence::healtime_mask;
    heal.work_multiplier = 4;
    return options;
}

/// Fires the fixture's 50-damage gun from one unit straight into another.
///
/// @param f the fixture
/// @param shooter_kills the shooter's kills
/// @param target_kills the target's kills
/// @return the target's health after the hit
int32_t veteran_hit(Fixture& f, uint16_t shooter_kills, uint16_t target_kills) {
    CHECK(
        oa::sim::combat_state::install_weapon_text(
            f.weapons, test_gun_tdf("0.1", "areaofeffect=16;", 50)
        ) == 1
    );
    std::copy(
        f.weapons.records().begin(),
        f.weapons.records().end(),
        std::begin(f.match->state().game.weapon_defs)
    );
    auto& shooter = f.spawn(0, 64, 64);
    auto& target = f.spawn(1, 160, 64);
    shooter.record.veteran_level = shooter_kills;
    target.record.veteran_level = target_kills;
    oa::Projectile shot{};
    shot.def = oa::oa_ref_from_index(1);
    shot.source = oa::oa_unit_ref_from_slot(shooter.unit_index);
    shot.owner_index = shooter.record.owner_index;
    shot.position = target.record.position;
    f.match->detonate(shot, &target);
    return target.unit->health;
}

/// The shooter's bonus comes from its own type's thresholds and the victim's
/// reduction from the victim's: 30 kills are level 3 (118%) and 25 level 2
/// (92%), where 3.1c gives 130% and 80%.
void veteran_shot_scales_by_both_types() {
    Fixture plain;
    CHECK(veteran_hit(plain, 30, 25) == 1000 - 50 * 130 / 100 * 80 / 100);
    Fixture veteran(veterancy_options());
    CHECK(veteran_hit(veteran, 30, 25) == 1000 - 50 * 118 / 100 * 92 / 100);
    std::cout << "veteran shot scales by both types passed\n";
}

/// Readies a repairer with worker time 60 and a 1000-point patient half
/// down, with energy in store, and returns the repair order.
///
/// @param f the fixture
/// @param[out] repairer the repairing unit
/// @param[out] patient the unit repaired
/// @return the repair order, stepped from its repairing phase
oa::sim::simulation_state::Order& ready_repair(
    Fixture& f, oa::sim::unit_spawn::Slot*& repairer, oa::sim::unit_spawn::Slot*& patient
) {
    f.def.worker_time = 60;
    f.def.build_time = 100;
    f.def.build_cost_energy = 1000;
    f.match->reload_unit_defs();
    repairer = &f.spawn(0, 64, 64);
    patient = &f.spawn(0, 80, 64);
    patient->unit->health = 500;
    auto& player = f.match->state().game.players[0];
    player.energy = player.energy_storage = 100000.0F;
    auto& order = f.match->issue_repair(repairer->unit_index, patient->unit_index, false);
    order.phase = 3;
    return order;
}

/// Counts the repair steps that bring the patient to full health.
///
/// @param f the fixture
/// @return the steps, at most 1000
uint32_t repair_steps(Fixture& f) {
    oa::sim::unit_spawn::Slot* repairer = nullptr;
    oa::sim::unit_spawn::Slot* patient = nullptr;
    auto& order = ready_repair(f, repairer, patient);
    runtime::MatchTickAccess host(*f.match);
    uint32_t steps = 0;
    while (patient->unit->health < 1000 && steps < 1000) {
        order.wait_events = 0;
        (void)host.dispatch_mission(f.match->state(), repairer->record, order, 0);
        ++steps;
    }
    CHECK(patient->unit->health == 1000);
    return steps;
}

/// Worker time 60 repairs 2 x 1000 / 100 = 20 points a step under
/// exact-remainder: 25 steps from 500, where 3.1c takes 500.
void repair_order_heals_at_the_exact_rate() {
    Fixture plain;
    CHECK(repair_steps(plain) == 500);
    Fixture exact(repair_options());
    CHECK(repair_steps(exact) == 25);
    std::cout << "repair order heals at the exact rate passed\n";
}

/// Runs ticks and returns the health a unit gained, with the ticks on which
/// it gained any.
///
/// @param f the fixture
/// @param unit the unit
/// @param ticks ticks to run
/// @param[out] healed_ticks the ticks on which its health rose
/// @return the health gained
int32_t regenerate(
    Fixture& f, oa::sim::unit_spawn::Slot& unit, uint32_t ticks, std::vector<uint32_t>& healed_ticks
) {
    const int32_t start = unit.unit->health;
    for (uint32_t i = 0; i < ticks; ++i) {
        const int32_t before = unit.unit->health;
        f.run(1);
        if (unit.unit->health > before)
            healed_ticks.push_back(f.match->simulation().tick);
    }
    return unit.unit->health - start;
}

/// Readies a 1000-point unit at half health.
///
/// @param f the fixture
/// @param heal_time its type's heal time
/// @return the unit
oa::sim::unit_spawn::Slot& ready_regeneration(Fixture& f, int16_t heal_time) {
    f.def.build_time = 100;
    f.def.build_cost_energy = 1000;
    f.types[1].simulation.heal_time = heal_time;
    f.match->reload_unit_defs();
    auto& unit = f.spawn(0, 64, 64);
    unit.unit->health = 500;
    auto& player = f.match->state().game.players[0];
    player.energy = player.energy_storage = 100000.0F;
    return unit;
}

/// Heal time 3 regenerates every 4 ticks at trunc(3 x 32 / 30) = 3 work, 30
/// points a step through the exact repair, and not while unfinished. In 3.1c
/// heal time 7 regenerates 1 point every 8 ticks, finished or not, and heal
/// time 3 passes no work at all.
void regeneration_follows_the_heal_time() {
    Fixture plain;
    auto& base_unit = ready_regeneration(plain, 7);
    std::vector<uint32_t> base_ticks;
    CHECK(regenerate(plain, base_unit, 32, base_ticks) == 4);
    for (const auto tick : base_ticks)
        CHECK(tick % 8 == 0);

    Fixture idle;
    auto& idle_unit = ready_regeneration(idle, 3);
    std::vector<uint32_t> idle_ticks;
    CHECK(regenerate(idle, idle_unit, 32, idle_ticks) == 0);

    Fixture exact(repair_options());
    auto& unit = ready_regeneration(exact, 3);
    std::vector<uint32_t> ticks;
    CHECK(regenerate(exact, unit, 32, ticks) == 8 * 30);
    CHECK(ticks.size() == 8);
    for (const auto tick : ticks)
        CHECK(tick % 4 == 0);

    unit.record.build_remaining = 0.5F;
    ticks.clear();
    CHECK(regenerate(exact, unit, 32, ticks) == 0);
    base_unit.record.build_remaining = 0.5F;
    base_ticks.clear();
    CHECK(regenerate(plain, base_unit, 32, base_ticks) == 4);
    std::cout << "regeneration follows the heal time passed\n";
}

/// The heal remainders are rule state only under exact-remainder: they move
/// the digest as they change and a save restores them.
void remainders_are_rule_state() {
    Fixture plain;
    CHECK(plain.match->repair_remainders().empty());
    CHECK(plain.match->rule_state().count == 0);

    auto options = repair_options();
    Fixture exact(options);
    const auto* table = runtime::find_rule_state(exact.match->rule_state(), "repair-remainders");
    CHECK(table != nullptr);
    CHECK(exact.match->repair_remainders().size() == exact.match->world().slots.size());
    const auto before = exact.match->fold_rule_state(0);
    const std::vector<uint8_t> saved(
        table->bytes(table->context).begin(), table->bytes(table->context).end()
    );
    // A repair step whose division leaves a remainder carries it: worker
    // time 30 over build time 300 is 3 points and 100 over.
    oa::sim::unit_spawn::Slot* repairer = nullptr;
    oa::sim::unit_spawn::Slot* patient = nullptr;
    auto& order = ready_repair(exact, repairer, patient);
    exact.def.worker_time = 30;
    exact.def.build_time = 300;
    exact.match->reload_unit_defs();
    runtime::MatchTickAccess host(*exact.match);
    (void)host.dispatch_mission(exact.match->state(), repairer->record, order, 0);
    const auto& carried = exact.match->repair_remainders()[repairer->unit_index].targets[0];
    CHECK(carried.target == static_cast<uint32_t>(patient->unit_index) + 1U);
    CHECK(carried.remainder == 100);
    CHECK(exact.match->fold_rule_state(0) != before);
    // A save's bytes put the table back; bytes of another size are refused.
    CHECK(table->restore(table->context, saved));
    CHECK(exact.match->fold_rule_state(0) == before);
    CHECK(!table->restore(table->context, std::span<const uint8_t>(saved).first(8)));
    std::cout << "remainders are rule state passed\n";
}

} // namespace

int main() {
    try {
        veteran_shot_scales_by_both_types();
        repair_order_heals_at_the_exact_rate();
        regeneration_follows_the_heal_time();
        remainders_are_rule_state();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
