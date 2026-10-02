// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace oa::sim::unit_health {
using UnitIdentity = uint16_t;
// The player a shared health event goes out for.
using RouteIdentity = uintptr_t;

inline constexpr uint8_t healing_damage_kind = 10;
inline constexpr uint8_t unshared_damage_kind = 11;
inline constexpr uint8_t mirrored_owner_status = 3;
inline constexpr uint8_t damage_scaling_flag = 0x02;
inline constexpr int32_t damage_scaling_limit = 30000;
inline constexpr uint32_t veteran_divisor = 5;
inline constexpr uint32_t maximum_veteran_reduction = 5;
inline constexpr int32_t base_damage_percent = 25;
inline constexpr int32_t damage_percent_scale = 4;
inline constexpr int32_t percent_divisor = 100;

// The requested, accepted and gate words of a unit's ResourceAccumulator.
struct EconomyDebit {
    float requested{};
    float accepted{};
    float gate{};
};

/// Requests an amount of one resource.
///
/// @param[in,out] debit the resource's accumulator; `requested` always grows, `accepted`
///        only when the gate is at or below zero
/// @param amount amount to take
/// @return true when accepted
/// @quirk A NaN gate passes, as in 3.1c.
[[nodiscard]] bool debit_resource(EconomyDebit& debit, float amount) noexcept;

/// Requests energy and metal together for one build step.
///
/// Both requests always grow; both are accepted only when neither gate is above zero.
///
/// @param[in,out] energy builder's energy accumulator (Unit.economy.energy)
/// @param[in,out] metal builder's metal accumulator (Unit.economy.metal)
/// @param energy_amount energy to take
/// @param metal_amount metal to take
/// @return true when both were accepted
[[nodiscard]] bool debit_resources(
    EconomyDebit& energy, EconomyDebit& metal, float energy_amount, float metal_amount
) noexcept;

/// Settles one resource accumulator at the end of an economy tick.
///
/// produced and requested move to last_produced and last_requested, gate becomes
/// `(accepted - stock_ratio * accepted) + (gate - supply_ratio * gate)`, and
/// produced, requested and accepted then clear.
///
/// @param[in,out] block a unit's Unit.economy.energy or Unit.economy.metal, as the
///        six floats of a ResourceAccumulator in field order
/// @param supply_ratio produced over requested; 1 when the request is covered
/// @param stock_ratio leftover against storage; 1 when storage still covers it
void scale_resource_block(float block[6], float supply_ratio, float stock_ratio) noexcept;

// Credit scales of computer players on easy and medium, applied as doubles.
inline constexpr double computer_easy_credit_scale = 0.5;
inline constexpr double computer_medium_credit_scale = 0.7;
/// Scales a resource credit for a computer player's difficulty.
///
/// @param amount credit before scaling
/// @param player_status the owner's Player.status; 2 is a computer player
/// @param difficulty 0 easy, 1 medium, anything else unscaled
/// @return the amount times 0.5 (easy) or 0.7 (medium) for a computer player, else unchanged
/// @quirk The float is multiplied as a double and rounded back to float.
[[nodiscard]] float
scale_computer_credit(float amount, uint8_t player_status, int32_t difficulty) noexcept;

/// Credits metal into a target economy's produced metal (UnitEconomy.metal.produced).
///
/// Only an easy or medium computer owner scales the amount; an absent owner record,
/// another status or another difficulty takes the unscaled branch.
///
/// @param[in,out] metal_accumulator the target economy's metal word
/// @param amount metal before scaling
/// @param owner_present whether the owner record is present (Player.in_use)
/// @param owner_status the owner's Player.status
/// @param difficulty computer difficulty
/// @return the amount actually added
[[nodiscard]] float credit_metal(
    float& metal_accumulator,
    float amount,
    bool owner_present,
    uint8_t owner_status,
    int32_t difficulty
) noexcept;

/// Credits energy into a target economy's produced energy (UnitEconomy.energy.produced).
///
/// Same owner, status and difficulty branch order as credit_metal.
///
/// @param[in,out] energy_accumulator the target economy's energy word
/// @param amount energy before scaling
/// @param owner_present whether the owner record is present (Player.in_use)
/// @param owner_status the owner's Player.status
/// @param difficulty computer difficulty
/// @return the amount actually added
[[nodiscard]] float credit_energy(
    float& energy_accumulator,
    float amount,
    bool owner_present,
    uint8_t owner_status,
    int32_t difficulty
) noexcept;

/// Bit of a unit's event flags (the high byte of Unit.events) that a hit sets
/// when its amount exceeds twice its threshold.
inline constexpr uint8_t hit_reaction_over_double = 0x40;
/// Bit of a unit's event flags that a hit sets when its amount is at most
/// twice its threshold.
inline constexpr uint8_t hit_reaction_within_double = 0x20;

