// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/core/unit.h"
#include "oa/core/world.h"

#include <cstdint>

// Paralyzer damage (kind 2), the Paralyzed mission it drives and the weapon
// target clears both use, over the canonical records. Order records are not
// canonical: the caller owns the paralyze order and passes its duration (the
// order's parameter word).
namespace oa::sim::unit_health {

inline constexpr uint8_t paralyzed_state_flag =
    0x10; // Unit.state_flags bit set through the activation change
inline constexpr int32_t paralysis_wait_limit = 0x708; // ticks per Paralyzed wait
inline constexpr uint32_t mission_result_waiting = 1;
inline constexpr uint32_t mission_result_finished = 5;

enum class ParalyzeAction : uint8_t {
    none,   // not live, not locally simulated, or immune
    extend, // add the amount to the head paralyze order's duration
    insert, // push a new paralyze order carrying the amount to the head of the queue
};

// Effects outside the canonical records, run where the game runs them.
struct ParalysisHooks {
    void* context{};
    // TargetCleared(slot) on the unit's script, after a weapon target clears.
    void (*target_cleared)(void* context, oa::Unit&, uint8_t slot){};
    // Sets or clears Unit.state_flags bits, with the script calls and the report
    // to other players a change brings (sim::unit_activation::change). When null only
    // the bits change.
    void (*set_state_flags)(void* context, oa::Unit&, uint8_t mask, bool on){};
    // Drops the goal the paralyze order holds.
    void (*release_order_goal)(void* context){};
};

/// Decides what kind-2 (paralyzer) damage does to a target.
///
/// @param world world the target lives in
/// @param target unit hit
/// @param head_order_is_paralyze whether the order at the head of its primary queue is
///        already Paralyze
/// @return none unless the target is live, owned by a present local or computer player,
///         and not immunetoparalyzer; else extend or insert
[[nodiscard]]
ParalyzeAction paralyze_action(
    const oa::World& world, const oa::Unit& target, bool head_order_is_paralyze
) noexcept;

struct ParalyzedStep {
    uint32_t result{};    // mission_result_finished or mission_result_waiting
    int32_t wait_ticks{}; // ticks to wait before the next step
};

/// Runs one step of the Paralyzed mission handler.
///
/// A zero duration ends the paralysis: the state flag clears and the order finishes.
/// Otherwise the duration is clamped to 0x708, every weapon slot loses its target
/// (tracked targets first, then each slot), the order's goal is released, the order
/// waits that long with the duration consumed, and the state flag is set. Damage that
/// extends the duration during the wait makes the next step wait again.
///
/// @param[in,out] unit paralyzed unit
/// @param[in,out] duration the paralyze order's pending ticks (its parameter word); consumed
/// @param hooks script, state-flag and order-goal effects
/// @return finished or waiting, and the ticks to wait
[[nodiscard]]
ParalyzedStep
paralyzed_mission_step(oa::Unit& unit, int32_t& duration, const ParalysisHooks& hooks);

/// Clears a weapon slot's unit or ground target, then runs target_cleared.
///
/// @param[in,out] unit unit whose slot is cleared
/// @param slot weapon slot 0..2; others do nothing
/// @param hooks target_cleared script hook
/// @return true when the slot had a target
bool clear_weapon_target(oa::Unit& unit, uint8_t slot, const ParalysisHooks& hooks);

/// Clears the targets of enabled weapon slots that carry the RETALIATE bit.
///
/// Each such slot loses the bit and its target.
///
/// @param[in,out] unit unit whose slots are cleared
/// @param slot weapon slot 0..2, or 3 for all three
/// @param hooks target_cleared script hook
/// @return a bit per slot whose target was cleared
uint8_t clear_tracked_weapon_targets(oa::Unit& unit, uint8_t slot, const ParalysisHooks& hooks);

} // namespace oa::sim::unit_health
