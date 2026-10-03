// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The veterancy rule (veterancy.model): the level a unit's kills give it and
// each effect of the level, at 3.1c's baseline, with the rule's defaults, with
// a type's own thresholds and with every parameter moved off its default; and
// the victim's reduction in the health events a shooter builds.
#include "oa/sim/unit_health.hpp"
#include "oa/sim/unit_health/veterancy.hpp"
#include "oa/test/check.hpp"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

namespace {
using namespace oa::sim::unit_health;
namespace match_rules = oa::data::match_rules;

/// Returns the rules with veterancy.model on at its defaults.
///
/// @return the rules
match_rules::MatchRules model_on() {
    match_rules::MatchRules rules{};
    auto& model = rules.veterancy.model;
    model.enabled = true;
    model.level_source = match_rules::VeterancyModelLevelSource::thresholds;
    model.default_thresholds = {5, 10, 15, 20, 25};
    model.damage_taken_per_level = 4;
    model.damage_dealt_per_level = 6;
    model.reload_per_level = 6;
    model.damage_taken_cap = 25;
    model.damage_dealt_cap = std::nullopt;
    model.reload_cap = 16;
    model.lead_after = match_rules::VeterancyModelLeadAfter::first_threshold;
    model.accuracy_rate_default = 12;
    model.capture_level = match_rules::VeterancyModelCaptureLevel::extended;
    return rules;
}

/// Returns a type's own thresholds.
///
/// @param kills the kill counts
/// @return the list
match_rules::FixedList<uint16_t, match_rules::max_veterancy_thresholds>
own_thresholds(std::initializer_list<uint16_t> kills) {
    return match_rules::FixedList<uint16_t, match_rules::max_veterancy_thresholds>(kills);
}

/// 3.1c: a level per 5 kills, each effect capped at 5 levels.
void baseline_counts_five_kills_a_level() {
    const match_rules::MatchRulesView view{};
    OA_CHECK(veterancy_level(view, 1, 0) == 0 && veterancy_level(view, 1, 4) == 0);
    OA_CHECK(veterancy_level(view, 1, 5) == 1 && veterancy_level(view, 1, 24) == 4);
    OA_CHECK(veterancy_level(view, 1, 30) == 6 && veterancy_level(view, 1, 65535) == 13107);
    OA_CHECK(veteran_damage_taken_percent(view, 1, 0) == 100);
    OA_CHECK(veteran_damage_taken_percent(view, 1, 5) == 96);
    OA_CHECK(veteran_damage_taken_percent(view, 1, 25) == 80);
    OA_CHECK(veteran_damage_taken_percent(view, 1, 65535) == 80);
    OA_CHECK(veteran_damage_dealt_percent(view, 1, 0) == 100);
    OA_CHECK(veteran_damage_dealt_percent(view, 1, 10) == 112);
    OA_CHECK(veteran_damage_dealt_percent(view, 1, 1000) == 130);
    OA_CHECK(veteran_reload_percent(view, 1, 0) == 100);
    OA_CHECK(
        veteran_reload_percent(view, 1, 25) == 70 && veteran_reload_percent(view, 1, 999) == 70
    );
    OA_CHECK(!veteran_leads(view, 1, 5) && veteran_leads(view, 1, 6));
    OA_CHECK(veteran_accuracy_divisor(view, 1, 23) == 1);
    OA_CHECK(veteran_accuracy_divisor(view, 1, 24) == 2);
    OA_CHECK(veteran_accuracy_divisor(view, 1, 65535) == 5461);
    OA_CHECK(veteran_capture_level(view, 1, 4) == 0 && veteran_capture_level(view, 1, 5) == 1);
    OA_CHECK(veteran_capture_level(view, 1, 1000) == 200);
    OA_CHECK(veteran_capture_level(view, 1, 65535) == 13107);
    // The rule's baseline preset is the record a match without a profile has.
    OA_CHECK(match_rules::MatchRules{}.veterancy.model.accuracy_rate_default == 12);
    std::cout << "baseline counts five kills a level passed\n";
}

/// The rule's defaults: thresholds 5..25, which give 3.1c's levels up to 5;
/// the caps and gates differ only where a list is longer.
void default_thresholds_match_the_old_levels() {
    const auto rules = model_on();
    const match_rules::MatchRulesView view{&rules};

    struct KillsLevel {
        uint16_t kills;
        uint32_t level;
    };

    for (const auto& [kills, level] : std::initializer_list<KillsLevel>{
             {0, 0}, {4, 0}, {5, 1}, {9, 1}, {10, 2}, {25, 5}, {26, 5}, {1000, 5}
         })
        OA_CHECK(veterancy_level(view, 1, kills) == level);
    OA_CHECK(veteran_damage_taken_percent(view, 1, 5) == 96);
    OA_CHECK(veteran_damage_taken_percent(view, 1, 1000) == 80);
    OA_CHECK(veteran_damage_dealt_percent(view, 1, 1000) == 130);
    OA_CHECK(veteran_reload_percent(view, 1, 1000) == 70);
    // The first threshold is 5: lead from 6 kills, as in 3.1c.
    OA_CHECK(!veteran_leads(view, 1, 5) && veteran_leads(view, 1, 6));
    OA_CHECK(veteran_accuracy_divisor(view, 1, 24) == 2);
    // The extended capture level continues at 5 kills a level: kills / 5.
    for (uint16_t kills = 0; kills <= 2000; ++kills)
        OA_CHECK(veteran_capture_level(view, 1, kills) == kills / 5U);
    std::cout << "default thresholds match the old levels passed\n";
}

/// A type's own thresholds and accuracy rate replace the defaults for that
/// type alone.
void own_thresholds_apply_to_their_type() {
    const auto rules = model_on();
    std::vector<match_rules::UnitTypeRules> types(4);
    types[2].veterancy_thresholds = own_thresholds({10, 20, 30, 40, 50});
    types[2].veterancy_accuracy_rate = uint16_t{24};
    types[3].veterancy_accuracy_rate = uint16_t{0};
    const match_rules::MatchRulesView view{&rules, types};
    OA_CHECK(veterancy_level(view, 2, 9) == 0 && veterancy_level(view, 2, 10) == 1);
    OA_CHECK(veterancy_level(view, 2, 49) == 4 && veterancy_level(view, 2, 50) == 5);
    OA_CHECK(veterancy_level(view, 2, 75) == 5 && veterancy_level(view, 1, 10) == 2);
    OA_CHECK(veteran_damage_taken_percent(view, 2, 25) == 92);
    OA_CHECK(veteran_damage_dealt_percent(view, 2, 30) == 118);
    OA_CHECK(veteran_reload_percent(view, 2, 30) == 82);
    // Kills equal to the first threshold count for the level but do not lead.
    OA_CHECK(!veteran_leads(view, 2, 10) && veteran_leads(view, 2, 11));
    OA_CHECK(veteran_capture_level(view, 2, 5) == 0 && veteran_capture_level(view, 2, 49) == 4);
    OA_CHECK(veteran_capture_level(view, 2, 75) == 7);
    OA_CHECK(
        veteran_accuracy_divisor(view, 2, 47) == 1 && veteran_accuracy_divisor(view, 2, 48) == 2
    );
    OA_CHECK(veteran_accuracy_divisor(view, 1, 48) == 4);
    // A rate of 0 takes no spread off however many kills.
    OA_CHECK(veteran_accuracy_divisor(view, 3, 60000) == 0);
    // A type past the records takes the defaults.
    OA_CHECK(veterancy_level(view, 9, 10) == 2);
    std::cout << "own thresholds apply to their type passed\n";
}

/// The caps bite only past five thresholds; each percentage follows its
/// per-level parameter, never below 0.
void caps_and_percentages_follow_the_parameters() {
    auto rules = model_on();
    auto& model = rules.veterancy.model;
    model.default_thresholds = {};
    for (int32_t kills = 1; kills <= 32; ++kills)
        model.default_thresholds.push(kills);
    const match_rules::MatchRulesView view{&rules};
    OA_CHECK(veterancy_level(view, 1, 40) == 32);
    OA_CHECK(veteran_damage_taken_percent(view, 1, 24) == 4);
    OA_CHECK(veteran_damage_taken_percent(view, 1, 40) == 0);
    OA_CHECK(veteran_damage_dealt_percent(view, 1, 40) == 292);
    OA_CHECK(veteran_reload_percent(view, 1, 40) == 4);
    model.damage_taken_cap = 5;
    model.damage_dealt_cap = 5;
    model.reload_cap = 5;
    OA_CHECK(veteran_damage_taken_percent(view, 1, 40) == 80);
    OA_CHECK(veteran_damage_dealt_percent(view, 1, 40) == 130);
    OA_CHECK(veteran_reload_percent(view, 1, 40) == 70);
    model.damage_dealt_cap = 0;
    OA_CHECK(veteran_damage_dealt_percent(view, 1, 40) == 100);
    model.damage_taken_cap = 25;
    model.reload_cap = 16;
    model.damage_taken_per_level = 10;
    model.damage_dealt_per_level = 10;
    model.reload_per_level = 10;
    model.damage_dealt_cap = std::nullopt;
    OA_CHECK(veteran_damage_taken_percent(view, 1, 3) == 70);
    OA_CHECK(veteran_damage_taken_percent(view, 1, 32) == 0);
    OA_CHECK(veteran_damage_dealt_percent(view, 1, 3) == 130);
    OA_CHECK(veteran_reload_percent(view, 1, 3) == 70);
    OA_CHECK(veteran_reload_percent(view, 1, 20) == 0);
    model.damage_taken_per_level = 0;
    OA_CHECK(veteran_damage_taken_percent(view, 1, 32) == 100);
    // A changed default list moves the levels of every type without its own.
    model.default_thresholds = {3, 6};
    OA_CHECK(veterancy_level(view, 1, 2) == 0 && veterancy_level(view, 1, 3) == 1);
    OA_CHECK(veterancy_level(view, 1, 6) == 2 && veterancy_level(view, 1, 600) == 2);
    OA_CHECK(!veteran_leads(view, 1, 3) && veteran_leads(view, 1, 4));
    OA_CHECK(veteran_capture_level(view, 1, 12) == 4);
    std::cout << "caps and percentages follow the parameters passed\n";
}

/// Each parameter alone, the rest at 3.1c's baseline.
void each_parameter_moves_alone() {
    match_rules::MatchRules rules{};
    auto& model = rules.veterancy.model;
    const match_rules::MatchRulesView view{&rules};
    model.level_source = match_rules::VeterancyModelLevelSource::thresholds;
    model.default_thresholds = {2, 4};
    OA_CHECK(veterancy_level(view, 1, 3) == 1 && veterancy_level(view, 1, 100) == 2);
    // The baseline gates still read the kills.
    OA_CHECK(veteran_leads(view, 1, 6) && !veteran_leads(view, 1, 5));
    OA_CHECK(veteran_capture_level(view, 1, 100) == 20);
    rules = {};
    model.lead_after = match_rules::VeterancyModelLeadAfter::first_threshold;
    model.default_thresholds = {2, 4};
    OA_CHECK(!veteran_leads(view, 1, 2) && veteran_leads(view, 1, 3));
    rules = {};
    model.capture_level = match_rules::VeterancyModelCaptureLevel::extended;
    model.default_thresholds = {10, 30};
    OA_CHECK(veteran_capture_level(view, 1, 10) == 1 && veteran_capture_level(view, 1, 70) == 4);
    rules = {};
    model.accuracy_rate_default = 6;
    OA_CHECK(veteran_accuracy_divisor(view, 1, 24) == 4);
    model.accuracy_rate_default = 0;
    OA_CHECK(veteran_accuracy_divisor(view, 1, 24) == 0);
    rules = {};
    model.damage_taken_cap = 2;
    OA_CHECK(veteran_damage_taken_percent(view, 1, 100) == 92);
    rules = {};
    model.damage_dealt_cap = std::nullopt;
    OA_CHECK(veteran_damage_dealt_percent(view, 1, 100) == 220);
    rules = {};
    model.reload_cap = 3;
    OA_CHECK(veteran_reload_percent(view, 1, 100) == 82);
    rules = {};
    model.damage_taken_per_level = 5;
    OA_CHECK(veteran_damage_taken_percent(view, 1, 25) == 75);
    rules = {};
    model.damage_dealt_per_level = 10;
    OA_CHECK(veteran_damage_dealt_percent(view, 1, 25) == 150);
    rules = {};
    model.reload_per_level = 2;
    OA_CHECK(veteran_reload_percent(view, 1, 25) == 90);
    std::cout << "each parameter moves alone passed\n";
}

/// Under thresholds a count past 32767 reads negative: past every threshold
/// for the level, the lead and the capture, and below any accuracy gain.
void counts_past_32767_read_negative() {
    auto rules = model_on();
    rules.veterancy.model.default_thresholds = {50000};
    const match_rules::MatchRulesView view{&rules};
    OA_CHECK(veterancy_level(view, 1, 40000) == 1);
    OA_CHECK(veteran_leads(view, 1, 40000));
    OA_CHECK(veteran_accuracy_divisor(view, 1, 40000) == -2128);
    OA_CHECK(veteran_capture_level(view, 1, 40000) == 0xffff9c40U / 50000U);
    // 3.1c's level reads the count without sign.
    const match_rules::MatchRulesView baseline{};
    OA_CHECK(veterancy_level(baseline, 1, 40000) == 8000);
    OA_CHECK(veteran_accuracy_divisor(baseline, 1, 40000) == 3333);
    std::cout << "counts past 32767 read negative passed\n";
}

/// The extended capture level: a single threshold divides the kills; a zero
/// last step stops at the number of thresholds.
void extended_capture_edges() {
    auto rules = model_on();
    auto& model = rules.veterancy.model;
    const match_rules::MatchRulesView view{&rules};
    model.default_thresholds = {20};
    OA_CHECK(veteran_capture_level(view, 1, 15) == 0);
    OA_CHECK(veteran_capture_level(view, 1, 20) == 1);
    OA_CHECK(veteran_capture_level(view, 1, 45) == 2);
    model.default_thresholds = {10, 10};
    OA_CHECK(veteran_capture_level(view, 1, 10) == 2 && veteran_capture_level(view, 1, 99) == 2);
    model.default_thresholds = {0};
    OA_CHECK(veteran_capture_level(view, 1, 0) == 1 && veteran_capture_level(view, 1, 9) == 1);
    std::cout << "extended capture edges passed\n";
}

/// The shooter's machine scales the victim's reduction by the victim's own
/// type, and 3.1c's arithmetic is kept bit for bit at the baseline.
void health_events_use_the_victims_thresholds() {
    UnitType type{0x10000, 600.0F, 300, 1000};
    Unit source{7, 0, 30, 100, &type};
    Unit target{9, 0, 25, 500, &type};
    target.type_index = 2;
    OA_CHECK(make_health_event(&source, target, 100, 1).amount == 80);
    OA_CHECK(make_health_event(&source, target, 30000, 1).amount == 24000);
    const auto rules = model_on();
    std::vector<match_rules::UnitTypeRules> types(3);
    types[2].veterancy_thresholds = own_thresholds({10, 20, 30, 40, 50});
    const match_rules::MatchRulesView view{&rules, types};
    OA_CHECK(make_health_event(&source, target, 100, 1, 0, view).amount == 92);
    // The shooter's kills play no part in what the victim takes.
    source.veteran_level = 0;
    OA_CHECK(make_health_event(&source, target, 100, 1, 0, view).amount == 92);
    // A heal is never scaled.
    OA_CHECK(make_health_event(&source, target, 100, healing_damage_kind, 0, view).amount == 100);
    // The product wraps at 32 bits as before.
    target.veteran_level = 0;
    target.type_index = 1;
    OA_CHECK(
        make_health_event(nullptr, target, 0x7fffffff, 1).amount ==
        make_health_event(nullptr, target, 0x7fffffff, 1, 0, view).amount
    );
    std::cout << "health events use the victim's thresholds passed\n";
}
} // namespace

int main() {
    baseline_counts_five_kills_a_level();
    default_thresholds_match_the_old_levels();
    own_thresholds_apply_to_their_type();
    caps_and_percentages_follow_the_parameters();
    each_parameter_moves_alone();
    counts_past_32767_read_negative();
    extended_capture_edges();
    health_events_use_the_victims_thresholds();
    return oa::test::check_exit_status();
}