/// Records how hard a hit was in a unit's event flags, the high byte of Unit.events.
///
/// Hit resolution passes enemy damage as the amount and friendly damage as the
/// threshold, or the reverse.
///
/// @param[in,out] event_flags gains hit_reaction_over_double when threshold * 2 < amount,
///                otherwise hit_reaction_within_double
/// @param amount damage compared
/// @param threshold damage compared against
void record_hit_reaction(uint8_t& event_flags, int amount, int threshold) noexcept;

/// Tests whether one more unit of a type fits its limit.
///
/// @param index unit type index; 0 never fits
/// @param table_limit number of unit types (Game.unit_def_count); an index at or past it
///        never fits
/// @param entry the type's limit; -1 is unlimited
/// @param compare current count
/// @return true when `compare < entry`, or the entry is unlimited
[[nodiscard]] bool
within_unit_limit(uint16_t index, int32_t table_limit, int32_t entry, int32_t compare) noexcept;

/// Tests a type against a player's limit row.
///
/// The row is not read for index 0 or an index at or past `table_limit`.
///
/// @param table_slot player slot; only its low byte selects the row
/// @param index unit type index
/// @param table_limit number of unit types (Game.unit_def_count)
/// @param row_tables limit table of each player's knowledge record
/// @param compare current count
/// @return within_unit_limit against the row's entry
[[nodiscard]] bool within_unit_limit_row(
    uint32_t table_slot,
    uint16_t index,
    int32_t table_limit,
    const int32_t* const* row_tables,
    int32_t compare
) noexcept;

inline constexpr uint16_t construction_event = 0x8000;      // Unit.events bit; wakes GetBuilt
inline constexpr uint32_t construction_dirty_flag = 0x2000; // OA_UNIT_FLAG_CONSTRUCTION_DIRTY
inline constexpr uint8_t construction_cancel_kind = 9;      // reclaim destroy
inline constexpr int32_t construction_cancel_amount = 30000;

// The UnitDef fields health and construction read.
struct UnitType {
    int32_t damage_scale_16_16{0x10000}; // UnitDef.damage_modifier
    float energy_cost{};                 // UnitDef.build_cost_energy
    int32_t build_time{};                // UnitDef.build_time
    uint32_t maximum_health{};           // UnitDef.max_damage
    float metal_cost{};                  // UnitDef.build_cost_metal
};

// The Unit fields health and construction read and write.
struct Unit {
    UnitIdentity identity{};  // Unit.id
    uint8_t state_flags{};    // Unit.state_flags
    uint16_t veteran_level{}; // Unit.veteran_level
    int16_t health{};         // Unit.health
    UnitType* type{};
    float build_remaining{}; // Unit.build_remaining; 1.0 unfinished, 0 finished
    uint16_t events{};       // Unit.events
    uint32_t flags{};        // Unit.flags
};

struct HealthEvent {
    UnitIdentity target{};
    UnitIdentity source{};
    int16_t amount{};
    uint8_t direction{};
    uint8_t kind{};
};

// submit_damage builds and submits the event. Healing (kind 10) is applied here;
// other health and death handling is a required boundary.
struct DamageHost {
    virtual ~DamageHost() = default;
    /// Tests whether the target can still take a health event.
    ///
    /// @param target unit hit
    /// @return true while it is live
    virtual bool target_is_live(const Unit& target) = 0;
    /// Applies a non-healing health event: damage scripts and death flagging.
    ///
    /// @param[in,out] target unit hit
    /// @param source unit responsible, or null
    /// @param event scaled event
    virtual void apply_health_event(Unit& target, const Unit* source, const HealthEvent& event) = 0;
    /// Tests whether the target's owner record is present.
    ///
    /// @param target unit hit
    /// @return true when present
    virtual bool target_owner_present(const Unit& target) = 0;
    /// Returns the target owner's status; 3 means another player's simulation owns it.
    ///
    /// @param target unit hit
    /// @return the owner's Player.status
    virtual uint8_t target_owner_status(const Unit& target) = 0;
    /// Returns the player a shared event goes out for when it has a source.
    ///
    /// @param source unit responsible
    /// @return the source owner's route
    virtual RouteIdentity source_owner_route(const Unit& source) = 0;
    /// Returns the route of a shared event without a source unit.
    ///
    /// @return the fallback route
    virtual RouteIdentity fallback_route() = 0;
    /// Shares a health event on a unit another player's simulation owns.
    ///
    /// @param route player the event goes out for
    /// @param event scaled event
    virtual void share_health_event(RouteIdentity route, const HealthEvent& event) = 0;
};

