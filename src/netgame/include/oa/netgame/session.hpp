// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Total Annihilation's multiplayer session layer over the DirectPlay engine:
// initialise, enumerate, create, join, add/remove player, send/receive and
// update the session description, with the game's flag values, limits and
// HRESULT-driven results. The engine is driven through a backend so the
// same code runs on sockets or in-memory.

#include "oa/netgame/dplay/engine.hpp"

#include <cstdint>

namespace oa::netgame::session {

using dplay::Guid;

struct Backend {
    dplay::Engine* engine{};
    void* context{};
    // Service I/O for at most wait_ms.
    void (*pump)(void* context, uint32_t wait_ms){};
    uint32_t (*now_ms)(void* context){};
    // Start answering enumeration requests (called on create).
    bool (*listen_enumeration)(void* context){};
    // Where enumeration requests go (broadcast unless an address was given).
    uint8_t enum_target[4]{255, 255, 255, 255};
};

inline constexpr uint32_t default_max_players = 0x10;
inline constexpr uint32_t default_enum_timeout_ms = 0x5dc;
inline constexpr std::size_t session_name_bytes = 0x11;
inline constexpr std::size_t player_name_bytes = 0x10;
inline constexpr std::size_t game_entry_name_bytes = 0x20;
inline constexpr std::size_t max_game_entries = dplay::max_sessions;

// One enumerated game, as the enumeration callback records it.
struct GameEntry {
    uint32_t user[4]{};
    uint32_t max_players{};
    char session_name[game_entry_name_bytes]{}; // longer names are cut to fit
    Guid instance{};
};

struct Session {
    Backend backend{};
    bool initialized{};
    char session_name[session_name_bytes]{};
    char player_long_name[player_name_bytes + 1]{};
    char player_short_name[player_name_bytes + 1]{};
    Guid application{};
    SessionDesc desc{};
    uint32_t recv_from{};
    uint32_t recv_to{};
    uint32_t max_players{default_max_players};
    uint32_t enum_timeout_ms{default_enum_timeout_ms};
    uint32_t session_count{};
    uint32_t last_join_result{};
    bool guaranteed{};
};

/// Binds a session to its backend and records the application GUID, with guaranteed delivery off.
///
/// @param[out] session Session to initialise; any previous state is dropped.
/// @param backend Engine and I/O callbacks; the session is initialised only when it has an engine.
/// @param application Application GUID sessions are enumerated and created under.
void session_init_multiplay(
    Session* session, const Backend& backend, const Guid& application
) noexcept;

/// Restores the default player limit (16) and enumeration timeout (1500 ms).
///
/// @param[in,out] session Session to reset.
void session_init_defaults(Session* session) noexcept;

/// Closes the engine when connected and marks the session uninitialised.
///
/// @param[in,out] session Session to release.
void session_uninit(Session* session) noexcept;

/// Enumerates games for enum_timeout_ms and lists what was found.
///
/// @param[in,out] session Connected session; its session_count is set to the entries written.
/// @param[out] entries Found games; names are cut to 31 characters.
/// @param capacity Number of entries available.
/// @return The number of entries written, or -1 without a connection or when enumeration fails.
[[nodiscard]] int32_t
session_get_games(Session* session, GameEntry* entries, std::size_t capacity) noexcept;

/// Creates and hosts a game with flags 4, the session's player limit and the given user dwords.
///
/// @param[in,out] session Connected session; its name (16 characters) and description are replaced.
/// @param name Session name.
/// @param password Session password; empty for none.
/// @param user1 First user dword of the description.
/// @param user2 Second user dword.
/// @param user3 Third user dword.
/// @param user4 Fourth user dword.
/// @return False without a connection, when enumeration answering cannot start or hosting fails.
[[nodiscard]] bool session_create_game(
    Session* session,
    const char* name,
    const char* password,
    uint32_t user1,
    uint32_t user2,
    uint32_t user3,
    uint32_t user4
) noexcept;

/// Joins an enumerated game by instance, waiting for the answer, and copies its name (16 characters).
///
/// @param[in,out] session Connected session; last_join_result records the outcome.
/// @param instance Instance GUID of the game.
/// @return True when joined.
[[nodiscard]] bool session_join_game(Session* session, const Guid& instance) noexcept;

/// Returns the result of the last join.
///
/// @param session Session to query.
/// @return The last join's HRESULT; DP_OK after a success.
[[nodiscard]] uint32_t session_last_join_result(const Session* session) noexcept;

/// Creates the local player with the 21-byte join block, waiting for the host when needed.
///
/// @param[in,out] session Connected session; its player names are recorded (16 characters each).
/// @param[out] id Transport id of the new player, when not null.
/// @param short_name Player short name.
/// @param long_name Player long name.
/// @param tag Join tag (the password presented), at most 16 characters.
/// @param version_low CreatePlayerData.version_low; the host requires 0
/// @param version_high CreatePlayerData.version_high; the host requires 0x50
/// @return True when the player was created.
[[nodiscard]] bool session_add_player(
    Session* session,
    uint32_t* id,
    const char* short_name,
    const char* long_name,
    const char* tag,
    uint16_t version_low,
    uint16_t version_high
) noexcept;

/// Destroys a session player.
///
/// @param[in,out] session Connected session.
/// @param id Transport id of the player.
/// @return The engine's result; uninitialized without a connection.
[[nodiscard]] uint32_t session_remove_player(Session* session, uint32_t id) noexcept;

/// Copies the short and long names of a session player, each cut to capacity - 1 characters.
///
/// @param session Connected session.
/// @param id Transport id of the player.
/// @param[out] short_name Short name, terminated.
/// @param[out] long_name Long name, terminated.
/// @param capacity Bytes available in each name buffer.
/// @return ok; uninitialized without a session; invalid_player for an unknown id; buffer_too_small for
///         capacity 0.
[[nodiscard]] uint32_t session_get_player_name(
    const Session* session, uint32_t id, char* short_name, char* long_name, size_t capacity
) noexcept;

inline constexpr uint32_t computer_player_id = 0xffffffffu;

/// Returns the names a seated slot takes: "COMPUTER" twice for the computer id, else the session's names.
///
/// @param session Session holding the player.
/// @param id Transport id, or computer_player_id.
/// @param[out] short_name Short name.
/// @param[out] long_name Long name.
/// @param capacity Bytes available in each name buffer.
/// @return False when the session has no such player or capacity is 0.
[[nodiscard]] bool session_player_names(
    const Session* session, uint32_t id, char* short_name, char* long_name, size_t capacity
) noexcept;

/// Calls visit for every session player that is neither a system player nor created here.
///
/// @param session Connected session.
/// @param visit Callback receiving each player id; null visits nothing.
/// @param context Passed to visit.
/// @return ok, or uninitialized without a session.
uint32_t session_enum_players(
    const Session* session, void (*visit)(void* context, uint32_t id), void* context
) noexcept;

/// Leaves the game: closes the engine and services it once.
///
/// @param[in,out] session Session to leave.
/// @return True, also when the session was closed or never open.
bool session_quit_game(Session* session) noexcept;

/// Publishes the session description with a new name; only the name server does this.
///
/// @param[in,out] session Connected hosting session; its name takes 16 characters of name.
/// @param name New session name.
/// @return The engine's result; no_connection without a connection.
[[nodiscard]] uint32_t session_update_game_info(Session* session, const char* name) noexcept;

/// Tells whether the session requires a password.
///
/// @param session Session to test; the engine's description when connected, else the local one.
/// @return True when the password-required flag (0x400) is set.
[[nodiscard]] bool session_password_required(const Session* session) noexcept;

/// Turns guaranteed delivery on or off for session_send.
///
/// @param[in,out] session Session to change.
/// @param guaranteed New setting.
/// @return The previous setting.
bool session_set_guaranteed(Session* session, bool guaranteed) noexcept;

/// Sends one datagram with the session's delivery setting.
///
/// @param[in,out] session Connected session.
/// @param from Transport id of the sending local player.
/// @param to Destination transport id; 0 broadcasts.
/// @param data Datagram bytes.
/// @param size Datagram length in bytes.
/// @return The engine's result; no_connection without a connection.
[[nodiscard]] uint32_t session_send(
    Session* session, uint32_t from, uint32_t to, const uint8_t* data, uint32_t size
) noexcept;

/// Services the engine once and receives one message, storing its sender and addressee in recv_from/recv_to.
///
/// @param[in,out] session Connected session.
/// @param[out] buffer Destination of the message.
/// @param[in,out] size Capacity on entry; message length, or the length needed, on return.
/// @return The engine's result; no_connection without a connection.
[[nodiscard]] uint32_t session_receive(Session* session, uint8_t* buffer, uint32_t* size) noexcept;

/// Returns a NetTransport over a session; its send passes the caller's flags through.
///
/// @param session Session passed back to the transport as its context.
/// @return The transport.
[[nodiscard]] NetTransport session_transport(Session* session) noexcept;

/// Gives the host-side verdict on a player-created system message, as the packet pump applies it.
///
/// @param image The system message.
/// @param size Message length in bytes.
/// @param game_closed Whether the host has closed the game.
/// @param password_required Whether a password is required.
/// @param expected_tag The password joiners must present.
/// @return 0 when accepted; 3 game closed; 4 wrong password; 8 version mismatch (also for a malformed message).
[[nodiscard]] uint8_t session_join_reject_reason(
    const uint8_t* image,
    std::size_t size,
    bool game_closed,
    bool password_required,
    const char* expected_tag
) noexcept;

} // namespace oa::netgame::session
