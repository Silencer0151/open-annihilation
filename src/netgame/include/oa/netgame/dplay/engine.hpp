// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Sans-I/O DirectPlay session engine: the name-server (host) and peer
// behaviour of the sessions the game plays in, speaking the core protocol in
// protocol.hpp. The caller feeds received stream messages
// and datagrams in and supplies send callbacks; nothing here touches sockets
// or clocks, and all storage is fixed-size.
//
// Players are addressed by DPID. The host issues ids as
// ((uniqueness << 16) | slot) ^ key, with key published in the session
// description's first reserved dword, as 3.1c hosts do.
//
// When the name server's machine leaves a session with the migrate-host
// flag, the machine with the lowest system player id takes its place and
// announces itself; while the session has the keep-alive flag, or while a
// new name server is awaited, machines that stay silent are pinged and
// dropped after eight unanswered pings.
//
// System messages are queued with from id 0 as byte images of the
// corresponding DPMSG_* structures (32-bit layout), with every pointer
// replaced by a byte offset from the start of the image (0 = none).

#include "oa/netgame/dplay/protocol.hpp"

#include <cstdint>

namespace oa::netgame::dplay {

struct EngineIo {
    void* context{};
    // Reliable ordered delivery of one complete message to a stream endpoint.
    bool (*send_stream)(void* context, const Address& to, const uint8_t* bytes, std::size_t size){};
    // One datagram; to.ip 255.255.255.255 broadcasts.
    bool (*send_datagram)(
        void* context, const Address& to, const uint8_t* bytes, std::size_t size
    ){};
    /// Finds this machine's address on the way to an endpoint. While the
    /// advertised stream address is 0.0.0.0, every stream message and every
    /// enumeration request carries it in its header, as a DirectPlay sender
    /// writes its own address there. Null, or false, leaves 0.0.0.0.
    bool (*local_address)(void* context, const Address& to, uint8_t ip[4]){};
    /// Starts answering enumeration requests on the enumeration port, when
    /// this machine takes the name server's place. Null leaves the port as
    /// it is.
    bool (*listen_enumeration)(void* context){};
};

struct EngineConfig {
    // Advertised stream (TCP) listen endpoint, written into every message
    // header; ip 0.0.0.0 writes the address EngineIo::local_address finds.
    Address stream{};
    Address datagram{}; // advertised datagram (UDP) endpoint
    uint16_t enum_port{dplay::enum_port};
    uint32_t seed{1}; // instance GUID and id-key source
    uint32_t request_timeout_ms{15000};
};

inline constexpr std::size_t max_players = 32;
inline constexpr std::size_t max_sessions = 16;
inline constexpr std::size_t receive_queue_bytes = 0x20000;
inline constexpr std::size_t receive_queue_entries = 1024;
inline constexpr uint32_t reservation_timeout_ms = 60000;
/// Period of the ping timer, in milliseconds.
inline constexpr uint32_t ping_period_ms = 35000;
/// Pings a silent machine is sent before it is dropped.
inline constexpr uint8_t max_unanswered_pings = 8;

// DirectPlay Send dwFlags bit for guaranteed delivery (TA passes exactly 0 or 1).
inline constexpr uint32_t send_guaranteed = send_flag_guaranteed;

// System-message images (see header comment). Offsets are into the image.
namespace system_message {
inline constexpr uint32_t player_type_player = 1;
// 0x0003 create: type, player type, id, current players, data, data size,
// DPNAME {size, flags, short, long}, parent id, flags.
inline constexpr std::size_t create_id = 0x08;
inline constexpr std::size_t create_current_players = 0x0c;
inline constexpr std::size_t create_data = 0x10;
inline constexpr std::size_t create_data_size = 0x14;
inline constexpr std::size_t create_name = 0x18;
inline constexpr std::size_t create_bytes = 0x30;
// 0x0005 destroy: type, player type, id, local data, local size, remote
// data, remote size, DPNAME, parent id, flags.
inline constexpr std::size_t destroy_id = 0x08;
inline constexpr std::size_t destroy_id_end = destroy_id + 4; // shortest image the game reads
inline constexpr std::size_t destroy_remote_data = 0x14;
inline constexpr std::size_t destroy_remote_data_size = 0x18;
inline constexpr std::size_t destroy_name = 0x1c;
inline constexpr std::size_t destroy_bytes = 0x34;
// DPNAME inside the above.
inline constexpr std::size_t name_short = 0x08;
inline constexpr std::size_t name_long = 0x0c;
inline constexpr std::size_t name_bytes = 0x10;
// 0x0104 session description: type, DPSESSIONDESC2 (its name_pointer and
// password_pointer fields hold offsets).
inline constexpr std::size_t session_desc = 0x04;
inline constexpr std::size_t session_desc_image_bytes = 4 + session_desc_bytes;
// 0x0101 this machine became the name server: the type alone.
inline constexpr std::size_t host_bytes = 0x04;
// 0x0102 player data: type, player type, id, data, data size.
inline constexpr std::size_t player_data_id = 0x08;
inline constexpr std::size_t player_data_data = 0x0c;
inline constexpr std::size_t player_data_size = 0x10;
inline constexpr std::size_t player_data_bytes = 0x14;
// 0x0103 player name: type, player type, id, DPNAME.
inline constexpr std::size_t player_name_id = 0x08;
inline constexpr std::size_t player_name_name = 0x0c;
inline constexpr std::size_t player_name_bytes = 0x1c;
} // namespace system_message

/// Reads the type every system message image starts with.
///
/// @param image Message image.
/// @param size Image length in bytes.
/// @param[out] type The image's SystemMessageType value; written only on success.
/// @return False for a null image or one shorter than its type field.
[[nodiscard]] bool
read_system_message_type(const uint8_t* image, std::size_t size, uint32_t* type) noexcept;

/// Decoded fields of a 0x0005 player-destroyed image that the game reads.
struct PlayerDestroyedView {
    uint32_t player_type{}; ///< player_type_player for a player, else a group
    uint32_t id{};
};

/// Decodes the type, player type and id of a 0x0005 player-destroyed system message image.
///
/// @param image Message image.
/// @param size Image length in bytes; at least the bytes through the id.
/// @param[out] out The fields; written only on success.
/// @return False for a null image, a short one or another message type.
[[nodiscard]] bool decode_player_destroyed_image(
    const uint8_t* image, std::size_t size, PlayerDestroyedView* out
) noexcept;

// Decoded view of a 0x0003 image (pointers into the caller's buffer).
struct CreatePlayerView {
    uint32_t id{};
    uint32_t current_players{};
    const uint8_t* data{};
    uint32_t data_size{};
    const char* short_name{};
    const char* long_name{};
};

/// Decodes a 0x0003 create-player system message image.
///
/// @param image Message image.
/// @param size Image length in bytes.
/// @param[out] out View whose data and names point into image; written only on success.
/// @return False for a short image, another message type, or a data block or name outside the image.
[[nodiscard]] bool
decode_create_player_image(const uint8_t* image, std::size_t size, CreatePlayerView* out) noexcept;

/// Decoded view of a 0x0102 player data image (the data points into the caller's buffer).
struct PlayerDataView {
    uint32_t id{};
    const uint8_t* data{};
    uint32_t data_size{};
};

/// Decodes a 0x0102 player data system message image.
///
/// @param image Message image.
/// @param size Image length in bytes.
/// @param[out] out View whose data points into image; written only on success.
/// @return False for a short image, another message type, another player type, or data outside the image.
[[nodiscard]] bool
decode_player_data_image(const uint8_t* image, std::size_t size, PlayerDataView* out) noexcept;

/// Decoded view of a 0x0103 player name image (the names point into the caller's buffer).
struct PlayerNameView {
    uint32_t id{};
    const char* short_name{}; ///< null when the player has none
    const char* long_name{};  ///< null when the player has none
};

/// Decodes a 0x0103 player name system message image.
///
/// @param image Message image.
/// @param size Image length in bytes.
/// @param[out] out View whose names point into image; written only on success.
/// @return False for a short image, another message type, another player type, or a name outside the image.
[[nodiscard]] bool
decode_player_name_image(const uint8_t* image, std::size_t size, PlayerNameView* out) noexcept;

enum class EngineState : uint8_t { idle, joining, open_host, open_client };

struct EnginePlayer {
    bool used{};
    bool local{};
    bool reserved{}; // host: id issued, player not yet announced
    uint32_t reserved_at{};
    /// Remote system player: messages heard from its machine since the ping
    /// timer last elapsed.
    uint32_t chatter_count{};
    /// Remote system player: pings sent to its machine since it was last heard from.
    uint8_t unanswered_pings{};
    PlayerInfo info{};
};

struct SessionEntry {
    Guid instance{};
    SessionDesc desc{};
    char name[max_name_chars + 1]{};
    Address host{}; // name server's stream endpoint
};

struct QueuedMessage {
    uint32_t from_id{};
    uint32_t to_id{};
    uint32_t offset{};
    uint32_t size{};
};

struct Engine {
    EngineConfig config{};
    EngineIo io{};
    EngineState state{EngineState::idle};
    bool session_lost{};
    uint32_t rng{};

