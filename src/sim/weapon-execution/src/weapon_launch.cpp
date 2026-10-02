// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/weapon_execution/weapon_launch.hpp"

#include "oa/sim/combat_state.hpp"
#include "oa/base/game_math.hpp"
#include "oa/sim/weapon_execution/interceptor.hpp"
#include "oa/sim/unit_movement/movement.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace oa::sim::weapon_execution {
namespace {
int32_t wrap_sub(int32_t a, int32_t b) noexcept {
    return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) - std::bit_cast<uint32_t>(b));
}

int32_t high_short(int32_t value) noexcept {
    return static_cast<int16_t>(static_cast<uint16_t>(std::bit_cast<uint32_t>(value) >> 16));
}

FixedVector flight_velocity(uint16_t heading, uint16_t pitch, int32_t speed) noexcept {
    const auto velocity = sim::unit_movement::aim_velocity(heading, pitch, speed);
    return {velocity[0], velocity[1], velocity[2]};
}

uint16_t word(int16_t value) noexcept {
    return static_cast<uint16_t>(value);
}
} // namespace

uint32_t weapon_muzzle_offset(const UnitWeapon& slot) noexcept {
    uint32_t value = 0;
    std::memcpy(&value, slot.muzzle_offset, sizeof value);
    return value;
}

void set_weapon_muzzle_offset(UnitWeapon& slot, int32_t value) noexcept {
    std::memcpy(slot.muzzle_offset, &value, sizeof value);
}

void arm_weapon_slot(
    UnitWeapon& slot,
    const sim::combat_state::WeaponSlot& armed,
    const sim::combat_state::WeaponDefinition* definition
) noexcept {
    slot.flags = armed.flags;
    slot.def = definition != nullptr ? oa_ref_from_index(definition->registry_index) : 0;
    slot.stockpile = 0;
    slot.reload = 0;
    set_weapon_muzzle_offset(slot, armed.muzzle_offset);
}

void record_aim_result(UnitWeapon& slot, int32_t result) noexcept {
    if (result != 0)
        slot.aim_ready = 1;
}

uint16_t damage_cheat_flags(const Game& game) noexcept {
    return game.console_flags;
}

Bearing bearing_toward(const FixedVector& from, const FixedVector& to) noexcept {
    const auto dx = wrap_sub(from[0], to[0]);
    const auto dz = wrap_sub(from[2], to[2]);
    Bearing bearing;
    bearing.heading = base::game_math::direction(dx, dz);
    bearing.distance = std::bit_cast<int32_t>(base::game_math::distance(dx, dz));
    const auto rise = high_short(wrap_sub(from[1], to[1]));
    bearing.pitch = base::game_math::direction(-rise, high_short(bearing.distance));
    return bearing;
}

int32_t launch_speed(const WeaponDef& weapon) noexcept {
    if (weapon.start_velocity != 0)
        return weapon.start_velocity;
    if (weapon.weapon_acceleration != 0)
        return 0;
    return weapon.weapon_velocity;
}

uint32_t auto_range_expiry_tick(const WeaponDef& weapon, uint32_t current_tick) noexcept {
    uint32_t ticks = word(weapon.weapon_timer);
    const uint32_t flags = weapon.flags;
    const int32_t range = weapon.range;
    const oa_fixed weapon_velocity = weapon.weapon_velocity;
    if (weapon_velocity != 0 && (flags & OA_WEAPON_FLAG_NO_AUTO_RANGE) == 0)
        ticks = (std::bit_cast<uint32_t>(range) << 16) / std::bit_cast<uint32_t>(weapon_velocity);
    return ticks + current_tick;
}

ProjectileLaunch launch_line_projectile(
    const WeaponDef& weapon,
    const FixedVector& muzzle,
    const FixedVector& target,
    uint32_t current_tick
) noexcept {
    const auto bearing = bearing_toward(muzzle, target);
    ProjectileLaunch launch;
    launch.heading = bearing.heading;
    launch.pitch = bearing.pitch;
    launch.distance = bearing.distance;
    launch.speed = launch_speed(weapon);
    launch.velocity = flight_velocity(launch.heading, launch.pitch, launch.speed);
    launch.lifetime_tick = auto_range_expiry_tick(weapon, current_tick);
    launch.burst_remaining = word(weapon.burst);
    return launch;
}

