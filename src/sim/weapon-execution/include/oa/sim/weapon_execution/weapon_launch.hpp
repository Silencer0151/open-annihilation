// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/core/game_state.h"
#include "oa/core/unit.h"
#include "oa/core/unit_def.h"
#include "oa/core/weapon_def.h"
#include "oa/core/world.h"
#include "oa/sim/combat_state.hpp"
#include "oa/sim/weapon_execution.hpp"

#include <array>
#include <cstdint>

// Projectile launch, flight and impact arithmetic of the weapon constructors,
// over the canonical records. The owner of the projectile pool applies the
// results to its records.
namespace oa::sim::weapon_execution {

using FixedVector = std::array<int32_t, 3>; // signed 16.16 world units

inline constexpr uint16_t vertical_launch_pitch = 0x4000;
inline constexpr int32_t rock_unit_magnitude = 800;

// Values written into a new projectile record.
struct ProjectileLaunch {
    uint16_t heading{};
    uint16_t pitch{};
    int32_t speed{};
    int32_t distance{}; // muzzle-to-target ground distance, truncated
    FixedVector velocity{};
    uint32_t lifetime_tick{};
    uint16_t burst_remaining{};
};

struct Bearing {
    uint16_t heading{};
    uint16_t pitch{};
    int32_t distance{}; // low 32 bits of the truncated ground distance
};

struct AimAngles {
    int16_t heading{};
    int16_t pitch{};
};

using RandomBounded = uint32_t (*)(void* context, uint32_t limit);

/// Records the return of a slot's Aim script, the callback an aim start hands it.
///
/// @param[in,out] slot weapon slot; a nonzero result readies it to fire
/// @param result the script's return value; zero leaves the slot waiting
void record_aim_result(UnitWeapon& slot, int32_t result) noexcept;

/// Returns a slot's muzzle offset (UnitWeapon.muzzle_offset).
///
/// It is 1.25 times the Z offset of the QueryWeapon piece from the AimFrom piece,
/// which sim::combat_state::initialize_spawn_combat computes and arm_weapon_slot
/// stores.
///
/// @param slot weapon slot
/// @return the offset, 16.16 world units
[[nodiscard]] uint32_t weapon_muzzle_offset(const UnitWeapon& slot) noexcept;
/// Stores a slot's muzzle offset (UnitWeapon.muzzle_offset).
///
/// @param[in,out] slot weapon slot
/// @param value offset, 16.16 world units
void set_weapon_muzzle_offset(UnitWeapon& slot, int32_t value) noexcept;
/// Arms a new unit's canonical weapon slot from the slot its spawn armed.
///
/// The slot takes the armed flags and muzzle offset and refers to `definition`;
/// its reload countdown and stockpile start at zero. Targets and aim are kept.
///
/// @param[in,out] slot canonical weapon slot
/// @param armed the slot sim::combat_state::initialize_spawn_combat armed
/// @param definition the slot's weapon, or null for none
void arm_weapon_slot(
    UnitWeapon& slot,
    const sim::combat_state::WeaponSlot& armed,
    const sim::combat_state::WeaponDefinition* definition
) noexcept;

// Bits of Game.console_flags that double and halve weapon damage.
inline constexpr uint16_t damage_cheat_double = OA_CONSOLE_FLAG_DOUBLE_SHOT;
inline constexpr uint16_t damage_cheat_halve = OA_CONSOLE_FLAG_HALF_SHOT;
/// Returns the damage cheat word (Game.console_flags).
///
/// @param game game record
/// @return damage_cheat_double and damage_cheat_halve bits
[[nodiscard]] uint16_t damage_cheat_flags(const Game& game) noexcept;

/// Returns the heading and elevation from one point toward another.
///
/// @param from start point, 16.16 world coordinates
/// @param to end point, 16.16 world coordinates
/// @return heading and pitch (65536 per turn) and the truncated ground distance, 16.16
[[nodiscard]] Bearing bearing_toward(const FixedVector& from, const FixedVector& to) noexcept;

/// Returns a new projectile's speed.
///
/// @param weapon weapon definition
/// @return the start velocity, else zero for an accelerating weapon, else the weapon
///         velocity; 16.16 world units per tick
[[nodiscard]] int32_t launch_speed(const WeaponDef& weapon) noexcept;

/// Returns the tick a projectile expires by its range.
///
/// @param weapon weapon definition
/// @param current_tick current game tick
/// @return range / velocity ticks from now, or the weapon timer when the velocity is zero
///         or noautorange is set
[[nodiscard]] uint32_t
auto_range_expiry_tick(const WeaponDef& weapon, uint32_t current_tick) noexcept;

/// Launches a projectile straight at the target.
///
/// @param weapon weapon definition
/// @param muzzle launch point, 16.16 world coordinates
/// @param target aim point, 16.16 world coordinates
/// @param current_tick current game tick
/// @return heading, pitch, speed, velocity, range expiry and burst count
[[nodiscard]] ProjectileLaunch launch_line_projectile(
    const WeaponDef& weapon,
    const FixedVector& muzzle,
    const FixedVector& target,
    uint32_t current_tick
) noexcept;

/// Launches a projectile straight up with no velocity; guidance turns it once under way.
///
/// @param weapon weapon definition
/// @param current_tick current game tick
/// @return pitch 0x4000, launch speed, range expiry and burst count
[[nodiscard]] ProjectileLaunch
launch_vertical_projectile(const WeaponDef& weapon, uint32_t current_tick) noexcept;

/// Launches a shell along the slot's aim angles.
///
/// A zero velocity takes no lift for the muzzle offset.
///
/// @param weapon weapon definition
/// @param slot aimed weapon slot; its muzzle offset costs lift
/// @param gravity game gravity, 16.16 per tick
/// @param muzzle launch point, 16.16 world coordinates
/// @param target aim point, 16.16 world coordinates
/// @param current_tick current game tick
/// @return heading, pitch, velocity, expiry and burst count
/// @quirk The lift loses gravity for the whole ticks the shell needs to cross the slot's
///        muzzle offset (unsigned division). Burnblow shells expire after the ground
///        distance at their horizontal speed.
[[nodiscard]] ProjectileLaunch launch_ballistic_projectile(
    const WeaponDef& weapon,
    const UnitWeapon& slot,
    int32_t gravity,
    const FixedVector& muzzle,
    const FixedVector& target,
    uint32_t current_tick
) noexcept;

/// Launches a dropped bomb with the unit's heading and ground speed.
///
/// @param unit dropping unit
/// @param unit_speed its ground speed, 16.16 per tick
/// @return heading and horizontal velocity; no lift and no lifetime
[[nodiscard]] ProjectileLaunch
launch_dropped_projectile(const Unit& unit, int32_t unit_speed) noexcept;

/// Turns a slot's aim into absolute angles with the weapon's random spread.
///
/// The slot heading becomes absolute, then both angles gain a random offset in
/// [-spread/2, spread/2) from two draws (sim::combat_state::accuracy_spread).
///
/// @param unit firing unit
/// @param type its unit type
/// @param weapon weapon definition
/// @param slot_aim slot's aim, heading relative to the unit
/// @param random synced random stream
/// @param random_context passed to `random`
/// @return the spread angles
[[nodiscard]] AimAngles apply_accuracy_spread(
    const Unit& unit,
    const UnitDef& type,
    const WeaponDef& weapon,
    AimAngles slot_aim,
    RandomBounded random,
    void* random_context
);

/// Returns the arguments of the RockUnit script call after a shot.
///
/// @param unit firing unit
/// @param slot_heading slot's absolute aim heading
/// @return the recoil as -cos and -sin of the slot heading relative to the unit, scaled
///         by 800
[[nodiscard]] std::array<int32_t, 2>
rock_unit_arguments(const Unit& unit, uint16_t slot_heading) noexcept;

/// Tests whether a shooter leads its target.
///
/// @param weapon weapon definition
/// @param shooter firing unit
/// @param target_has_movement whether the target has a movement object
/// @return true for veterans above level 5 against a moving target, except cruise and
///         zero-velocity weapons
[[nodiscard]] bool
lead_applies(const WeaponDef& weapon, const Unit& shooter, bool target_has_movement) noexcept;

/// Returns the lead a veteran adds to its aim point.
///
/// @param weapon weapon definition
/// @param shooter firing unit
/// @param aim_point aim point, 16.16 world coordinates
/// @param target_velocity target's velocity, 16.16 per tick
/// @return 0.8 of the travel time times the target velocity; zero for a zero-velocity weapon
[[nodiscard]] FixedVector veteran_lead_offset(
    const WeaponDef& weapon,
    const Unit& shooter,
    const FixedVector& aim_point,
    const FixedVector& target_velocity
) noexcept;

struct BurstChild {
    uint32_t lifetime_tick{};
    FixedVector parent_velocity{}; // spray turns the spawner for the next child
    bool sprayed{};
};

/// Times and aims the next child of a burst.
///
/// Without a weapon timer the child flies (distance + 16 units) / speed ticks; random
/// decay then shifts that by [-decay/2, decay/2), and a spray angle turns the spawner
/// for the next child. Decay draws before spray.
///
/// @param weapon weapon definition
/// @param parent_distance spawner's launch distance, 16.16
/// @param parent_speed spawner's speed, 16.16 per tick; zero expires the child at once
/// @param parent_heading spawner's heading
/// @param parent_pitch spawner's pitch
/// @param parent_velocity spawner's velocity
/// @param current_tick current game tick
/// @param random synced random stream
/// @param random_context passed to `random`
/// @return the child's expiry tick and the spawner's next velocity
[[nodiscard]] BurstChild burst_child(
    const WeaponDef& weapon,
    int32_t parent_distance,
    int32_t parent_speed,
    uint16_t parent_heading,
    uint16_t parent_pitch,
    const FixedVector& parent_velocity,
    uint32_t current_tick,
    RandomBounded random,
    void* random_context
);

/// Turns a guided projectile toward a bearing by at most the turn rate per axis, heading first.
///
/// @param[in,out] heading projectile heading
/// @param[in,out] pitch projectile pitch
/// @param desired bearing to the target
/// @param weapon weapon definition (turn rate, burnblow)
/// @return false when a burnblow projectile's error on an axis reaches 0x6979: it stops
///         turning and detonates
[[nodiscard]] bool steer_projectile(
    uint16_t& heading, uint16_t& pitch, const Bearing& desired, const WeaponDef& weapon
) noexcept;

enum class FlightMode : uint8_t {
    inert,     // neither moves nor collides
    line,      // expires at its lifetime
    ballistic, // gravity and wind; expires only with a weapon timer
    dropped,   // gravity and wind; never expires
    meteor,
    self_propelled, // accelerates, optionally guided
};

/// Returns how a weapon's projectiles fly.
///
/// @param weapon weapon definition
/// @return self-propelled, line, ballistic, dropped or meteor by the first matching flag
///         in that order, else inert
[[nodiscard]] FlightMode flight_mode(const WeaponDef& weapon) noexcept;

/// Returns the damage a projectile deals.
///
/// @param amount weapon damage against the target
/// @param scale area damage scale
/// @param source firing unit, or null
/// @param game game record, for the damage cheats
/// @return amount * scale truncated, plus the source's veteran bonus of 6% per five levels
///         up to 30%, then doubled or halved by the damage cheats
/// @quirk An out-of-range conversion gives 0x80000000.
[[nodiscard]] int32_t
projectile_damage(int32_t amount, float scale, const Unit* source, const Game& game) noexcept;

/// Returns the area damage scale at a distance from the blast.
///
/// @param weapon weapon definition (edge effectiveness)
/// @param reach distance from the blast, world units
/// @param radius area of effect, world units
/// @return edge + (1 - edge) * (reach / radius - 1)^2; exactly 1 at reach 0
[[nodiscard]] float
area_damage_scale(const WeaponDef& weapon, int32_t reach, int32_t radius) noexcept;

inline constexpr int32_t area_blast_unit_capacity = 20;
inline constexpr int32_t area_blast_feature_capacity = 64;

// What one area blast has already damaged: units, and the origin plots of
// features.
struct AreaBlastVisits {
    int32_t unit_count{};
    int32_t feature_count{};
    const Unit* units[area_blast_unit_capacity]{};
    uint32_t feature_plots[area_blast_feature_capacity]{};
};

/// Notes a unit an area blast reached.
///
/// Once 20 are noted, further units are damaged without being remembered.
///
/// @param[in,out] visits what the blast has damaged
/// @param unit unit reached
/// @return false when the unit was noted before
[[nodiscard]]
bool note_blast_unit(AreaBlastVisits& visits, const Unit& unit) noexcept;

/// Notes a feature's origin plot an area blast reached, up to 64.
///
/// @param[in,out] visits what the blast has damaged
/// @param plot the feature's origin plot index
/// @return false when the plot was noted before
[[nodiscard]]
bool note_blast_feature(AreaBlastVisits& visits, uint32_t plot) noexcept;

/// Returns the distance from a point to a box.
///
/// @param point blast centre, 16.16 world coordinates
/// @param low box minimum corner
/// @param high box maximum corner
/// @return the signed high word of the 16.16 length of each axis's gap outside
///         [low, high] (zero inside), in world units
[[nodiscard]] int32_t blast_reach_to_box(
    const FixedVector& point, const FixedVector& low, const FixedVector& high
) noexcept;

/// Returns the bearing from a unit to an impact, relative to its heading.
///
/// @param impact impact point, 16.16 world coordinates
/// @param unit unit hit
/// @return angle, 65536 per turn
[[nodiscard]] uint16_t impact_direction(const FixedVector& impact, const Unit& unit) noexcept;

struct ShotPlan {
    bool fired{};
    ProjectileLaunch launch{};
    AimAngles slot_aim{};        // written back to the weapon slot
    bool fire_script{};          // run Fire<slot> and RockUnit
    bool spends_aim{};           // a fired turret or vlaunch shot clears the slot's aim_ready
    oa_ref32 intercept_target{}; // Projectile an interceptor launch homes on
};

/// Launches a turret shot on the route its weapon selects.
///
/// Line of sight or self-propelled weapons take the line route, others the ballistic
/// route along the slot's aim.
///
/// @param weapon weapon definition
/// @param aimed slot with its aim angles already spread
/// @param gravity game gravity, 16.16 per tick
/// @param muzzle launch point, 16.16 world coordinates
/// @param target aim point, 16.16 world coordinates
/// @param current_tick current game tick
/// @param[out] out receives the launch
/// @return false for a weapon with neither route
[[nodiscard]] bool launch_turret_projectile(
    const WeaponDef& weapon,
    const UnitWeapon& aimed,
    int32_t gravity,
    const FixedVector& muzzle,
    const FixedVector& target,
    uint32_t current_tick,
    ProjectileLaunch* out
) noexcept;

/// Plans a shot of the fixed line constructor.
///
/// Aims the slot along the muzzle-to-target bearing and fires on the line route only
/// while that bearing is within the weapon's tolerance of the unit's heading and pitch.
///
/// @param weapon weapon definition
/// @param unit firing unit
/// @param muzzle launch point, 16.16 world coordinates
/// @param target aim point, 16.16 world coordinates
/// @param current_tick current game tick
/// @return the plan; `fired` is false outside tolerance
[[nodiscard]] ShotPlan plan_line_shot(
    const WeaponDef& weapon,
    const Unit& unit,
    const FixedVector& muzzle,
    const FixedVector& target,
    uint32_t current_tick
) noexcept;

/// Plans a shot of the dropped constructor: it always fires, at the unit's heading and ground speed, and runs no script.
///
/// @param unit dropping unit
/// @param slot weapon slot 0..2
/// @param unit_speed ground speed, 16.16 per tick
/// @return the plan
[[nodiscard]] ShotPlan
plan_dropped_shot(const Unit& unit, uint8_t slot, int32_t unit_speed) noexcept;

/// Plans a shot with the constructor the weapon's flags select (select_fire_mode).
///
/// The turret constructor spreads the slot aim, then launches through
/// launch_turret_projectile. The vertical-launch constructor waits for the slot's Aim
/// script, aims the slot along the bearing and, for an interceptor, fires only at a
/// projectile the target search finds.
///
/// @param weapon weapon definition
/// @param unit firing unit
/// @param type its unit type
/// @param slot weapon slot 0..2
/// @param muzzle launch point, 16.16 world coordinates
/// @param target aim point, 16.16 world coordinates
/// @param unit_speed ground speed, 16.16 per tick (dropped weapons)
/// @param world world, for gravity, the tick and the interceptor search
/// @param random synced random stream
/// @param random_context passed to `random`
/// @return the plan; `fired` is false when nothing launches
[[nodiscard]] ShotPlan plan_weapon_shot(
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
);

} // namespace oa::sim::weapon_execution
