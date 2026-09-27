// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Flight model of the air movement layer: one tick of velocity steering
// toward the air driver's point, then bank and pitch from the change.

#include "oa/sim/air/driver.hpp"
#include "oa/sim/unit_movement/movement.hpp"

#include <array>
#include <cstdint>

namespace oa::sim::air {

/// Runs one flight tick.
///
/// Off the air layer the movement record comes to rest. In the air it damps the
/// velocity, turns horizontal speed above the def's brake rate onto the heading,
/// climbs or sinks toward the target (not while off the map), turns to the
/// target heading, accelerates toward the target point matching its velocity,
/// then banks and pitches.
///
/// @param[in,out] unit the flying unit's movement view
/// @param[in,out] movement its movement record
/// @param[in,out] attitude the movement record's filtered velocity change
/// @param target point, velocity and heading to steer toward
/// @param def the unit's type
/// @param gravity map gravity, signed 16.16
/// @param outside_map whether the unit is in the off-map bucket
/// @param[out] bank receives Unit.bank
void air_flight_step(
    sim::unit_movement::Unit& unit,
    sim::unit_movement::Movement& movement,
    std::array<sim::unit_movement::Fixed, 3>& attitude,
    const AirSteeringTarget& target,
    const UnitDef& def,
    oa_fixed gravity,
    bool outside_map,
    int16_t* bank
);

/// Rotates an integer X/Z pair by a 16-bit angle, rounding to nearest.
///
/// @param angle 16-bit angle, 65536 per turn
/// @param[in,out] x X to rotate
/// @param[in,out] z Z to rotate
void rotate_xz(int16_t angle, int32_t* x, int32_t* z) noexcept;

// Per-tick decay of the movement record's filtered velocity change, and the
// divisor that turns gravity into the vertical side of the bank and pitch
// angles.
inline constexpr sim::unit_movement::Fixed attitude_decay = 0xf333;
inline constexpr sim::unit_movement::Fixed attitude_gravity_divisor = 0xccd;

/// Updates the filtered velocity change and derives bank and pitch from it.
///
/// Decays `filtered`, adds this tick's velocity change, turns the sum into the
/// unit's heading frame and derives bank (Unit.bank) and pitch (Unit.pitch),
/// both from its negated X. With no change the bank eases back to
/// level by the decay alone.
///
/// @param[in,out] unit the flying unit; its pitch is written
/// @param[in,out] filtered the movement record's filtered velocity change
/// @param change this tick's velocity change
/// @param bank_scale 16.16 scale of the bank angle
/// @param pitch_scale 16.16 scale of the pitch angle
/// @param gravity map gravity, signed 16.16
/// @param[out] bank receives Unit.bank
void air_attitude(
    sim::unit_movement::Unit& unit,
    std::array<sim::unit_movement::Fixed, 3>& filtered,
    const std::array<sim::unit_movement::Fixed, 3>& change,
    int32_t bank_scale,
    int32_t pitch_scale,
    oa_fixed gravity,
    int16_t* bank
) noexcept;

/// Returns the planar length of a 16.16 vector as the game computes it.
///
/// The larger component is factored out before the square root, each step kept
/// at double precision.
///
/// @param x X component
/// @param z Z component
/// @return the length in the same units
[[nodiscard]] double planar_length(double x, double z) noexcept;

} // namespace oa::sim::air
