// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/persist/save_orders.hpp"

#include "bank_util.hpp"

#include "oa/core/unit_def.h"
#include "oa/data/defs/unit_records.hpp"
#include "oa/data/mission_types.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace oa::data::persist {
namespace {

using detail::load_le16;
using detail::load_le32;
using detail::store_le16;
using detail::store_le32;

// Room for "%s%s" of a blob name and a suffix: 127 characters, as in 3.1c.
constexpr std::size_t key_bytes = 0x80;

void suffixed(char (&out)[key_bytes], const char* blob_name, const char* suffix) {
    std::snprintf(out, sizeof(out), "%s%s", blob_name, suffix);
}

void type_name_key(char (&out)[key_bytes], uint16_t type) {
    std::snprintf(out, sizeof(out), order_key::type_name_format, static_cast<int>(type));
}

void write_blob(Bank* bank, const char* blob_name, const uint8_t* bytes, uint32_t size) {
    bank_open_blob_name(bank, blob_name);
    bank_blob_seek(bank, 0);
    bank_blob_write(bank, bytes, size);
}

bool read_blob(Bank* bank, const char* blob_name, uint8_t* bytes, uint32_t size) {
    bank_open_blob_name(bank, blob_name);
    bank_blob_seek(bank, 0);
    return bank_blob_read(bank, bytes, size) == size;
}

void store_point(uint8_t* out, const int32_t (&point)[3]) {
    for (std::size_t axis = 0; axis < 3; ++axis)
        store_le32(out + axis * 4, static_cast<uint32_t>(point[axis]));
}

void load_point(const uint8_t* in, int32_t (&point)[3]) {
    for (std::size_t axis = 0; axis < 3; ++axis)
        point[axis] = static_cast<int32_t>(load_le32(in + axis * 4));
}

// UnitDef.flags bit the saved-type lookup skips when counting types by position.
constexpr uint32_t unnumbered_type_flag = OA_UNIT_DEF_FLAG_DOWNLOADABLE;

// The order kind at a saved position among the orders without the unnumbered
// flag, or 0 for a position past them.
uint8_t mission_at_numbered_position(uint8_t position) {
    const auto count = data::mission_types::registered_names().size();
    uint32_t numbered = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if ((data::mission_types::mission_flags(static_cast<uint8_t>(index)) &
             data::mission_types::unnumbered_order) != 0)
            continue;
        if (numbered == position)
            return static_cast<uint8_t>(index);
        ++numbered;
    }
    return data::mission_types::unknown_mission;
}

/// Encodes a circle goal as its saved blob; bytes 0..3 stay zero.
///
/// @param goal goal to encode
/// @param[out] blob circle_goal_blob_bytes bytes of blob
void encode_goal(const SavedCircleGoal& goal, uint8_t* blob) {
    std::memset(blob, 0, circle_goal_blob_bytes);
    store_le16(blob + 0x4, static_cast<uint16_t>(goal.cell[0]));
    store_le16(blob + 0x6, static_cast<uint16_t>(goal.cell[1]));
    store_le32(blob + 0x8, static_cast<uint32_t>(goal.tolerance));
    store_le32(blob + 0xc, static_cast<uint32_t>(goal.radius_squared));
}

/// Encodes a ring goal as its saved blob; bytes 0..3 stay zero.
///
/// @param goal goal to encode
/// @param[out] blob ring_goal_blob_bytes bytes of blob
void encode_goal(const SavedRingGoal& goal, uint8_t* blob) {
    std::memset(blob, 0, ring_goal_blob_bytes);
    store_le16(blob + 0x4, static_cast<uint16_t>(goal.cell[0]));
    store_le16(blob + 0x6, static_cast<uint16_t>(goal.cell[1]));
    store_le32(blob + 0x8, static_cast<uint32_t>(goal.inner_range));
    store_le32(blob + 0xc, static_cast<uint32_t>(goal.outer_range));
    store_le32(blob + 0x10, static_cast<uint32_t>(goal.inner_radius_squared));
    store_le32(blob + 0x14, static_cast<uint32_t>(goal.outer_radius_squared));
}

