// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/match_runtime/structure_gifts.hpp"

#include "oa/sim/match_runtime.hpp"

#include <algorithm>
#include <cstring>

namespace oa::sim::match_runtime {
namespace {

/// Ticks a second at normal speed, which the waiting message counts in.
constexpr int32_t ticks_per_second = 30;
/// Bytes of the encoded state's header: three counts and a zero word.
constexpr size_t encoded_header_bytes = 8;
/// Bytes of one encoded gift run, batch and waiting structure.
constexpr size_t encoded_run_bytes = 8;
constexpr size_t encoded_batch_bytes = 8;
constexpr size_t encoded_gift_bytes = 6;

/// Writes a little-endian value at a position and moves past it.
///
/// @param[in,out] at the position
/// @param value the value
template <typename T>
void put(uint8_t*& at, T value) noexcept {
    std::memcpy(at, &value, sizeof value);
    at += sizeof value;
}

/// Reads a little-endian value at a position and moves past it.
///
/// @param[in,out] at the position
/// @return the value
template <typename T>
T take(const uint8_t*& at) noexcept {
    T value{};
    std::memcpy(&value, at, sizeof value);
    at += sizeof value;
    return value;
}

} // namespace

void prune_structure_gifts(
    StructureGiftState& state, uint32_t tick, int32_t window_ticks
) noexcept {
    uint16_t expired = 0;
    while (expired < state.run_count &&
           static_cast<int32_t>(tick - state.runs[expired].tick) >= window_ticks)
        ++expired;
    if (expired == 0)
        return;
    std::copy(
        state.runs.begin() + expired, state.runs.begin() + state.run_count, state.runs.begin()
    );
    state.run_count = static_cast<uint16_t>(state.run_count - expired);
}

uint32_t recent_structure_gifts(const StructureGiftState& state) noexcept {
    uint32_t count = 0;
    for (uint16_t at = 0; at < state.run_count; ++at)
        count += state.runs[at].count;
    return count;
}

void note_structure_gift(StructureGiftState& state, uint32_t tick, uint32_t count) noexcept {
    if (count == 0)
        return;
    if (state.run_count == state.runs.size()) {
        state.runs[1].count += state.runs[0].count;
        std::copy(state.runs.begin() + 1, state.runs.end(), state.runs.begin());
        --state.run_count;
    }
    state.runs[state.run_count++] = {tick, count};
}

uint16_t defer_structure_gifts(
    StructureGiftState& state, uint32_t due_tick, std::span<const StructureGift> batch
) noexcept {
    if (batch.empty() || state.batch_count == state.batches.size())
        return 0;
    const size_t room = state.waiting.size() - state.waiting_count;
    const auto count = static_cast<uint16_t>(std::min(batch.size(), room));
    if (count == 0)
        return 0;
    std::copy_n(batch.begin(), count, state.waiting.begin() + state.waiting_count);
    state.waiting_count = static_cast<uint16_t>(state.waiting_count + count);
    state.batches[state.batch_count++] = {due_tick, count};
    return count;
}

uint16_t take_due_structure_gifts(
    StructureGiftState& state,
    uint32_t tick,
    std::array<StructureGift, structure_gift_capacity>& batch
) noexcept {
    if (state.batch_count == 0 ||
        static_cast<int32_t>(state.batches[0].due_tick) > static_cast<int32_t>(tick))
        return 0;
    const uint16_t count = state.batches[0].count;
    std::copy_n(state.waiting.begin(), count, batch.begin());
    std::copy(
        state.waiting.begin() + count,
        state.waiting.begin() + state.waiting_count,
        state.waiting.begin()
    );
    state.waiting_count = static_cast<uint16_t>(state.waiting_count - count);
    std::copy(
        state.batches.begin() + 1, state.batches.begin() + state.batch_count, state.batches.begin()
    );
    --state.batch_count;
    return count;
}

std::span<const uint8_t> encode_structure_gifts(StructureGiftState& state) noexcept {
    uint8_t* at = state.encoded.data();
    put<uint16_t>(at, state.run_count);
    put<uint16_t>(at, state.batch_count);
    put<uint16_t>(at, state.waiting_count);
    put<uint16_t>(at, 0);
    for (uint16_t i = 0; i < state.run_count; ++i) {
        put<uint32_t>(at, state.runs[i].tick);
        put<uint32_t>(at, state.runs[i].count);
    }
    for (uint16_t i = 0; i < state.batch_count; ++i) {
        put<uint32_t>(at, state.batches[i].due_tick);
        put<uint16_t>(at, state.batches[i].count);
        put<uint16_t>(at, 0);
    }
    for (uint16_t i = 0; i < state.waiting_count; ++i) {
        const StructureGift& gift = state.waiting[i];
        put<uint16_t>(at, gift.unit);
        put<uint16_t>(at, gift.type);
        put<uint8_t>(at, gift.owner);
        put<uint8_t>(at, gift.recipient);
    }
    return {state.encoded.data(), static_cast<size_t>(at - state.encoded.data())};
}

bool decode_structure_gifts(StructureGiftState& state, std::span<const uint8_t> bytes) noexcept {
    if (bytes.size() < encoded_header_bytes)
        return false;
    const uint8_t* at = bytes.data();
    const auto runs = take<uint16_t>(at);
    const auto batches = take<uint16_t>(at);
    const auto waiting = take<uint16_t>(at);
    if (take<uint16_t>(at) != 0 || runs > state.runs.size() || batches > state.batches.size() ||
        waiting > state.waiting.size())
        return false;
    const size_t size = encoded_header_bytes + runs * encoded_run_bytes +
                        batches * encoded_batch_bytes + waiting * encoded_gift_bytes;
    if (bytes.size() != size)
        return false;
    StructureGiftState read{};
    read.run_count = runs;
    read.batch_count = batches;
    read.waiting_count = waiting;
    for (uint16_t i = 0; i < runs; ++i) {
        read.runs[i].tick = take<uint32_t>(at);
        read.runs[i].count = take<uint32_t>(at);
    }
    uint32_t batched = 0;
    for (uint16_t i = 0; i < batches; ++i) {
        read.batches[i].due_tick = take<uint32_t>(at);
        read.batches[i].count = take<uint16_t>(at);
        if (take<uint16_t>(at) != 0)
            return false;
        batched += read.batches[i].count;
    }
    if (batched != waiting)
        return false;
    for (uint16_t i = 0; i < waiting; ++i) {
        read.waiting[i].unit = take<uint16_t>(at);
        read.waiting[i].type = take<uint16_t>(at);
        read.waiting[i].owner = take<uint8_t>(at);
        read.waiting[i].recipient = take<uint8_t>(at);
    }
    state.runs = read.runs;
    state.run_count = read.run_count;
    state.batches = read.batches;
    state.batch_count = read.batch_count;
    state.waiting = read.waiting;
    state.waiting_count = read.waiting_count;
    state.collected_count = 0;
    return true;
}

void Match::keep_structure_gifts() {
    if (!input_.rules.sharing.structure_gift_rate_limit.enabled)
        return;
    structure_gifts_ = std::make_unique<StructureGiftState>();
    const RuleStateTable table{
        structure_gifts_table_name,
        structure_gifts_.get(),
        [](void* context) {
            return encode_structure_gifts(*static_cast<StructureGiftState*>(context));
        },
        [](void* context, std::span<const uint8_t> bytes) {
            return decode_structure_gifts(*static_cast<StructureGiftState*>(context), bytes);
        },
    };
    if (!add_rule_state(rule_state_, table))
        fault_.note("the structure gift state found no room among the rule-state tables");
}

void Match::begin_share_gift() noexcept {
    if (structure_gifts_)
        structure_gifts_->collected_count = 0;
}

void Match::share_gift_unit(uint16_t unit, uint8_t recipient) {
    if (structure_gifts_ && unit < state().unit_slot_count && recipient < OA_PLAYER_COUNT) {
        const Unit& record = state().units[unit];
        const UnitDef* def = world_unit_def_of(&state(), &record);
        auto& gifts = *structure_gifts_;
        if (def != nullptr && def->bm_code == 0) {
            if (gifts.collected_count < gifts.collected.size())
                gifts.collected[gifts.collected_count++] = {
                    record.id, record.type_index, record.owner_index, recipient
                };
            return;
        }
    }
    transfer_unit(unit, recipient);
}

ShareGiftOutcome Match::end_share_gift() {
    ShareGiftOutcome outcome{};
    if (!structure_gifts_ || structure_gifts_->collected_count == 0)
        return outcome;
    auto& gifts = *structure_gifts_;
    const auto& rule = input_.rules.sharing.structure_gift_rate_limit;
    const uint32_t tick = state().game.tick;
    const std::span<const StructureGift> batch{gifts.collected.data(), gifts.collected_count};
    gifts.collected_count = 0;
    prune_structure_gifts(gifts, tick, rule.window_ticks);
    const auto recent = static_cast<int32_t>(recent_structure_gifts(gifts));
    if (recent + static_cast<int32_t>(batch.size()) <= rule.immediate_max) {
        outcome.given = give_structure_batch(batch);
        return outcome;
    }
    outcome.waiting =
        defer_structure_gifts(gifts, tick + static_cast<uint32_t>(rule.defer_ticks), batch);
    outcome.wait_seconds = rule.defer_ticks / ticks_per_second;
    return outcome;
}

void Match::give_due_structure_gifts() {
    if (!structure_gifts_)
        return;
    auto& gifts = *structure_gifts_;
    const uint32_t tick = state().game.tick;
    prune_structure_gifts(gifts, tick, input_.rules.sharing.structure_gift_rate_limit.window_ticks);
    for (;;) {
        const uint16_t count = take_due_structure_gifts(gifts, tick, gifts.collected);
        if (count == 0)
            return;
        give_structure_batch({gifts.collected.data(), count});
    }
}

uint16_t Match::give_structure_batch(std::span<const StructureGift> batch) {
    uint16_t given = 0;
    for (const StructureGift& gift : batch) {
        // Each structure goes only while it is still the same live unit of
        // the same player, and the recipient still plays.
        if (gift.unit >= state().unit_slot_count || gift.recipient >= OA_PLAYER_COUNT)
            continue;
        const Unit& unit = state().units[gift.unit];
        if (unit.type_index != gift.type || unit.owner_index != gift.owner ||
            (unit.flags & OA_UNIT_FLAG_LIVE) == 0 || (unit.flags & OA_UNIT_FLAG_DEATH_PENDING) != 0)
            continue;
        if (state().game.players[gift.recipient].in_use == 0)
            continue;
        transfer_unit(gift.unit, gift.recipient);
        ++given;
    }
    if (structure_gifts_)
        note_structure_gift(*structure_gifts_, state().game.tick, given);
    return given;
}

} // namespace oa::sim::match_runtime
