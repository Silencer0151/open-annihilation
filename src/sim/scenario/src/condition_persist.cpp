// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/scenario/condition_persist.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace oa::sim::scenario {
namespace {

constexpr const char* satisfied_field = "Satisfied";
constexpr const char* celebrated_field = "Celebrated";
// Map kind of the single-player map list, the only one whose saves hold conditions.
constexpr int32_t campaign_map_kind = 1;

// The savegame account of one condition kind.
struct Persisted {
    Kind kind{};
    const char* record{};
    const char* counter_field{};    // null for a kind that saves no counter
    int32_t Condition::* counter{}; // the field counter_field saves
};

constexpr std::array<Persisted, kind_count> persisted{{
    {Kind::kill_enemy_commander, "VictoryCondition_KillEnemyCommander", nullptr, nullptr},
    {Kind::destroy_all_units, "VictoryCondition_DestroyAllUnits", nullptr, nullptr},
    {Kind::kill_all_mobile_units,
     "VictoryCondition_KillAllMobileUnits",
     "NumUnits",
     &Condition::units_counted},
    {Kind::build_unit_type, "VictoryCondition_BuildUnitType", nullptr, nullptr},
    {Kind::capture_unit_type, "VictoryCondition_CaptureUnitType", nullptr, nullptr},
    {Kind::kill_all_of_type, "VictoryCondition_KillAllOfType", nullptr, nullptr},
    {Kind::kill_unit_type,
     "VictoryCondition_KillUnitType",
     "NumLeftToKill",
     &Condition::kills_left},
    {Kind::move_unit_to_radius, "VictoryCondition_MoveUnitToRadius", nullptr, nullptr},
    {Kind::unit_type_passes_x, "VictoryCondition_UnitTypePassesX", nullptr, nullptr},
    {Kind::unit_type_passes_z, "VictoryCondition_UnitTypePassesZ", nullptr, nullptr},
    {Kind::victory_timer, "VictoryCondition_VictoryTimerRunsOut", nullptr, nullptr},
    {Kind::commander_killed, "DefeatCondition_CommanderKilled", nullptr, nullptr},
    {Kind::all_units_killed, "DefeatCondition_AllUnitsKilled", nullptr, nullptr},
    {Kind::all_units_killed_of_type, "DefeatCondition_AllUnitsKilledOfType", nullptr, nullptr},
    {Kind::unit_type_killed,
     "DefeatCondition_UnitTypeKilled",
     "NumLeftToKill",
     &Condition::kills_left},
    {Kind::death_timer, "DefeatCondition_DeathTimerRunsOut", nullptr, nullptr},
    {Kind::any_unit_passes_x, "DefeatCondition_AnyUnitPassesX", nullptr, nullptr},
    {Kind::any_unit_passes_z, "DefeatCondition_AnyUnitPassesZ", nullptr, nullptr},
}};

/// Tells whether every account sits at its kind's index, which entry() relies on.
///
/// @return true when the table is in Kind order
constexpr bool persisted_in_kind_order() {
    for (std::size_t i = 0; i < persisted.size(); ++i)
        if (static_cast<std::size_t>(persisted[i].kind) != i)
            return false;
    return true;
}

static_assert(persisted_in_kind_order());

/// Returns the savegame entry of a condition kind.
///
/// Throws std::invalid_argument for a kind outside the table.
///
/// @param kind condition kind
/// @return its account name and counter
const Persisted& entry(Kind kind) {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= persisted.size())
        throw std::invalid_argument("unknown scenario condition kind");
    return persisted[index];
}

/// Restores the Satisfied and Celebrated flags every condition shares.
///
/// @param[in,out] condition condition whose satisfied and celebrated fields are set
/// @param bank savegame bank with the condition's account open
void load_common_flags(Condition& condition, data::persist::Bank* bank) {
    condition.satisfied = data::persist::bank_get_int(bank, satisfied_field, 0);
    condition.celebrated = data::persist::bank_get_int(bank, celebrated_field, 0);
}

} // namespace

const char* condition_record_name(Kind kind) {
    return entry(kind).record;
}

void save_condition(const Condition& condition, data::persist::Bank* bank) {
    const Persisted& p = entry(condition.kind);
    data::persist::bank_open_account(bank, p.record);
    if (p.counter_field != nullptr)
        data::persist::bank_set_int(bank, p.counter_field, condition.*p.counter);
    data::persist::bank_set_int(bank, satisfied_field, condition.satisfied);
    data::persist::bank_set_int(bank, celebrated_field, condition.celebrated);
}

void load_condition(Condition& condition, data::persist::Bank* bank) {
    const Persisted& p = entry(condition.kind);
    data::persist::bank_open_account(bank, p.record);
    if (p.counter_field != nullptr)
        condition.*p.counter = data::persist::bank_get_int(bank, p.counter_field, 0);
    load_common_flags(condition, bank);
}

void save_conditions(Controller& controller, data::persist::Bank* bank, int32_t map_kind) {
    if (map_kind != campaign_map_kind)
        return;
    visit_conditions(controller, [bank](Condition& condition) { save_condition(condition, bank); });
}

void load_conditions(Controller& controller, data::persist::Bank* bank, int32_t map_kind) {
    if (map_kind != campaign_map_kind)
        return;
    visit_conditions(controller, [bank](Condition& condition) { load_condition(condition, bank); });
}

} // namespace oa::sim::scenario