/// Encodes an outline goal as its saved blob; bytes 0..3 stay zero.
///
/// @param goal goal to encode
/// @param[out] blob outline_goal_blob_bytes bytes of blob
void encode_goal(const SavedOutlineGoal& goal, uint8_t* blob) {
    std::memset(blob, 0, outline_goal_blob_bytes);
    store_le32(blob + 0x4, static_cast<uint32_t>(goal.left));
    store_le32(blob + 0x8, static_cast<uint32_t>(goal.right));
    store_le32(blob + 0xc, static_cast<uint32_t>(goal.top));
    store_le32(blob + 0x10, static_cast<uint32_t>(goal.bottom));
}

/// Encodes an air target goal as its saved blob; bytes 0..7 and 10..25 stay zero.
///
/// @param goal goal to encode
/// @param[out] blob air_target_goal_blob_bytes bytes of blob
void encode_goal(const SavedAirTargetGoal& goal, uint8_t* blob) {
    std::memset(blob, 0, air_target_goal_blob_bytes);
    store_le16(blob + 0x08, goal.unit_id);
    store_le16(blob + 0x1a, goal.target_id);
    store_le16(blob + 0x1c, goal.flags);
    store_le16(blob + 0x1e, static_cast<uint16_t>(goal.arrival_radius));
    store_le16(blob + 0x20, static_cast<uint16_t>(goal.altitude));
    store_le16(blob + 0x22, goal.bearing);
    store_le16(blob + 0x24, static_cast<uint16_t>(goal.query_point));
    store_point(blob + 0x26, goal.point);
    store_le32(blob + 0x32, static_cast<uint32_t>(goal.stand_off));
}

/// Encodes an air seek goal as its saved blob; bytes 0..7 stay zero.
///
/// @param goal goal to encode
/// @param[out] blob air_seek_goal_blob_bytes bytes of blob
void encode_goal(const SavedAirSeekGoal& goal, uint8_t* blob) {
    std::memset(blob, 0, air_seek_goal_blob_bytes);
    store_le16(blob + 0x08, goal.unit_id);
    store_le16(blob + 0x0a, goal.flags);
    store_point(blob + 0x0c, goal.point);
    store_point(blob + 0x18, goal.step);
    store_le16(blob + 0x24, goal.reserved_after_step);
    store_le16(blob + 0x26, goal.heading);
    store_le16(blob + 0x28, goal.reserved_after_heading);
}

} // namespace

void save_encode_order(const SavedOrder* order, uint8_t* blob) {
    namespace b = order_blob;
    std::memset(blob, 0, order_blob_bytes);
    store_le16(blob + b::owner_id, order->owner_id);
    store_le16(blob + b::target_id, order->target_id);
    store_le32(blob + b::goal_kind, static_cast<uint32_t>(order->goal_kind));
    blob[b::kind] = order->kind;
    blob[b::phase] = order->phase;
    store_le32(blob + b::wait_events, order->wait_events);
    store_le32(blob + b::wake_tick, order->wake_tick);
    store_point(blob + b::point, order->point);
    store_le16(blob + b::anchor, static_cast<uint16_t>(order->anchor[0]));
    store_le16(blob + b::anchor + 2, static_cast<uint16_t>(order->anchor[1]));
    store_le32(blob + b::seen_cell, order->seen_cell);
    store_le32(blob + b::parameter_1, static_cast<uint32_t>(order->parameter_1));
    store_le32(blob + b::parameter_2, static_cast<uint32_t>(order->parameter_2));
    store_le32(blob + b::parameter_3, static_cast<uint32_t>(order->parameter_3));
    blob[b::preserve_flags] = order->preserve_flags;
    blob[b::command_flags] = order->command_flags;
    blob[b::flags] = order->flags;
    blob[b::mission_byte] = order->mission_byte;
    store_le32(blob + b::raised_events, order->raised_events);
}

