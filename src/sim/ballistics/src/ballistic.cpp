// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/ballistics.hpp"
#include "oa/base/game_math.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace oa::sim::ballistics {
namespace {

constexpr double maximum_ballistic_angle_radians = 0.7853981633974475;
constexpr double right_angle_radians = 1.570796326794895;
// The exact binary64 constants 3.1c uses. Pitch is radians * 32768 * ~1/pi,
// truncated toward zero.
constexpr double ta_angle_half_turn = 32768.0;
constexpr double reciprocal_pi = std::bit_cast<double>(uint64_t{0x3fd45f306dc9c889});

// The launch solver rounds each of these intermediates to binary64. Keep
// those rounding points even when the portable build enables contraction.
double rounded(double value) noexcept {
    volatile double stored = value;
    return stored;
}

int32_t signed_bits(uint32_t value) noexcept {
    return std::bit_cast<int32_t>(value);
}

int32_t wrapped_difference(uint32_t left, uint32_t right) noexcept {
    return signed_bits(left - right);
}

int16_t integral_coordinate(uint32_t fixed) noexcept {
    return std::bit_cast<int16_t>(static_cast<uint16_t>(fixed >> 16U));
}

uint32_t signed_square_high(int32_t value) noexcept {
    const auto square = static_cast<uint64_t>(static_cast<int64_t>(value) * value);
    return static_cast<uint32_t>(square >> 32U);
}

using base::game_math::Extended;

// The launch angle for a root of the launch equation, kept at the arccosine's
// 64-bit significand; no value stands for NaN, an arccosine outside its domain.
std::optional<Extended> launch_angle(double root, double velocity) noexcept {
    if (!(root > 0.0))
        return base::game_math::to_extended(right_angle_radians);
    return base::game_math::arccosine(rounded(base::game_math::square_root(root) / velocity));
}

double stored_angle(const std::optional<Extended>& angle) noexcept {
    return angle ? base::game_math::to_double(*angle) : std::numeric_limits<double>::quiet_NaN();
}

bool acceptable_first_angle(double angle, float minimum_angle) noexcept {
    // A NaN angle or minimum passes both comparisons.
    return !(static_cast<double>(minimum_angle) >= angle) &&
           !(angle > maximum_ballistic_angle_radians);
}

// The second angle is compared before it is rounded to a double. A high arc
// must lie above the quarter turn's bound, a flat one at or below it.
bool acceptable_second_angle(
    const std::optional<Extended>& angle, float minimum_angle, bool high_arc
) noexcept {
    // A NaN angle or minimum is rejected before the bound comparison.
    if (!angle || std::isnan(minimum_angle))
        return false;
    using base::game_math::compare;
    using base::game_math::to_extended;
    if (compare(*angle, to_extended(static_cast<double>(minimum_angle))) <= 0)
        return false;
    const auto against_bound = compare(*angle, to_extended(maximum_ballistic_angle_radians));
    return high_arc ? against_bound > 0 : against_bound <= 0;
}

int16_t pitch_from_radians(double angle) noexcept {
    const double scaled = rounded(angle * ta_angle_half_turn) * reciprocal_pi;
    // A NaN angle, which acceptable_first_angle lets through, gives pitch 0.
    if (std::isnan(scaled))
        return 0;
    return static_cast<int16_t>(static_cast<int64_t>(scaled));
}

} // namespace