ProjectileLaunch
launch_vertical_projectile(const WeaponDef& weapon, uint32_t current_tick) noexcept {
    ProjectileLaunch launch;
    launch.pitch = vertical_launch_pitch;
    launch.speed = launch_speed(weapon);
    launch.lifetime_tick = auto_range_expiry_tick(weapon, current_tick);
    launch.burst_remaining = word(weapon.burst);
    return launch;
}

ProjectileLaunch launch_ballistic_projectile(
    const WeaponDef& weapon,
    const UnitWeapon& slot,
    int32_t gravity,
    const FixedVector& muzzle,
    const FixedVector& target,
    uint32_t current_tick
) noexcept {
    ProjectileLaunch launch;
    launch.heading = word(slot.aim_heading);
    launch.pitch = word(slot.aim_pitch);
    const oa_fixed weapon_velocity = weapon.weapon_velocity;
    const auto velocity = std::bit_cast<uint32_t>(weapon_velocity);
    // A zero velocity costs the barrel no gravity.
    const auto barrel_ticks = velocity != 0 ? weapon_muzzle_offset(slot) / velocity : 0U;
    const auto lift = sim::unit_movement::sine_scaled(launch.pitch, weapon.weapon_velocity);
    launch.velocity[1] = std::bit_cast<int32_t>(
        std::bit_cast<uint32_t>(lift) - barrel_ticks * std::bit_cast<uint32_t>(gravity)
    );
    const auto level = sim::unit_movement::cosine_scaled(launch.pitch, weapon.weapon_velocity);
    launch.velocity[0] = -sim::unit_movement::sine_scaled(launch.heading, level);
    launch.velocity[2] = -sim::unit_movement::cosine_scaled(launch.heading, level);
    uint32_t ticks = word(weapon.weapon_timer);
    const uint32_t flags = weapon.flags;
    if ((flags & OA_WEAPON_FLAG_BURN_BLOW) != 0) {
        const auto ground = std::bit_cast<int32_t>(base::game_math::distance(
            wrap_sub(muzzle[0], target[0]), wrap_sub(muzzle[2], target[2])
        ));
        ticks = level != 0 ? static_cast<uint32_t>(static_cast<int64_t>(ground) / level) : 0U;
    }
    launch.lifetime_tick = ticks + current_tick;
    launch.burst_remaining = word(weapon.burst);
    return launch;
}

ProjectileLaunch launch_dropped_projectile(const Unit& unit, int32_t unit_speed) noexcept {
    ProjectileLaunch launch;
    launch.heading = unit.heading;
    launch.velocity[0] = -sim::unit_movement::sine_scaled(unit.heading, unit_speed);
    launch.velocity[2] = -sim::unit_movement::cosine_scaled(unit.heading, unit_speed);
    return launch;
}

AimAngles apply_accuracy_spread(
    const Unit& unit,
    const UnitDef& type,
    const WeaponDef& weapon,
    AimAngles slot_aim,
    RandomBounded random,
    void* random_context
) {
    slot_aim.heading = static_cast<int16_t>(slot_aim.heading + static_cast<int16_t>(unit.heading));
    const auto spread = sim::combat_state::accuracy_spread(
        weapon.accuracy, unit.health, type.max_damage, unit.veteran_level
    );
    if (spread == 0)
        return slot_aim;
    const auto half = static_cast<int16_t>(spread >> 1);
    const auto heading_offset = static_cast<int16_t>(random(random_context, spread));
    slot_aim.heading = static_cast<int16_t>(slot_aim.heading + (heading_offset - half));
    const auto pitch_offset = static_cast<int16_t>(random(random_context, spread));
    slot_aim.pitch = static_cast<int16_t>(slot_aim.pitch + (pitch_offset - half));
    return slot_aim;
}

std::array<int32_t, 2> rock_unit_arguments(const Unit& unit, uint16_t slot_heading) noexcept {
    const auto relative = static_cast<uint16_t>(slot_heading - unit.heading);
    return {
        -sim::unit_movement::cosine_scaled(relative, rock_unit_magnitude),
        -sim::unit_movement::sine_scaled(relative, rock_unit_magnitude)
    };
}

