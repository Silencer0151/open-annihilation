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
/// @param kind condition kind
/// @return the account name, or null for a kind outside the table
[[nodiscard]] const char* condition_record_name(Kind kind) noexcept;

/// Writes one condition's state to its account.
///
/// Opens (creating) the kind's account and stores, for the kinds that count, the
/// counter ("NumUnits" from units_counted or "NumLeftToKill" from kills_left), then
/// "Satisfied" and "Celebrated". A kind outside the table writes nothing.
///
/// @param condition condition to save
/// @param[in,out] bank bank to write; the condition's account is left open
void save_condition(const Condition& condition, data::persist::Bank* bank);

/// Restores one condition's state from its account.
///
/// A kind outside the table reads nothing.
///
/// @param[in,out] condition condition whose counter, satisfied and celebrated fields
///     are restored; absent fields read as 0
/// @param[in,out] bank bank to read; the account is created when absent
void load_condition(Condition& condition, data::persist::Bank* bank);

/// Saves every registered victory and defeat condition (campaign maps only).
///
/// @param[in,out] controller scenario controller holding the conditions
/// @param[in,out] bank bank to write
/// @param map_kind map kind; conditions are saved only for kind 1
/// @return false on a campaign map whose conditions were never registered, or
///         whose groups visit_conditions stopped in
bool save_conditions(Controller& controller, data::persist::Bank* bank, int32_t map_kind);

/// Restores every registered victory and defeat condition (campaign maps only).
///
/// @param[in,out] controller scenario controller holding the conditions
/// @param[in,out] bank bank to read
/// @param map_kind map kind; conditions are restored only for kind 1
/// @return false on a campaign map whose conditions were never registered, or
///         whose groups visit_conditions stopped in
bool load_conditions(Controller& controller, data::persist::Bank* bank, int32_t map_kind);

} // namespace oa::sim::scenario
