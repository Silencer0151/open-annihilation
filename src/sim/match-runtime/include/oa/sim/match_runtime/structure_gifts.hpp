// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The structure gift rate limit (sharing.structure-gift-rate-limit): the
// structures a player gives through the share panel are held back as one
// batch, given at once while few structures went across lately, and
// otherwise given a fixed time later. The state lives here, beside the match
// that applies it (Match::begin_share_gift and its kin); a match keeps it
// only while the rule is on, and then digests and saves it as the rule-state
// table structure_gifts_table_name.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace oa::sim::match_runtime {

/// The name of the gift state's rule-state table.
inline constexpr const char* structure_gifts_table_name = "structure-gifts";

/// The most structures held back at once, over every waiting batch.
inline constexpr size_t structure_gift_capacity = 2048;
/// The most batches waiting at once.
inline constexpr size_t structure_gift_batch_capacity = 64;
/// The most recent gifts the window remembers.
inline constexpr size_t structure_gift_run_capacity = 256;

/// One structure a gift holds back: the unit, and what it must still be when
/// the gift is carried out.
struct StructureGift {
    uint16_t unit{};     ///< Unit.id
    uint16_t type{};     ///< Unit.type_index when given
    uint8_t owner{};     ///< Unit.owner_index when given
    uint8_t recipient{}; ///< the receiving player, 0..9
};

static_assert(sizeof(StructureGift) == 6);

/// Structures that went across in one tick's gift.
struct StructureGiftRun {
    uint32_t tick{};  ///< Game.tick of the gift
    uint32_t count{}; ///< structures handed over
};

/// A batch that waits: its due tick and how many of the held-back structures
/// are its own (StructureGiftState::waiting, in batch order).
struct StructureGiftBatch {
    uint32_t due_tick{};
    uint16_t count{};
};

/// The gift state of one match.
struct StructureGiftState {
    /// Recent gifts, oldest first; those older than the window are dropped
    /// before the window is counted (prune_structure_gifts).
    std::array<StructureGiftRun, structure_gift_run_capacity> runs{};
    uint16_t run_count{};
    /// Waiting batches, earliest due first.
    std::array<StructureGiftBatch, structure_gift_batch_capacity> batches{};
    uint16_t batch_count{};
    /// The waiting batches' structures, the first batch's first.
    std::array<StructureGift, structure_gift_capacity> waiting{};
    uint16_t waiting_count{};
    /// The share panel's gift being collected (Match::begin_share_gift); never
    /// kept across ticks, so it is neither digested nor saved.
    std::array<StructureGift, structure_gift_capacity> collected{};
    uint16_t collected_count{};
    /// The encoded state, rewritten each time encode_structure_gifts runs.
    std::array<
        uint8_t,
        8 + structure_gift_run_capacity * 8 + structure_gift_batch_capacity * 8 +
            structure_gift_capacity * 6>
        encoded{};
};

/// Forgets the gifts that left the window.
///
/// Gifts made window_ticks or more before `tick` no longer count.
///
/// @param[in,out] state the gift state
/// @param tick Game.tick now
/// @param window_ticks the window, ticks
void prune_structure_gifts(StructureGiftState& state, uint32_t tick, int32_t window_ticks) noexcept;

/// Counts the structures given within the window.
///
/// @param state the gift state, pruned
/// @return the structures of every remembered gift
[[nodiscard]] uint32_t recent_structure_gifts(const StructureGiftState& state) noexcept;

/// Remembers a gift of structures.
///
/// When the memory is full its oldest gift is merged into the next, which
/// keeps the count while the oldest structures leave the window later.
///
/// @param[in,out] state the gift state
/// @param tick Game.tick of the gift
/// @param count structures handed over; 0 remembers nothing
void note_structure_gift(StructureGiftState& state, uint32_t tick, uint32_t count) noexcept;

/// Queues a batch to be given at a later tick.
///
/// @param[in,out] state the gift state
/// @param due_tick the Game.tick from which the batch is given
/// @param batch its structures
/// @return the structures queued: all of them, fewer when the waiting room is
///         short, or none when no batch fits
uint16_t defer_structure_gifts(
    StructureGiftState& state, uint32_t due_tick, std::span<const StructureGift> batch
) noexcept;

/// Takes the first waiting batch when it is due.
///
/// @param[in,out] state the gift state; the batch leaves the queue
/// @param tick Game.tick now
/// @param[out] batch receives its structures
/// @return the structures written to `batch`, 0 when no batch is due
uint16_t take_due_structure_gifts(
    StructureGiftState& state,
    uint32_t tick,
    std::array<StructureGift, structure_gift_capacity>& batch
) noexcept;

/// Encodes the kept state (gifts in the window and waiting batches) into
/// StructureGiftState::encoded, little-endian.
///
/// The bytes are the run, batch and waiting counts as 16-bit words and a
/// zero word, then each run (tick, count as 32-bit words), each batch (due
/// tick as a 32-bit word, count and a zero as 16-bit words) and each waiting
/// structure (unit, type as 16-bit words, owner, recipient as bytes).
///
/// @param[in,out] state the gift state
/// @return the encoded bytes, a view of StructureGiftState::encoded
std::span<const uint8_t> encode_structure_gifts(StructureGiftState& state) noexcept;

/// Replaces the kept state with encoded bytes (encode_structure_gifts).
///
/// @param[in,out] state the gift state; unchanged on failure
/// @param bytes the encoded state
/// @return false when the bytes are not a whole state within the capacities
bool decode_structure_gifts(StructureGiftState& state, std::span<const uint8_t> bytes) noexcept;

/// What became of a share panel's gift of structures.
struct ShareGiftOutcome {
    uint16_t given{};       ///< structures handed over at once
    uint16_t waiting{};     ///< structures held back for later
    int32_t wait_seconds{}; ///< seconds at normal speed until they go across
};

} // namespace oa::sim::match_runtime
