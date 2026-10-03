// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The repair step under each repair.rate mode: 3.1c's one point a step, at
// least one point a step, and the exact heal with its remainder carried per
// repairer and target.
#include "oa/sim/unit_health.hpp"
#include "oa/test/check.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {
using namespace oa::sim::unit_health;
namespace match_rules = oa::data::match_rules;

/// Counts what a repair step asks of its host.
struct Host final : RecoveryHost {
    EconomyDebit debit{};
    int heals{};

    EconomyDebit& energy_debit(Unit&) override { return debit; }

    bool target_is_live(const Unit&) override {
        ++heals;
        return true;
    }

    void apply_health_event(Unit&, const Unit*, const HealthEvent&) override {}

    bool target_owner_present(const Unit&) override { return false; }

    uint8_t target_owner_status(const Unit&) override { return 0; }

    RouteIdentity source_owner_route(const Unit&) override { return 0; }

    RouteIdentity fallback_route() override { return 0; }

    void share_health_event(RouteIdentity, const HealthEvent&) override {}
};

/// Returns rules with repair.rate in a mode.
///
/// @param mode the mode
/// @return the rules
match_rules::MatchRules rate_rules(match_rules::RepairRateMode mode) {
    match_rules::MatchRules rules{};
    rules.repair.rate.enabled = mode != match_rules::RepairRateMode::clamp_max_1;
    rules.repair.rate.mode = mode;
    return rules;
}

/// 3.1c heals one point a step and charges one energy.
void baseline_heals_one_point() {
    UnitType type{0x10000, 600.0F, 300, 1000};
    Unit repairer{5, 0, 0, 100, &type};
    Unit target{9, 0, 0, 500, &type};
    Host host;
    const auto step = recover_health(repairer, target, 2.0F, host);
    OA_CHECK(step.performed && step.health_amount == 1 && step.energy_amount == 1);
    OA_CHECK(target.health == 501 && host.debit.requested == 1.0F);
    std::cout << "baseline heals one point passed\n";
}

/// clamp-min-1 keeps 3.1c's rounded-up amounts, each at least 1.
void clamp_min_1_scales_with_worker_time() {
    const auto rules = rate_rules(match_rules::RepairRateMode::clamp_min_1);
    UnitType type{0x10000, 600.0F, 300, 1000};
    Unit repairer{5, 0, 0, 100, &type};
    Unit target{9, 0, 0, 500, &type};
    Host host;
    host.rules = {&rules};
    auto step = recover_health(repairer, target, 30.0F, host);
    OA_CHECK(step.performed && step.health_amount == 100 && step.energy_amount == 60);
    OA_CHECK(target.health == 600 && host.debit.requested == 60.0F);
    step = recover_health(repairer, target, 0.0F, host);
    OA_CHECK(step.performed && step.health_amount == 1 && step.energy_amount == 1);
    OA_CHECK(target.health == 601);
    std::cout << "clamp-min-1 scales with worker time passed\n";
}

/// exact-remainder heals trunc(rate) x maximum / build time and carries the
/// remainder, so the long-run rate is exact.
void exact_remainder_carries_the_rest() {
    const auto rules = rate_rules(match_rules::RepairRateMode::exact_remainder);
    std::vector<RepairRemainders> table(16);
    UnitType type{0x10000, 600.0F, 300, 1000};
    Unit repairer{5, 0, 0, 100, &type};
    Unit target{9, 0, 0, 100, &type};
    Host host;
    host.rules = {&rules};
    host.repair_remainders = table;
    // Worker time 30: 1000 / 300 is 3 and 100 over; every third step heals 4.
    const std::vector<int32_t> one{3, 3, 4, 3, 3, 4};
    for (const auto heal : one) {
        const auto before = target.health;
        const auto step = recover_health(repairer, target, 1.0F, host);
        OA_CHECK(step.performed && step.health_amount == heal && step.energy_amount == 2);
        OA_CHECK(target.health == before + heal);
    }
    OA_CHECK(target.health == 120 && host.debit.requested == 12.0F);
    OA_CHECK(table[5].targets[0].target == 10 && table[5].targets[0].remainder == 0);
    // Worker time 60: 6 and 200 over; 6, 7, 7 heal 20 every three steps.
    const std::vector<int32_t> two{6, 7, 7};
    for (const auto heal : two)
        OA_CHECK(recover_health(repairer, target, 2.0F, host).health_amount == heal);
    // Worker time 45 passes 1.5: the heal takes the whole part, the energy
    // the rate itself.
    const auto half = recover_health(repairer, target, 1.5F, host);
    OA_CHECK(half.health_amount == 3 && half.energy_amount == 3);
    std::cout << "exact remainder carries the rest passed\n";
}

