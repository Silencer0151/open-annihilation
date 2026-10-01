// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/air/flight.hpp"

#include "oa/base/game_math.hpp"

#include <algorithm>
#include <cmath>

namespace oa::sim::air {
namespace {

using sim::unit_movement::Fixed;

constexpr float fixed_to_world = 1.52587890625e-05f; // 1 / 65536
constexpr double world_to_fixed = 65536.0;
constexpr float minimum_steer_distance = 8.0f; // world units
constexpr int32_t minimum_climb_speed = 0x40000;
constexpr int32_t minimum_climb_rate = 0x10000;

// Keeps each product at double precision so the compiler cannot fuse it.
double rounded(double value) noexcept {
    volatile double result = value;
    return result;
}

float to_float(double value) noexcept {
    volatile float result = static_cast<float>(value);
    return result;
}

// Truncating float to integer conversion keeping the low 32 bits.
int32_t truncate(double value) noexcept {
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

int32_t scale_fixed(int32_t value, int32_t scale) noexcept {
    return static_cast<int32_t>((static_cast<int64_t>(value) * scale) >> 16);
}

} // namespace

double planar_length(double x, double z) noexcept {
    return base::game_math::hypotenuse(x, z);
}

void rotate_xz(int16_t angle, int32_t* x, int32_t* z) noexcept {
    if (angle == 0)
        return;
    const auto rotated = base::game_math::rotate_pair(*x, *z, angle);
    *x = static_cast<int32_t>(std::nearbyint(rotated.first));
    *z = static_cast<int32_t>(std::nearbyint(rotated.second));
}

void air_flight_step(
    sim::unit_movement::Unit& unit,
    sim::unit_movement::Movement& movement,
    std::array<Fixed, 3>& attitude,
    const AirSteeringTarget& target,
    const UnitDef& def,
    oa_fixed gravity,
    bool outside_map,
    int16_t* bank
) {
    auto& velocity = movement.velocity;
    if ((movement.flags & sim::unit_movement::occupancy_mask) != layer_air) {
        velocity = {0, 0, 0};
        movement.speed = 0;
        movement.turn = 0;
        return;
    }
    const auto previous = velocity;
    const float acceleration = to_float(static_cast<double>(def.acceleration) * fixed_to_world);
    int32_t damping = 0;
    if (def.max_velocity != 0)
        damping =
            static_cast<int32_t>((static_cast<int64_t>(def.acceleration) << 16) / def.max_velocity);
    for (auto& component : velocity)
        component = scale_fixed(component, 0x10000 - damping);

    // Horizontal speed above brake_rate is turned onto the unit's heading.
    const float horizontal = to_float(
        planar_length(static_cast<double>(velocity[0]), static_cast<double>(velocity[2])) *
        fixed_to_world
    );
    const float brake = to_float(static_cast<double>(def.brake_rate) * fixed_to_world);
    if (horizontal > brake) {
        const int32_t keep =
            truncate(rounded(rounded(static_cast<double>(brake) / horizontal) * world_to_fixed));
        velocity[0] = scale_fixed(keep, velocity[0]);
        velocity[2] = scale_fixed(velocity[2], keep);
        const int32_t excess =
            truncate(rounded(rounded(static_cast<double>(horizontal) - brake) * world_to_fixed));
        velocity[0] += -sim::unit_movement::sine_scaled(unit.heading, excess);
        velocity[2] += -sim::unit_movement::cosine_scaled(unit.heading, excess);
    }

    const int32_t dz = unit.position[2] - target.position.z;
    const int32_t dx = unit.position[0] - target.position.x;
    const int32_t relative_x = velocity[0] - target.velocity.x;
    const int32_t dy = unit.position[1] - target.position.y;
    const int32_t relative_z = velocity[2] - target.velocity.z;
    if (!outside_map) {
        const int32_t speed = movement.speed;
        const int32_t climb = (speed & ~3) < minimum_climb_speed ? minimum_climb_rate : speed >> 2;
        if (dy <= -climb)
            velocity[1] = climb;
        else if (dy >= climb)
            velocity[1] = -climb;
        else
            velocity[1] = -dy;
    }

    const float distance =
        to_float(planar_length(static_cast<double>(dx), static_cast<double>(dz)) * fixed_to_world);
    sim::unit_movement::turn(unit, movement, static_cast<int16_t>(target.heading - unit.heading));
    const float steer_distance =
        distance < minimum_steer_distance ? minimum_steer_distance : distance;
    const double gain = -base::game_math::square_root(
        rounded(rounded(static_cast<double>(acceleration) + acceleration) / steer_distance)
    );
    const double scale = fixed_to_world;
    float ax = to_float(rounded(rounded(rounded(dx * gain) * scale) - rounded(relative_x * scale)));
    float az = to_float(rounded(rounded(rounded(dz * gain) * scale) - rounded(relative_z * scale)));
    const double magnitude = planar_length(ax, az);
    if (magnitude > acceleration) {
        const double ratio = rounded(acceleration / magnitude);
        ax = to_float(rounded(ratio * ax));
        az = to_float(rounded(ratio * az));
    }
    velocity[0] += truncate(rounded(static_cast<double>(ax) * world_to_fixed));
    velocity[2] += truncate(rounded(static_cast<double>(az) * world_to_fixed));
    movement.speed = base::game_math::truncated_length(velocity[0], velocity[1], velocity[2]);
    const std::array<Fixed, 3> change{
        velocity[0] - previous[0],
        velocity[1] - previous[1],
        velocity[2] - previous[2],
    };
    air_attitude(unit, attitude, change, def.bank_scale, def.pitch_scale, gravity, bank);
}

void air_attitude(
    sim::unit_movement::Unit& unit,
    std::array<Fixed, 3>& filtered,
    const std::array<Fixed, 3>& change,
    int32_t bank_scale,
    int32_t pitch_scale,
    oa_fixed gravity,
    int16_t* bank
) noexcept {
    for (std::size_t i = 0; i < filtered.size(); ++i)
        filtered[i] = static_cast<Fixed>(
            static_cast<uint32_t>(scale_fixed(filtered[i], attitude_decay)) +
            static_cast<uint32_t>(change[i])
        );
    int32_t x = filtered[0];
    int32_t z = filtered[2];
    rotate_xz(static_cast<int16_t>(unit.heading), &x, &z);
    const auto lateral = static_cast<int32_t>(0u - static_cast<uint32_t>(x));
    const auto vertical =
        static_cast<int32_t>((static_cast<int64_t>(gravity) << 16) / attitude_gravity_divisor);
    const auto roll = static_cast<int32_t>((static_cast<int64_t>(bank_scale) * lateral) >> 16);
    const auto tilt = static_cast<int32_t>((static_cast<int64_t>(pitch_scale) * lateral) >> 16);
    if (bank != nullptr)
        *bank = static_cast<int16_t>(base::game_math::direction(roll, vertical));
    unit.pitch = static_cast<int16_t>(base::game_math::direction(tilt, vertical));
}

} // namespace oa::sim::air