bool lead_applies(const WeaponDef& weapon, const Unit& shooter, bool target_has_movement) noexcept {
    constexpr uint16_t minimum_leading_veteran_level = 5;
    const uint32_t flags = weapon.flags;
    return (flags & OA_WEAPON_FLAG_CRUISE) == 0 && target_has_movement &&
           shooter.veteran_level > minimum_leading_veteran_level && weapon.weapon_velocity != 0;
}

FixedVector veteran_lead_offset(
    const WeaponDef& weapon,
    const Unit& shooter,
    const FixedVector& aim_point,
    const FixedVector& target_velocity
) noexcept {
    constexpr int64_t lead_fraction = 0xcccc; // 0.8
    FixedVector offset{};
    if (weapon.weapon_velocity == 0)
        return offset;
    const auto length = base::game_math::truncated_length(
        wrap_sub(shooter.position.x, aim_point[0]),
        wrap_sub(shooter.position.y, aim_point[1]),
        wrap_sub(shooter.position.z, aim_point[2])
    );
    const auto travel = static_cast<int32_t>(
        static_cast<int64_t>(static_cast<uint64_t>(static_cast<int64_t>(length)) << 16) /
        weapon.weapon_velocity
    );
    const auto factor = static_cast<int32_t>((static_cast<int64_t>(travel) * lead_fraction) >> 16);
    base::game_math::scale_vector_fixed(offset.data(), target_velocity.data(), factor);
    return offset;
}

BurstChild burst_child(
    const WeaponDef& weapon,
    int32_t parent_distance,
    int32_t parent_speed,
    uint16_t parent_heading,
    uint16_t parent_pitch,
    const FixedVector& parent_velocity,
    uint32_t current_tick,
    RandomBounded random,
    void* random_context
) {
    constexpr uint32_t burst_reach_bias = 0x100000;
    BurstChild child;
    child.parent_velocity = parent_velocity;
    const auto timer = word(weapon.weapon_timer);
    if (timer == 0) {
        const auto speed = std::bit_cast<uint32_t>(parent_speed);
        // A zero speed makes the child expire at once.
        child.lifetime_tick =
            (speed != 0 ? (std::bit_cast<uint32_t>(parent_distance) + burst_reach_bias) / speed
                        : 0U) +
            current_tick;
    } else
        child.lifetime_tick = current_tick + timer;
    const auto decay = word(weapon.random_decay);
    if (decay != 0)
        child.lifetime_tick += random(random_context, decay) - (static_cast<uint32_t>(decay) >> 1);
    const auto spray = word(weapon.spray_angle);
    if (spray != 0) {
        const auto roll = random(random_context, spray);
        const auto heading = static_cast<uint16_t>(
            static_cast<uint16_t>(roll) + static_cast<uint16_t>(parent_heading - (spray >> 1))
        );
        const auto level = sim::unit_movement::cosine_scaled(parent_pitch, weapon.weapon_velocity);
        child.parent_velocity[0] = -sim::unit_movement::sine_scaled(heading, level);
        child.parent_velocity[2] = -sim::unit_movement::cosine_scaled(heading, level);
        child.sprayed = true;
    }
    return child;
}

bool steer_projectile(
    uint16_t& heading, uint16_t& pitch, const Bearing& desired, const WeaponDef& weapon
) noexcept {
    constexpr int32_t burnout_error = 0x6979;
    const uint32_t flags = weapon.flags;
    const bool burn_blow = (flags & OA_WEAPON_FLAG_BURN_BLOW) != 0;
    const auto rate = word(weapon.turn_rate);
    const auto turn = [&](uint16_t& angle, uint16_t goal) {
        const auto error = static_cast<int16_t>(static_cast<uint16_t>(goal - angle));
        const auto magnitude = static_cast<int16_t>(std::abs(static_cast<int32_t>(error)));
        if (magnitude >= burnout_error && burn_blow)
            return false;
        if (magnitude < static_cast<int32_t>(rate))
            angle = goal;
        else if (error < 0)
            angle = static_cast<uint16_t>(angle - rate);
        else
            angle = static_cast<uint16_t>(angle + rate);
        return true;
    };
    return turn(heading, desired.heading) && turn(pitch, desired.pitch);
}

