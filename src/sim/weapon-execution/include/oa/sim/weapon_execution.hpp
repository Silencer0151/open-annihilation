// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/core/unit.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oa::sim::weapon_execution {

inline constexpr size_t weapon_slot_count = 3;
inline constexpr uint8_t aimed_flag = 0x01;
inline constexpr uint8_t enabled_flag = 0x02;
inline constexpr uint32_t vlaunch_flag = 0x00000010;
inline constexpr uint32_t turret_flag = 0x00080000;
inline constexpr uint32_t stockpile_flag = 0x10000000;
// Unit event raised when a slot cannot reach or aim at its target.
inline constexpr uint16_t target_unreachable_event = 0x1000;
// Unit events a shot raises; the commandfire one ends the attack order that
// aimed the weapon.
inline constexpr uint16_t shot_fired_event = 0x0400;
inline constexpr uint16_t commandfire_shot_event = 0x0800;
inline constexpr uint32_t commandfire_flag = 0x04000000; // TDF commandfire, bit 26

struct Point {
    std::array<uint32_t, 3> fixed{};
};

// A nonzero unit_identity denotes a live-unit target. Zero denotes a coordinate
// target (the game marks it with a second angle other than 0x8000).
struct Target {
    Point point;
    uintptr_t unit_identity{};
};

struct WeaponDefinition {
    bool projectile_constructor_present{}; // a constructor is set (WeaponDef.fire_callback)
    uint32_t flags{};                      // WeaponDef.flags
    uint16_t base_reload_ticks{};          // WeaponDef.reload_time
    int32_t range_fixed{};                 // WeaponDef.range
    float energy_per_shot{};               // WeaponDef.energy_per_shot
    float metal_per_shot{};                // WeaponDef.metal_per_shot
};

// A weapon slot as tick_weapons sees it. The reload counts down in the unit's
// canonical slot itself; the stockpile and flags are working copies the caller
// writes back after the tick.
struct WeaponSlot {
    const WeaponDefinition* definition{};
    UnitWeapon* record{}; // the unit's canonical slot; required while the slot is enabled
    uint8_t stockpile_count{};
    uint8_t flags{};
};

struct UnitState {
    std::array<WeaponSlot, weapon_slot_count> slots{};
    Point position;
    uint16_t veteran_level{};   // Unit.veteran_level
    int16_t health{};           // Unit.health
    uint32_t maximum_health{};  // UnitDef.max_damage; must be nonzero
    uint16_t shot_event_bits{}; // Unit.events
};

enum class SlotResult : uint8_t {
    disabled,
    target_lost,
    waiting,
    out_of_range,
    insufficient_resources,
    projectile_rejected,
    fired,
};

struct TickResult {
    std::array<SlotResult, weapon_slot_count> slots{};
};

// A ready turret's aim, solved again before it fires.
enum class TurretAim : uint8_t {
    unsolved,   // the solver finds no angles
    off_target, // the slot's angles are outside tolerance of the solution
    on_target,
};

// These calls retain ownership of live units, COB instances and projectiles in
// the match runtime. fire_projectile must run the projectile constructor and, on
// success, dispatch FirePrimary/Secondary/Tertiary.
struct Host {
    virtual ~Host() = default;
    /// Resolves a slot's current target.
    ///
    /// @param slot weapon slot 0..2
    /// @param[out] target receives the target unit or point
    /// @return false when the slot has no target
    virtual bool resolve_target(uint8_t slot, Target& target) = 0;
    /// Returns the firing unit's position.
    ///
    /// @return 16.16 world position
    virtual Point source_position() = 0;
    /// Starts a turret slot's Aim script at a target.
    ///
    /// Clears the slot's aim_ready and starts its Aim script with record_aim_result
    /// as the return callback.
    ///
    /// @param slot weapon slot 0..2
    /// @param target target to aim at
    /// @return false when no aim solution exists
    virtual bool begin_turret_aim(uint8_t slot, const Target& target) = 0;
    /// Starts a vertical-launch slot's Aim script.
    ///
    /// Clears the slot's aim_ready and starts its Aim script with record_aim_result
    /// as the return callback.
    ///
    /// @param slot weapon slot 0..2
    virtual void begin_vlaunch_aim(uint8_t slot) = 0;
    /// Returns whether the slot's Aim script has returned nonzero (UnitWeapon.aim_ready).
    ///
    /// @param slot weapon slot 0..2
    /// @return true once the aim is done
    virtual bool aim_ready(uint8_t slot) = 0;
    /// Tests whether a slot's weapon can reach a target from a position.
    ///
    /// @param slot weapon slot 0..2
    /// @param source firing position, 16.16
    /// @param target target
    /// @return true when range, water, height and ballistic checks pass
    virtual bool can_reach(uint8_t slot, const Point& source, const Target& target) = 0;
    /// Solves a ready turret's aim again and checks the slot's angles against it.
    ///
    /// @param slot weapon slot 0..2
    /// @param target target
    /// @return unsolved, off target, or on target
    virtual TurretAim turret_aim(uint8_t slot, const Target& target) = 0;
    /// Runs the projectile constructor and dispatches the slot's Fire script.
    ///
    /// @param slot weapon slot 0..2
    /// @param target target
    /// @return true when a projectile was built and the callback ran
    virtual bool fire_projectile(uint8_t slot, const Target& target) = 0;
    /// Reports that a stockpiled shot was used.
    ///
    /// @param slot weapon slot 0..2
    virtual void stockpile_consumed(uint8_t slot) = 0;
    /// Tests whether both owner stores (Player.energy and Player.metal) cover a shot.
    ///
    /// @param energy energy cost of the shot
    /// @param metal metal cost of the shot
    /// @return false skips the projectile constructor
    virtual bool can_pay_shot_cost(float energy, float metal) = 0;
    /// Pays for a shot after the constructor fired.
    ///
    /// @param energy energy cost of the shot
    /// @param metal metal cost of the shot
    virtual void pay_shot_cost(float energy, float metal) = 0;
};