    SessionDesc desc{};
    char session_name[max_name_chars + 1]{};
    char password[max_name_chars + 1]{};
    /// Session descriptions published again leave their password field 0
    /// (network.session-desc-clear).
    bool publish_without_password{};
    uint32_t id_key{};
    uint16_t uniqueness[max_players]{};
    uint32_t system_id{};
    uint32_t name_server_id{}; ///< 0 while a new name server is awaited
    Address name_server{};
    /// The name server left a migrate-host session and another machine has
    /// yet to announce itself in its place.
    bool awaiting_name_server{};
    bool ping_timer_running{};
    uint32_t ping_due_at{}; ///< when the ping timer next elapses, in milliseconds
    EnginePlayer players[max_players]{};

    Guid enum_application{};
    SessionEntry sessions[max_sessions]{};
    uint32_t session_count{};

    // Outstanding id request (join's system player or a local player).
    bool request_pending{};
    bool request_is_join{};
    uint32_t request_started{};
    uint32_t request_result{};
    uint32_t request_id{};
    PlayerInfo request_player{};
    bool joined_roster_pending{};

    QueuedMessage queue[receive_queue_entries]{};
    uint32_t queue_head{};
    uint32_t queue_count{};
    uint32_t bytes_start{};
    uint32_t bytes_end{};
    uint32_t dropped_messages{};
    uint8_t queue_bytes[receive_queue_bytes]{};

