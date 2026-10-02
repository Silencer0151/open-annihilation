// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/dplay.hpp"
#include <cstdint>
#include <cstring>

namespace oa::netgame {
namespace {

constexpr std::size_t create_player_tag_bytes = 17;
constexpr std::size_t create_player_tag_copy = 16;

char ascii_lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// Case-insensitive compare of a bounded, possibly unterminated tag with a C string.
bool tag_equals_nocase(const char* tag, std::size_t bound, const char* expected) noexcept {
    std::size_t i = 0;
    for (; i < bound; ++i) {
        if (ascii_lower(tag[i]) != ascii_lower(expected[i]))
            return false;
        if (tag[i] == '\0')
            return true;
    }
    return expected[i] == '\0';
}

} // namespace

WireError
encode_create_player_data(const CreatePlayerData& in, uint8_t* out, std::size_t capacity) noexcept {
    if (out == nullptr)
        return WireError::bad_argument;
    if (capacity < create_player_data_bytes)
        return WireError::buffer_too_small;
    std::memset(out, 0, create_player_tag_bytes);
    for (std::size_t i = 0; i < create_player_tag_copy && in.tag[i] != '\0'; ++i)
        out[i] = static_cast<uint8_t>(in.tag[i]);
    store_u16(out + 0x11, in.version_low);
    store_u16(out + 0x13, in.version_high);
    return WireError::ok;
}

WireError
decode_create_player_data(const uint8_t* bytes, std::size_t size, CreatePlayerData* out) noexcept {
    if (bytes == nullptr || out == nullptr)
        return WireError::bad_argument;
    if (size != create_player_data_bytes)
        return size < create_player_data_bytes ? WireError::truncated : WireError::length_mismatch;
    std::memcpy(out->tag, bytes, create_player_tag_bytes);
    out->version_low = load_u16(bytes + 0x11);
    out->version_high = load_u16(bytes + 0x13);
    return WireError::ok;
}

JoinVerdict check_create_player_data(
    const uint8_t* data, std::size_t size, bool password_required, const char* expected_tag
) noexcept {
    CreatePlayerData block{};
    if (data == nullptr || decode_create_player_data(data, size, &block) != WireError::ok ||
        block.version_low != create_player_version_low ||
        block.version_high != create_player_version_high)
        return JoinVerdict::version_mismatch;
    if (password_required && (expected_tag == nullptr ||
                              !tag_equals_nocase(block.tag, create_player_tag_bytes, expected_tag)))
        return JoinVerdict::wrong_password;
    return JoinVerdict::accepted;
}

WireError encode_session_desc(const SessionDesc& in, uint8_t* out, std::size_t capacity) noexcept {
    if (out == nullptr)
        return WireError::bad_argument;
    if (capacity < session_desc_bytes)
        return WireError::buffer_too_small;
    store_u32(out + 0x00, in.size);
    store_u32(out + 0x04, in.flags);
    std::memcpy(out + 0x08, in.instance_guid, 16);
    std::memcpy(out + 0x18, in.application_guid, 16);
    store_u32(out + 0x28, in.max_players);
    store_u32(out + 0x2c, in.current_players);
    store_u32(out + 0x30, in.name_pointer);
    store_u32(out + 0x34, in.password_pointer);
    store_u32(out + 0x38, in.reserved[0]);
    store_u32(out + 0x3c, in.reserved[1]);
    for (std::size_t i = 0; i < 4; ++i)
        store_u32(out + 0x40 + 4 * i, in.user[i]);
    return WireError::ok;
}

WireError decode_session_desc(const uint8_t* bytes, std::size_t size, SessionDesc* out) noexcept {
    if (bytes == nullptr || out == nullptr)
        return WireError::bad_argument;
    if (size < session_desc_bytes)
        return WireError::truncated;
    SessionDesc d{};
    d.size = load_u32(bytes + 0x00);
    d.flags = load_u32(bytes + 0x04);
    std::memcpy(d.instance_guid, bytes + 0x08, 16);
    std::memcpy(d.application_guid, bytes + 0x18, 16);
    d.max_players = load_u32(bytes + 0x28);
    d.current_players = load_u32(bytes + 0x2c);
    d.name_pointer = load_u32(bytes + 0x30);
    d.password_pointer = load_u32(bytes + 0x34);
    d.reserved[0] = load_u32(bytes + 0x38);
    d.reserved[1] = load_u32(bytes + 0x3c);
    for (std::size_t i = 0; i < 4; ++i)
        d.user[i] = load_u32(bytes + 0x40 + 4 * i);
    *out = d;
    return WireError::ok;
}

WireError decode_dplay_envelope(
    const uint8_t* bytes, std::size_t size, DplayEnvelope* out, std::size_t* next_offset
) noexcept {
    if (bytes == nullptr || out == nullptr)
        return WireError::bad_argument;
    if (size < 4)
        return WireError::truncated;
    const auto word = load_u32(bytes);
    if ((word >> 20) != dplay_envelope_token)
        return WireError::invalid_type;
    const std::size_t declared = word & dplay_envelope_size_mask;
    const std::size_t extent = declared < 4 || declared > size ? size : declared;
    if (next_offset)
        *next_offset = extent;
    if (extent < dplay_envelope_bytes)
        return WireError::truncated;
    DplayEnvelope e{};
    e.size = static_cast<uint32_t>(declared);
    e.family = load_u16(bytes + 4);
    e.port = static_cast<uint16_t>((bytes[6] << 8) | bytes[7]);
    std::memcpy(e.address, bytes + 8, 4);
    std::size_t body = dplay_envelope_bytes;
    if (extent >= dplay_envelope_bytes + dplay_play_header_bytes &&
        load_u32(bytes + dplay_envelope_bytes) == dplay_play_signature) {
        e.has_play_header = true;
        e.command = load_u16(bytes + dplay_envelope_bytes + 4);
        e.version = load_u16(bytes + dplay_envelope_bytes + 6);
        body += dplay_play_header_bytes;
    }
    e.body = bytes + body;
    e.body_size = extent - body;
    *out = e;
    return WireError::ok;
}

WireError encode_dplay_envelope(
    const DplayEnvelope& in, uint8_t* out, std::size_t capacity, std::size_t* written
) noexcept {
    if (out == nullptr || (in.body == nullptr && in.body_size != 0))
        return WireError::bad_argument;
    const std::size_t total =
        dplay_envelope_bytes + (in.has_play_header ? dplay_play_header_bytes : 0) + in.body_size;
    if (total > dplay_envelope_size_mask)
        return WireError::bad_argument;
    if (capacity < total)
        return WireError::buffer_too_small;
    store_u32(out, (dplay_envelope_token << 20) | static_cast<uint32_t>(total));
    store_u16(out + 4, in.family);
    out[6] = static_cast<uint8_t>(in.port >> 8);
    out[7] = static_cast<uint8_t>(in.port);
    std::memcpy(out + 8, in.address, 4);
    std::memset(out + 12, 0, 8);
    std::size_t at = dplay_envelope_bytes;
    if (in.has_play_header) {
        store_u32(out + at, dplay_play_signature);
        store_u16(out + at + 4, in.command);
        store_u16(out + at + 6, in.version);
        at += dplay_play_header_bytes;
    }
    if (in.body_size != 0)
        std::memcpy(out + at, in.body, in.body_size);
    if (written)
        *written = total;
    return WireError::ok;
}

} // namespace oa::netgame