void save_decode_order(const uint8_t* blob, SavedOrder* order) {
    namespace b = order_blob;
    order->owner_id = load_le16(blob + b::owner_id);
    order->target_id = load_le16(blob + b::target_id);
    order->goal_kind = static_cast<int32_t>(load_le32(blob + b::goal_kind));
    order->kind = blob[b::kind];
    order->phase = blob[b::phase];
    order->wait_events = load_le32(blob + b::wait_events);
    order->wake_tick = load_le32(blob + b::wake_tick);
    load_point(blob + b::point, order->point);
    order->anchor[0] = static_cast<int16_t>(load_le16(blob + b::anchor));
    order->anchor[1] = static_cast<int16_t>(load_le16(blob + b::anchor + 2));
    order->seen_cell = load_le32(blob + b::seen_cell);
    order->parameter_1 = static_cast<int32_t>(load_le32(blob + b::parameter_1));
    order->parameter_2 = static_cast<int32_t>(load_le32(blob + b::parameter_2));
    order->parameter_3 = static_cast<int32_t>(load_le32(blob + b::parameter_3));
    order->preserve_flags = blob[b::preserve_flags];
    order->command_flags = blob[b::command_flags];
    order->flags = blob[b::flags];
    order->mission_byte = blob[b::mission_byte];
    order->raised_events = load_le32(blob + b::raised_events);
}

uint32_t save_encode_goal(const SavedGoal* goal, uint8_t* blob) {
    switch (static_cast<SavedGoalKind>(goal->kind)) {
    case SavedGoalKind::circle:
        encode_goal(goal->circle, blob);
        return circle_goal_blob_bytes;
    case SavedGoalKind::ring:
        encode_goal(goal->ring, blob);
        return ring_goal_blob_bytes;
    case SavedGoalKind::outline:
        encode_goal(goal->outline, blob);
        return outline_goal_blob_bytes;
    case SavedGoalKind::air_target:
        encode_goal(goal->air_target, blob);
        return air_target_goal_blob_bytes;
    case SavedGoalKind::air_seek:
        encode_goal(goal->air_seek, blob);
        return air_seek_goal_blob_bytes;
    case SavedGoalKind::none:
        break;
    }
    return 0;
}

void save_write_circle_goal(const SavedCircleGoal* goal, Bank* bank, const char* blob_name) {
    uint8_t blob[circle_goal_blob_bytes];
    encode_goal(*goal, blob);
    write_blob(bank, blob_name, blob, sizeof(blob));
}

bool save_read_circle_goal(Bank* bank, const char* blob_name, SavedCircleGoal* goal) {
    uint8_t blob[circle_goal_blob_bytes];
    if (bank == nullptr || blob_name == nullptr || !read_blob(bank, blob_name, blob, sizeof(blob)))
        return false;
    goal->cell[0] = static_cast<int16_t>(load_le16(blob + 0x4));
    goal->cell[1] = static_cast<int16_t>(load_le16(blob + 0x6));
    goal->tolerance = static_cast<int32_t>(load_le32(blob + 0x8));
    goal->radius_squared = static_cast<int32_t>(load_le32(blob + 0xc));
    return true;
}

void save_write_ring_goal(const SavedRingGoal* goal, Bank* bank, const char* blob_name) {
    uint8_t blob[ring_goal_blob_bytes];
    encode_goal(*goal, blob);
    write_blob(bank, blob_name, blob, sizeof(blob));
}

bool save_read_ring_goal(Bank* bank, const char* blob_name, SavedRingGoal* goal) {
    uint8_t blob[ring_goal_blob_bytes];
    if (bank == nullptr || blob_name == nullptr || !read_blob(bank, blob_name, blob, sizeof(blob)))
        return false;
    goal->cell[0] = static_cast<int16_t>(load_le16(blob + 0x4));
    goal->cell[1] = static_cast<int16_t>(load_le16(blob + 0x6));
    goal->inner_range = static_cast<int32_t>(load_le32(blob + 0x8));
    goal->outer_range = static_cast<int32_t>(load_le32(blob + 0xc));
    goal->inner_radius_squared = static_cast<int32_t>(load_le32(blob + 0x10));
    goal->outer_radius_squared = static_cast<int32_t>(load_le32(blob + 0x14));
    return true;
}

void save_write_outline_goal(const SavedOutlineGoal* goal, Bank* bank, const char* blob_name) {
    uint8_t blob[outline_goal_blob_bytes];
    encode_goal(*goal, blob);
    write_blob(bank, blob_name, blob, sizeof(blob));
}

