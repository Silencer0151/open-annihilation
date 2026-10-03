// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The match rules' records: a default-constructed record is 3.1c, the
// helpers hold what they are given, and the extension table finds mounts.
// That every field's default is its registry baseline is checked against the
// registry by data-mod-profile-bindings.

#include "oa/data/match_rules.hpp"
#include "oa/test/check.hpp"

#include <array>
#include <type_traits>

namespace {

using namespace oa::data::match_rules;

// Simulation code may copy the records freely.
static_assert(std::is_trivially_copyable_v<MatchRules>);
static_assert(std::is_trivially_copyable_v<UnitTypeRules>);
static_assert(std::is_trivially_copyable_v<WeaponTypeRules>);
static_assert(std::is_trivially_copyable_v<MatchRulesView>);

void test_defaults() {
    constexpr MatchRules rules{};
    OA_CHECK(!rules.veterancy.model.enabled);
    OA_CHECK(rules.veterancy.model.damage_dealt_per_level == 6);
    OA_CHECK(rules.veterancy.model.damage_dealt_cap == 5);
    OA_CHECK(rules.veterancy.model.default_thresholds.count == 5);
    OA_CHECK(rules.veterancy.model.default_thresholds.items[4] == 25);
    OA_CHECK(rules.ai.income_multipliers.production[1] == 0.7);
    OA_CHECK(rules.units.id_reuse_delay.ticks == 0);
    OA_CHECK(rules.repair.rate.mode == RepairRateMode::clamp_max_1);
    OA_CHECK(!rules.orders.weapons_free_while_busy.states.contains(
        OrdersWeaponsFreeWhileBusyStates::nanolathe
    ));
    OA_CHECK(rules.setup.ai_player_name_format.format.view() == "AI:%s");
    OA_CHECK(rules.script_get.count == 0 && rules.script_set.count == 0);
    OA_CHECK(rules.script_fidelity == ScriptFidelity::exact);
    OA_CHECK(MatchRules{} == rules);
    OA_CHECK(enum_size(RepairRateMode{}) == 3);
}

void test_helpers() {
    FixedText<4> text{};
    OA_CHECK(text.assign("abcd") && text.view() == "abcd");
    OA_CHECK(!text.assign("abcde") && text.view() == "abcd");
    FixedList<int32_t, 2> list{};
    OA_CHECK(list.push(1) && list.push(2) && !list.push(3) && list.count == 2);
    EnumSet<RepairRateMode, 3> set{RepairRateMode::exact_remainder};
    OA_CHECK(set.contains(RepairRateMode::exact_remainder));
    OA_CHECK(!set.contains(RepairRateMode::clamp_min_1));

    ScriptExtensionTable table{};
    table.mounts[0] = ScriptMount{32, ScriptExtension::unit_kills_x100};
    table.mounts[1] = ScriptMount{71, ScriptExtension::unit_my_id};
    table.count = 2;
    OA_CHECK(table.find(71) == ScriptExtension::unit_my_id);
    OA_CHECK(table.find(33) == ScriptExtension::none);
    OA_CHECK(
        script_extension_ids[static_cast<size_t>(ScriptExtension::unit_my_id)] == "unit.my-id"
    );
}

} // namespace

void test_view() {
    const MatchRulesView unset{};
    OA_CHECK(unset.rules() == MatchRules{});
    OA_CHECK(unset.unit_type(7) == UnitTypeRules{});
    OA_CHECK(unset.unit_type(7).build_facings == build_facing::south);
    OA_CHECK(!unset.unit_type(7).veterancy_thresholds.has_value());
    OA_CHECK(unset.weapon(3) == WeaponTypeRules{});

    MatchRules rules{};
    rules.repair.rate.enabled = true;
    rules.repair.rate.mode = RepairRateMode::exact_remainder;
    std::array<UnitTypeRules, 3> types{};
    types[2].build_facings = build_facing::south | build_facing::east;
    types[2].veterancy_thresholds = FixedList<uint16_t, max_veterancy_thresholds>{1, 3, 9};
    std::array<WeaponTypeRules, 2> weapons{};
    weapons[1].not_to_air = 1;
    const MatchRulesView view{&rules, types, weapons};
    OA_CHECK(view.rules().repair.rate.mode == RepairRateMode::exact_remainder);
    OA_CHECK(view.unit_type(2).build_facings == (build_facing::south | build_facing::east));
    OA_CHECK(view.unit_type(2).veterancy_thresholds->count == 3);
    OA_CHECK(view.unit_type(1) == UnitTypeRules{});
    // Past the records given, the view answers with 3.1c's.
    OA_CHECK(view.unit_type(3) == UnitTypeRules{});
    OA_CHECK(view.weapon(1).not_to_air == 1);
    OA_CHECK(view.weapon(255) == WeaponTypeRules{});
}

int main() {
    test_defaults();
    test_helpers();
    test_view();
    return oa::test::check_exit_status();
}
