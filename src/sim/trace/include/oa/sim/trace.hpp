// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Per-tick digests of the canonical simulation state, written as a trace
// stream so that two runs of the same match compare tick by tick.
//
// A tick is described twice. The tick record holds five words sampled at the
// head of each simulation tick, so two streams compare word for word. The
// section digests hash the fuller state in slot order, one 64-bit value per
// section and a total over them, to name the system two runs part over. Both
// go into one stream.
#pragma once

#include "oa/core/world.h"

#include <cstddef>
#include <cstdint>

namespace oa::sim::trace {

// Stream layout: a header of four little-endian words (magic, version,
// record size, seed), then records of six words whose first names the kind.
inline constexpr uint32_t stream_magic = 0x3154414fu; // "OAT1" in file order
inline constexpr uint32_t stream_version = 1;
inline constexpr size_t header_size = 16;
inline constexpr size_t record_size = 24;

enum class RecordKind : uint32_t {
    tick = 1,       // tick, live units, unit fold, economy fold, generator
    section = 0x10, // tick, section, digest low word, digest high word, items
};

// Section 0 is the total over the tick and the other sections.
enum class Section : uint32_t {
    total,
    units,
    weapons,
    orders,
    projectiles,
    players,
    random,
    count
};
inline constexpr size_t section_count = static_cast<size_t>(Section::count);

// Values of one unit slot kept outside the canonical records.
struct UnitSide {
    uint32_t movement_speed{}; // the movement object's speed, 16.16
    uint8_t has_movement{};    // Unit.movement holds a movement object (mobile units only)
    uint8_t has_order{};
    uint8_t order_kind{}; // head of the primary order queue
};

struct RandomState {
    uint32_t core{}; // the shared synced generator
    uint32_t lcg{};  // the linear congruential generator: x = x * 214013 + 2531011
};

// The tick record, sampled once Game.tick has advanced and before the unit
// sweep. Units with a movement object and the live flag are counted and
// folded in slot order, each as these words:
// type_index, health and build_remaining bits, state_flags, flags & 3, whether
// attach_parent is set, then the carrier's id and attach_piece or position
// x, y, z, heading, pitch, bank and the movement object's speed. Signed
// bytes and halves are sign-extended except pitch and bank, read unsigned.
struct TickRecord {
    uint32_t tick{}; // Game.tick
    uint32_t live_units{};
    uint32_t unit_fold{};
    uint32_t economy_fold{}; // every slot's in_use; metal then energy bits of those in use
    uint32_t random{};       // core generator state
};

struct SectionDigest {
    uint64_t value{};
    uint32_t items{}; // units, projectiles or players folded
};

struct TickDigest {
    uint32_t tick{};
    SectionDigest sections[section_count]{};
};

// Per-unit fields in the order they are folded and dumped. The weapon
// fields repeat per slot, w0 first.
enum class UnitField : uint8_t {
    type,
    owner,
    flags,
    state,
    health,
    build, // build_remaining float bits
    x,
    y,
    z,
    heading,
    pitch,
    bank,
    parent, // attach_parent as a unit slot, 0 none
    piece,
    movement,
    speed,
    order,
    order_kind,
    w0_target_a,
    w0_target_b,
    w0_reload,
    w0_flags,
    w0_stockpile,
    w1_target_a,
    w1_target_b,
    w1_reload,
    w1_flags,
    w1_stockpile,
    w2_target_a,
    w2_target_b,
    w2_reload,
    w2_flags,
    w2_stockpile,
    count
};
inline constexpr size_t unit_field_count = static_cast<size_t>(UnitField::count);

/// Folds one word into the tick record's unit or economy fold.
///
/// @param state fold so far
/// @param value word to fold
/// @return (state xor value) times the 32-bit FNV prime, rotated left by 13
uint32_t tick_fold(uint32_t state, uint32_t value) noexcept;

// Offset basis and prime of the 64-bit FNV-1a digests: the section digests
// fold each word's four little-endian bytes (digest_fold), the match-state
// digest each value's bytes as the host holds them.
inline constexpr uint64_t digest_basis = 0xcbf29ce484222325ull;
inline constexpr uint64_t digest_prime = 0x100000001b3ull;

/// Folds one word into a section digest: 64-bit FNV-1a over its four little-endian bytes.
///
/// @param state digest so far; digest_basis to start
/// @param value word to fold
/// @return the new digest
uint64_t digest_fold(uint64_t state, uint32_t value) noexcept;

/// Samples the tick record of the current tick.
///
/// @param world units, players and Game.tick
/// @param sides one entry per unit slot, or null when no slot has a movement object or
///        an order
/// @param core_random synced generator state
/// @return the five words of the tick record
TickRecord
sample_tick_record(const World& world, const UnitSide* sides, uint32_t core_random) noexcept;

/// Computes the section digests of the current tick.
///
/// Folds the occupied unit slots (type_index != 0) in slot order, the live projectile
/// records in pool order and all ten player slots, each section seeded with
/// digest_basis; the total folds the tick and every section's value and count, then,
/// when the match keeps rule state, its digest and table count the same way.
///
/// @param world units, projectiles, players and Game.tick
/// @param sides one entry per unit slot, or null
/// @param random generator states
/// @param rule_state the digest of the state a mod's rules keep outside the
///        canonical records and its number of tables; null when there is none
/// @return the digests
TickDigest tick_digest(
    const World& world,
    const UnitSide* sides,
    const RandomState& random,
    const SectionDigest* rule_state = nullptr
) noexcept;

/// Returns the name of a digest section.
///
/// @param section section
/// @return its lower-case name
const char* section_name(Section section) noexcept;
/// Returns the name of a folded unit field.
///
/// @param field field
/// @return its name as format_unit prints it
const char* unit_field_name(UnitField field) noexcept;
/// Returns the section a unit field is folded into.
///
/// @param field field
/// @return units, weapons or orders
Section unit_field_section(UnitField field) noexcept;
/// Returns a unit field as the word folded into its section.
///
/// @param world unit table, for the carrier slot
/// @param unit unit record
/// @param side the slot's values kept outside the canonical records
/// @param field field to read
/// @return the word; signed fields sign-extended
uint32_t unit_field_value(
    const World& world, const Unit& unit, const UnitSide& side, UnitField field
) noexcept;

/// Formats one text line of an occupied slot's fields.
///
/// The line is "tick=T slot=S name=value ..." with a newline.
///
/// @param world unit table
/// @param slot unit slot
/// @param sides one entry per unit slot, or null
/// @param tick tick printed
/// @param[out] out receives the line, truncated to `capacity`
/// @param capacity bytes of `out`
/// @return the full length, as snprintf returns it
size_t format_unit(
    const World& world,
    uint32_t slot,
    const UnitSide* sides,
    uint32_t tick,
    char* out,
    size_t capacity
) noexcept;

/// Encodes the stream header: magic, version, record size and seed.
///
/// @param[out] out receives four little-endian words
/// @param seed seed recorded in the header
void encode_header(uint8_t (&out)[header_size], uint32_t seed) noexcept;
/// Encodes a tick record.
///
/// @param[out] out receives six little-endian words
/// @param record tick record words
void encode_tick_record(uint8_t (&out)[record_size], const TickRecord& record) noexcept;
/// Encodes one section digest as a section record.
///
/// @param[out] out receives six little-endian words
/// @param tick tick of the digest
/// @param section section
/// @param digest digest value and item count
void encode_section_record(
    uint8_t (&out)[record_size], uint32_t tick, Section section, const SectionDigest& digest
) noexcept;

} // namespace oa::sim::trace