bool save_read_outline_goal(Bank* bank, const char* blob_name, SavedOutlineGoal* goal) {
    uint8_t blob[outline_goal_blob_bytes];
    if (bank == nullptr || blob_name == nullptr || !read_blob(bank, blob_name, blob, sizeof(blob)))
        return false;
    goal->left = static_cast<int32_t>(load_le32(blob + 0x4));
    goal->right = static_cast<int32_t>(load_le32(blob + 0x8));
    goal->top = static_cast<int32_t>(load_le32(blob + 0xc));
    goal->bottom = static_cast<int32_t>(load_le32(blob + 0x10));
    return true;
}

void save_write_air_target_goal(const SavedAirTargetGoal* goal, Bank* bank, const char* blob_name) {
    uint8_t blob[air_target_goal_blob_bytes];
    encode_goal(*goal, blob);
    write_blob(bank, blob_name, blob, sizeof(blob));
}

bool save_read_air_target_goal(Bank* bank, const char* blob_name, SavedAirTargetGoal* goal) {
    uint8_t blob[air_target_goal_blob_bytes];
    if (bank == nullptr || blob_name == nullptr || !read_blob(bank, blob_name, blob, sizeof(blob)))
        return false;
    goal->unit_id = load_le16(blob + 0x08);
    goal->target_id = load_le16(blob + 0x1a);
    goal->flags = load_le16(blob + 0x1c);
    goal->arrival_radius = static_cast<int16_t>(load_le16(blob + 0x1e));
    goal->altitude = static_cast<int16_t>(load_le16(blob + 0x20));
    goal->bearing = load_le16(blob + 0x22);
    goal->query_point = static_cast<int16_t>(load_le16(blob + 0x24));
    load_point(blob + 0x26, goal->point);
    goal->stand_off = static_cast<int32_t>(load_le32(blob + 0x32));
    return true;
}

void save_write_air_seek_goal(const SavedAirSeekGoal* goal, Bank* bank, const char* blob_name) {
    uint8_t blob[air_seek_goal_blob_bytes];
    encode_goal(*goal, blob);
    write_blob(bank, blob_name, blob, sizeof(blob));
}

bool save_read_air_seek_goal(Bank* bank, const char* blob_name, SavedAirSeekGoal* goal) {
    uint8_t blob[air_seek_goal_blob_bytes];
    if (bank == nullptr || blob_name == nullptr || !read_blob(bank, blob_name, blob, sizeof(blob)))
        return false;
    goal->unit_id = load_le16(blob + 0x08);
    goal->flags = load_le16(blob + 0x0a);
    load_point(blob + 0x0c, goal->point);
    load_point(blob + 0x18, goal->step);
    goal->reserved_after_step = load_le16(blob + 0x24);
    goal->heading = load_le16(blob + 0x26);
    goal->reserved_after_heading = load_le16(blob + 0x28);
    return true;
}

bool save_order_names_type(uint8_t kind) {
    const auto names = data::mission_types::registered_names();
    if (kind >= names.size())
        return false;
    const std::string_view name = names[kind];
    return name == "MobileBuild" || name == "VTOL_MobileBuild" || name == "BuildingBuild";
}

void save_write_order_type_name(const World* world, Bank* bank, uint16_t type) {
    char key[key_bytes];
    type_name_key(key, type);
    if (bank_has_field(bank, key) || type == 0 || type >= world->unit_def_count)
        return;
    char name[sizeof(world->unit_defs[type].unit_name) + 1] = {};
    std::memcpy(name, world->unit_defs[type].unit_name, sizeof(world->unit_defs[type].unit_name));
    bank_set_text(bank, key, name);
}

uint16_t save_resolve_order_type(const World* world, Bank* bank, uint16_t saved) {
    char key[key_bytes];
    type_name_key(key, saved);
    if (bank_has_field(bank, key)) {
        const char* name = bank_get_text(bank, key, nullptr);
        return name != nullptr
                   ? data::defs::unit_defs_type_id(world->unit_defs, world->unit_def_count, name)
                   : 0;
    }
    uint16_t numbered = 0;
    for (uint32_t type = 1; type < world->unit_def_count; ++type) {
        if ((world->unit_defs[type].flags & unnumbered_type_flag) != 0)
            continue;
        if (numbered == saved)
            return static_cast<uint16_t>(type - 1);
        ++numbered;
    }
    return 0;
}

