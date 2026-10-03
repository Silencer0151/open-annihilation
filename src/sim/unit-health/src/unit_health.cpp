// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_health.hpp"
#include "oa/base/game_math.hpp"
#include "oa/sim/unit_health/veterancy.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace oa::sim::unit_health {
using base::game_math::truncate_low32;

namespace {
int32_t wrap_multiply(int32_t left, int32_t right) noexcept {
    return std::bit_cast<int32_t>(static_cast<uint32_t>(left) * static_cast<uint32_t>(right));
}

/// Tests whether a float counts as zero.
///
/// @param value value to test
/// @return true for zero of either sign or a NaN
/// @quirk NaN counts as zero, as in 3.1c.
bool zero_or_unordered(float value) noexcept {
    return !(value < 0.0F) && !(value > 0.0F);
}

int32_t repair_quantity(double numerator, float rate, int32_t build_time) noexcept {
    // The game evaluates (numerator*rate-1)/build_time+1, rounding the product
    // first; a separate statement keeps it from being fused.
    const double product = numerator * static_cast<double>(rate);
    return truncate_low32((product - 1.0) / static_cast<double>(build_time) + 1.0);
}

/// Returns a rate truncated toward zero, as the exact repair reads it.
///
/// @param rate worker time this step
/// @return the whole part; a NaN or a value outside 32 bits gives INT32_MIN
int32_t whole_rate(float rate) noexcept {
    constexpr float limit = 2147483648.0F;
    if (!(rate < limit && rate > -limit))
        return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(rate);
}

/// Finds or claims the remainder a repairer carries for a target.
///
/// @param table each unit's remainders, indexed by unit index
/// @param repairer the repairer's unit index
/// @param target the target's unit index
/// @return the remainder, or null when the repairer carries none
int32_t* carried_remainder(
    std::span<RepairRemainders> table, UnitIdentity repairer, UnitIdentity target
) noexcept {
    // The repairer's index is read as a signed 16-bit value.
    if (static_cast<int16_t>(repairer) <= 0 || repairer >= table.size())
        return nullptr;
    auto& entries = table[repairer].targets;
    const uint32_t key = static_cast<uint32_t>(target) + 1U;
    for (auto& entry : entries) {
        if (entry.target == key)
            return &entry.remainder;
    }
    for (auto& entry : entries) {
        if (entry.target == 0) {
            entry = {key, 0};
            return &entry.remainder;
        }
    }
    // Both entries are taken: the first is given to the new target.
    entries[0] = {key, 0};
    return &entries[0].remainder;
}

/// Runs one repair step under repair.rate exact-remainder (see recover_health).
///
/// @param repairer unit paying for the repair
/// @param[in,out] target unit repaired, below its maximum health
/// @param rate worker time this step
/// @param host economy and damage services and the remainder table
/// @return whether the step happened and its amounts
RecoveryResult
recover_health_exactly(Unit& repairer, Unit& target, float rate, RecoveryHost& host) {
    constexpr int32_t heal_limit = 0xffff;
    const int32_t build_time = target.type->build_time;
    if (build_time <= 0)
        return {};
    const int32_t whole = whole_rate(rate);
    const int32_t energy = std::max(
        repair_quantity(static_cast<double>(target.type->energy_cost), rate, build_time), 1
    );
    RecoveryResult result{false, 0, energy};
    if (!debit_resource(host.energy_debit(repairer), static_cast<float>(energy)))
        return result;
    result.performed = true;
    if (whole <= 0)
        return result;
    const int64_t product =
        static_cast<int64_t>(whole) * std::bit_cast<int32_t>(target.type->maximum_health);
    auto heal = static_cast<int32_t>(static_cast<uint32_t>(product / build_time));
    const auto remainder = static_cast<int32_t>(product % build_time);
    if (int32_t* carried =
            carried_remainder(host.repair_remainders, repairer.identity, target.identity)) {
        *carried = std::bit_cast<int32_t>(
            static_cast<uint32_t>(*carried) + static_cast<uint32_t>(remainder)
        );
        if (*carried >= build_time) {
            *carried -= build_time;
            ++heal;
        }
        if (heal <= 0)
            return result;
    } else {
        if (remainder > 0)
            ++heal;
        heal = std::max(heal, 1);
    }
    heal = std::min(heal, heal_limit);
    result.health_amount = heal;
    (void)submit_damage(&repairer, target, heal, healing_damage_kind, host, 0);
    return result;
}
} // namespace