/// Builds a health event, scaling damage the way the game does.
///
/// For kinds other than healing: a target with state flag 0x02 scales an amount under
/// 30000 by its type's 16.16 damage scale; then the amount is multiplied by
/// 25 - min(veteran / 5, 5) and by 4 and divided by 100.
///
/// @param source unit responsible, or null
/// @param target unit hit; its type must be set
/// @param amount damage, or health for healing
/// @param kind damage kind; 10 heals
/// @param direction_word hit direction; its high byte is kept
/// @return the event, with the amount narrowed to 16 bits
/// @quirk The multiplications wrap at 32 bits.
[[nodiscard]] HealthEvent make_health_event(
    const Unit* source,
    const Unit& target,
    int32_t amount,
    uint32_t kind,
    uint32_t direction_word = 0
) noexcept;
/// Builds and submits a health event.
///
/// A live target is healed here for kind 10 or handed to the host otherwise. The event
/// is then shared when another player's simulation owns the target, unless the kind is
/// 11.
///
/// A target without a type takes no event.
///
/// @param source unit responsible, or null
/// @param[in,out] target unit hit
/// @param amount damage, or health for healing
/// @param kind damage kind
/// @param host damage application and sharing
/// @param direction_word hit direction; its high byte is kept
/// @return false when the target has no type and nothing was submitted
bool submit_damage(
    const Unit* source,
    Unit& target,
    int32_t amount,
    uint32_t kind,
    DamageHost& host,
    uint32_t direction_word = 0
);

/// Returns a unit's health after a heal event.
///
/// @param health current health, signed
/// @param amount heal amount, read as an unsigned word
/// @param maximum_health the type's maximum
/// @return the sum, capped at the maximum by an unsigned compare
[[nodiscard]] int16_t
healed_health(int16_t health, int16_t amount, uint32_t maximum_health) noexcept;

struct RecoveryHost : DamageHost {
    /// Returns the repairer's energy accumulator (Unit.economy.energy).
    ///
    /// @param repairer unit paying for the repair
    /// @return its energy debit
    virtual EconomyDebit& energy_debit(Unit& repairer) = 0;
};

struct RecoveryResult {
    bool performed{};
    int32_t health_amount{};
    int32_t energy_amount{};
    bool target_untyped{}; // the target has no type, and nothing was done
};

/// Runs one repair step.
///
/// Below maximum health, the repairer pays at most one energy for at most one health,
/// applied as a kind-10 event. Natural regeneration supplies
/// `float((uint16(heal_time) * 8) / 30)` as the rate; repair orders use their own.
///
/// A target without a type is not repaired.
///
/// @param repairer unit paying for the repair
/// @param[in,out] target unit repaired
/// @param rate worker time this step
/// @param host economy and damage services
/// @return whether the repair happened, the health and energy amounts, and whether
///         the target had no type
/// @quirk A zero build time divides by zero; the out-of-range conversions leave both
///        amounts zero.
[[nodiscard]] RecoveryResult
recover_health(Unit& repairer, Unit& target, float rate, RecoveryHost& host);

struct ConstructionHost : RecoveryHost {
    /// Returns the builder's metal accumulator (Unit.economy.metal).
    ///
    /// @param builder unit paying for the build
    /// @return its metal debit
    virtual EconomyDebit& metal_debit(Unit& builder) = 0;
    /// Credits metal back to the target's economy, as credit_metal does.
    ///
    /// @param target decaying frame
    /// @param amount metal returned
    virtual void refund_metal(Unit& target, float amount) = 0;
    /// Finishes a construction whose build_remaining is already zero.
    ///
    /// Activation, notifications and the interface are the host's work.
    ///
    /// @param builder unit that finished it
    /// @param target finished unit
    virtual void complete_construction(Unit& builder, Unit& target) = 0;
};

struct ConstructionResult {
    bool performed{};
    bool completed{};
    float energy_amount{};
    float metal_amount{};
    bool target_untyped{}; // the target has no type, and nothing was done
};

/// Adds worker time to an unfinished target, or takes it away.
///
/// The builder pays for the step and health moves with the build fraction. A negative
/// rate decays the frame: metal is refunded, and a frame decayed back to nothing is
/// destroyed as a reclaim.
///
/// A target without a type is not built.
///
/// @param builder unit paying for the step
/// @param[in,out] target unfinished unit
/// @param rate worker_time / 30 (integer, then float) for MobileBuild, or the negative
///        build_decay_rate
/// @param host economy and completion services
/// @return whether the step happened, whether it completed the target, its costs, and
///         whether the target had no type
/// @quirk A zero or NaN fraction or rate returns without touching resources; the
///        step keeps 53 bits, and a zero build time divides to infinity.
[[nodiscard]] ConstructionResult
apply_build_progress(Unit& builder, Unit& target, float rate, ConstructionHost& host);

/// Returns the rate at which an unattended frame decays.
///
/// The caller applies it to the frame as its own builder.
///
/// @param type frame's unit type
/// @param ticks ticks the frame has been left
/// @return -(wrapped int32(build_time * ticks) / energy_cost), computed at 53 bits and
///         stored as a float
/// @quirk A zero energy cost divides to infinity, so the frame still decays.
[[nodiscard]] float build_decay_rate(const UnitType& type, int32_t ticks) noexcept;
} // namespace oa::sim::unit_health