int16_t
launch_pitch(const BallisticParameters& parameters, int32_t dx, int32_t dy, int32_t dz) noexcept {
    const double horizontal =
        rounded(base::game_math::hypotenuse(static_cast<double>(dx), static_cast<double>(dz)));
    const double vertical = static_cast<double>(dy);
    const double velocity = static_cast<double>(parameters.projectile_velocity);
    const double gravity = static_cast<double>(parameters.simulation_gravity);
    const double horizontal_squared = rounded(horizontal * horizontal);
    const double velocity_squared = rounded(velocity * velocity);
    const double vertical_squared = rounded(vertical * vertical);
    const double distance_squared = rounded(vertical_squared + horizontal_squared);
    const double gravity_vertical = rounded(gravity * vertical);
    const double horizontal_fourth = rounded(horizontal_squared * horizontal_squared);
    const auto gravity_squared_integer = signed_bits(
        static_cast<uint32_t>(parameters.simulation_gravity) *
        static_cast<uint32_t>(parameters.simulation_gravity)
    );
    const double gravity_squared = static_cast<double>(gravity_squared_integer);
    double velocity_term = rounded(gravity_vertical + gravity_vertical);
    velocity_term = rounded(velocity_term + velocity_squared);
    velocity_term = rounded(velocity_term * velocity_squared);
    double first_discriminant_term = rounded(gravity_squared * vertical_squared);
    first_discriminant_term = rounded(first_discriminant_term + velocity_term);
    first_discriminant_term = rounded(first_discriminant_term * horizontal_fourth);
    double second_discriminant_term = rounded(gravity_squared * horizontal_fourth);
    second_discriminant_term = rounded(second_discriminant_term * distance_squared);
    const double discriminant = rounded(first_discriminant_term - second_discriminant_term);
    if (!(discriminant >= 0.0))
        return invalid_launch_pitch;

    const double denominator = rounded(distance_squared + distance_squared);
    const double numerator =
        rounded(rounded(gravity_vertical + velocity_squared) * horizontal_squared);
    const double radical = base::game_math::square_root(discriminant);
    const double first_root = rounded(rounded(numerator + radical) / denominator);
    const double second_root = rounded(rounded(numerator - radical) / denominator);
    // The first angle is stored as a double; the second is kept as computed.
    const double first = stored_angle(launch_angle(first_root, velocity));
    const auto second = launch_angle(second_root, velocity);
    if (acceptable_first_angle(first, parameters.minimum_barrel_angle_radians))
        return pitch_from_radians(first);
    if (acceptable_second_angle(
            second, parameters.minimum_barrel_angle_radians, parameters.accept_high_arc
        ))
        return pitch_from_radians(stored_angle(second));
    return invalid_launch_pitch;
}

bool ballistic_feasible(
    const BallisticParameters& parameters,
    const std::array<uint32_t, 3>& source,
    const std::array<uint32_t, 3>& target
) noexcept {
    return launch_pitch(
               parameters,
               wrapped_difference(source[0], target[0]),
               wrapped_difference(source[1], target[1]),
               wrapped_difference(source[2], target[2])
           ) != invalid_launch_pitch;
}

bool weapon_can_reach(
    const WeaponReachParameters& weapon,
    const ReachUnitGeometry& source,
    const ReachUnitGeometry& target,
    uint8_t sea_level
) noexcept {
    const auto source_y = static_cast<int32_t>(integral_coordinate(source.position[1]));
    const auto target_y = static_cast<int32_t>(integral_coordinate(target.position[1]));
    const auto sea = static_cast<int32_t>(sea_level);

    const bool airborne = (target.unit_flags & target_air_state_mask) == target_air_state;
    if ((weapon.weapon_flags & water_weapon_flag) == 0U) {
        if (source_y + source.model_maximum_y <= sea || target_y + target.model_maximum_y <= sea)
            return false;
        if (weapon.keys.not_to_air && airborne)
            return false;
        if ((weapon.weapon_flags & to_air_weapon_flag) != 0U && !airborne)
            return false;
        if ((weapon.weapon_flags & ballistic_weapon_flag) != 0U &&
            !ballistic_feasible(weapon.ballistic, source.position, target.position))
            return false;
    } else {
        // A surface-firing water weapon skips both above-sea tests, except
        // against an airborne target when it may not fire at aircraft.
        const bool above_sea_allowed =
            weapon.keys.surface_fire && !(weapon.keys.not_to_air && airborne);
        if (!above_sea_allowed) {
            if ((target.type_flags & target_type_floats_flag) == 0U && target_y > sea)
                return false;
            if ((target.type_flags & target_type_hover_flag) != 0U &&
                target_y + (static_cast<int32_t>(target.model_maximum_y) >> 1) > sea)
                return false;
        }
        if (weapon.keys.not_to_underwater && target_y + target.model_maximum_y <= sea)
            return false;
    }

    const auto x = wrapped_difference(target.position[0], source.position[0]);
    const auto z = wrapped_difference(target.position[2], source.position[2]);
    const auto distance = signed_bits(signed_square_high(x) + signed_square_high(z));
    const auto range = static_cast<uint32_t>(weapon.range_world_units);
    const auto range_squared = signed_bits(range * range);
    return distance <= range_squared;
}