    Message message{};
    uint8_t scratch[max_message_bytes]{};
};

/// Clears an engine and binds its configuration and I/O callbacks; it starts idle.
///
/// @param[out] engine Engine to initialise.
/// @param config Advertised endpoints, enumeration port, random seed and request timeout.
/// @param io Stream and datagram send callbacks.
void engine_init(Engine* engine, const EngineConfig& config, const EngineIo& io) noexcept;

/// Creates a session as name server, with the system player in slot 0.
///
/// The instance GUID and id key are generated; a password sets the
/// password-required flag.
///
/// @param[in,out] engine Idle engine.
/// @param desc Flags, application GUID, player limit and user dwords.
/// @param name Session name.
/// @param password Session password; empty for none.
/// @param now Current time in milliseconds (unused).
/// @return ok, or already_initialized when the engine is not idle.
[[nodiscard]] uint32_t engine_host(
    Engine* engine, const SessionDesc& desc, const char* name, const char* password, uint32_t now
) noexcept;

/// Starts an enumeration: clears the session list and sends one request on the enumeration port.
///
/// @param[in,out] engine Engine whose session list is refilled by the replies.
/// @param application Application GUID to enumerate.
/// @param target_ip Destination address; 255.255.255.255 broadcasts.
/// @param now Current time in milliseconds (unused).
/// @return ok, or no_connection when the request cannot be sent.
[[nodiscard]] uint32_t engine_enum_sessions(
    Engine* engine, const Guid& application, const uint8_t target_ip[4], uint32_t now
) noexcept;

/// Starts joining an enumerated session by asking its name server for a system player id.
///
/// Once the id arrives, the AddForwardRequest that follows ends with the
/// session description's first reserved dword as the enumeration reply gave
/// it. Poll engine_join_status for the outcome.
///
/// @param[in,out] engine Idle engine.
/// @param instance Instance GUID of an enumerated session.
/// @param password Password to present, or null.
/// @param now Current time in milliseconds; the request times out from here.
/// @return ok while the join proceeds; already_initialized; no_sessions for an unknown instance;
///         no_connection when the request cannot be sent.
[[nodiscard]] uint32_t
engine_join(Engine* engine, const Guid& instance, const char* password, uint32_t now) noexcept;

/// Returns the state of a join.
///
/// @param engine Engine joining.
/// @return connecting while pending, then ok or the failure (generic when none was recorded).
[[nodiscard]] uint32_t engine_join_status(const Engine* engine) noexcept;

/// Creates a local player.
///
/// The host completes at once and announces the player; a peer asks the
/// name server for an id (poll engine_create_player_status).
///
/// @param[in,out] engine Open engine.
/// @param short_name Short name, or null.
/// @param long_name Long name, or null.
/// @param data Player data block; may be null only when data_size is 0.
/// @param data_size Data length in bytes, at most max_player_data_bytes.
/// @param now Current time in milliseconds; a peer's request times out from here.
/// @param[out] id New player id on the host, when not null.
/// @return ok on the host; connecting on a peer or while another request is pending; no_connection;
///         invalid_params; cant_create_player when the host is full.
[[nodiscard]] uint32_t engine_create_player(
    Engine* engine,
    const char* short_name,
    const char* long_name,
    const uint8_t* data,
    std::size_t data_size,
    uint32_t now,
    uint32_t* id
) noexcept;

/// Returns the state of the last player creation.
///
/// @param engine Engine creating the player.
/// @param[out] id The new player id once finished, when not null.
/// @return connecting while pending, then the request's result.
[[nodiscard]] uint32_t engine_create_player_status(const Engine* engine, uint32_t* id) noexcept;

/// Destroys a local non-system player and tells every other system.
///
/// @param[in,out] engine Open engine.
/// @param id Player id.
/// @return ok, or invalid_player when the engine is closed or the player is not a local application player.
[[nodiscard]] uint32_t engine_destroy_player(Engine* engine, uint32_t id) noexcept;

/// Renames a local non-system player and tells every other machine.
///
/// This machine's own players are not told, as they are not told of a
/// player created here either.
///
/// @param[in,out] engine Open engine.
/// @param id Player id.
/// @param short_name New short name, or null for none.
/// @param long_name New long name, or null for none.
/// @return ok; no_connection when the engine is closed; invalid_player when the player is not a local
///         application player; generic when the message cannot be built.
[[nodiscard]] uint32_t engine_set_player_name(
    Engine* engine, uint32_t id, const char* short_name, const char* long_name
) noexcept;

/// Replaces a local non-system player's data and tells every other machine.
///
/// Nothing is sent when the session has the no-data-messages flag. This
/// machine's own players are not told.
///
/// @param[in,out] engine Open engine.
/// @param id Player id.
/// @param data New data; may be null only when size is 0.
/// @param size Data length in bytes, at most max_player_data_bytes.
/// @return ok; no_connection when the engine is closed; invalid_player when the player is not a local
///         application player; invalid_params; generic when the message cannot be built.
[[nodiscard]] uint32_t
engine_set_player_data(Engine* engine, uint32_t id, const uint8_t* data, std::size_t size) noexcept;

/// Replaces the session description and broadcasts the change; name server only.
///
/// Instance, application, key and player count are kept from the engine's
/// own description; a password sets the password-required flag. In the
/// broadcast SessionDescChanged the description's name and password fields
/// hold the strings' offsets, never 0.
///
/// @param[in,out] engine Hosting engine.
/// @param desc New description.
/// @param name New session name.
/// @param password New session password; empty for none.
/// @return ok; access_denied when not hosting; generic when the message cannot be built.
[[nodiscard]] uint32_t engine_set_session_desc(
    Engine* engine, const SessionDesc& desc, const char* name, const char* password
) noexcept;

/// Sends application data from a local player, guaranteed over streams or as datagrams.
///
/// A broadcast goes to every remote system and is also queued for the other
/// local players; data for a local player is queued directly.
///
/// @param[in,out] engine Open engine.
/// @param from_id Local sending player.
/// @param to_id Destination player, or all_players_id.
/// @param flags Send flags; send_guaranteed selects the stream.
/// @param data Payload; may be null only when size is 0.
/// @param size Payload length in bytes.
/// @return ok; no_connection; invalid_player; invalid_params; send_too_big for a datagram over the limit;
///         out_of_memory when the local queue is full; generic when a send failed.
[[nodiscard]] uint32_t engine_send(
    Engine* engine,
    uint32_t from_id,
    uint32_t to_id,
    uint32_t flags,
    const uint8_t* data,
    std::size_t size
) noexcept;

/// Receives the oldest queued message, from any sender to any local player.
///
/// @param[in,out] engine Engine whose queue is read.
/// @param[out] from_id Sender id (0 for a system message), when not null.
/// @param[out] to_id Addressee id, when not null.
/// @param[out] buffer Destination; may be null only for an empty message.
/// @param[in,out] size Capacity on entry; message length, or the length needed, on return.
/// @return ok; no_messages; buffer_too_small, leaving the message queued; invalid_params for a null size.
[[nodiscard]] uint32_t engine_receive(
    Engine* engine, uint32_t* from_id, uint32_t* to_id, uint8_t* buffer, uint32_t* size
) noexcept;

/// Announces the departure of every local player, application players first, and returns to idle.
///
/// The configuration, I/O callbacks and random state are kept.
///
/// @param[in,out] engine Engine to close.
void engine_close(Engine* engine) noexcept;

/// Feeds one complete stream message: a control message or guaranteed application data.
///
/// @param[in,out] engine Engine to update.
/// @param source Peer address the stream connects from.
/// @param bytes Message bytes.
/// @param size Message length in bytes.
/// @param now Current time in milliseconds.
void engine_on_stream(
    Engine* engine, const Address& source, const uint8_t* bytes, std::size_t size, uint32_t now
) noexcept;

/// Feeds one datagram from the enumeration or data port: an enveloped control message or application data.
///
/// Some recorded datagrams carry two leading bytes before the ids; either
/// form is accepted when the sender id is known.
///
/// @param[in,out] engine Engine to update.
/// @param source Sender address.
/// @param bytes Datagram bytes.
/// @param size Datagram length in bytes.
/// @param now Current time in milliseconds.
void engine_on_datagram(
    Engine* engine, const Address& source, const uint8_t* bytes, std::size_t size, uint32_t now
) noexcept;

/// Expires a pending request after request_timeout_ms and, on the host, id reservations after 60 s, and runs
/// the ping timer.
///
/// The ping timer runs while the session has the keep-alive flag, or while
/// a new name server is awaited, and elapses every ping_period_ms. Each time
/// it elapses, every machine it watches that was not heard from since the
/// last time is pinged: the name server watches every other machine, any
/// other machine watches the name server alone, or every other machine
/// while a new name server is awaited. A machine that is due its ninth
/// unanswered ping is dropped instead, with its players, as if it had
/// left.
///
/// @param[in,out] engine Engine to update.
/// @param now Current time in milliseconds.
void engine_poll(Engine* engine, uint32_t now) noexcept;

/// Finds a player by id.
///
/// @param engine Engine holding the players.
/// @param id Player id.
/// @return The player, or null.
[[nodiscard]] const EnginePlayer* engine_find_player(const Engine* engine, uint32_t id) noexcept;

/// Counts the non-system players, local and remote.
///
/// @param engine Engine holding the players.
/// @return The count.
[[nodiscard]] uint32_t engine_player_count(const Engine* engine) noexcept;

} // namespace oa::netgame::dplay