bool save_write_order(
    const World* world,
    const Unit* unit,
    const SavedOrder* order,
    const SavedGoal* goal,
    Bank* bank,
    const char* blob_name
) {
    const auto names = data::mission_types::registered_names();
    if (order->owner_id != unit->id || bank == nullptr || blob_name == nullptr ||
        std::strlen(blob_name) > order_blob_name_limit || order->kind >= names.size())
        return false;
    uint8_t blob[order_blob_bytes];
    save_encode_order(order, blob);
    write_blob(bank, blob_name, blob, sizeof(blob));
    char key[key_bytes];
    suffixed(key, blob_name, order_key::name_suffix);
    const std::string_view mission = names[order->kind];
    char mission_name[key_bytes] = {};
    std::memcpy(
        mission_name, mission.data(), mission.size() < key_bytes ? mission.size() : key_bytes - 1
    );
    bank_set_text(bank, key, mission_name);
    if (save_order_names_type(order->kind))
        save_write_order_type_name(world, bank, static_cast<uint16_t>(order->parameter_1));
    if (order->goal_kind == 0)
        return true;
    suffixed(key, blob_name, order_key::goal_suffix);
    switch (static_cast<SavedGoalKind>(order->goal_kind)) {
    case SavedGoalKind::circle:
        save_write_circle_goal(&goal->circle, bank, key);
        break;
    case SavedGoalKind::ring:
        save_write_ring_goal(&goal->ring, bank, key);
        break;
    case SavedGoalKind::outline:
        save_write_outline_goal(&goal->outline, bank, key);
        break;
    case SavedGoalKind::air_target:
        save_write_air_target_goal(&goal->air_target, bank, key);
        break;
    case SavedGoalKind::air_seek:
        save_write_air_seek_goal(&goal->air_seek, bank, key);
        break;
    case SavedGoalKind::none:
        break;
    }
    return true;
}

bool save_read_order(
    const World* world,
    const Unit* unit,
    Bank* bank,
    const char* blob_name,
    SavedOrder* order,
    SavedGoal* goal
) {
    *order = {};
    *goal = {};
    if (bank == nullptr || blob_name == nullptr || std::strlen(blob_name) > order_blob_name_limit)
        return false;
    uint8_t blob[order_blob_bytes];
    if (!read_blob(bank, blob_name, blob, sizeof(blob)))
        return false;
    SavedOrder saved;
    save_decode_order(blob, &saved);
    char key[key_bytes];
    suffixed(key, blob_name, order_key::name_suffix);
    const char* mission = bank_get_text(bank, key, nullptr);
    saved.kind = mission != nullptr ? data::mission_types::index_for_name(mission)
                                    : mission_at_numbered_position(saved.kind);
    if (saved.owner_id == 0 || saved.owner_id != unit->id ||
        saved.kind == data::mission_types::unknown_mission)
        return false;
    if (save_order_names_type(saved.kind))
        saved.parameter_1 =
            save_resolve_order_type(world, bank, static_cast<uint16_t>(saved.parameter_1));
    *order = saved;
    suffixed(key, blob_name, order_key::goal_suffix);
    bool read = false;
    switch (static_cast<SavedGoalKind>(saved.goal_kind)) {
    case SavedGoalKind::circle:
        read = save_read_circle_goal(bank, key, &goal->circle);
        break;
    case SavedGoalKind::ring:
        read = save_read_ring_goal(bank, key, &goal->ring);
        break;
    case SavedGoalKind::outline:
        read = save_read_outline_goal(bank, key, &goal->outline);
        break;
    case SavedGoalKind::air_target:
        read = save_read_air_target_goal(bank, key, &goal->air_target);
        break;
    case SavedGoalKind::air_seek:
        read = save_read_air_seek_goal(bank, key, &goal->air_seek);
        break;
    case SavedGoalKind::none:
        break;
    }
    if (read)
        goal->kind = saved.goal_kind;
    else
        *goal = {};
    return true;
}

} // namespace oa::data::persist
