// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Type 0x2c unit-state stream: header, per-unit delta list and the
// round-robin full unit record. A unit's delta layout depends on its
// movement class, which the wire does not carry, so the caller supplies it
// through UnitDeltaCodec; a stream with entries and no codec is reported as
// unsupported.

#include "oa/netgame/bitstream.hpp"

#include <cstdint>

namespace oa::netgame {

inline constexpr uint16_t unit_state_list_terminator = 0xffff;
// The builder stops adding units once the stream reaches this many bytes.
inline constexpr std::size_t unit_state_soft_limit_bytes = 0x200;
inline constexpr unsigned unit_state_unit_index_bits = 16;
inline constexpr unsigned unit_state_carrier_index_bits = 15;
inline constexpr unsigned unit_state_max_def_index_bits = 16;
inline constexpr std::size_t unit_state_max_entries = 512;

// Base fields of one unit, with the Unit fields they come from.
struct FullUnitRecord {
    uint16_t unit_def_index{}; // type_index; 0 = slot empty and nothing else follows
    int16_t health{};          // health
    uint8_t build_byte{};      // build_remaining, coded by unit_state_build_byte
    uint8_t state_flags{};     // state_flags
    uint8_t occupancy{};       // flags & 3
    bool attached{};           // attach_parent != 0 selects the attached variant
    // detached variant
    int32_t position[3]{};  // position x, y, z
    uint16_t heading{};     // heading
    uint16_t pitch{};       // pitch
    uint16_t bank{};        // bank
    bool has_object_word{}; // present only when the unit has a movement driver
    uint32_t object_word{}; // the movement driver's speed
    // attached variant
    uint16_t attached_unit_index{}; // 15 bits: id of the unit at attach_parent
    int8_t attach_piece{};          // attach_piece, sign-extended on read
};

/// Encodes a build fraction as its wire byte.
///
/// @param fraction Build fraction, 0.0..1.0.
/// @return 0 for exactly 0, otherwise (1 - trunc(fraction * -254)) & 0xff.
/// @quirk Encoding and decoding are not inverses; NaN and products outside the 64-bit range
///        truncate to INT64_MIN.
[[nodiscard]] uint8_t unit_state_build_byte(float fraction) noexcept;

/// Decodes a wire byte into a build fraction.
///
/// @param wire Wire byte.
/// @return wire multiplied by the float 1/255.
[[nodiscard]] float unit_state_build_fraction(uint8_t wire) noexcept;

/// Tells whether a received build byte differs from a unit's current build fraction.
///
/// The receiver then stores the decoded value and sets OA_UNIT_FLAG_CONSTRUCTION_DIRTY in Unit.flags.
///
/// @param wire Received wire byte.
/// @param current The unit's current build fraction.
/// @return True when wire * (float 1/255), compared in double precision, differs from current.
[[nodiscard]] bool unit_state_build_fraction_differs(uint8_t wire, float current) noexcept;

/// Returns the pool index of the unit whose full record rides on a sender tick.
///
/// @param sender_tick The sender's game tick.
/// @param units_per_player Size of each player's unit pool.
/// @return Signed sender_tick % units_per_player; 0 when units_per_player is 0.
/// @quirk Ticks of 2^31 and above give negative indices, as in 3.1c.
[[nodiscard]] int32_t
unit_state_full_record_slot(uint32_t sender_tick, uint16_t units_per_player) noexcept;

/// Writes a full unit record in the game's bit order and widths.
///
/// A zero def index is written alone and ends the record; otherwise health,
/// build byte, state flags, the two low bits of occupancy and the attached
/// bit follow, then the detached or attached variant.
///
/// @param[in,out] writer Writer to append to; its error becomes bad_argument for bad def_index_bits.
/// @param record Record to write.
/// @param def_index_bits Width of the def index field, 1..16.
void write_full_unit_record(
    BitWriter* writer, const FullUnitRecord& record, unsigned def_index_bits
) noexcept;

/// Reads a full unit record written by write_full_unit_record.
///
/// @param[in,out] reader Reader to advance.
/// @param def_index_bits Width of the def index field, 1..16.
/// @param object_word_present Whether the receiver's unit has a movement driver; the wire does not say,
///        and the object word is read only in that case.
/// @param[out] out Decoded record; written only on success.
/// @return ok; bad_argument for bad def_index_bits or a null out; truncated when the reader overran.
[[nodiscard]] WireError read_full_unit_record(
    BitReader* reader, unsigned def_index_bits, bool object_word_present, FullUnitRecord* out
) noexcept;

// Per-class delta codec supplied by the caller.
struct UnitDeltaCodec {
    void* context{};
    WireError (*read)(void* context, uint16_t unit_index, uint16_t def_index, BitReader* reader){};
    WireError (*write)(void* context, uint16_t unit_index, uint16_t def_index, BitWriter* writer){};
};

// The one per-class layout the engine encodes: a flag bit, a 2-bit count
// (at most 3) and that many 16-bit coordinate pairs.
struct WaypointDelta {
    bool flag{};     // the movement record's movement_blocked bit
    uint8_t count{}; // 0..3
    int16_t points[3][2]{};
};

/// Writes a waypoint delta: flag bit, 2-bit count, then count pairs of 16-bit coordinates.
///
/// @param[in,out] writer Writer to append to.
/// @param delta Delta to write; a count above 3 is written as 3.
void write_waypoint_delta(BitWriter* writer, const WaypointDelta& delta) noexcept;

/// Reads a waypoint delta written by write_waypoint_delta.
///
/// @param[in,out] reader Reader to advance.
/// @param[out] out Decoded delta.
void read_waypoint_delta(BitReader* reader, WaypointDelta* out) noexcept;

struct UnitStateEntry {
    uint16_t unit_index{}; // unit id minus the owner's base id
    uint16_t def_index{};
    uint32_t delta_bit_offset{}; // from the record start
    uint32_t delta_bit_count{};
};

struct UnitStateBody {
    uint16_t length{};
    uint32_t sender_tick{};
    uint16_t entry_count{};
    UnitStateEntry entries[unit_state_max_entries]{};
    bool has_full_record{};
    FullUnitRecord full{};
    uint32_t bits_consumed{};
};

struct UnitStateDecodeOptions {
    unsigned def_index_bits{}; // Game.unit_def_id_bits
    bool full_record_object_word_present{};
    const UnitDeltaCodec* delta_codec{}; // required when the list is non-empty
};

/// Decodes a complete 0x2c record: header, per-unit delta list and the optional full unit record.
///
/// @param bytes Record start, type byte first.
/// @param size Bytes readable from bytes; at least the record's own u16 length.
/// @param options Def index width, full-record object word presence and the per-class delta codec.
/// @param[out] out Decoded body; partially written on failure.
/// @return ok; bad_argument for a null pointer or bad def index width; truncated; invalid_type;
///         length_mismatch; overflow beyond 512 entries; unsupported_delta_layout when the list is not
///         empty and no codec is supplied; or the codec's error.
[[nodiscard]] WireError decode_unit_state(
    const uint8_t* bytes,
    std::size_t size,
    const UnitStateDecodeOptions& options,
    UnitStateBody* out
) noexcept;

/// Starts a 0x2c record: type byte, a zero length to patch later, and the sender tick.
///
/// @param[in,out] writer Writer positioned at the record start.
/// @param sender_tick The sender's game tick.
void unit_state_begin(BitWriter* writer, uint32_t sender_tick) noexcept;

/// Writes the header of one delta list entry; the caller writes the per-class delta after it.
///
/// @param[in,out] writer Writer to append to; its error becomes bad_argument for bad def_index_bits.
/// @param unit_index Unit id minus the owner's base id.
/// @param def_index The unit's def index.
/// @param def_index_bits Width of the def index field, 1..16.
void unit_state_write_entry_header(
    BitWriter* writer, uint16_t unit_index, uint16_t def_index, unsigned def_index_bits
) noexcept;

/// Tells whether the builder would stop adding further units.
///
/// @param writer Writer building the record.
/// @return True once the record has reached 0x200 bytes.
[[nodiscard]] bool unit_state_full(const BitWriter* writer) noexcept;

/// Ends a 0x2c record: list terminator, flag bit 1, the full unit record, then the length patch.
///
/// @param[in,out] writer Writer building the record.
/// @param full Full unit record to append.
/// @param def_index_bits Width of the def index field, 1..16.
/// @param[out] length Record length in bytes, when not null.
/// @return ok, or the writer's error; overflow when the record exceeds 0xffff bytes.
[[nodiscard]] WireError unit_state_finish(
    BitWriter* writer, const FullUnitRecord& full, unsigned def_index_bits, uint16_t* length
) noexcept;

} // namespace oa::netgame
