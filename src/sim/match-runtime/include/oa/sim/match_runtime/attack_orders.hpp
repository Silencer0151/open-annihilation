// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/data/match_rules.hpp"
#include "oa/sim/unit_spawn/legacy_views.hpp"
#include <array>
#include <cstdint>

namespace oa::sim::match_runtime {
using AttackPoint = std::array<uint32_t, 3>;

// The attack family's copy of an order's words.
struct AttackOrderState {
    sim::simulation_state::Unit* target{}; // the target the order observes
    AttackPoint destination{};             // the order's point
    std::array<int16_t, 2> origin{};       // the order's anchor, X then Z
    // The order's first, second and third parameters.
    int32_t weapon_slot{}, retry{}, leash{};
};

// What an attack mission step asks of the match.
struct AttackOrderHost {
    virtual ~AttackOrderHost() = default;
    /// Plays the order's acknowledgement once: only while the order still
    /// carries its announce bit (command flags bit 0x20), which it clears.
    ///
    /// @param caption The order's caption, or null for none.
    virtual void announce(const char* caption) = 0;
    /// Returns the weapon slot an attack uses when the order names none.
    ///
    /// @return The first enabled slot, 0 or 1; otherwise the third slot's
    ///     enabled bit itself, 2 when set and 0 when no slot is enabled.
    virtual uint8_t selected_weapon() = 0;
    /// Occupies a weapon slot for an order: an enabled slot that may pick
    /// its own targets (OA_UNIT_WEAPON_RETALIATE) loses that bit and its
    /// target, so it fires only at what an order gives it. reset_weapons
    /// frees the slots again.
    ///
    /// @param slot Weapon slot 0..2, or 3 for all three.
    virtual void release_weapon_targets(uint32_t slot) = 0;
    /// Aims a weapon slot at a unit.
    ///
    /// @param target Unit to attack.
    /// @param slot Weapon slot 0..2.
    virtual void assign_target(sim::simulation_state::Unit& target, int32_t slot) = 0;
    /// Stands every enabled weapon slot down and drops its target.
    virtual void reset_weapons() = 0;
    /// Tells whether a weapon slot can reach the target from where the unit
    /// stands.
    ///
    /// @param target Unit to attack.
    /// @param slot Weapon slot 0..2.
    /// @return True when the target is in range and reachable.
    virtual bool can_reach(sim::simulation_state::Unit& target, uint8_t slot) = 0;
    /// Returns the range of the unit's weapon in a slot.
    ///
    /// @param slot Weapon slot 0..2.
    /// @return Range in world units.
    virtual int32_t range(uint8_t slot) = 0;
    /// Removes the order's movement goal.
    virtual void clear_goal() = 0;
    /// Replaces the order's movement goal with one around a point.
    ///
    /// @param point Signed 16.16 goal point as bit patterns.
    /// @param radius Arrival radius in world units.
    virtual void circle_goal(const AttackPoint& point, int32_t radius) = 0;
    /// Draws from the match's shared stream.
    ///
    /// @param bound Exclusive upper limit.
    /// @return A value below `bound`, or 0 for a bound below 2.
    virtual uint32_t random(uint32_t bound) = 0;
    /// Re-resolves the attack command against a target and turns the order
    /// into the mission it names, keeping the order's descriptor bits 0x600.
    ///
    /// @param target Unit under attack, or null for a ground attack.
    /// @return The order's new mission kind.
    virtual uint8_t morph_attack_command(sim::simulation_state::Unit* target) = 0;
    /// Aims a weapon slot at a ground point.
    ///
    /// @param point Signed 16.16 point as bit patterns.
    /// @param slot Weapon slot 0..2.
    virtual void assign_ground(const AttackPoint& point, int32_t slot) = 0;
};

inline constexpr uint8_t air_strike_kind = 2;           // AirStrike
inline constexpr uint8_t air_to_air_kind = 3;           // AirToAir
inline constexpr uint8_t air_to_ground_kind = 4;        // AirToGround
inline constexpr uint8_t air_to_ground_hover_kind = 5;  // AirToGroundHover
inline constexpr uint8_t attack_chase_kind = 6;         // Attack_Chase
inline constexpr uint8_t attack_no_move_kind = 8;       // Attack_NoMove
inline constexpr uint8_t attack_special_kind = 9;       // AttackSpecial
inline constexpr uint8_t suppress_kind = 46;            // Suppress
inline constexpr uint8_t standing_fire_order_kind = 43; // Standing_FireOrder
inline constexpr uint8_t self_destruct_kind = 38;       // SelfDestruct
inline constexpr uint8_t self_destruct_fg_kind = 39;    // SelfDestructFG, same handler
inline constexpr uint8_t teleport_kind = 47;            // Teleport
inline constexpr uint8_t qmove_kind = 30;               // QMove
inline constexpr uint8_t qpatrol_kind = 31;             // QPatrol, same handler
inline constexpr uint8_t standing_move_order_kind = 44; // Standing_MoveOrder
inline constexpr uint8_t cloak_on_kind = 16;            // Cloak_On
inline constexpr uint8_t cloak_off_kind = 15;           // Cloak_Off
inline constexpr uint8_t activate_kind = 1;             // Activate
inline constexpr uint8_t deactivate_kind = 17;          // Deactivate
inline constexpr uint8_t make_selectable_kind = 24;     // MakeSelectable
inline constexpr uint8_t wait_for_attack_kind = 67;     // WaitForAttack
inline constexpr uint8_t stop_kind = 45;                // Stop
inline constexpr uint8_t wait_kind = 66;                // Wait

/// Runs one step of the Wait mission for an order without a unit to wait
/// for (a null second parameter).
///
/// The branch that waits on a unit search is separate.
///
/// @param phase The order's phase: 0 starts the wait, 1 finishes it.
/// @param duration Ticks to wait (the order's first parameter).
/// @param[in,out] wait_events Events the order waits for; phase 0 adds the
///     timer event.
/// @param[out] wake_tick Tick the timer event fires at, set in phase 0.
/// @param now Current simulation tick.
/// @return 1 (next phase) in phase 0, 5 (done) in phase 1, 7 (invalid) for
///     any other phase.
[[nodiscard]] inline uint32_t wait_order(
    uint8_t phase, int32_t duration, uint32_t& wait_events, uint32_t& wake_tick, uint32_t now
) noexcept {
    if (phase == 0) {
        wait_events |= 1;
        wake_tick = now + static_cast<uint32_t>(duration);
        return 1;
    }
    if (phase == 1)
        return 5;
    return 7;
}

inline constexpr uint8_t vtol_seek_attack_kind = 62; // VTOL_SeekAttack

/// Tells whether an aircraft type never breaks off to a repair pad, by
/// air.no-repair-retreat-flag: with the flag cantbetransported, a type that
/// sets it keeps attacking, patrolling or guarding below three quarters of its
/// health; in 3.1c every aircraft breaks off.
///
/// @param rules the match's rules
/// @param abilities the type's UnitDef.abilities
/// @return true when the type never breaks off
[[nodiscard]] inline bool
never_retreats_to_repair(const data::match_rules::MatchRules& rules, uint32_t abilities) noexcept {
    return rules.air.no_repair_retreat_flag.flag ==
               data::match_rules::AirNoRepairRetreatFlagFlag::cantbetransported &&
           (abilities & OA_UNIT_DEF_ABILITY_CANT_BE_TRANSPORTED) != 0;
}

} // namespace oa::sim::match_runtime
