// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/unit_state.hpp"
#include "oa/base/game_math.hpp"

#include <cstdint>

namespace oa::netgame {
using base::game_math::truncate_to_int64;

namespace {

constexpr float build_fraction_encode_scale = -254.0f;
constexpr float build_fraction_decode_scale = 0.0039215689f; // 1/255 as a float

bool valid_def_bits(unsigned bits) noexcept {
    return bits >= 1 && bits <= unit_state_max_def_index_bits;
}

} // namespace

uint8_t unit_state_build_byte(float fraction) noexcept {
    if (fraction == 0.0f)
        return 0;
    // The float product is exact in double.
    const auto scaled = truncate_to_int64(
        static_cast<double>(fraction) * static_cast<double>(build_fraction_encode_scale)
    );
    return static_cast<uint8_t>(1u - static_cast<uint32_t>(scaled));
}

float unit_state_build_fraction(uint8_t wire) noexcept {
    return static_cast<float>(
        static_cast<double>(wire) * static_cast<double>(build_fraction_decode_scale)
    );
}

bool unit_state_build_fraction_differs(uint8_t wire, float current) noexcept {
    return static_cast<double>(wire) * static_cast<double>(build_fraction_decode_scale) !=
           static_cast<double>(current);
}

int32_t unit_state_full_record_slot(uint32_t sender_tick, uint16_t units_per_player) noexcept {
    if (units_per_player == 0)
        return 0;
    return static_cast<int32_t>(sender_tick) % static_cast<int32_t>(units_per_player);
}

void write_full_unit_record(
    BitWriter* writer, const FullUnitRecord& r, unsigned def_index_bits
) noexcept {
    if (!valid_def_bits(def_index_bits)) {
        writer->error = WireError::bad_argument;
        return;
    }
    bit_writer_write(writer, r.unit_def_index, def_index_bits);
    if (r.unit_def_index == 0)
        return;
    bit_writer_write(writer, static_cast<uint32_t>(int32_t{r.health}), 16);
    bit_writer_write(writer, r.build_byte, 8);
    bit_writer_write(writer, r.state_flags, 8);
    bit_writer_write(writer, r.occupancy & 3u, 2);
    bit_writer_write_bit(writer, r.attached);
    if (!r.attached) {
        for (auto c : r.position)
            bit_writer_write(writer, static_cast<uint32_t>(c), 32);
        bit_writer_write(writer, r.heading, 16);
        bit_writer_write(writer, r.pitch, 16);
        bit_writer_write(writer, r.bank, 16);
        if (r.has_object_word)
            bit_writer_write(writer, r.object_word, 32);
        return;
    }
    bit_writer_write(writer, r.attached_unit_index, unit_state_carrier_index_bits);
    bit_writer_write(writer, static_cast<uint32_t>(int32_t{r.attach_piece}), 8);
}

WireError read_full_unit_record(
    BitReader* reader, unsigned def_index_bits, bool object_word_present, FullUnitRecord* out
) noexcept {
    if (!valid_def_bits(def_index_bits) || out == nullptr)
        return WireError::bad_argument;
    FullUnitRecord r{};
    r.unit_def_index = static_cast<uint16_t>(bit_reader_read(reader, def_index_bits));
    if (r.unit_def_index != 0) {
        r.health = static_cast<int16_t>(bit_reader_read(reader, 16));
        r.build_byte = static_cast<uint8_t>(bit_reader_read(reader, 8));
        r.state_flags = static_cast<uint8_t>(bit_reader_read(reader, 8));
        r.occupancy = static_cast<uint8_t>(bit_reader_read(reader, 2));
        r.attached = bit_reader_read_bit(reader);
        if (!r.attached) {
            for (auto& c : r.position)
                c = static_cast<int32_t>(bit_reader_read(reader, 32));
            r.heading = static_cast<uint16_t>(bit_reader_read(reader, 16));
            r.pitch = static_cast<uint16_t>(bit_reader_read(reader, 16));
            r.bank = static_cast<uint16_t>(bit_reader_read(reader, 16));
            r.has_object_word = object_word_present;
            if (object_word_present)
                r.object_word = bit_reader_read(reader, 32);
        } else {
            r.attached_unit_index =
                static_cast<uint16_t>(bit_reader_read(reader, unit_state_carrier_index_bits));
            r.attach_piece = static_cast<int8_t>(bit_reader_read_signed(reader, 8));
        }
    }
    if (bit_reader_overrun(reader))
        return WireError::truncated;
    *out = r;
    return WireError::ok;
}

void write_waypoint_delta(BitWriter* writer, const WaypointDelta& delta) noexcept {
    const uint8_t count = delta.count > 3 ? 3 : delta.count;
    bit_writer_write_bit(writer, delta.flag);
    bit_writer_write(writer, count, 2);
    for (uint8_t i = 0; i < count; ++i) {
        bit_writer_write(writer, static_cast<uint32_t>(int32_t{delta.points[i][0]}), 16);
        bit_writer_write(writer, static_cast<uint32_t>(int32_t{delta.points[i][1]}), 16);
    }
}

void read_waypoint_delta(BitReader* reader, WaypointDelta* out) noexcept {
    WaypointDelta delta{};
    delta.flag = bit_reader_read_bit(reader);
    delta.count = static_cast<uint8_t>(bit_reader_read(reader, 2));
    for (uint8_t i = 0; i < delta.count; ++i) {
        delta.points[i][0] = static_cast<int16_t>(bit_reader_read(reader, 16));
        delta.points[i][1] = static_cast<int16_t>(bit_reader_read(reader, 16));
    }
    *out = delta;
}

WireError decode_unit_state(
    const uint8_t* bytes,
    std::size_t size,
    const UnitStateDecodeOptions& options,
    UnitStateBody* out
) noexcept {
    if (bytes == nullptr || out == nullptr || !valid_def_bits(options.def_index_bits))
        return WireError::bad_argument;
    if (size < unit_state_header_bytes)
        return WireError::truncated;
    if (bytes[0] != static_cast<uint8_t>(RecordType::unit_state))
        return WireError::invalid_type;
    const auto length = load_u16(bytes + 1);
    if (length < unit_state_header_bytes || length > size)
        return WireError::length_mismatch;

    BitReader reader;
    bit_reader_init(&reader, bytes, length);
    out->entry_count = 0;
    out->has_full_record = false;
    (void)bit_reader_read(&reader, 8);
    out->length = static_cast<uint16_t>(bit_reader_read(&reader, 16));
    out->sender_tick = bit_reader_read(&reader, 32);

    auto index = static_cast<uint16_t>(bit_reader_read(&reader, unit_state_unit_index_bits));
    while (index != unit_state_list_terminator) {
        if (out->entry_count == unit_state_max_entries)
            return WireError::overflow;
        auto& entry = out->entries[out->entry_count++];
        entry.unit_index = index;
        entry.def_index = static_cast<uint16_t>(bit_reader_read(&reader, options.def_index_bits));
        entry.delta_bit_offset = static_cast<uint32_t>(bit_reader_bits_consumed(&reader));
        entry.delta_bit_count = 0;
        if (bit_reader_overrun(&reader))
            return WireError::truncated;
        const auto* codec = options.delta_codec;
        if (codec == nullptr || codec->read == nullptr)
            return WireError::unsupported_delta_layout;
        const auto error = codec->read(codec->context, entry.unit_index, entry.def_index, &reader);
        if (error != WireError::ok)
            return error;
        if (bit_reader_overrun(&reader))
            return WireError::truncated;
        entry.delta_bit_count =
            static_cast<uint32_t>(bit_reader_bits_consumed(&reader)) - entry.delta_bit_offset;
        index = static_cast<uint16_t>(bit_reader_read(&reader, unit_state_unit_index_bits));
    }
    if (bit_reader_read_bit(&reader)) {
        const auto error = read_full_unit_record(
            &reader, options.def_index_bits, options.full_record_object_word_present, &out->full
        );
        if (error != WireError::ok)
            return error;
        out->has_full_record = true;
    }
    if (bit_reader_overrun(&reader))
        return WireError::truncated;
    out->bits_consumed = static_cast<uint32_t>(bit_reader_bits_consumed(&reader));
    return WireError::ok;
}

void unit_state_begin(BitWriter* writer, uint32_t sender_tick) noexcept {
    bit_writer_write(writer, static_cast<uint8_t>(RecordType::unit_state), 8);
    bit_writer_write(writer, 0, 16);
    bit_writer_write(writer, sender_tick, 32);
}

void unit_state_write_entry_header(
    BitWriter* writer, uint16_t unit_index, uint16_t def_index, unsigned def_index_bits
) noexcept {
    if (!valid_def_bits(def_index_bits)) {
        writer->error = WireError::bad_argument;
        return;
    }
    bit_writer_write(writer, unit_index, unit_state_unit_index_bits);
    bit_writer_write(writer, def_index, def_index_bits);
}

bool unit_state_full(const BitWriter* writer) noexcept {
    return bit_writer_byte_length(writer) >= unit_state_soft_limit_bytes;
}

WireError unit_state_finish(
    BitWriter* writer, const FullUnitRecord& full, unsigned def_index_bits, uint16_t* length
) noexcept {
    bit_writer_write(writer, 0xffffffffu, unit_state_unit_index_bits);
    bit_writer_write_bit(writer, true);
    write_full_unit_record(writer, full, def_index_bits);
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
