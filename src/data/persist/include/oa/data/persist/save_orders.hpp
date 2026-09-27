// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Saved unit orders: each order of a unit's two queues is one "u%04xm%04x"
// blob in the Units account, with its mission name in a "<blob>_name" field,
// the unit type of a build order in a "UTYPENAME%4d" field, and the order's
// movement goal in a "<blob>g" blob.
#pragma once

#include "oa/data/persist/hapibank.hpp"

#include "oa/core/unit.h"
#include "oa/core/world.h"

#include <cstddef>
#include <cstdint>

namespace oa::data::persist {

// Size of an order blob.
inline constexpr uint32_t order_blob_bytes = 0x3a;

// Offsets in the order blob of the fields of SavedOrder.
namespace order_blob {
inline constexpr std::size_t owner_id = 0x00;  // the unit the order belongs to
inline constexpr std::size_t target_id = 0x02; // the unit the order targets
inline constexpr std::size_t goal_kind = 0x04; // SavedGoalKind of the order's goal
inline constexpr std::size_t kind = 0x08, phase = 0x09, wait_events = 0x0a, wake_tick = 0x0e,
                             point = 0x12, anchor = 0x1e, seen_cell = 0x22, parameter_1 = 0x26,
                             parameter_2 = 0x2a, parameter_3 = 0x2e, preserve_flags = 0x32,
                             command_flags = 0x33, flags = 0x34, mission_byte = 0x35,
                             raised_events = 0x36;
} // namespace order_blob

// The words of an order that a save keeps.
struct SavedOrder {
    uint16_t owner_id{};
    uint16_t target_id{}; // 0 when the order has none or it is dead
    int32_t goal_kind{};  // SavedGoalKind; 0 when the order has no goal
    uint8_t kind{};       // mission index
    uint8_t phase{};
    uint32_t wait_events{};
    uint32_t wake_tick{};
    int32_t point[3]{};    // 16.16
    int16_t anchor[2]{};   // X and Z in whole world units
    uint32_t seen_cell{};  // target cell the order overlays last saw: X low, Z high
    int32_t parameter_1{}; // the unit type of the build missions
    int32_t parameter_2{};
    int32_t parameter_3{};
    uint8_t preserve_flags{};
    uint8_t command_flags{};
    uint8_t flags{};        // bit 2 puts the order on the secondary queue
    uint8_t mission_byte{}; // data::mission_types::mission_flags() of the order's kind
    uint32_t raised_events{};
};

// Goal kinds a saved order records.
enum class SavedGoalKind : int32_t {
    none = 0,
    air_target = 2,
    air_seek = 3,
    circle = 4,
    ring = 5,
    outline = 6,
};

// Goal blob sizes. The ground goals' first four bytes and the air goals'
// first eight carry no data and are written as zero.
inline constexpr uint32_t circle_goal_blob_bytes = 0x10;
inline constexpr uint32_t ring_goal_blob_bytes = 0x18;
inline constexpr uint32_t outline_goal_blob_bytes = 0x14;
inline constexpr uint32_t air_target_goal_blob_bytes = 0x36;
inline constexpr uint32_t air_seek_goal_blob_bytes = 0x2a;

struct SavedCircleGoal {
    int16_t cell[2]{}; // X, Z
    int32_t tolerance{};
    int32_t radius_squared{};
};

struct SavedRingGoal {
    int16_t cell[2]{}; // X, Z
    int32_t inner_range{};
    int32_t outer_range{};
    int32_t inner_radius_squared{};
    int32_t outer_radius_squared{};
};

// Border cells, all inclusive.
struct SavedOutlineGoal {
    int32_t left{}, right{}, top{}, bottom{};
};

// Blob bytes +0x0a..+0x19 carry no data and are written as zero.
struct SavedAirTargetGoal {
    uint16_t unit_id{};   // unit flying to the goal
    uint16_t target_id{}; // the followed unit
    uint16_t flags{};
    int16_t arrival_radius{};
    int16_t altitude{};
    uint16_t bearing{};
    int16_t query_point{};
    int32_t point[3]{};
    int32_t stand_off{};
};

struct SavedAirSeekGoal {
    uint16_t unit_id{}; // unit flying to the goal
    uint16_t flags{};
    int32_t point[3]{};
    int32_t step[3]{};
    uint16_t reserved_after_step{}; // no known use: a match saves 0 and ignores it on load
    uint16_t heading{};
    uint16_t reserved_after_heading{}; // no known use: a match saves 0 and ignores it on load
};

struct SavedGoal {
    int32_t kind{}; // SavedGoalKind
    SavedCircleGoal circle{};
    SavedRingGoal ring{};
    SavedOutlineGoal outline{};
    SavedAirTargetGoal air_target{};
    SavedAirSeekGoal air_seek{};
};

// One order of a unit's queues, head first.
using SavedOrderVisit = void (*)(void* walk, const SavedOrder* order, const SavedGoal* goal);

// Field and blob name suffixes and the type name key.
namespace order_key {
inline constexpr const char* name_suffix = "_name";
inline constexpr const char* goal_suffix = "g";
inline constexpr const char* type_name_format = "UTYPENAME%4d";
} // namespace order_key

// Longest order blob name the writer and reader accept.
inline constexpr std::size_t order_blob_name_limit = 0x1f;

/// Encodes the saved words of an order as its order_blob_bytes blob.
///
/// @param order order words to encode
/// @param[out] blob order_blob_bytes bytes, laid out as order_blob gives; the
///     gaps are zero
void save_encode_order(const SavedOrder* order, uint8_t* blob);

/// Decodes an order blob into its saved words.
///
/// @param blob order_blob_bytes bytes
/// @param[out] order order words; every field is written
void save_decode_order(const uint8_t* blob, SavedOrder* order);

inline constexpr uint32_t goal_blob_max_bytes = air_target_goal_blob_bytes;

/// Encodes a goal as its kind's writer stores it.
///
/// @param goal goal whose `kind` selects the member to encode
/// @param[out] blob goal_blob_max_bytes bytes; the encoded size is written
/// @return the blob size, or 0 for no goal or an unknown kind
uint32_t save_encode_goal(const SavedGoal* goal, uint8_t* blob);

/// Writes a circle goal blob into the open account.
///
/// The blob holds a zero word, cell X and Z, tolerance and radius squared.
///
/// @param goal goal to write
/// @param[in,out] bank bank with an open account
/// @param blob_name name of the goal blob, created when absent
void save_write_circle_goal(const SavedCircleGoal* goal, Bank* bank, const char* blob_name);

/// Reads a circle goal blob from the open account.
///
/// @param[in,out] bank bank with an open account; null fails
/// @param blob_name name of the goal blob; null fails; a missing blob is created empty
/// @param[out] goal goal to fill; unchanged unless the whole blob is read
/// @return true when the whole blob was read
bool save_read_circle_goal(Bank* bank, const char* blob_name, SavedCircleGoal* goal);

/// Writes a ring goal blob into the open account.
///
/// The blob holds a zero word, cell X and Z, the inner and outer ranges and
/// their squares.
///
/// @param goal goal to write
/// @param[in,out] bank bank with an open account
/// @param blob_name name of the goal blob, created when absent
void save_write_ring_goal(const SavedRingGoal* goal, Bank* bank, const char* blob_name);

/// Reads a ring goal blob from the open account.
///
/// @param[in,out] bank bank with an open account; null fails
/// @param blob_name name of the goal blob; null fails; a missing blob is created empty
/// @param[out] goal goal to fill; unchanged unless the whole blob is read
/// @return true when the whole blob was read
bool save_read_ring_goal(Bank* bank, const char* blob_name, SavedRingGoal* goal);

/// Writes an outline goal blob into the open account.
///
/// The blob holds a zero word, then left, right, top and bottom.
///
/// @param goal goal to write
/// @param[in,out] bank bank with an open account
/// @param blob_name name of the goal blob, created when absent
void save_write_outline_goal(const SavedOutlineGoal* goal, Bank* bank, const char* blob_name);

/// Reads an outline goal blob from the open account.
///
/// @param[in,out] bank bank with an open account; null fails
/// @param blob_name name of the goal blob; null fails; a missing blob is created empty
/// @param[out] goal goal to fill; unchanged unless the whole blob is read
/// @return true when the whole blob was read
bool save_read_outline_goal(Bank* bank, const char* blob_name, SavedOutlineGoal* goal);

/// Writes an air target goal blob into the open account.
///
/// The blob holds the unit id at +08, sixteen zero bytes, the target id at
/// +1a, then flags, arrival radius, altitude, bearing, query point, point and
/// stand-off.
///
/// @param goal goal to write
/// @param[in,out] bank bank with an open account
/// @param blob_name name of the goal blob, created when absent
void save_write_air_target_goal(const SavedAirTargetGoal* goal, Bank* bank, const char* blob_name);

/// Reads an air target goal blob from the open account.
///
/// @param[in,out] bank bank with an open account; null fails
/// @param blob_name name of the goal blob; null fails; a missing blob is created empty
/// @param[out] goal goal to fill; unchanged unless the whole blob is read
/// @return true when the whole blob was read
bool save_read_air_target_goal(Bank* bank, const char* blob_name, SavedAirTargetGoal* goal);

/// Writes an air seek goal blob into the open account.
///
/// The blob holds eight zero bytes, the unit id, flags, point, step,
/// reserved_after_step, heading and reserved_after_heading.
///
/// @param goal goal to write
/// @param[in,out] bank bank with an open account
/// @param blob_name name of the goal blob, created when absent
void save_write_air_seek_goal(const SavedAirSeekGoal* goal, Bank* bank, const char* blob_name);

/// Reads an air seek goal blob from the open account.
///
/// @param[in,out] bank bank with an open account; null fails
/// @param blob_name name of the goal blob; null fails; a missing blob is created empty
/// @param[out] goal goal to fill; unchanged unless the whole blob is read
/// @return true when the whole blob was read
bool save_read_air_seek_goal(Bank* bank, const char* blob_name, SavedAirSeekGoal* goal);

/// Tests whether a mission's order keeps a unit type in parameter_1.
///
/// @param kind mission index
/// @return true for MobileBuild, VTOL_MobileBuild and BuildingBuild; false
///     for other and unknown missions
bool save_order_names_type(uint8_t kind);

/// Records a unit type's name in the open account's "UTYPENAME%4d" field.
///
/// @param world world holding the unit table
/// @param[in,out] bank bank with an open account
/// @param type unit type id; nothing is written for 0, a type past the table
///     or a field that already exists
void save_write_order_type_name(const World* world, Bank* bank, uint16_t type);

/// Maps a saved unit type to its current index.
///
/// The name held in the type's UTYPENAME field is looked up in the current
/// table; without the field the saved value is taken as a position among the
/// types without the downloadable flag.
///
/// @param world world holding the sorted unit table
/// @param[in,out] bank bank with the Units account open
/// @param saved unit type id as the save stored it
/// @return the current type id, or 0 when the name or position matches nothing
/// @quirk A position resolves to the type id one below the type found at that
///     position, as in 3.1c.
uint16_t save_resolve_order_type(const World* world, Bank* bank, uint16_t saved);

/// Writes one order of a unit into the open account.
///
/// The order blob goes under `blob_name`, the mission name into
/// "<blob_name>_name", the unit type name of a build order into its UTYPENAME
/// field and the goal, when the order has one, into "<blob_name>g".
///
/// @param world world holding the unit table
/// @param unit unit that owns the order
/// @param order order words to write
/// @param goal goal of the kind order->goal_kind names
/// @param[in,out] bank bank with the Units account open
/// @param blob_name "u%04xm%04x" blob name; at most order_blob_name_limit characters
/// @return false, writing nothing, when the order belongs to another unit, an
///     argument is null, the name is too long or the mission is outside the table
bool save_write_order(
    const World* world,
    const Unit* unit,
    const SavedOrder* order,
    const SavedGoal* goal,
    Bank* bank,
    const char* blob_name
);

/// Reads one saved order of a unit from the open account.
///
/// The mission is found by its saved name, else by its position among the
/// missions without the record's unnumbered bit; the unit type of a build
/// mission is remapped to the current table; the goal of the saved kind is
/// read from "<blob_name>g". A goal whose blob is missing or short is dropped;
/// 3.1c keeps a goal of the saved kind without its saved fields.
///
/// @param world world holding the unit table
/// @param unit unit that owns the order
/// @param[in,out] bank bank with the Units account open
/// @param blob_name "u%04xm%04x" blob name; at most order_blob_name_limit characters
/// @param[out] order order words; zeroed first, filled when the order is accepted
/// @param[out] goal goal; zeroed first, filled when its blob is read whole
/// @return false when an argument is null, the name is too long, the blob is
///     missing or short, the order belongs to another unit or none, or the
///     mission is unknown (3.1c still links an order of an unknown mission,
///     with no unit)
bool save_read_order(
    const World* world,
    const Unit* unit,
    Bank* bank,
    const char* blob_name,
    SavedOrder* order,
    SavedGoal* goal
);

} // namespace oa::data::persist
