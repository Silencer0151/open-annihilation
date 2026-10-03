// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Self-heal under repair.healtime-self-heal: when a unit whose type heals
// regenerates, and the work each step passes to the repair, at 3.1c's
// baseline and under each parameter.
#include "oa/sim/simulation_state.hpp"
#include "oa/test/check.hpp"

#include <cstdint>
#include <iostream>
#include <utility>

namespace {
using namespace oa::sim::simulation_state;
namespace match_rules = oa::data::match_rules;

/// 3.1c: every eighth tick, while the type heals and the unit is hurt,
/// finished or not; the work is (heal time x 8) / 30, read without sign.
void baseline_heals_every_eighth_tick() {
    const match_rules::RepairHealtimeSelfHeal rule{};
    for (uint32_t tick = 0; tick < 32; ++tick)
        OA_CHECK(self_heal_due(rule, 7, 10, 100, 0.0F, tick) == (tick % 8 == 0));
    OA_CHECK(!self_heal_due(rule, 0, 10, 100, 0.0F, 8));
    OA_CHECK(!self_heal_due(rule, 7, 100, 100, 0.0F, 8));
    // Health is sign-extended and compared without sign: negative is never hurt.
    OA_CHECK(!self_heal_due(rule, 7, -5, 100, 0.0F, 8));
    OA_CHECK(self_heal_due(rule, 7, 10, 100, 1.0F, 8));
    OA_CHECK(self_heal_rate(rule, 30) == 8.0F);
    OA_CHECK(self_heal_rate(rule, 7) == 1.0F);
    OA_CHECK(self_heal_rate(rule, 3) == 0.0F);
    // A heal time past 32767 is read without sign.
    OA_CHECK(self_heal_rate(rule, static_cast<int16_t>(-30)) == 17468.0F);
    std::cout << "baseline heals every eighth tick passed\n";
}

/// The rule's defaults: unfinished units wait, the heal time's low byte is
/// the tick mask, and the work is four times as much, read with its sign.
void defaults_use_the_heal_time_mask() {
    match_rules::RepairHealtimeSelfHeal rule{};
    rule.enabled = true;
    rule.skip_under_construction = true;
    rule.cadence = match_rules::RepairHealtimeSelfHealCadence::healtime_mask;
    rule.work_multiplier = 4;

    struct Cadence {
        int16_t heal_time;
        uint32_t period;
    };

    for (const auto& [heal_time, period] :
         {Cadence{1, 2}, Cadence{3, 4}, Cadence{7, 8}, Cadence{15, 16}}) {
        for (uint32_t tick = 0; tick < 64; ++tick)
            OA_CHECK(self_heal_due(rule, heal_time, 10, 100, 0.0F, tick) == (tick % period == 0));
    }
    // Only the low byte masks the tick: 0x101 heals on odd ticks never.
    OA_CHECK(self_heal_due(rule, 0x100, 10, 100, 0.0F, 1));
    OA_CHECK(!self_heal_due(rule, 0x101, 10, 100, 0.0F, 1));
    // An unfinished unit waits; a build fraction of -0.0 counts as unfinished.
    OA_CHECK(!self_heal_due(rule, 7, 10, 100, 0.5F, 8));
    OA_CHECK(!self_heal_due(rule, 7, 10, 100, -0.0F, 8));
    OA_CHECK(self_heal_due(rule, 7, 10, 100, 0.0F, 8));
    OA_CHECK(self_heal_rate(rule, 7) == 7.0F);
    OA_CHECK(self_heal_rate(rule, 30) == 32.0F);
    OA_CHECK(self_heal_rate(rule, 1) == 1.0F);
    OA_CHECK(self_heal_rate(rule, static_cast<int16_t>(-30)) == -32.0F);
    std::cout << "defaults use the heal time mask passed\n";
}

/// Each parameter alone, the rest at the baseline.
void each_parameter_moves_alone() {
    match_rules::RepairHealtimeSelfHeal rule{};
    rule.skip_under_construction = true;
    OA_CHECK(!self_heal_due(rule, 7, 10, 100, 0.5F, 8));
    OA_CHECK(self_heal_due(rule, 7, 10, 100, 0.0F, 8) && !self_heal_due(rule, 7, 10, 100, 0.0F, 4));
    rule = {};
    rule.cadence = match_rules::RepairHealtimeSelfHealCadence::healtime_mask;
    OA_CHECK(self_heal_due(rule, 3, 10, 100, 0.5F, 4) && !self_heal_due(rule, 3, 10, 100, 0.5F, 2));
    OA_CHECK(self_heal_rate(rule, 30) == 8.0F);
    rule = {};
    rule.work_multiplier = 2;
    OA_CHECK(self_heal_rate(rule, 30) == 16.0F && self_heal_rate(rule, 7) == 3.0F);
    rule.work_multiplier = 64;
    OA_CHECK(self_heal_rate(rule, 30) == 512.0F);
    std::cout << "each parameter moves alone passed\n";
}
} // namespace

int main() {
    baseline_heals_every_eighth_tick();
    defaults_use_the_heal_time_mask();
    each_parameter_moves_alone();
    return oa::test::check_exit_status();
}