bool fire_can_reach(
    const WeaponReachParameters& weapon,
    const ReachUnitGeometry& source,
    const std::array<uint32_t, 3>& target,
    uint8_t sea_level
) noexcept {
    // Target-minus-source X/Z high squares against range^2, then the
    // non-water source-height and ballistic gates. Water weapons, and those
    // with the surface_fire key, stop at range.
    const auto x = wrapped_difference(target[0], source.position[0]);
    const auto z = wrapped_difference(target[2], source.position[2]);
    const auto distance = signed_bits(signed_square_high(x) + signed_square_high(z));
    const auto range = static_cast<uint32_t>(weapon.range_world_units);
    const auto range_squared = signed_bits(range * range);
    if (range_squared < distance)
        return false;
    if ((weapon.weapon_flags & water_weapon_flag) == 0U && !weapon.keys.surface_fire) {
        const auto source_y = static_cast<int32_t>(integral_coordinate(source.position[1]));
        const auto sea = static_cast<int32_t>(sea_level);
        if (source_y + static_cast<int32_t>(source.model_maximum_y) <= sea)
            return false;
        if ((weapon.weapon_flags & ballistic_weapon_flag) != 0U &&
            !ballistic_feasible(weapon.ballistic, source.position, target))
            return false;
    }
    return true;
}

TurretAimResult turret_aim_ballistic(
    const BallisticParameters& parameters, const TurretAimGeometry& geometry
) noexcept {
    // dx/dy/dz are the wrapped AimFrom-minus-target deltas; the heading uses
    // (dx, dz) and the launch pitch (dx, dy, dz).
    const auto dx = wrapped_difference(geometry.aim_from[0], geometry.target[0]);
    const auto dy = wrapped_difference(geometry.aim_from[1], geometry.target[1]);
    const auto dz = wrapped_difference(geometry.aim_from[2], geometry.target[2]);
    const auto heading = static_cast<int16_t>(
        static_cast<int16_t>(base::game_math::direction(dx, dz)) - geometry.unit_yaw
    );
    const auto pitch = launch_pitch(parameters, dx, dy, dz);
    return {heading, pitch, pitch != invalid_launch_pitch};
}

TurretAimResult turret_aim_line_of_sight(const TurretAimGeometry& geometry) noexcept {
    // Heading from base::game_math::direction, then pitch from base::game_math::direction
    // over the wrapped dy high word and the horizontal range's high word.
    const auto dx = wrapped_difference(geometry.aim_from[0], geometry.target[0]);
    const auto dy = wrapped_difference(geometry.aim_from[1], geometry.target[1]);
    const auto dz = wrapped_difference(geometry.aim_from[2], geometry.target[2]);
    const auto heading = static_cast<int16_t>(
        static_cast<int16_t>(base::game_math::direction(dx, dz)) - geometry.unit_yaw
    );
    const auto horizontal = base::game_math::distance(dx, dz);
    const auto dy_high =
        static_cast<int16_t>(static_cast<uint16_t>(static_cast<uint32_t>(dy) >> 16U));
    const auto horizontal_high =
        static_cast<int16_t>(static_cast<uint16_t>(static_cast<uint32_t>(horizontal) >> 16U));
    const auto pitch = static_cast<int16_t>(base::game_math::direction(
        static_cast<int32_t>(-static_cast<int32_t>(dy_high)), static_cast<int32_t>(horizontal_high)
    ));
    return {heading, pitch, true};
}

TurretAimResult turret_aim(
    uint32_t weapon_flags, const BallisticParameters& parameters, const TurretAimGeometry& geometry
) noexcept {
    if ((weapon_flags & ballistic_weapon_flag) != 0U)
        return turret_aim_ballistic(parameters, geometry);
    if ((weapon_flags & line_of_sight_weapon_flag) != 0U)
        return turret_aim_line_of_sight(geometry);
    return {};
}

int32_t simulation_gravity(
    bool campaign_gravity_applies, int32_t campaign_gravity, int32_t map_gravity
) noexcept {
    // The exact binary64 constants 3.1c uses: 65536 and 1/900.
    constexpr double fixed_one = 65536.0;
    constexpr double per_tick_squared = std::bit_cast<double>(uint64_t{0x3f523456789abcdf});
    int32_t setting = 0;
    if (campaign_gravity_applies && campaign_gravity >= 0)
        setting = campaign_gravity;
    else if (map_gravity != 0)
        setting = map_gravity;
    else
        return default_simulation_gravity;
    const auto scaled =
        rounded(rounded(static_cast<double>(setting) * fixed_one) * per_tick_squared);
    return static_cast<int32_t>(std::trunc(scaled));
}

} // namespace oa::sim::ballistics
