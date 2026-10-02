// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// DirectPlay boundary: the transport interface the game's network layer
// calls, the DirectPlay system messages it handles, the CreatePlayer data
// block it validates, and the DirectPlay-over-IP datagram envelope seen on
// the wire around every game datagram. No sockets live here.

#include "oa/netgame/wire.hpp"

#include <cstdint>

namespace oa::netgame {

// Transport results (HRESULT bit patterns the game tests for).
namespace transport_result {
inline constexpr uint32_t ok = 0;
inline constexpr uint32_t no_connection = 0x887700aa; // interface missing
inline constexpr uint32_t no_messages = 0x887700be;   // nothing to receive
inline constexpr uint32_t buffer_too_small = 0x8877001e;
inline constexpr uint32_t uninitialized = 0x88770140;
inline constexpr uint32_t enum_sessions_retry = 0x8877015e;
inline constexpr uint32_t out_of_memory = 0x8007000e;
inline constexpr uint32_t generic_failure = 0x80004005;
} // namespace transport_result

// Transport sender id of system messages.
inline constexpr uint32_t system_message_sender_id = 0;
// Send dwFlags: exactly 1 while guaranteed delivery is on, else 0.
inline constexpr uint32_t send_flag_guaranteed = 1;
inline constexpr uint32_t receive_flag_all = 1;

// Platform transport. size is capacity on entry and the received (or
// required, with buffer_too_small) length on return.
struct NetTransport {
    void* context{};
    uint32_t (*send)(
        void* context,
        uint32_t from_id,
        uint32_t to_id,
        uint32_t flags,
        const uint8_t* data,
        uint32_t size
    ){};
    uint32_t (*receive)(
        void* context, uint32_t* from_id, uint32_t* to_id, uint8_t* buffer, uint32_t* size
    ){};
};

// System message dwType values the packet pump handles (0x31 and 0x101
// arrive too and are ignored).
enum class SystemMessageType : uint32_t {
    player_created = 0x0003,
    player_destroyed = 0x0005,
    session_lost = 0x0031,
    host_changed = 0x0101,
    player_data_changed = 0x0102,
    player_name_changed = 0x0103,
    session_desc_changed = 0x0104,
};

inline constexpr std::size_t player_data_block_bytes = 0xb9; // copied by 0x0102
inline constexpr std::size_t player_name_copy_bytes = 0x1e;  // most name bytes 0x0103 copies

// CreatePlayer data block (21 bytes): join tag, then two u16 the receiver
// requires to be 0 and 0x50.
struct CreatePlayerData {
    char tag[17]{};          // at most 16 characters, zero-padded; byte 16 always 0
    uint16_t version_low{};  // ? the joining game's version; must be 0
    uint16_t version_high{}; // ? the joining game's version; must be 0x50
};

inline constexpr std::size_t create_player_data_bytes = 0x15;
// ? The version a joining game must present; any other value is refused with
// reject reason 8 (version too old).
inline constexpr uint16_t create_player_version_low = 0;
inline constexpr uint16_t create_player_version_high = 0x50;

/// Encodes a CreatePlayer data block of 21 bytes.
///
/// @param in Block to encode; at most 16 tag characters are copied and byte 16 stays 0.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return ok; bad_argument when out is null; buffer_too_small below 21 bytes.
[[nodiscard]] WireError
encode_create_player_data(const CreatePlayerData& in, uint8_t* out, std::size_t capacity) noexcept;

/// Decodes a CreatePlayer data block of exactly 21 bytes.
///
/// @param bytes Block start.
/// @param size Block size.
/// @param[out] out Decoded block; the tag is copied as is and may be unterminated.
/// @return ok; bad_argument for a null pointer; truncated below 21 bytes; length_mismatch above.
[[nodiscard]] WireError
decode_create_player_data(const uint8_t* bytes, std::size_t size, CreatePlayerData* out) noexcept;

enum class JoinVerdict : uint8_t { accepted, version_mismatch, wrong_password };

/// Validates a joining player's CreatePlayer data block as the host does.
///
/// The "game closed" check (reject reason 3) depends on host state and is not here.
///
/// @param data Block start, or null.
/// @param size Block size in bytes.
/// @param password_required Whether the session requires a password.
/// @param expected_tag Session password, compared ignoring ASCII case; null fails when one is required.
/// @return version_mismatch (reject reason 8) when the block is null, not 21 bytes, or its two u16 are
///         not 0 and 0x50; wrong_password (reason 4) when a required password does not match; else accepted.
[[nodiscard]] JoinVerdict check_create_player_data(
    const uint8_t* data, std::size_t size, bool password_required, const char* expected_tag
) noexcept;

// DPSESSIONDESC2 as copied by the game (0x50 bytes; pointers are opaque).
struct SessionDesc {
    uint32_t size{0x50};
    uint32_t flags{};
    uint8_t instance_guid[16]{};
    uint8_t application_guid[16]{};
    uint32_t max_players{};
    uint32_t current_players{};
    uint32_t name_pointer{};
    uint32_t password_pointer{};
    uint32_t reserved[2]{};
    uint32_t user[4]{};
};

inline constexpr std::size_t session_desc_bytes = 0x50;
inline constexpr uint32_t session_flag_game = 0x4; // set on create and join
// Set once the local player's PlayerSetupInfo.options hold
// OA_SETUP_OPTION_STARTED.
inline constexpr uint32_t session_flag_join_disabled = 0x20;
inline constexpr uint32_t session_flag_password_required = 0x400;

// Total Annihilation's DirectPlay application GUID in wire byte order.
inline constexpr uint8_t application_guid[16] = {
    0x20,
    0x74,
    0x79,
    0x99,
    0xf5,
    0xf5,
    0xcf,
    0x11,
    0x98,
    0x27,
    0x00,
    0xa0,
    0x24,
    0x14,
    0x96,
    0xc8,
};

/// Encodes a session description as its 0x50-byte little-endian layout.
///
/// @param in Description to encode; the pointer fields are written as opaque values.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return ok; bad_argument when out is null; buffer_too_small below 0x50 bytes.
[[nodiscard]] WireError
encode_session_desc(const SessionDesc& in, uint8_t* out, std::size_t capacity) noexcept;

/// Decodes a session description from its 0x50-byte little-endian layout.
///
/// @param bytes Description start.
/// @param size Bytes readable from bytes; extra bytes are ignored.
/// @param[out] out Decoded description; written only on success.
/// @return ok; bad_argument for a null pointer; truncated below 0x50 bytes.
[[nodiscard]] WireError
decode_session_desc(const uint8_t* bytes, std::size_t size, SessionDesc* out) noexcept;

// DirectPlay-over-IP envelope written by the service provider:
//   u32 LE (token << 20 | datagram size), sockaddr_in (family LE, port BE,
//   IPv4 address, 8 zero bytes); reliable core messages then carry the
//   "play" signature, a u16 command and a u16 version.
inline constexpr uint32_t dplay_envelope_token = 0xfab;
inline constexpr uint32_t dplay_envelope_size_mask = 0xfffff;
inline constexpr std::size_t dplay_envelope_bytes = 20;
inline constexpr uint32_t dplay_play_signature = 0x79616c70; // "play"
inline constexpr std::size_t dplay_play_header_bytes = 8;
inline constexpr uint16_t dplay_address_family_inet = 2;

enum class DplayCommand : uint16_t {
    enum_sessions_reply = 0x0001,
    enum_sessions = 0x0002,
    request_player_id = 0x0005,
    request_player_reply = 0x0007,
    create_player = 0x0008,
    delete_player = 0x000b,
    add_forward_request = 0x0013,
    ping = 0x0016,
    ping_reply = 0x0017,
    session_desc_changed = 0x001a,
    super_enum_players_reply = 0x0029,
};

struct DplayEnvelope {
    uint32_t size{}; // whole datagram, envelope included
    uint16_t family{dplay_address_family_inet};
    uint16_t port{}; // host order
    uint8_t address[4]{};
    bool has_play_header{};
    uint16_t command{};
    uint16_t version{};
    const uint8_t* body{}; // after the envelope (and play header when present)
    std::size_t body_size{};
};

/// Parses one DirectPlay-over-IP envelope and the "play" header that may follow it.
///
/// @param bytes Envelope start.
/// @param size Bytes readable from bytes.
/// @param[out] out Parsed envelope whose body aliases bytes; written only on success.
/// @param[out] next_offset Where the next batched envelope starts, when not null: the declared size, or
///        size when the declared size is below 4 or does not fit. Written once the token matches.
/// @return ok; bad_argument for a null pointer; truncated when fewer than 4 bytes, or than the 20-byte
///         envelope, are available; invalid_type when the leading dword lacks the token.
[[nodiscard]] WireError decode_dplay_envelope(
    const uint8_t* bytes, std::size_t size, DplayEnvelope* out, std::size_t* next_offset
) noexcept;

/// Writes an envelope, the "play" header when has_play_header is set, and the body.
///
/// @param in Envelope to write; its size field is ignored and computed from the parts.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @param[out] written Bytes written, when not null.
/// @return ok; bad_argument when out is null, the body is missing or the total exceeds 0xfffff bytes;
///         buffer_too_small.
[[nodiscard]] WireError encode_dplay_envelope(
    const DplayEnvelope& in, uint8_t* out, std::size_t capacity, std::size_t* written
) noexcept;

} // namespace oa::netgame
