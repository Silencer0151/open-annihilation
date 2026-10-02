// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Network boundary behind the multiplayer screens.
//
// The screens never touch a transport. Provider enumeration, session
// enumeration, host/join and the lobby records (0x05 chat, 0x08 start,
// 0x1a unit-content handshake, 0x1b reject, 0x20 player info) pass through a
// LobbyNet table of function pointers. The integrator binds it to the
// DirectPlay session layer; `loopback_lobby_net` is the offline default.
//
// Records are queued and go out when the battle room flushes, as 3.1c
// batches them: each leaves from the player it concerns (the local player
// or one of this machine's computer players), so every machine hears from
// every seated player.
#pragma once

#include "oa/core/player.h"

#include <cstddef>
#include <cstdint>

namespace oa::ui::frontend_multiplayer {

inline constexpr std::size_t kProviderNameBytes = 64;
inline constexpr std::size_t kSessionNameBytes = 32; // 16-byte game name + 16-byte map name
inline constexpr std::size_t kSessionGameNameBytes = 0x10;
inline constexpr std::size_t kSessionMapNameBytes = 0xf;
inline constexpr std::size_t kSessionUserBytes = 16; // DPSESSIONDESC2 dwUser1..4
inline constexpr std::size_t kMaxProviders = 10;     // connections the provider list holds
inline constexpr std::size_t kMaxSessions = 20;      // sessions the game list holds
inline constexpr std::size_t kPlayerInfoBytes = 0xb9;
inline constexpr std::size_t kChatTextBytes = 64;
inline constexpr std::size_t kLobbyRecordBytes = 0xba; // largest lobby record (0x20)

// Connection kinds matched against the stored provider GUID.
enum class ProviderKind : uint8_t { modem = 0, tcpip = 1, ipx = 2, serial = 3, other = 4 };

struct Provider {
    char name[kProviderNameBytes]{};
    uint8_t guid[16]{};
    ProviderKind kind{};
};

// One enumerated session. `user` carries the host's PlayerSetupInfo from
// memory_mb through version_minor (DPSESSIONDESC2 dwUser1..4).
struct SessionEntry {
    uint8_t instance_guid[16]{};
    char name[kSessionNameBytes]{}; // game name padded to 16, then the map name
    uint32_t max_players{};
    uint32_t current_players{};
    uint32_t flags{}; // DPSESSIONDESC2 dwFlags
    uint8_t user[16]{};
};

inline constexpr std::size_t kSessionUserInfoOffset = 0x99; // info bytes carried in `user`

// players is the battle room's table (Game.players), which the frame
// receiver reads for the whole session.
struct HostRequest {
    const char* game_name{};
    const char* nickname{};
    const char* password{}; // empty = none
    uint32_t max_players{};
    const Player* players{};
    /// The session's name as it is first published: the game name, then the
    /// map name, space padded to 31 characters; null creates it with
    /// game_name.
    const char* session_name{};
    /// The session's first user bytes (the host's PlayerSetupInfo from
    /// memory_mb through version_minor); null creates it with zeros.
    const uint8_t* user{};
};

struct JoinRequest {
    const SessionEntry* session{};
    const char* nickname{};
    const char* password{};
    bool watch{};
    const Player* players{};
};

enum class LobbyEventKind : uint8_t {
    none,
    player_joined, // a remote player appeared (player id, name)
    player_left,   // player id
    record,        // one game record from player id (0x05, 0x08, 0x1a, 0x1b, 0x20 ...)
    session_lost,
};

struct LobbyEvent {
    LobbyEventKind kind{};
    uint32_t player_id{};
    char name[32]{};
    /// player_joined on the host: the RejectReason the joiner is refused
    /// with (3 closed, 4 wrong password, 8 version), 0 when it may stay.
    /// A refused joiner stays in the session until it leaves by itself.
    uint8_t refuse_reason{};
    uint16_t size{};
    uint8_t data[kLobbyRecordBytes]{};
};

// Result codes for host/join.
enum class LobbyResult : int32_t {
    ok = 0,
    failed = 1,
    wrong_password = 4,
    game_full = 5,
    version_mismatch = 8,
};

struct LobbyNet {
    void* context{};
    // Fills up to `capacity` providers; returns the count.
    int32_t (*providers)(void* context, Provider* out, int32_t capacity){};
    // Selects the provider and its address (TCP/IP host or empty to search).
    bool (*open)(void* context, const Provider* provider, const char* address){};
    // Session list poll: count (>= 0), or -1 on transport failure.
    int32_t (*enumerate)(void* context, SessionEntry* out, int32_t capacity){};
    LobbyResult (*host)(void* context, const HostRequest* request, uint32_t* local_id){};
    LobbyResult (*join)(void* context, const JoinRequest* request, uint32_t* local_id){};
    // Queues one record from the local player; to_id 0 broadcasts. flush
    // sends it.
    void (*send)(void* context, uint32_t to_id, const uint8_t* data, std::size_t size){};
    // Pops the next event; false when none is queued.
    bool (*receive)(void* context, LobbyEvent* out){};
    // Publishes the host's session name (game name, then map name, space
    // padded to 31 characters), user bytes (PlayerSetupInfo from memory_mb
    // through version_minor), flags and player limit. Once published, flag
    // 0x20 (the game started) stays set.
    void (*describe)(
        void* context,
        const char* name,
        const uint8_t user[16],
        uint32_t flags,
        uint32_t max_players
    ){};
    // Leaves the current session; the provider stays open for the game list.
    void (*leave)(void* context){};
    // Releases the provider.
    void (*close)(void* context){};
    // Creates another session player on this machine (a computer player)
    // with `tag` in its join block; its id comes back in *id.
    LobbyResult (*add_player)(void* context, const char* name, const char* tag, uint32_t* id){};
    // Destroys a session player this machine created.
    void (*remove_player)(void* context, uint32_t id){};
    /// Queues one record from a player this machine created: the local
    /// player or one of its computer players. An id this machine did not
    /// create sends from the local player instead. to_id 0 broadcasts. An
    /// unguaranteed record goes out at once in a frame of its own without
    /// guaranteed delivery, after everything queued before it, as 3.1c
    /// sends a ping.
    void (*send_from)(
        void* context,
        uint32_t from_id,
        uint32_t to_id,
        const uint8_t* data,
        std::size_t size,
        bool unguaranteed
    ){};
    /// Sends every queued record now.
    void (*flush)(void* context){};
};

inline constexpr int32_t kLoopbackSentRecords = 64; // records a loopback keeps

// Offline loopback: the four stock providers, sessions created by `host` on
// the same object listed by `enumerate`, sends recorded, events injected.
struct LoopbackNet {
    Provider provider_table[4];
    int32_t provider_count{};
    SessionEntry sessions[kMaxSessions];
    int32_t session_count{};
    bool opened{};
    char address[128]{};
    uint32_t next_player_id{};
    uint32_t local_id{}; // the player host or join created; send's sender
    LobbyEvent queue[32];
    int32_t queue_head{};  // index of the oldest queued event
    int32_t queue_count{}; // events queued
    // Every sent record in order (bounded; oldest dropped).
    uint8_t sent[kLoopbackSentRecords][kLobbyRecordBytes]{};
    uint16_t sent_size[kLoopbackSentRecords]{};
    uint32_t sent_to[kLoopbackSentRecords]{};
    uint32_t sent_from[kLoopbackSentRecords]{};
    bool sent_unguaranteed[kLoopbackSentRecords]{};
    // flush_count when the record was queued: records with the same value
    // went out together.
    int32_t sent_flush[kLoopbackSentRecords]{};
    int32_t sent_count{};
    int32_t flush_count{};     // flush calls so far; an unguaranteed send flushes twice
    int32_t hosted{};          // index of the session this side created, -1 none
    uint32_t removed_player{}; // last id remove_player destroyed
};

/// Clears the loopback and lists the four stock providers (IPX, TCP/IP, modem, serial).
///
/// @param[out] loopback Loopback to reset; no session is hosted.
void loopback_reset(LoopbackNet& loopback) noexcept;

/// Returns the LobbyNet table over a loopback.
///
/// @param loopback Loopback passed back to every entry as its context.
/// @return The table.
[[nodiscard]] LobbyNet loopback_lobby_net(LoopbackNet& loopback) noexcept;

/// Queues an event for the next `receive`.
///
/// @param[in,out] loopback Loopback whose 32-event queue grows.
/// @param event Event to queue.
/// @return False when the queue is full.
bool loopback_inject(LoopbackNet& loopback, const LobbyEvent& event) noexcept;

/// Adds a remote session to the list returned by `enumerate`.
///
/// @param[in,out] loopback Loopback whose session list grows.
/// @param session Session to list.
/// @return False when 20 sessions are already listed.
bool loopback_add_session(LoopbackNet& loopback, const SessionEntry& session) noexcept;

// DirectPlay service-provider GUIDs in memory order.
extern const uint8_t kProviderGuidIpx[16];
extern const uint8_t kProviderGuidTcpip[16];
extern const uint8_t kProviderGuidModem[16];
extern const uint8_t kProviderGuidSerial[16];

} // namespace oa::ui::frontend_multiplayer
