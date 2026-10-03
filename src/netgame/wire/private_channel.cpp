// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/private_channel.hpp"
#include "oa/netgame/recorder_messages.hpp"

#include <cstring>

namespace oa::netgame {

namespace {

constexpr std::size_t sub_id_offset = 2;
constexpr std::size_t length_word_offset = 3;
constexpr std::size_t op_offset = 5;

// A 0xfd recorded state is its 0x2c's length less the four tick bytes.
constexpr uint16_t recorded_state_tick_bytes = 4;

} // namespace

WireError
decode_private_message(const uint8_t* record, std::size_t size, PrivateMessage* out) noexcept {
    if (out == nullptr || !is_private_chat(record, size))
        return WireError::bad_argument;
    if (size < private_record_bytes)
        return WireError::truncated;
    if (load_u16(record + length_word_offset) != private_length_word)
        return WireError::length_mismatch;
    out->sub_id = record[sub_id_offset];
    out->op = record[op_offset];
    std::memcpy(out->payload, record + private_header_bytes, private_payload_bytes);
    return WireError::ok;
}

WireError encode_private_message(
    const PrivateMessage& message, uint8_t* out, std::size_t capacity, std::size_t* written
) noexcept {
    if (out == nullptr || written == nullptr)
        return WireError::bad_argument;
    if (capacity < private_record_bytes)
        return WireError::buffer_too_small;
    out[0] = static_cast<uint8_t>(RecordType::chat);
    out[1] = 0;
    out[sub_id_offset] = message.sub_id;
    store_u16(out + length_word_offset, private_length_word);
    out[op_offset] = message.op;
    std::memcpy(out + private_header_bytes, message.payload, private_payload_bytes);
    *written = private_record_bytes;
    return WireError::ok;
}

bool private_message_accepted(PrivateChannel channel, const PrivateMessage& message) noexcept {
    switch (channel) {
    case PrivateChannel::sub_id_dispatch:
        return message.sub_id == private_sub_integrity || message.sub_id == private_sub_vote;
    case PrivateChannel::integrity_only:
        return message.sub_id == private_sub_integrity &&
               ((message.op >= integrity_op_challenge && message.op <= integrity_op_last_report) ||
                message.op == integrity_op_module_reply || message.op == integrity_op_data_reply);
    case PrivateChannel::none:
        break;
    }
    return false;
}

WireError
recorder_record_length(const uint8_t* bytes, std::size_t available, uint16_t* length) noexcept {
    if (bytes == nullptr || length == nullptr)
        return WireError::bad_argument;
    *length = 0;
    if (available == 0)
        return WireError::truncated;
    switch (static_cast<RecorderRecordType>(bytes[0])) {
    case RecorderRecordType::spare:
    case RecorderRecordType::replayer:
    case RecorderRecordType::recorded_empty:
        *length = 1;
        return WireError::ok;
    case RecorderRecordType::ally_chat:
        *length = recorder_ally_chat_bytes;
        return WireError::ok;
    case RecorderRecordType::message:
        if (available < 2)
            return WireError::truncated;
        *length = static_cast<uint16_t>(recorder_message_header_bytes + bytes[1]);
        return WireError::ok;
    case RecorderRecordType::camera:
    case RecorderRecordType::recorded_tick:
        *length = recorder_camera_bytes;
        return WireError::ok;
    case RecorderRecordType::recorded_state: {
        if (available < 3)
            return WireError::truncated;
        const auto word = load_u16(bytes + 1);
        if (word <= recorded_state_tick_bytes)
            return WireError::zero_length_record;
        *length = static_cast<uint16_t>(word - recorded_state_tick_bytes);
        return WireError::ok;
    }
    }
    return WireError::invalid_type;
}

WireError encode_recorder_message(
    RecorderMessageKind kind,
    const uint8_t* payload,
    std::size_t size,
    uint8_t* out,
    std::size_t capacity,
    std::size_t* written
) noexcept {
    if (out == nullptr || written == nullptr || (payload == nullptr && size != 0) ||
        size > recorder_message_max_payload)
        return WireError::bad_argument;
    if (capacity < recorder_message_header_bytes + size)
        return WireError::buffer_too_small;
    out[0] = static_cast<uint8_t>(RecorderRecordType::message);
    out[1] = static_cast<uint8_t>(size);
    out[2] = static_cast<uint8_t>(kind);
    if (size != 0)
        std::memcpy(out + recorder_message_header_bytes, payload, size);
    *written = recorder_message_header_bytes + size;
    return WireError::ok;
}

WireError
decode_recorder_message(const uint8_t* record, std::size_t size, RecorderMessage* out) noexcept {
    if (record == nullptr || out == nullptr || size == 0 ||
        record[0] != static_cast<uint8_t>(RecorderRecordType::message))
        return WireError::bad_argument;
    if (size < recorder_message_header_bytes)
        return WireError::truncated;
    if (size != recorder_message_header_bytes + record[1])
        return WireError::length_mismatch;
    out->kind = static_cast<RecorderMessageKind>(record[2]);
    out->payload = record + recorder_message_header_bytes;
    out->size = record[1];
    return WireError::ok;
}

void encode_recorder_host_options(const RecorderHostOptions& options, uint8_t* out) noexcept {
    out[0] = options.autopause;
    out[1] = options.f1_off;
    out[2] = options.commander_warp;
    out[3] = options.speed_lock;
    out[4] = options.speed_high;
    out[5] = options.speed_low;
}

WireError decode_recorder_host_options(
    const uint8_t* payload, std::size_t size, RecorderHostOptions* out
) noexcept {
    if (payload == nullptr || out == nullptr)
        return WireError::bad_argument;
    if (size < recorder_host_options_bytes)
        return WireError::truncated;
    out->autopause = payload[0];
    out->f1_off = payload[1];
    out->commander_warp = payload[2];
    out->speed_lock = payload[3];
    out->speed_high = payload[4];
    out->speed_low = payload[5];
    return WireError::ok;
}

void encode_recorder_camera(uint16_t x, uint16_t y, uint8_t* out) noexcept {
    out[0] = static_cast<uint8_t>(RecorderRecordType::camera);
    store_u16(out + 1, x);
    store_u16(out + 3, y);
}

} // namespace oa::netgame