/// The cases where the exact step pays or heals nothing.
void exact_remainder_edges() {
    const auto rules = rate_rules(match_rules::RepairRateMode::exact_remainder);
    std::vector<RepairRemainders> table(16);
    UnitType type{0x10000, 600.0F, 300, 1000};
    Unit repairer{5, 0, 0, 100, &type};
    Unit target{9, 0, 0, 100, &type};
    Host host;
    host.rules = {&rules};
    host.repair_remainders = table;
    // Refused energy: nothing healed, and the step has not happened.
    host.debit.gate = 2.0F;
    auto step = recover_health(repairer, target, 1.0F, host);
    OA_CHECK(!step.performed && step.health_amount == 0 && step.energy_amount == 2);
    OA_CHECK(target.health == 100 && host.debit.requested == 2.0F && host.heals == 0);
    OA_CHECK(table[5].targets[0].target == 0);
    // A rate below 1 (worker time under 30) pays and heals nothing.
    host = {};
    host.rules = {&rules};
    host.repair_remainders = table;
    step = recover_health(repairer, target, 0.5F, host);
    OA_CHECK(step.performed && step.health_amount == 0 && step.energy_amount == 1);
    OA_CHECK(target.health == 100 && host.debit.requested == 1.0F && host.heals == 0);
    // A zero or negative build time, or full health, does nothing at all.
    for (const int32_t build_time : {0, -5}) {
        type.build_time = build_time;
        host.debit = {};
        step = recover_health(repairer, target, 1.0F, host);
        OA_CHECK(!step.performed && step.energy_amount == 0 && host.debit.requested == 0.0F);
    }
    type.build_time = 300;
    target.health = 1000;
    step = recover_health(repairer, target, 1.0F, host);
    OA_CHECK(!step.performed && host.debit.requested == 0.0F);
    // A carried heal still short of a point heals nothing, but the step has
    // happened; the next one reaches the point.
    UnitType small{0x10000, 600.0F, 300, 200};
    Unit frail{9, 0, 0, 100, &small};
    host = {};
    host.rules = {&rules};
    table.assign(16, {});
    host.repair_remainders = table;
    step = recover_health(repairer, frail, 1.0F, host);
    OA_CHECK(step.performed && step.health_amount == 0 && host.heals == 0 && frail.health == 100);
    step = recover_health(repairer, frail, 1.0F, host);
    OA_CHECK(step.performed && step.health_amount == 1 && frail.health == 101);
    OA_CHECK(table[5].targets[0].remainder == 100);
    std::cout << "exact remainder edges passed\n";
}

