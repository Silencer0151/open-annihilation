// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Campaign victory/defeat condition state in savegames. Implemented in
// src/sim/scenario (target oa-sim-scenario, which links oa-data-persist); include
// only from code that links that target.
#pragma once

#include "oa/data/persist/hapibank.hpp"
#include "oa/sim/scenario/state.hpp"

#include <cstdint>

namespace oa::sim::scenario {

/// Returns the savegame account name of a condition kind, e.g. "VictoryCondition_KillUnitType".
///
/// Throws std::invalid_argument for a kind outside the table.
///
/// @param kind condition kind
/// @return the account name
const char* condition_record_name(Kind kind);

/// Writes one condition's state to its account.
///
/// Opens (creating) the kind's account and stores, for the kinds that count, the
/// counter ("NumUnits" from units_counted or "NumLeftToKill" from kills_left), then
/// "Satisfied" and "Celebrated".
///
/// @param condition condition to save
/// @param[in,out] bank bank to write; the condition's account is left open
void save_condition(const Condition& condition, data::persist::Bank* bank);

/// Restores one condition's state from its account.
///
/// @param[in,out] condition condition whose counter, satisfied and celebrated fields
///     are restored; absent fields read as 0
/// @param[in,out] bank bank to read; the account is created when absent
void load_condition(Condition& condition, data::persist::Bank* bank);

/// Saves every registered victory and defeat condition (campaign maps only).
///
/// Throws std::logic_error on a campaign map before registration.
///
/// @param[in,out] controller scenario controller holding the conditions
/// @param[in,out] bank bank to write
/// @param map_kind map kind; conditions are saved only for kind 1
void save_conditions(Controller& controller, data::persist::Bank* bank, int32_t map_kind);

/// Restores every registered victory and defeat condition (campaign maps only).
///
/// Throws std::logic_error on a campaign map before registration.
///
/// @param[in,out] controller scenario controller holding the conditions
/// @param[in,out] bank bank to read
/// @param map_kind map kind; conditions are restored only for kind 1
void load_conditions(Controller& controller, data::persist::Bank* bank, int32_t map_kind);

} // namespace oa::sim::scenario
