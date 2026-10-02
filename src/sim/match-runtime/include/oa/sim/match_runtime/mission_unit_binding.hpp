// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The match's services for the campaign mission units (sim::mission_units):
// creating a schema's placed units and queueing their InitialMission scripts.
#pragma once

#include "oa/sim/match_runtime.hpp"
#include "oa/sim/mission_units.hpp"

#include <cstdint>
#include <string>

namespace oa::sim::match_runtime {

struct MissionUnitBinding {
    Match& match;
    // The first fatal report a hook met; the mission-unit code itself
    // continues past it.
    std::string failure;
};

/// Builds the mission-unit hooks over a match.
///
/// The fatal-report hook records its first report in `binding.failure` and
/// lets the mission-unit code continue; a unit creation, order or carry the
/// match refuses is noted in the match's fault record (Match::fault).
///
/// @param[in,out] binding Match the hooks act on; the hooks keep a pointer
///     to it as their context, so it must outlive them.
/// @return Hooks for sim::mission_units.
sim::mission_units::Hooks mission_unit_hooks(MissionUnitBinding& binding);

/// Creates a mission schema's units in the match and queues their
/// InitialMission scripts.
///
/// Every entry is processed; afterwards the first fatal report a hook met,
/// such as the game's report for an entry whose player slot is inactive, is
/// noted in the match's fault record.
///
/// @param match Match the units are created in.
/// @param units The schema's unit entries.
/// @param count Number of entries in `units`.
/// @return False when the unit table could not be allocated or a hook made
///     a fatal report; the match's fault record says which.
bool create_mission_units(Match& match, const data::campaign::MissionUnit* units, int32_t count);

} // namespace oa::sim::match_runtime