/// A repairer with no entry in the table heals one more for any remainder,
/// at least 1; a heal is at most 0xffff.
void exact_remainder_without_a_table() {
    const auto rules = rate_rules(match_rules::RepairRateMode::exact_remainder);
    UnitType type{0x10000, 600.0F, 300, 1000};
    Unit repairer{0, 0, 0, 100, &type};
    Unit target{9, 0, 0, 100, &type};
    Host host;
    host.rules = {&rules};
    std::vector<RepairRemainders> table(16);
    host.repair_remainders = table;
    // Index 0 carries nothing: 3 and 100 over heals 4.
    OA_CHECK(recover_health(repairer, target, 1.0F, host).health_amount == 4);
    // Past the table: likewise.
    repairer.identity = 16;
    OA_CHECK(recover_health(repairer, target, 1.0F, host).health_amount == 4);
    // An exact division heals its quotient; a heal below one point heals 1.
    UnitType even{0x10000, 600.0F, 300, 900};
    Unit even_target{9, 0, 0, 100, &even};
    OA_CHECK(recover_health(repairer, even_target, 1.0F, host).health_amount == 3);
    UnitType tiny{0x10000, 600.0F, 300, 200};
    Unit tiny_target{9, 0, 0, 100, &tiny};
    OA_CHECK(recover_health(repairer, tiny_target, 1.0F, host).health_amount == 1);
    // An index read negative as 16 bits carries nothing either.
    std::vector<RepairRemainders> large(40000);
    host.repair_remainders = large;
    repairer.identity = 39000;
    OA_CHECK(recover_health(repairer, target, 1.0F, host).health_amount == 4);
    OA_CHECK(large[39000].targets[0].target == 0);
    // The heal is capped at 0xffff points, applied up to the maximum.
    UnitType huge{0x10000, 1.0F, 1, 30000};
    Unit hurt{9, 0, 0, 10, &huge};
    const auto step = recover_health(repairer, hurt, 3.0F, host);
    OA_CHECK(step.health_amount == 0xffff && hurt.health == 30000);
    std::cout << "exact remainder without a table passed\n";
}

/// A repairer carries remainders for two targets; a third takes the first
/// entry with nothing carried. Self-repair carries under the unit's own index.
void exact_remainder_keeps_two_targets() {
    const auto rules = rate_rules(match_rules::RepairRateMode::exact_remainder);
    std::vector<RepairRemainders> table(16);
    UnitType type{0x10000, 600.0F, 300, 1000};
    Unit repairer{5, 0, 0, 100, &type};
    Unit first{9, 0, 0, 100, &type};
    Unit second{10, 0, 0, 100, &type};
    Unit third{11, 0, 0, 100, &type};
    Host host;
    host.rules = {&rules};
    host.repair_remainders = table;
    (void)recover_health(repairer, first, 1.0F, host);
    (void)recover_health(repairer, second, 1.0F, host);
    (void)recover_health(repairer, second, 1.0F, host);
    OA_CHECK(table[5].targets[0].target == 10 && table[5].targets[0].remainder == 100);
    OA_CHECK(table[5].targets[1].target == 11 && table[5].targets[1].remainder == 200);
    (void)recover_health(repairer, third, 1.0F, host);
    OA_CHECK(table[5].targets[0].target == 12 && table[5].targets[0].remainder == 100);
    OA_CHECK(table[5].targets[1].target == 11 && table[5].targets[1].remainder == 200);
    // The first target lost its remainder and takes the first entry again.
    (void)recover_health(repairer, first, 1.0F, host);
    OA_CHECK(table[5].targets[0].target == 10 && table[5].targets[0].remainder == 100);
    // The second still carries 200: its next step reaches a point.
    OA_CHECK(recover_health(repairer, second, 1.0F, host).health_amount == 4);
    // Self-repair carries under the unit's own index.
    Unit self{7, 0, 0, 100, &type};
    (void)recover_health(self, self, 1.0F, host);
    OA_CHECK(table[7].targets[0].target == 8 && table[7].targets[0].remainder == 100);
    std::cout << "exact remainder keeps two targets passed\n";
}
} // namespace

int main() {
    baseline_heals_one_point();
    clamp_min_1_scales_with_worker_time();
    exact_remainder_carries_the_rest();
    exact_remainder_edges();
    exact_remainder_without_a_table();
    exact_remainder_keeps_two_targets();
    return oa::test::check_exit_status();
}
