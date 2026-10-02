// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Per-class 0x2c unit deltas. Mobile units (def bm_code 1) get a movement
// record at creation whose driver object is chosen from the unit def: a
// ground or an air driver, each with a local (sending) and a remote (receiving) class.
// Each driver's has-delta, write and read operations produce the delta that
// follows a unit's index and def index in the stream.
// Only two layouts exist; the stream does not say which one follows, so both
// ends derive it from the unit def.

#include "oa/core/unit_def.h"
#include "oa/netgame/unit_state.hpp"

namespace oa::netgame {

enum class MovementClass : uint8_t {
    none = 0,   // no driver: the unit never has a delta
    ground = 1, // local and remote ground drivers
    air = 2,    // local and remote air drivers
};

// Tag written before an air driver's goal. The local writer derives it from
// the goal object's kind: kind 2 -> 1, kind 3 -> 2.
inline constexpr uint8_t air_goal_tag_none = 0;
inline constexpr uint8_t air_goal_tag_target = 1; // a target goal follows
inline constexpr uint8_t air_goal_tag_seek = 2;   // a VTOL seek goal follows
inline constexpr unsigned air_goal_tag_bits = 2;
inline constexpr unsigned air_substate_bits = 2;

// Bits of AirTargetGoal.flags selecting the optional fields, in wire order.
inline constexpr uint8_t air_target_has_unit = 0x01;
inline constexpr uint8_t air_target_has_arrival_radius = 0x10;
inline constexpr uint8_t air_target_has_altitude = 0x08;
inline constexpr uint8_t air_target_has_bearing = 0x40;
inline constexpr uint8_t air_target_has_position = 0x20;

// Target goal as carried on the wire.
struct AirTargetGoal {
    uint8_t flags{};          // air_target_has_*; only the low byte is on the wire
    int16_t query_point{};    // with air_target_has_unit
    uint16_t target_unit{};   // id of the linked unit; 0 = none
    int16_t arrival_radius{}; // with air_target_has_arrival_radius, else cleared
    int16_t altitude{};       // with air_target_has_altitude, else cleared
    int16_t bearing{};        // with air_target_has_bearing, else cleared
    int32_t position[3]{};    // with air_target_has_position
};

// VTOL seek goal as carried on the wire.
struct AirSeekGoal {
    bool flag{}; // turn the step toward heading
    int32_t point[3]{};
    int32_t step[3]{};  // added to the point every tick
    uint16_t heading{}; // with flag
};

struct AirDelta {
    uint8_t goal_tag{}; // air_goal_tag_*; 3 reads as "no goal"
    // The local writer emits no tag at all when the driver holds a goal of
    // another kind, leaving the receiver to read the next field as the tag.
    bool goal_tag_omitted{};
    AirTargetGoal target{};
    AirSeekGoal seek{};
    uint8_t substate{}; // MovementRecord.flags & movement_substate_mask
};

struct UnitDelta {
    MovementClass movement{};
    WaypointDelta ground{};
    AirDelta air{};
};

/// Writes an air delta: the 2-bit goal tag and its goal, then the 2-bit substate.
///
/// @param[in,out] writer Writer to append to.
/// @param delta Delta to write; with goal_tag_omitted no tag or goal is written, as 3.1c does for a
///        goal of another kind.
void write_air_delta(BitWriter* writer, const AirDelta& delta) noexcept;

/// Reads an air delta written by write_air_delta.
///
/// @param[in,out] reader Reader to advance.
/// @param[out] out Decoded delta; tags 0 and 3 carry no goal.
void read_air_delta(BitReader* reader, AirDelta* out) noexcept;

/// Writes the delta body of a unit's movement class; class none writes nothing.
///
/// @param[in,out] writer Writer to append to.
/// @param delta Delta to write; its movement class picks the layout.
void write_unit_delta(BitWriter* writer, const UnitDelta& delta) noexcept;

/// Reads the delta body of a movement class; class none reads nothing.
///
/// @param[in,out] reader Reader to advance.
/// @param movement Layout to read, derived from the unit def.
/// @param[out] out Decoded delta.
/// @return ok; bad_argument when out is null; truncated when the reader overran.
[[nodiscard]] WireError
read_unit_delta(BitReader* reader, MovementClass movement, UnitDelta* out) noexcept;

inline constexpr int8_t unit_def_bm_code_mobile = 1;

/// Returns the delta layout of a unit def.
///
/// Only defs with bm_code 1 get a movement record; the driver choice then
/// gives can-fly defs the air driver and the rest the ground driver (local
/// or remote does not change the layout).
///
/// @param def Unit definition.
/// @return air, ground, or none for a def without a movement record.
[[nodiscard]] MovementClass movement_class_for_def(const UnitDef& def) noexcept;

// Delta layout per unit def index, supplied by whoever owns the defs.
struct MovementClassTable {
    const MovementClass* by_def_index{};
    std::size_t count{};
};

// Codec state: reads decode through the class table into deltas[] (one per
// list entry, in order); writes emit source[] in order. Either buffer may be
// absent for the direction not used.
struct DeltaCodecState {
    MovementClassTable classes{};
    UnitDelta* deltas{};
    std::size_t capacity{};
    std::size_t count{};
    const UnitDelta* source{};
    std::size_t source_count{};
    std::size_t source_next{};
};

/// Returns a UnitDeltaCodec bound to a codec state.
///
/// Reads fail with unsupported_delta_layout for a def index outside the
/// class table or of class none, and with overflow once deltas[] is full;
/// writes fail with bad_argument once source[] is exhausted.
///
/// @param state Codec state passed back to the codec as its context; must outlive the codec.
/// @return The codec.
[[nodiscard]] UnitDeltaCodec delta_codec(DeltaCodecState* state) noexcept;

/// Rebuilds a decoded 0x2c record: header, entries with deltas[i] as each entry's delta, terminator and
/// full record flag (with the record when present), then the length patch.
///
/// @param[in,out] writer Freshly initialised writer that receives the record.
/// @param body Decoded record whose header, entries and full record are written.
/// @param deltas One delta per entry; may be null only when there are no entries.
/// @param def_index_bits Width of the def index field, 1..16.
/// @param[out] length Record length in bytes, when not null.
/// @return ok; bad_argument for a null writer or missing deltas; overflow above 0xffff bytes; the writer's error.
[[nodiscard]] WireError encode_unit_state(
    BitWriter* writer,
    const UnitStateBody& body,
    const UnitDelta* deltas,
    unsigned def_index_bits,
    uint16_t* length
) noexcept;

} // namespace oa::netgame
