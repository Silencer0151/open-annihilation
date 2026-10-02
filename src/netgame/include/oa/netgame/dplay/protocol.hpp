// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// DirectPlay 4 core messages as exchanged by the TCP/IP service provider.
//
// Every stream message and every control datagram starts with the 20-byte
// service-provider envelope (oa/netgame/dplay.hpp). Control messages follow it
// with "play", a u16 command and a u16 version; offsets embedded in bodies
// count from the "play" signature. Player-to-player data carries no
// signature: stream data is envelope + u32 from + u32 to + payload, datagram
// data is u32 from + u32 to + payload with no envelope.
//
// Layouts here describe the DirectPlay wire protocol as TA sessions
// exchange it.

#include "oa/netgame/dplay.hpp"

namespace oa::netgame::dplay {

inline constexpr uint16_t protocol_version = 0x000e; // DirectPlay 4 core protocol
inline constexpr uint16_t enum_port = 47624;         // host's session-enumeration UDP port
inline constexpr uint16_t stream_port_first = 2300;  // TCP listen range
inline constexpr uint16_t stream_port_last = 2400;
inline constexpr uint16_t datagram_port_first = 2350; // UDP data range
inline constexpr uint16_t datagram_port_last = 2400;
inline constexpr std::size_t header_bytes = dplay_envelope_bytes + dplay_play_header_bytes; // 28
inline constexpr std::size_t sockaddr_bytes = 16;
inline constexpr std::size_t data_ids_bytes = 8;          // u32 from + u32 to
inline constexpr std::size_t max_message_bytes = 0x11000; // bound on any single stream message
inline constexpr std::size_t max_datagram_bytes = 0x2000; // unguaranteed payload bound
inline constexpr uint32_t all_players_id = 0;             // send/receive destination: everyone

// Command words beyond those in oa::netgame::DplayCommand.
namespace command {
inline constexpr uint16_t enum_sessions_reply = 0x0001;
inline constexpr uint16_t enum_sessions = 0x0002;
inline constexpr uint16_t request_player_id = 0x0005;
inline constexpr uint16_t request_player_reply = 0x0007;
inline constexpr uint16_t create_player = 0x0008;
inline constexpr uint16_t delete_player = 0x000b;
inline constexpr uint16_t player_data_changed = 0x000f;
inline constexpr uint16_t player_name_changed = 0x0010;
inline constexpr uint16_t add_forward_request = 0x0013;
inline constexpr uint16_t ping = 0x0016;
inline constexpr uint16_t ping_reply = 0x0017;
/// The name server tells a machine that it is not in the session.
inline constexpr uint16_t you_are_dead = 0x0018;
inline constexpr uint16_t session_desc_changed = 0x001a;
inline constexpr uint16_t add_forward_reply = 0x0024;
inline constexpr uint16_t super_enum_players_reply = 0x0029;
inline constexpr uint16_t add_forward = 0x002e;
inline constexpr uint16_t add_forward_ack = 0x002f;
/// A machine announces that it took the name server's place.
inline constexpr uint16_t i_am_name_server = 0x0035;
} // namespace command

// Packed-player flags.
namespace player_flag {
inline constexpr uint32_t system_player = 0x1;
inline constexpr uint32_t name_server = 0x2;
inline constexpr uint32_t in_group = 0x4;
inline constexpr uint32_t local = 0x8; // sender's view; receivers clear it
} // namespace player_flag

// Session-description dwFlags (public DirectPlay values).
namespace session_flag {
inline constexpr uint32_t new_players_disabled = 0x1;
inline constexpr uint32_t migrate_host = 0x4;   // the 4 TA sets on create/join
inline constexpr uint32_t join_disabled = 0x20; // the game's session_flag_join_disabled
/// Machines ping one another to find one that stopped answering; the game
/// never sets it.
inline constexpr uint32_t keep_alive = 0x40;
/// A player's data changes are not sent to the other machines.
inline constexpr uint32_t no_data_messages = 0x80;
inline constexpr uint32_t password_required = 0x400;
} // namespace session_flag

// EnumSessions wire flags (TA passes 0x81).
namespace enum_flag {
inline constexpr uint32_t available = 0x1;
inline constexpr uint32_t all = 0x2;
inline constexpr uint32_t password_required = 0x40;
inline constexpr uint32_t return_status = 0x80; // API flag, observed on the wire
} // namespace enum_flag

// HRESULTs (bit patterns) beyond oa::netgame::transport_result.
namespace result {
inline constexpr uint32_t ok = 0;
inline constexpr uint32_t unsupported = 0x80004001;
inline constexpr uint32_t generic = 0x80004005;
inline constexpr uint32_t out_of_memory = 0x8007000e;
inline constexpr uint32_t invalid_params = 0x80070057;
inline constexpr uint32_t already_initialized = 0x88770005;
inline constexpr uint32_t access_denied = 0x8877000a;
inline constexpr uint32_t active_players = 0x88770014;
inline constexpr uint32_t buffer_too_small = 0x8877001e;
inline constexpr uint32_t cant_add_player = 0x88770028;
inline constexpr uint32_t cant_create_player = 0x8877003c;
inline constexpr uint32_t invalid_flags = 0x88770078;
inline constexpr uint32_t invalid_object = 0x88770082;
inline constexpr uint32_t invalid_player = 0x88770096;
inline constexpr uint32_t no_connection = 0x887700aa;
inline constexpr uint32_t no_messages = 0x887700be;
inline constexpr uint32_t no_name_server_found = 0x887700c8;
inline constexpr uint32_t no_sessions = 0x887700dc;
inline constexpr uint32_t send_too_big = 0x887700e6;
inline constexpr uint32_t timeout = 0x887700f0;
inline constexpr uint32_t user_cancel = 0x88770118;
inline constexpr uint32_t session_lost = 0x88770136;
inline constexpr uint32_t uninitialized = 0x88770140;
inline constexpr uint32_t connecting = 0x8877015e; // EnumSessions retry value
inline constexpr uint32_t invalid_password = 0x88770168;
} // namespace result

/// Returns the symbolic DPERR_* name of a result.
///
/// @param hresult Result bit pattern.
/// @return The name, or nullptr for values 3.1c reports as undocumented.
[[nodiscard]] const char* result_name(uint32_t hresult) noexcept;

struct Guid {
    uint8_t bytes[16]{};
};

/// Tells whether two GUIDs are byte for byte equal.
///
/// @param a First GUID.
/// @param b Second GUID.
/// @return True when all 16 bytes match.
[[nodiscard]] bool guid_equal(const Guid& a, const Guid& b) noexcept;

/// Tells whether a GUID is all zero.
///
/// @param g GUID to test.
/// @return True when all 16 bytes are zero.
[[nodiscard]] bool guid_is_zero(const Guid& g) noexcept;

// IPv4 endpoint; port in host order.
struct Address {
    uint8_t ip[4]{};
    uint16_t port{};
};

/// Tells whether two endpoints have the same address and port.
///
/// @param a First endpoint.
/// @param b Second endpoint.
/// @return True when equal.
[[nodiscard]] bool address_equal(const Address& a, const Address& b) noexcept;

/// Tells whether an endpoint's IPv4 address is 0.0.0.0.
///
/// @param a Endpoint to test.
/// @return True for the unspecified address.
[[nodiscard]] bool address_ip_is_zero(const Address& a) noexcept;

/// Writes an endpoint as the provider writes sockaddr_in: family LE, port BE, IPv4, 8 zero bytes.
///
/// @param[out] out Destination of 16 bytes.
/// @param in Endpoint to write.
void store_sockaddr(uint8_t* out, const Address& in) noexcept;

/// Reads the address and port of a 16-byte sockaddr_in.
///
/// @param in Source of 16 bytes.
/// @return The endpoint, port in host order.
[[nodiscard]] Address load_sockaddr(const uint8_t* in) noexcept;

inline constexpr std::size_t max_name_chars = 32; // ANSI names, NUL excluded
inline constexpr std::size_t max_player_data_bytes = 256;
inline constexpr std::size_t max_roster_players = 32;

// Player record as carried in CreatePlayer / AddForward (packed) and in
// SuperEnumPlayersReply (super-packed). Names are ANSI; the wire uses
// UTF-16LE and characters above U+00FF become '?'.
struct PlayerInfo {
    uint32_t flags{};
    uint32_t id{};
    uint32_t system_id{};               // own id for system players
    uint32_t version{protocol_version}; // system players only
    uint32_t parent_id{};
    char short_name[max_name_chars + 1]{};
    char long_name[max_name_chars + 1]{};
    bool has_short_name{};
    bool has_long_name{};
    bool has_addresses{}; // service-provider data present
    Address stream{};     // SP data: stream then datagram sockaddr
    Address datagram{};
    uint16_t data_size{};
    uint8_t data[max_player_data_bytes]{};
};

// One control message, decoded. Only the fields of its command are set.
struct Message {
    Address reply_to{}; // envelope sockaddr
    uint16_t command{};
    uint16_t version{};
    uint32_t id_to{};
    /// Request reply id, created, deleted or forwarded player, pinging
    /// player, the new name server's system player, or the player whose name
    /// or data changed.
    uint32_t player_id{};
    uint32_t group_id{};
    uint32_t flags{};   // enum flags, request-player flags, the new name server's player flags
    uint32_t result{};  // request reply / add-forward reply result
    uint32_t tick{};    // ping tick, add-forward tick
    Guid application{}; // enum request
    SessionDesc desc{};
    char session_name[max_name_chars + 1]{};
    char password[max_name_chars + 1]{};
    /// Create / add-forward: the player. Name server announcement: its
    /// addresses. Name or data change: the new names or data.
    PlayerInfo player{};
    uint32_t roster_count{};
    PlayerInfo roster[max_roster_players]{};
};

enum class ParseError : uint8_t {
    ok,
    not_dplay, // no 0xFAB token
    truncated,
    not_control, // envelope without "play": a data message
    bad_offset,  // embedded offset outside the message
    too_many_players,
    unsupported, // command not decoded here
};

/// Returns the size of a complete stream message from its first four bytes.
///
/// @param bytes Message start; four bytes must be readable.
/// @return The declared size, or 0 when the token is wrong or the size is outside [20, max_message_bytes].
[[nodiscard]] std::size_t stream_message_size(const uint8_t* bytes) noexcept;

/// Decodes one control message.
///
/// @param bytes Message start, envelope first.
/// @param size Bytes readable from bytes; at least the declared size.
/// @param[out] out Decoded message; only the fields of its command are set.
/// @return ok; truncated; not_dplay without the token; not_control for a data message; bad_offset;
///         too_many_players; unsupported for a command not decoded here.
[[nodiscard]] ParseError
decode_message(const uint8_t* bytes, std::size_t size, Message* out) noexcept;

// Writers return the message size or 0 when capacity is short. The envelope
// sockaddr is reply_to.

/// Encodes an EnumSessions request.
///
/// @param reply_to Endpoint written into the envelope sockaddr.
/// @param application Application GUID to enumerate.
/// @param password Password to present, or null or empty for none.
/// @param flags enum_flag bits.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short.
[[nodiscard]] std::size_t encode_enum_sessions(
    const Address& reply_to,
    const Guid& application,
    const char* password,
    uint32_t flags,
    uint8_t* out,
    std::size_t capacity
) noexcept;
/// Encodes an EnumSessions reply describing a hosted session.
///
/// @param reply_to Endpoint written into the envelope sockaddr.
/// @param desc Session description.
/// @param name Session name, or null for none.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short.
[[nodiscard]] std::size_t encode_enum_sessions_reply(
    const Address& reply_to,
    const SessionDesc& desc,
    const char* name,
    uint8_t* out,
    std::size_t capacity
) noexcept;
/// Encodes a request to the name server for a new player id.
///
/// @param reply_to Endpoint written into the envelope sockaddr.
/// @param flags Request flags (system or application player).
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short.
[[nodiscard]] std::size_t encode_request_player_id(
    const Address& reply_to, uint32_t flags, uint8_t* out, std::size_t capacity
) noexcept;
/// Encodes the name server's reply to a player id request.
///
/// @param reply_to Endpoint written into the envelope sockaddr.
/// @param id Issued player id.
/// @param result Result of the request.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short.
[[nodiscard]] std::size_t encode_request_player_reply(
    const Address& reply_to, uint32_t id, uint32_t result, uint8_t* out, std::size_t capacity
) noexcept;
/// Encodes a message carrying one packed player.
///
/// The forward forms append the password and a trailing dword;
/// create_player appends six reserved zero bytes.
///
/// @param reply_to Endpoint written into the envelope sockaddr.
/// @param command create_player, add_forward_request or add_forward.
/// @param id_to Addressee id.
/// @param player Player to pack.
/// @param password Session password, written by the forward forms.
/// @param tick Trailing dword of the forward forms; a joiner's
///     AddForwardRequest carries the session description's first reserved
///     dword.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short.
[[nodiscard]] std::size_t encode_player_message(
    const Address& reply_to,
    uint16_t command,
    uint32_t id_to,
    const PlayerInfo& player,
    const char* password,
    uint32_t tick,
    uint8_t* out,
    std::size_t capacity
) noexcept;
/// Encodes a DeletePlayer message.
///
/// @param reply_to Endpoint written into the envelope sockaddr.
/// @param id_to Addressee id; 0 for everyone.
/// @param player_id Player being deleted.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short.
[[nodiscard]] std::size_t encode_delete_player(
    const Address& reply_to, uint32_t id_to, uint32_t player_id, uint8_t* out, std::size_t capacity
) noexcept;
/// Encodes an AddForward reply.
///
/// @param reply_to Endpoint written into the envelope sockaddr.
/// @param result Result of the forward request.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short.
[[nodiscard]] std::size_t encode_add_forward_reply(
    const Address& reply_to, uint32_t result, uint8_t* out, std::size_t capacity
) noexcept;
/// Encodes an AddForward acknowledgement.
///
/// @param reply_to Endpoint written into the envelope sockaddr.
/// @param player_id Player whose forward is acknowledged.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short.
[[nodiscard]] std::size_t encode_add_forward_ack(
    const Address& reply_to, uint32_t player_id, uint8_t* out, std::size_t capacity
) noexcept;
/// Encodes a Ping or PingReply.
///
/// @param reply_to Endpoint written into the envelope sockaddr.
/// @param command ping or ping_reply.
/// @param from_id Pinging player id; a reply echoes the ping's.
/// @param tick Tick of the ping in milliseconds, echoed by the reply.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short.
[[nodiscard]] std::size_t encode_ping(
    const Address& reply_to,
    uint16_t command,
    uint32_t from_id,
    uint32_t tick,
    uint8_t* out,
    std::size_t capacity
) noexcept;
/// Bytes of a name server announcement's service-provider data: the stream
/// socket address, then the datagram socket address.
inline constexpr uint32_t name_server_addresses_bytes = static_cast<uint32_t>(2 * sockaddr_bytes);

/// Encodes the announcement a machine sends each other machine when it takes the name server's place.
///
/// Body: u32 addressee, u32 the new name server's system player, u32 that
/// player's flags, u32 the size of the service-provider data (32), then its
/// stream and datagram socket addresses.
///
/// @param reply_to Endpoint written into the envelope sockaddr.
/// @param id_to System player of the machine addressed.
/// @param host_id System player of the new name server.
/// @param flags Player flags of that system player.
/// @param stream Its stream endpoint; the address is written as the caller gives it.
/// @param datagram Its datagram endpoint.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short.
[[nodiscard]] std::size_t encode_i_am_name_server(
    const Address& reply_to,
    uint32_t id_to,
    uint32_t host_id,
    uint32_t flags,
    const Address& stream,
    const Address& datagram,
    uint8_t* out,
    std::size_t capacity
) noexcept;
/// Encodes the name server's message telling a machine it is not in the session: the header alone.
///
/// @param reply_to Endpoint written into the envelope sockaddr.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short.
[[nodiscard]] std::size_t
encode_you_are_dead(const Address& reply_to, uint8_t* out, std::size_t capacity) noexcept;
/// Encodes a player data change.
///
/// Body: u32 addressee, u32 player, u32 data size, u32 the data's offset
/// from the "play" signature (0x18), then the data.
///
/// @param reply_to Endpoint written into the envelope sockaddr.
/// @param id_to Addressee id, 0.
/// @param player_id Player whose data changed.
/// @param data The new data; may be null only when size is 0.
/// @param size Data length in bytes.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short or data is null with a size.
[[nodiscard]] std::size_t encode_player_data_changed(
    const Address& reply_to,
    uint32_t id_to,
    uint32_t player_id,
    const uint8_t* data,
    std::size_t size,
    uint8_t* out,
    std::size_t capacity
) noexcept;
/// Encodes a player name change.
///
/// Body: u32 addressee, u32 player, u32 short name offset, u32 long name
/// offset (from the "play" signature, 0 for no name), then the names in
/// UTF-16 with terminators, short name first.
///
/// @param reply_to Endpoint written into the envelope sockaddr.
/// @param id_to Addressee id, 0.
/// @param player_id Player whose name changed.
/// @param short_name New short name, or null for none.
/// @param long_name New long name, or null for none.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short.
[[nodiscard]] std::size_t encode_player_name_changed(
    const Address& reply_to,
    uint32_t id_to,
    uint32_t player_id,
    const char* short_name,
    const char* long_name,
    uint8_t* out,
    std::size_t capacity
) noexcept;
/// Offset, from the "play" signature, of a SessionDescChanged's session name.
inline constexpr uint32_t session_desc_changed_name_offset =
    static_cast<uint32_t>(dplay_play_header_bytes + 12 + session_desc_bytes);
/// Zero bytes a SessionDescChanged carries after its password.
inline constexpr std::size_t session_desc_changed_tail_bytes = 80;

/// Returns the offset, from the "play" signature, of a SessionDescChanged's password.
///
/// @param name Session name, which comes first.
/// @return The offset: the name's offset plus its UTF-16 length with the terminator.
[[nodiscard]] uint32_t session_desc_changed_password_offset(const char* name) noexcept;

/// Encodes a SessionDescChanged message.
///
/// Body: u32 addressee, u32 name offset, u32 password offset, the 0x50-byte
/// description as given (its name and password fields included), the name
/// and the password in UTF-16 with terminators, then 80 zero bytes.
///
/// @param reply_to Endpoint written into the envelope sockaddr.
/// @param id_to Addressee id; 0 for everyone.
/// @param desc New session description.
/// @param name Session name.
/// @param password Session password.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short.
[[nodiscard]] std::size_t encode_session_desc_changed(
    const Address& reply_to,
    uint32_t id_to,
    const SessionDesc& desc,
    const char* name,
    const char* password,
    uint8_t* out,
    std::size_t capacity
) noexcept;
/// Encodes a SuperEnumPlayersReply: the session description and every player, super-packed.
///
/// @param reply_to Endpoint written into the envelope sockaddr.
/// @param desc Session description.
/// @param name Session name.
/// @param password Session password, or null or empty for none.
/// @param players Players to list.
/// @param player_count Number of players.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short.
[[nodiscard]] std::size_t encode_super_enum_players_reply(
    const Address& reply_to,
    const SessionDesc& desc,
    const char* name,
    const char* password,
    const PlayerInfo* players,
    std::size_t player_count,
    uint8_t* out,
    std::size_t capacity
) noexcept;

// Player data messages.

/// Encodes guaranteed player data: envelope, from and to ids, then the payload.
///
/// @param reply_to Endpoint written into the envelope sockaddr.
/// @param from_id Sending player id.
/// @param to_id Destination player id; 0 for everyone.
/// @param payload Payload; may be null only when payload_size is 0.
/// @param payload_size Payload length in bytes.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short.
[[nodiscard]] std::size_t encode_stream_data(
    const Address& reply_to,
    uint32_t from_id,
    uint32_t to_id,
    const uint8_t* payload,
    std::size_t payload_size,
    uint8_t* out,
    std::size_t capacity
) noexcept;
/// Encodes unguaranteed player data: from and to ids, then the payload, with no envelope.
///
/// @param from_id Sending player id.
/// @param to_id Destination player id; 0 for everyone.
/// @param payload Payload; may be null only when payload_size is 0.
/// @param payload_size Payload length in bytes.
/// @param[out] out Destination buffer.
/// @param capacity Bytes available at out.
/// @return The message size, or 0 when capacity is short.
[[nodiscard]] std::size_t encode_datagram_data(
    uint32_t from_id,
    uint32_t to_id,
    const uint8_t* payload,
    std::size_t payload_size,
    uint8_t* out,
    std::size_t capacity
) noexcept;

struct DataMessage {
    uint32_t from_id{};
    uint32_t to_id{};
    const uint8_t* payload{};
    std::size_t payload_size{};
};

/// Decodes guaranteed player data: an envelope without "play", the ids and the payload.
///
/// @param bytes Message start.
/// @param size Bytes readable from bytes.
/// @param[out] out Ids and a payload view into bytes; written only on success.
/// @return ok; truncated; not_dplay without the token; unsupported for a control message.
[[nodiscard]] ParseError
decode_stream_data(const uint8_t* bytes, std::size_t size, DataMessage* out) noexcept;

} // namespace oa::netgame::dplay
