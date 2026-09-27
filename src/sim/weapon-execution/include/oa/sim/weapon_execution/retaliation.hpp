// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/core/unit.h"
#include "oa/core/world.h"

#include <cstdint>

// A damaged unit's automatic reaction to its attacker: chase it, re-aim its
// weapon slots at it, and raise the under-attack notice.
namespace oa::sim::weapon_execution {

// Bits of a head order's flag word that retaliate reads. The word holds the
// order's preserve flags in bits 0..7, its command flags in bits 8..15 and its
// order flags in bits 16..23, as a mission descriptor does.

/// Set on Standby, Standby_Mine and VTOL_Standby orders: a unit whose head order
/// carries it chases its attacker.
inline constexpr uint32_t order_standby_flag = 0x20000;
inline constexpr uint32_t order_mutes_attack_notice_flag = 0x80;
inline constexpr uint32_t computer_alert_base_ticks = 30;
inline constexpr uint32_t computer_alert_random_ticks = 300;
inline constexpr uint16_t retarget_event_mask = 0x83ff; // events kept when a slot re-aims

/// Returns the unit a weapon slot aims at.
///
/// @param world unit table
/// @param unit unit owning the slot
/// @param slot weapon slot 0..2
/// @return the target unit, or null when the slot aims at a point or nothing
[[nodiscard]]
Unit* slot_target_unit(World& world, const Unit& unit, uint8_t slot) noexcept;

/// Aims a weapon slot at a unit and keeps only the event bits in retarget_event_mask.
///
/// @param[in,out] shooter unit owning the slot
/// @param target unit to aim at
/// @param slot weapon slot 0..2
void aim_slot_at_unit(Unit& shooter, const Unit& target, uint8_t slot) noexcept;

/// Aims a weapon slot at a ground point's integral X/Z.
///
/// Keeps only the event bits in retarget_event_mask.
///
/// @param[in,out] shooter unit owning the slot
/// @param point ground point, 16.16 world coordinates
/// @param slot weapon slot 0..2
/// @quirk A Z of -0x8000 (the unit-target marker) is stored as -0x7fff.
void aim_slot_at_point(Unit& shooter, const FixedVec3& point, uint8_t slot) noexcept;

/// Tests whether a slot's weapon reaches a unit from where the shooter stands.
///
/// @param world weapons and units
/// @param shooter unit owning the slot
/// @param target unit to reach
/// @param slot weapon slot 0..2
/// @return true when in reach
[[nodiscard]]
bool slot_reaches_unit(
    const World& world, const Unit& shooter, const Unit& target, uint8_t slot
) noexcept;

/// Tests whether a slot's weapon reaches a point from where the shooter stands.
///
/// @param world weapons and units
/// @param shooter unit owning the slot
/// @param target point to reach, 16.16 world coordinates
/// @param slot weapon slot 0..2
/// @return true when in reach
[[nodiscard]]
bool slot_reaches_point(
    const World& world, const Unit& shooter, const FixedVec3& target, uint8_t slot
) noexcept;

struct RetaliationHooks {
    void* context{};
    // Whether a UnitDef category ref (a 64-byte type mask kept outside World) holds the type index.
    bool (*category_contains)(void* context, oa_ref32 mask, uint16_t type_index){};
    // The flag word of the unit's head order (see order_standby_flag); false
    // without one.
    bool (*head_order_flags)(void* context, const Unit&, uint32_t* flags){};
    // Queues an attack order on the attacker (sim::combat_state::issue_attack_order);
    // true when accepted.
    bool (*queue_attack)(void* context, Unit& victim, Unit& attacker){};
    uint32_t (*random)(void* context, uint32_t limit){}; // synced random stream
    // A computer-owned capturer was hit: store the tick until which the
    // owner's computer player gives the unit no build task, then clear the
    // unit's orders (not all).
    void (*computer_alert)(void* context, Unit& victim, uint32_t tick){};
};

struct RetaliationResult {
    bool computer_alert{};
    uint32_t computer_alert_tick{};
    bool chased{};              // attack order queued
    uint8_t retargeted_slots{}; // bit per weapon slot now aiming at the attacker
    bool attack_notice{};       // the under-attack notice, speech category 2, is due
};

/// Runs a damaged unit's automatic reaction to its attacker.
///
/// A computer-owned capturer raises the computer alert. A finished, armed (or kamikaze),
/// locally simulated victim of a non-allied attacker then tries an attack order when
/// its head order allows it, the attacker is not in nochasecategory or the primary
/// bad-target category, and slot 0 reaches it. Failing that, unless holding fire, each
/// enabled slot with RETALIATE that reaches the attacker and is not commandfire re-aims
/// at it, unless its current unit target is still reachable and not a bad target. The
/// notice needs a muting-free head order (read after the attack order was queued) and
/// either a foreign last attacker or weapon damage. Waking the orders that target the
/// victim with event 0x10 stays with the caller, which runs it first.
///
/// @param[in,out] world units, players and weapons
/// @param[in,out] victim damaged unit
/// @param attacker unit that caused the damage; null or a typeless unit counts as none
/// @param hooks category masks, orders, random stream and the computer alert
/// @return what the reaction did
[[nodiscard]]
RetaliationResult
retaliate(World& world, Unit& victim, Unit* attacker, const RetaliationHooks& hooks);

} // namespace oa::sim::weapon_execution