bool debit_resource(EconomyDebit& debit, float amount) noexcept {
    debit.requested = static_cast<float>(debit.requested + amount);
    const bool accepted = !(debit.gate > 0.0F);
    if (accepted)
        debit.accepted = static_cast<float>(debit.accepted + amount);
    return accepted;
}

bool debit_resources(
    EconomyDebit& energy, EconomyDebit& metal, float energy_amount, float metal_amount
) noexcept {
    energy.requested = static_cast<float>(energy.requested + energy_amount);
    metal.requested = static_cast<float>(metal.requested + metal_amount);
    if (energy.gate > 0.0F || metal.gate > 0.0F)
        return false;
    energy.accepted = static_cast<float>(energy.accepted + energy_amount);
    metal.accepted = static_cast<float>(metal.accepted + metal_amount);
    return true;
}

void scale_resource_block(float block[6], float supply_ratio, float stock_ratio) noexcept {
    const auto requested = block[1];
    block[5] = requested;
    block[1] = 0.0F;
    const auto produced = block[0];
    block[4] = produced;
    block[0] = 0.0F;
    block[3] = (block[2] - stock_ratio * block[2]) + (block[3] - supply_ratio * block[3]);
    block[2] = 0.0F;
}

float scale_computer_credit(float amount, uint8_t player_status, int32_t difficulty) noexcept {
    if (player_status != 2)
        return amount;
    if (difficulty == 0)
        return static_cast<float>(static_cast<double>(amount) * computer_easy_credit_scale);
    if (difficulty == 1)
        return static_cast<float>(static_cast<double>(amount) * computer_medium_credit_scale);
    return amount;
}

float credit_metal(
    float& metal_accumulator,
    float amount,
    bool owner_present,
    uint8_t owner_status,
    int32_t difficulty
) noexcept {
    // An absent owner (Player.in_use zero), a status other than 2 or a
    // difficulty outside 0..1 all take the unscaled branch; only easy/medium
    // computer credit scales.
    auto credited = amount;
    if (owner_present && owner_status == 2 && (difficulty == 0 || difficulty == 1))
        credited = scale_computer_credit(amount, owner_status, difficulty);
    metal_accumulator += credited;
    return credited;
}

float credit_energy(
    float& energy_accumulator,
    float amount,
    bool owner_present,
    uint8_t owner_status,
    int32_t difficulty
) noexcept {
    // Same branch order as credit_metal, on the energy accumulator.
    auto credited = amount;
    if (owner_present && owner_status == 2 && (difficulty == 0 || difficulty == 1))
        credited = scale_computer_credit(amount, owner_status, difficulty);
    energy_accumulator += credited;
    return credited;
}

float scale_computer_income(
    float amount, int32_t difficulty, const ComputerIncomeScales& scales
) noexcept {
    return static_cast<float>(
        static_cast<double>(amount) * scales[computer_income_index(difficulty)]
    );
}

float credit_metal(
    float& metal_accumulator,
    float amount,
    bool owner_present,
    uint8_t owner_status,
    int32_t difficulty,
    const ComputerIncomeScales& scales
) noexcept {
    auto credited = amount;
    if (owner_present && owner_status == 2)
        credited = scale_computer_income(amount, difficulty, scales);
    metal_accumulator += credited;
    return credited;
}

