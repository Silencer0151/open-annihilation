// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/replication/deltas.hpp"

#include <cstdint>

namespace oa::netgame {
namespace {

uint32_t s16_bits(int16_t value) noexcept {
    return static_cast<uint32_t>(int32_t{value});
}

/// Writes a target goal: the flag byte, then each optional field its flags select, in wire order.
///
/// @param[in,out] writer Writer to append to.
/// @param goal Goal to write.
void write_target_goal(BitWriter* writer, const AirTargetGoal& goal) noexcept {
    bit_writer_write(writer, goal.flags, 8);
    if (goal.flags & air_target_has_unit) {
        bit_writer_write(writer, s16_bits(goal.query_point), 16);
        bit_writer_write(writer, goal.target_unit, 16);
    }
    if (goal.flags & air_target_has_arrival_radius)
        bit_writer_write(writer, s16_bits(goal.arrival_radius), 16);
    if (goal.flags & air_target_has_altitude)
        bit_writer_write(writer, s16_bits(goal.altitude), 16);
    if (goal.flags & air_target_has_bearing)
        bit_writer_write(writer, s16_bits(goal.bearing), 16);
    if (goal.flags & air_target_has_position)
        for (auto c : goal.position)
            bit_writer_write(writer, static_cast<uint32_t>(c), 32);
}

/// Reads a target goal written by write_target_goal.
///
/// Every field its flags leave out is zero.
///
/// @param[in,out] reader Reader to advance.
/// @param[out] goal Decoded goal; fields its flags leave out are zero.
void read_target_goal(BitReader* reader, AirTargetGoal* goal) noexcept {
    *goal = AirTargetGoal{};
    goal->flags = static_cast<uint8_t>(bit_reader_read(reader, 8));
    if (goal->flags & air_target_has_unit) {
        goal->query_point = static_cast<int16_t>(bit_reader_read(reader, 16));
        goal->target_unit = static_cast<uint16_t>(bit_reader_read(reader, 16));
    }
    if (goal->flags & air_target_has_arrival_radius)
        goal->arrival_radius = static_cast<int16_t>(bit_reader_read(reader, 16));
    if (goal->flags & air_target_has_altitude)
        goal->altitude = static_cast<int16_t>(bit_reader_read(reader, 16));
    if (goal->flags & air_target_has_bearing)
        goal->bearing = static_cast<int16_t>(bit_reader_read(reader, 16));
    if (goal->flags & air_target_has_position)
        for (auto& c : goal->position)
            c = static_cast<int32_t>(bit_reader_read(reader, 32));
}

/// Writes a VTOL seek goal: the flag bit, point and step (three 32-bit coordinates each), then the
/// heading when the flag is set.
///
/// @param[in,out] writer Writer to append to.
/// @param goal Goal to write.
void write_seek_goal(BitWriter* writer, const AirSeekGoal& goal) noexcept {
    bit_writer_write_bit(writer, goal.flag);
    for (auto c : goal.point)
        bit_writer_write(writer, static_cast<uint32_t>(c), 32);
    for (auto c : goal.step)
        bit_writer_write(writer, static_cast<uint32_t>(c), 32);
    if (goal.flag)
        bit_writer_write(writer, goal.heading, 16);
}

/// Reads a VTOL seek goal written by write_seek_goal.
///
/// An unflagged heading is zero.
///
/// @param[in,out] reader Reader to advance.
/// @param[out] goal Decoded goal; the heading is zero when the flag is clear.
void read_seek_goal(BitReader* reader, AirSeekGoal* goal) noexcept {
    *goal = AirSeekGoal{};
    goal->flag = bit_reader_read_bit(reader);
    for (auto& c : goal->point)
        c = static_cast<int32_t>(bit_reader_read(reader, 32));
    for (auto& c : goal->step)
        c = static_cast<int32_t>(bit_reader_read(reader, 32));
    if (goal->flag)
        goal->heading = static_cast<uint16_t>(bit_reader_read(reader, 16));
}

WireError codec_read(void* context, uint16_t, uint16_t def_index, BitReader* reader) {
    auto* state = static_cast<DeltaCodecState*>(context);
    if (def_index >= state->classes.count || state->classes.by_def_index == nullptr)
        return WireError::unsupported_delta_layout;
    const auto movement = state->classes.by_def_index[def_index];
    if (movement == MovementClass::none)
        return WireError::unsupported_delta_layout;
    UnitDelta scratch{};
    UnitDelta* out = &scratch;
    if (state->deltas != nullptr) {
        if (state->count >= state->capacity)
            return WireError::overflow;
        out = &state->deltas[state->count];
    }
    const auto error = read_unit_delta(reader, movement, out);
    if (error == WireError::ok)
        ++state->count;
    return error;
}

WireError codec_write(void* context, uint16_t, uint16_t, BitWriter* writer) {
    auto* state = static_cast<DeltaCodecState*>(context);
    if (state->source == nullptr || state->source_next >= state->source_count)
        return WireError::bad_argument;
    write_unit_delta(writer, state->source[state->source_next++]);
    return writer->error;
}

} // namespace

void write_air_delta(BitWriter* writer, const AirDelta& delta) noexcept {
    if (!delta.goal_tag_omitted) {
        bit_writer_write(writer, delta.goal_tag, air_goal_tag_bits);
        if (delta.goal_tag == air_goal_tag_target)
            write_target_goal(writer, delta.target);
        else if (delta.goal_tag == air_goal_tag_seek)
            write_seek_goal(writer, delta.seek);
    }
    bit_writer_write(writer, delta.substate & 3u, air_substate_bits);
}

void read_air_delta(BitReader* reader, AirDelta* out) noexcept {
    AirDelta delta{};
    delta.goal_tag = static_cast<uint8_t>(bit_reader_read(reader, air_goal_tag_bits));
    if (delta.goal_tag == air_goal_tag_target)
        read_target_goal(reader, &delta.target);
    else if (delta.goal_tag == air_goal_tag_seek)
        read_seek_goal(reader, &delta.seek);
    delta.substate = static_cast<uint8_t>(bit_reader_read(reader, air_substate_bits));
    *out = delta;
}

void write_unit_delta(BitWriter* writer, const UnitDelta& delta) noexcept {
    switch (delta.movement) {
    case MovementClass::ground:
        write_waypoint_delta(writer, delta.ground);
        break;
    case MovementClass::air:
        write_air_delta(writer, delta.air);
        break;
    case MovementClass::none:
        break;
    }
}

WireError read_unit_delta(BitReader* reader, MovementClass movement, UnitDelta* out) noexcept {
    if (out == nullptr)
        return WireError::bad_argument;
    *out = UnitDelta{};
    out->movement = movement;
    switch (movement) {
    case MovementClass::ground:
        read_waypoint_delta(reader, &out->ground);
        break;
    case MovementClass::air:
        read_air_delta(reader, &out->air);
        break;
    case MovementClass::none:
        break;
    }
    return bit_reader_overrun(reader) ? WireError::truncated : WireError::ok;
}

MovementClass movement_class_for_def(const UnitDef& def) noexcept {
    if (def.bm_code != unit_def_bm_code_mobile)
        return MovementClass::none;
    return (def.flags & OA_UNIT_DEF_FLAG_CAN_FLY) ? MovementClass::air : MovementClass::ground;
}

UnitDeltaCodec delta_codec(DeltaCodecState* state) noexcept {
    UnitDeltaCodec codec;
    codec.context = state;
    codec.read = codec_read;
    codec.write = codec_write;
    return codec;
}

WireError encode_unit_state(
    BitWriter* writer,
    const UnitStateBody& body,
    const UnitDelta* deltas,
    unsigned def_index_bits,
    uint16_t* length
) noexcept {
    if (writer == nullptr || (body.entry_count != 0 && deltas == nullptr))
        return WireError::bad_argument;
    unit_state_begin(writer, body.sender_tick);
    for (uint16_t i = 0; i < body.entry_count; ++i) {
        const auto& entry = body.entries[i];
        unit_state_write_entry_header(writer, entry.unit_index, entry.def_index, def_index_bits);
        write_unit_delta(writer, deltas[i]);
    }
    if (body.has_full_record)
        return unit_state_finish(writer, body.full, def_index_bits, length);
    bit_writer_write(writer, unit_state_list_terminator, unit_state_unit_index_bits);
    bit_writer_write_bit(writer, false);
    const auto bytes = bit_writer_byte_length(writer);
    if (writer->error != WireError::ok)
        return writer->error;
    if (bytes > 0xffff)
        return WireError::overflow;
    bit_writer_patch_byte(writer, 1, static_cast<uint8_t>(bytes));
    bit_writer_patch_byte(writer, 2, static_cast<uint8_t>(bytes >> 8));
    if (length)
        *length = static_cast<uint16_t>(bytes);
    return writer->error;
}

} // namespace oa::netgame