FlightMode flight_mode(const WeaponDef& weapon) noexcept {
    const uint32_t flags = weapon.flags;
    if ((flags & OA_WEAPON_FLAG_SELF_PROP) != 0)
        return FlightMode::self_propelled;
    if ((flags & OA_WEAPON_FLAG_LINE_OF_SIGHT) != 0)
        return FlightMode::line;
    if ((flags & OA_WEAPON_FLAG_BALLISTIC) != 0)
        return FlightMode::ballistic;
    if ((flags & OA_WEAPON_FLAG_DROPPED) != 0)
        return FlightMode::dropped;
    if ((flags & OA_WEAPON_FLAG_METEOR) != 0)
        return FlightMode::meteor;
    return FlightMode::inert;
}

int32_t
projectile_damage(int32_t amount, float scale, const Unit* source, const Game& game) noexcept {
    constexpr int32_t veteran_step = 5;
    constexpr int32_t maximum_veteran_steps = 5;
    constexpr int32_t percent_per_step = 6;
    constexpr int32_t whole_percent = 100;
    const auto scaled = static_cast<double>(amount) * static_cast<double>(scale);
    // Out-of-range conversions give 0x80000000.
    auto damage = std::isfinite(scaled) && std::fabs(scaled) < 2147483648.0
                      ? static_cast<int32_t>(std::trunc(scaled))
                      : static_cast<int32_t>(0x80000000U);
    if (source != nullptr) {
        auto steps = static_cast<int32_t>(source->veteran_level) / veteran_step;
        if (steps > maximum_veteran_steps)
            steps = maximum_veteran_steps;
        damage = std::bit_cast<int32_t>(
                     std::bit_cast<uint32_t>(steps * percent_per_step + whole_percent) *
                     std::bit_cast<uint32_t>(damage)
                 ) /
                 whole_percent;
    }
    const auto cheats = damage_cheat_flags(game);
    if ((cheats & damage_cheat_double) != 0)
        damage = std::bit_cast<int32_t>(std::bit_cast<uint32_t>(damage) * 2U);
    if ((cheats & damage_cheat_halve) != 0)
        damage /= 2;
    return damage;
}

float area_damage_scale(const WeaponDef& weapon, int32_t reach, int32_t radius) noexcept {
    if (reach == 0)
        return 1.0F;
    const auto edge = static_cast<double>(weapon.edge_effectiveness);
    const auto fraction = static_cast<double>(reach) / static_cast<double>(radius) - 1.0;
    return static_cast<float>(fraction * fraction * (1.0 - edge) + edge);
}

bool note_blast_unit(AreaBlastVisits& visits, const Unit& unit) noexcept {
    for (int32_t i = 0; i < visits.unit_count; ++i)
        if (visits.units[i] == &unit)
            return false;
    if (visits.unit_count < area_blast_unit_capacity)
        visits.units[visits.unit_count++] = &unit;
    return true;
}

bool note_blast_feature(AreaBlastVisits& visits, uint32_t plot) noexcept {
    for (int32_t i = 0; i < visits.feature_count; ++i)
        if (visits.feature_plots[i] == plot)
            return false;
    if (visits.feature_count < area_blast_feature_capacity)
        visits.feature_plots[visits.feature_count++] = plot;
    return true;
}

int32_t blast_reach_to_box(
    const FixedVector& point, const FixedVector& low, const FixedVector& high
) noexcept {
    FixedVector gap{};
    for (std::size_t axis = 0; axis < gap.size(); ++axis) {
        if (point[axis] < low[axis])
            gap[axis] = wrap_sub(low[axis], point[axis]);
        else if (high[axis] < point[axis])
            gap[axis] = wrap_sub(point[axis], high[axis]);
    }
    const auto length =
        std::bit_cast<uint32_t>(base::game_math::truncated_length(gap[0], gap[1], gap[2]));
    return static_cast<int16_t>(length >> 16);
}