float credit_energy(
    float& energy_accumulator,
    float amount,
    bool owner_present,
    uint8_t owner_status,
    int32_t difficulty,
    const ComputerIncomeScales& scales
) noexcept {
    auto credited = amount;
    if (owner_present && owner_status == 2)
        credited = scale_computer_income(amount, difficulty, scales);
    energy_accumulator += credited;
    return credited;
}

void record_hit_reaction(uint8_t& event_flags, int amount, int threshold) noexcept {
    if (threshold * 2 < amount)
        event_flags = static_cast<uint8_t>(event_flags | hit_reaction_over_double);
    else
        event_flags = static_cast<uint8_t>(event_flags | hit_reaction_within_double);
}

bool within_unit_limit(
    uint16_t index, int32_t table_limit, int32_t entry, int32_t compare
) noexcept {
    if (index == 0 || table_limit <= static_cast<int32_t>(index))
        return false;
    if (entry == -1)
        return true;
    return compare < entry;
}

bool within_unit_limit_row(
    uint32_t table_slot,
    uint16_t index,
    int32_t table_limit,
    const int32_t* const* row_tables,
    int32_t compare
) noexcept {
    // The row is read only after the index gate.
    int32_t entry = 0;
    if (index != 0 && !(table_limit <= static_cast<int32_t>(index)))
        entry = row_tables[table_slot & 0xffu][index];
    return within_unit_limit(index, table_limit, entry, compare);
}

HealthEvent make_health_event(
    const Unit* source,
    const Unit& target,
    int32_t amount,
    uint32_t kind,
    uint32_t direction_word,
    const data::match_rules::MatchRulesView& rules
) noexcept {
    int32_t adjusted = amount;
    if (kind != healing_damage_kind) {
        if ((target.state_flags & damage_scaling_flag) != 0 && amount < damage_scaling_limit) {
            const auto product = static_cast<int64_t>(target.type->damage_scale_16_16) * amount;
            adjusted = static_cast<int32_t>(product >> 16);
        }
        // Wrapping at 32 bits, amount x percent equals 3.1c's
        // amount x (25 - level) x 4.
        const auto percent =
            veteran_damage_taken_percent(rules, target.type_index, target.veteran_level);
        adjusted = wrap_multiply(adjusted, percent) / whole_percent;
    }
    return {
        target.identity,
        source ? source->identity : UnitIdentity{},
        static_cast<int16_t>(static_cast<uint16_t>(adjusted)),
        static_cast<uint8_t>(direction_word >> 8U),
        static_cast<uint8_t>(kind)
    };
}

bool submit_damage(
    const Unit* source,
    Unit& target,
    int32_t amount,
    uint32_t kind,
    DamageHost& host,
    uint32_t direction_word
) {
    if (!target.type)
        return false;
    const auto event = make_health_event(source, target, amount, kind, direction_word, host.rules);
    if (host.target_is_live(target)) {
        if (event.kind == healing_damage_kind)
            target.health = healed_health(target.health, event.amount, target.type->maximum_health);
        else
            host.apply_health_event(target, source, event);
    }
    if (host.target_owner_present(target) &&
        host.target_owner_status(target) == mirrored_owner_status && kind != unshared_damage_kind) {
        const auto route = source ? host.source_owner_route(*source) : host.fallback_route();
        host.share_health_event(route, event);
    }
    return true;
}

int16_t healed_health(int16_t health, int16_t amount, uint32_t maximum_health) noexcept {
    auto sum = static_cast<uint32_t>(static_cast<uint16_t>(amount)) +
               static_cast<uint32_t>(static_cast<int32_t>(health));
    if (maximum_health <= sum)
        sum = maximum_health;
    return static_cast<int16_t>(static_cast<uint16_t>(sum));
}

