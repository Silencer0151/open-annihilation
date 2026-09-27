// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Aircraft attack missions. Each takes off, makes attack runs at its target
// (a unit, or the order destination), and hands over to VTOL_SeekAttack or
// VTOL_Evade when the run ends.

#include "oa/sim/air/orders.hpp"

namespace oa::sim::air {

/// Tests an attack order's leash.
///
/// @param order the order; parameter_3 is the leash in world units from the anchor
/// @return true once the unit strays past the leash; a zero leash never ends it
[[nodiscard]] bool air_attack_leash_exceeded(const AirOrder* order) noexcept;

/// Runs the AirToGround handler.
///
/// Takes off, approaches at a random angle, flies over the target with weapon 0
/// aimed, pulls out three weapon ranges beyond it and swings round, breaking
/// off to a repair pad below 3/4 health.
///
/// @param[in,out] order the order snapshot; the target is a unit or the destination
/// @param host air services
/// @param events events that woke the order
/// @return what the order tick does next
[[nodiscard]] AirStep
air_attack_ground(AirOrder* order, const AirHost& host, uint32_t events) noexcept;

/// Runs the AirToAir handler.
///
/// Takes off and chases the target, leading it by its velocity; once lined up
/// behind it flies a straight 30-tick pass, and evades when the chase drags on.
/// A target that is missing during the chase finishes the order.
///
/// @param[in,out] order the order snapshot
/// @param host air services
/// @param events events that woke the order
/// @return what the order tick does next
[[nodiscard]] AirStep
air_attack_air(AirOrder* order, const AirHost& host, uint32_t events) noexcept;

/// Runs the VTOL_Evade handler.
///
/// Breaks a quarter turn to a random side by one weapon range, then by two
/// more, and finishes. Ends early when the target is gone.
///
/// @param[in,out] order the order snapshot
/// @param host air services
/// @param events events that woke the order
/// @return what the order tick does next
[[nodiscard]] AirStep air_evade(AirOrder* order, const AirHost& host, uint32_t events) noexcept;

} // namespace oa::sim::air