uint16_t impact_direction(const FixedVector& impact, const Unit& unit) noexcept {
    return static_cast<uint16_t>(
        base::game_math::direction(
            wrap_sub(impact[0], unit.position.x), wrap_sub(impact[2], unit.position.z)
        ) -
        unit.heading
    );
}

bool launch_turret_projectile(
    const WeaponDef& weapon,
    const UnitWeapon& aimed,
    int32_t gravity,
    const FixedVector& muzzle,
    const FixedVector& target,
    uint32_t current_tick,
    ProjectileLaunch* out
) noexcept {
    switch (projectile_route(weapon.flags)) {
    case ProjectileRoute::line:
        *out = launch_line_projectile(weapon, muzzle, target, current_tick);
        return true;
    case ProjectileRoute::ballistic:
        *out = launch_ballistic_projectile(weapon, aimed, gravity, muzzle, target, current_tick);
        return true;
    case ProjectileRoute::none:
        break;
    }
    return false;
}

ShotPlan plan_line_shot(
    const WeaponDef& weapon,
    const Unit& unit,
    const FixedVector& muzzle,
    const FixedVector& target,
    uint32_t current_tick
) noexcept {
    ShotPlan plan;
    const auto bearing = bearing_toward(muzzle, target);
    plan.slot_aim = {
        std::bit_cast<int16_t>(bearing.heading), std::bit_cast<int16_t>(bearing.pitch)
    };
    const oa_angle unit_heading = unit.heading;
    if (!turret_within_tolerance(
            {word(weapon.tolerance),
             word(weapon.pitch_tolerance),
             plan.slot_aim.heading,
             plan.slot_aim.pitch,
             std::bit_cast<int16_t>(unit_heading),
             unit.pitch,
             (unit.flags & OA_UNIT_FLAG_MOVE_RATE_MASK) != 0}
        ))
        return plan;
    plan.launch = launch_line_projectile(weapon, muzzle, target, current_tick);
    plan.fired = true;
    plan.fire_script = true;
    return plan;
}

ShotPlan plan_dropped_shot(const Unit& unit, uint8_t slot, int32_t unit_speed) noexcept {
    ShotPlan plan;
    plan.slot_aim = {unit.weapons[slot].aim_heading, unit.weapons[slot].aim_pitch};
    plan.launch = launch_dropped_projectile(unit, unit_speed);
    plan.fired = true;
    return plan;
}

ShotPlan plan_weapon_shot(
    const WeaponDef& weapon,
    const Unit& unit,
    const UnitDef& type,
    uint8_t slot,
    const FixedVector& muzzle,
    const FixedVector& target,
    int32_t unit_speed,
    const World& world,
    RandomBounded random,
    void* random_context
) {
    const Game& game = world.game;
    const auto& weapon_slot = unit.weapons[slot];
    ShotPlan plan;
    plan.slot_aim = {weapon_slot.aim_heading, weapon_slot.aim_pitch};
    switch (select_fire_mode(weapon.flags)) {
    case FireMode::none:
        return plan;
    case FireMode::turret: {
        plan.spends_aim = true;
        plan.slot_aim =
            apply_accuracy_spread(unit, type, weapon, plan.slot_aim, random, random_context);
        auto aimed = weapon_slot;
        aimed.aim_heading = plan.slot_aim.heading;
        aimed.aim_pitch = plan.slot_aim.pitch;
        if (!launch_turret_projectile(
                weapon, aimed, game.gravity, muzzle, target, game.tick, &plan.launch
            ))
            return plan;
        break;
    }
    case FireMode::vertical_launch: {
        const VerticalLaunchShot shot =
            plan_vertical_launch_shot(world, weapon, unit, slot, muzzle, target, game.tick);
        plan.slot_aim = shot.slot_aim;
        if (!shot.fired)
            return plan;
        plan.launch = shot.launch;
        plan.intercept_target = shot.intercept_target;
        plan.spends_aim = true;
        break;
    }
    case FireMode::line:
        return plan_line_shot(weapon, unit, muzzle, target, game.tick);
    case FireMode::dropped:
        return plan_dropped_shot(unit, slot, unit_speed);
    }
    plan.fired = true;
    plan.fire_script = true;
    return plan;
}

} // namespace oa::sim::weapon_execution