RecoveryResult recover_health(Unit& repairer, Unit& target, float rate, RecoveryHost& host) {
    if (!target.type) {
        RecoveryResult untyped{};
        untyped.target_untyped = true;
        return untyped;
    }
    if (static_cast<int32_t>(target.health) >= std::bit_cast<int32_t>(target.type->maximum_health))
        return {};
    const auto mode = host.rules.rules().repair.rate.mode;
    if (mode == data::match_rules::RepairRateMode::exact_remainder)
        return recover_health_exactly(repairer, target, rate, host);
    auto health = repair_quantity(
        static_cast<int32_t>(target.type->maximum_health), rate, target.type->build_time
    );
    auto energy = repair_quantity(
        static_cast<double>(target.type->energy_cost), rate, target.type->build_time
    );
    if (mode == data::match_rules::RepairRateMode::clamp_min_1) {
        health = std::max(health, 1);
        energy = std::max(energy, 1);
    } else {
        health = std::min(health, 1);
        energy = std::min(energy, 1);
    }
    RecoveryResult result{false, health, energy};
    if (debit_resource(host.energy_debit(repairer), static_cast<float>(energy))) {
        (void)submit_damage(&repairer, target, health, healing_damage_kind, host, 0);
        result.performed = true;
    }
    return result;
}

ConstructionResult
apply_build_progress(Unit& builder, Unit& target, float rate, ConstructionHost& host) {
    ConstructionResult result{};
    if (!target.type) {
        result.target_untyped = true;
        return result;
    }
    if (zero_or_unordered(target.build_remaining))
        return result;
    if (rate >= 0.0F)
        target.events = static_cast<uint16_t>(target.events | construction_event);
    if (zero_or_unordered(rate))
        return result;
    const double remaining = target.build_remaining;
    const double step =
        remaining - static_cast<double>(rate) / static_cast<double>(target.type->build_time);
    const double floored = step > 0.0 ? step : 0.0;
    const double next = floored >= 1.0 ? 1.0 : (step > 0.0 ? step : 0.0);
    const auto stored = static_cast<float>(next);
    const double delta = remaining - next;
    const auto energy_amount =
        static_cast<float>(delta * static_cast<double>(target.type->energy_cost));
    const auto metal_amount =
        static_cast<float>(delta * static_cast<double>(target.type->metal_cost));
    const auto maximum = static_cast<double>(target.type->maximum_health);
    const auto health_delta = static_cast<int32_t>(
        static_cast<uint32_t>(truncate_low32(maximum * remaining)) -
        static_cast<uint32_t>(truncate_low32(maximum * next))
    );
    if (rate >= 0.0F) {
        if (debit_resources(
                host.energy_debit(builder), host.metal_debit(builder), energy_amount, metal_amount
            )) {
            auto health = static_cast<int32_t>(target.health) + health_delta;
            if (static_cast<uint32_t>(health) >= target.type->maximum_health)
                health = static_cast<int32_t>(target.type->maximum_health);
            target.health = static_cast<int16_t>(static_cast<uint16_t>(health));
            target.flags |= construction_dirty_flag;
            target.build_remaining = stored;
            result.performed = true;
            result.energy_amount = energy_amount;
            result.metal_amount = metal_amount;
        }
    } else {
        host.refund_metal(target, -metal_amount);
        auto health = static_cast<int32_t>(target.health) + health_delta;
        if (health < 1)
            health = 0;
        target.flags |= construction_dirty_flag;
        target.health = static_cast<int16_t>(static_cast<uint16_t>(health));
        target.build_remaining = stored;
        if (stored >= 1.0F)
            submit_damage(
                &target, target, construction_cancel_amount, construction_cancel_kind, host, 0
            );
    }
    if (zero_or_unordered(target.build_remaining)) {
        result.completed = true;
        host.complete_construction(builder, target);
    }
    return result;
}

float build_decay_rate(const UnitType& type, int32_t ticks) noexcept {
    // The product keeps the low 32 bits; it is divided by the energy cost and
    // negated.
    const auto product = wrap_multiply(type.build_time, ticks);
    return static_cast<float>(
        -(static_cast<double>(product) / static_cast<double>(type.energy_cost))
    );
}
} // namespace oa::sim::unit_health