// Projectile constructor that WeaponDef.flags select for
// WeaponDef.fire_callback.
enum class FireMode : uint8_t {
    none,            // no constructor installed
    line,            // fixed line-of-sight or self-propelled weapon
    vertical_launch, // vlaunch
    dropped,         // dropped bomb
    turret,          // turret, any projectile route
};

/// Chooses the projectile constructor a weapon's flags select.
///
/// The order is turret, then vlaunch, then line-of-sight or selfprop, then dropped.
/// The game makes this choice once, as the weapon loads, and a weapon whose flags
/// match none of the four keeps the constructor it already had; here the choice is
/// made for each shot, and such a weapon has none.
///
/// @param weapon_flags WeaponDef.flags
/// @return the constructor, or none when no bit matches
[[nodiscard]] FireMode select_fire_mode(uint32_t weapon_flags) noexcept;

// Command-fire spawn route from WeaponDef.flags.
enum class ProjectileRoute : uint8_t {
    none,
    ballistic,
    line,
};

/// Chooses the command-fire spawn route from a weapon's flags.
///
/// Line of sight or selfprop selects the line route, else the ballistic flag selects
/// the ballistic route. Neither constructor is called here.
///
/// @param weapon_flags WeaponDef.flags
/// @return the route, or none (nothing spawns) for any other flags
[[nodiscard]] ProjectileRoute projectile_route(uint32_t weapon_flags) noexcept;

// Turret slew input: the weapon's current aim angles against the desired
// ones, with the definition's limits.
struct TurretSlewInput {
    uint16_t yaw_limit{};    // WeaponDef.tolerance
    uint16_t pitch_limit{};  // WeaponDef.pitch_tolerance
    int16_t current_yaw{};   // UnitWeapon.aim_heading
    int16_t current_pitch{}; // UnitWeapon.aim_pitch
    int16_t desired_yaw{};
    int16_t desired_pitch{};
    bool moving{}; // Unit.flags has a move rate (OA_UNIT_FLAG_MOVE_RATE_MASK)
};

/// Tests whether both current turret angles lie within the limits of the desired ones.
///
/// A zero yaw limit uses 150 for both while stopped and 2000 while moving; a zero pitch
/// limit alone takes the yaw limit.
///
/// @param input current and desired angles, limits and whether the unit moves
/// @return true when both 16-bit truncated differences are within their limits
[[nodiscard]] bool turret_within_tolerance(const TurretSlewInput& input) noexcept;

/// Returns a slot's reload after a shot, shortened by experience and health.
///
/// The base is scaled by 100 - 6 * min(veteran / 5, 5) percent, then by
/// 120 - health * 20 / maximum_health percent.
///
/// Throws std::invalid_argument for a zero maximum health.
///
/// @param base WeaponDef reload, ticks
/// @param veteran_level unit's veteran level (Unit.veteran_level)
/// @param health unit's health
/// @param maximum_health the type's maximum health
/// @return the reload, ticks, narrowed to 16 bits
/// @quirk The products wrap at 32 bits.
[[nodiscard]] uint16_t reload_ticks_after_shot(
    uint16_t base, uint16_t veteran_level, int16_t health, uint32_t maximum_health
);

/// Runs one tick of a unit's three weapon slots.
///
/// For each enabled slot: counts the reload down, resolves the target, starts the Aim
/// script of a turret or vertical-launch slot, then, once reloaded and in reach and
/// with the shot paid for or stockpiled, runs the projectile constructor. A shot sets
/// the reload (or uses a stockpiled round), raises the shot event and is paid for
/// after the constructor fired. The reload, in ticks, is read and written in each
/// enabled slot's canonical record (UnitWeapon.reload).
///
/// @param[in,out] unit slots, position, experience and event bits; the slots'
///        canonical records are written too
/// @param host targets, scripts, reach, projectiles and the owner's stores
/// @return what each slot did
[[nodiscard]] TickResult tick_weapons(UnitState& unit, Host& host);

// One step of a burst spawner.
struct BurstFire {
    uint16_t shots{};
    uint16_t remaining{};
    uint32_t anchor{};
    uint32_t next_due{};
    bool has_next{};
};

/// Advances a burst spawner by one projectile tick.
///
/// One child is due when current_tick >= anchor_tick + burst_rate (mod 2^32, unsigned).
/// Allocating the child, the muzzle query and retiring the spawner are not done here.
///
/// @param burst_remaining children left (Projectile.burst_remaining, copied from
///        WeaponDef.burst); zero is a flying shot and emits nothing
/// @param burst_rate ticks between children
/// @param anchor_tick Projectile.burst_tick: the spawn tick, advanced by burst_rate per child
/// @param current_tick current game tick
/// @return children to emit (0 or 1), the new remaining count and anchor, and the next due
///         tick while shots remain
/// @quirk Intervals that elapsed together still emit one child per call.
[[nodiscard]] BurstFire burst_fire_step(
    uint16_t burst_remaining, uint16_t burst_rate, uint32_t anchor_tick, uint32_t current_tick
) noexcept;

} // namespace oa::sim::weapon_execution
