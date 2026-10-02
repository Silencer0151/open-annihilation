// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Offline loopback binding of the lobby network boundary.
#include "oa/ui/frontend_multiplayer/lobby_net.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace oa::ui::frontend_multiplayer {

const uint8_t kProviderGuidIpx[16] = {
    0x00, 0xc4, 0x5b, 0x68, 0x2c, 0x9d, 0xcf, 0x11, 0xa9, 0xcd, 0x00, 0xaa, 0x00, 0x68, 0x86, 0xe3
};
const uint8_t kProviderGuidTcpip[16] = {
    0xe0, 0x5e, 0xe9, 0x36, 0x77, 0x85, 0xcf, 0x11, 0x96, 0x0c, 0x00, 0x80, 0xc7, 0x53, 0x4e, 0x82
};
const uint8_t kProviderGuidModem[16] = {
    0x60, 0xa7, 0xea, 0x44, 0x68, 0xcb, 0xcf, 0x11, 0x9c, 0x4e, 0x00, 0xa0, 0xc9, 0x05, 0x42, 0x5e
};
const uint8_t kProviderGuidSerial[16] = {
    0x60, 0x68, 0x1d, 0x0f, 0xd9, 0x88, 0xcf, 0x11, 0x9c, 0x4e, 0x00, 0xa0, 0xc9, 0x05, 0x42, 0x5e
};

namespace {

constexpr uint32_t kFirstLoopbackPlayerId = 0x100;
constexpr uint32_t kSessionFlagStarted = 0x20; // stays set once published

LoopbackNet& self(void* context) {
    return *static_cast<LoopbackNet*>(context);
}

void add_provider(
    LoopbackNet& loopback, const char* name, const uint8_t (&guid)[16], ProviderKind kind
) {
    auto& provider = loopback.provider_table[loopback.provider_count++];
    std::snprintf(provider.name, sizeof(provider.name), "%s", name);
    std::memcpy(provider.guid, guid, sizeof(provider.guid));
    provider.kind = kind;
}

int32_t providers(void* context, Provider* out, int32_t capacity) {
    auto& loopback = self(context);
    const auto count = std::min(loopback.provider_count, capacity);
    for (int32_t index = 0; index < count; ++index)
        out[index] = loopback.provider_table[index];
    return count;
}

bool open(void* context, const Provider*, const char* address) {
    auto& loopback = self(context);
    loopback.opened = true;
    std::snprintf(
        loopback.address, sizeof(loopback.address), "%s", address != nullptr ? address : ""
    );
    return true;
}

int32_t enumerate(void* context, SessionEntry* out, int32_t capacity) {
    auto& loopback = self(context);
    if (!loopback.opened)
        return -1;
    const auto count = std::min(loopback.session_count, capacity);
    for (int32_t index = 0; index < count; ++index)
        out[index] = loopback.sessions[index];
    return count;
}

LobbyResult host(void* context, const HostRequest* request, uint32_t* local_id) {
    auto& loopback = self(context);
    if (request == nullptr || loopback.session_count >= static_cast<int32_t>(kMaxSessions))
        return LobbyResult::failed;
    SessionEntry session{};
    session.instance_guid[0] = static_cast<uint8_t>(loopback.session_count + 1);
    if (request->session_name != nullptr)
        std::snprintf(session.name, sizeof(session.name), "%s", request->session_name);
    else
        std::snprintf(session.name, sizeof(session.name), "%-16.16s", request->game_name);
    if (request->user != nullptr)
        std::memcpy(session.user, request->user, sizeof(session.user));
    session.max_players = request->max_players;
    session.current_players = 1;
    loopback.hosted = loopback.session_count;
    loopback.sessions[loopback.session_count++] = session;
    *local_id = loopback.next_player_id++;
    loopback.local_id = *local_id;
    return LobbyResult::ok;
}

LobbyResult join(void* context, const JoinRequest* request, uint32_t* local_id) {
    auto& loopback = self(context);
    if (request == nullptr || request->session == nullptr)
        return LobbyResult::failed;
    for (int32_t index = 0; index < loopback.session_count; ++index) {
        auto& session = loopback.sessions[index];
        if (std::memcmp(session.instance_guid, request->session->instance_guid, 16) != 0)
            continue;
        if (session.current_players >= session.max_players && session.max_players != 0)
            return LobbyResult::game_full;
        ++session.current_players;
        *local_id = loopback.next_player_id++;
        loopback.local_id = *local_id;
        return LobbyResult::ok;
    }
    return LobbyResult::failed;
}

/// Records one sent record, dropping the oldest once the table is full.
///
/// @param[in,out] loopback Loopback whose sent records grow.
/// @param from_id Sender id.
/// @param to_id Destination id; 0 broadcasts.
/// @param data Record bytes; ignored when null or empty.
/// @param size Record length in bytes.
/// @param unguaranteed Sent without guaranteed delivery.
void record_sent(
    LoopbackNet& loopback,
    uint32_t from_id,
    uint32_t to_id,
    const uint8_t* data,
    std::size_t size,
    bool unguaranteed
) {
    if (data == nullptr || size == 0)
        return;
    constexpr auto capacity = kLoopbackSentRecords;
    if (loopback.sent_count == capacity) {
        const auto shift = [](auto& table) {
            std::memmove(table, table + 1, sizeof(table[0]) * (capacity - 1));
        };
        shift(loopback.sent);
        shift(loopback.sent_size);
        shift(loopback.sent_to);
        shift(loopback.sent_from);
        shift(loopback.sent_unguaranteed);
        shift(loopback.sent_flush);
        --loopback.sent_count;
    }
    const auto length = std::min(size, kLobbyRecordBytes);
    const auto at = loopback.sent_count++;
    std::memcpy(loopback.sent[at], data, length);
    loopback.sent_size[at] = static_cast<uint16_t>(length);
    loopback.sent_to[at] = to_id;
    loopback.sent_from[at] = from_id;
    loopback.sent_unguaranteed[at] = unguaranteed;
    loopback.sent_flush[at] = loopback.flush_count;
}

void send(void* context, uint32_t to_id, const uint8_t* data, std::size_t size) {
    auto& loopback = self(context);
    record_sent(loopback, loopback.local_id, to_id, data, size, false);
}

// The loopback does not know which players this side created: every
// sender is recorded as given.
void send_from(
    void* context,
    uint32_t from_id,
    uint32_t to_id,
    const uint8_t* data,
    std::size_t size,
    bool unguaranteed
) {
    auto& loopback = self(context);
    if (!unguaranteed) {
        record_sent(loopback, from_id, to_id, data, size, false);
        return;
    }
    ++loopback.flush_count;
    record_sent(loopback, from_id, to_id, data, size, true);
    ++loopback.flush_count;
}

void flush(void* context) {
    ++self(context).flush_count;
}

bool receive(void* context, LobbyEvent* out) {
    auto& loopback = self(context);
    if (loopback.queue_count == 0 || out == nullptr)
        return false;
    *out = loopback.queue[loopback.queue_head];
    loopback.queue_head = (loopback.queue_head + 1) % 32;
    --loopback.queue_count;
    return true;
}

void describe(
    void* context, const char* name, const uint8_t user[16], uint32_t flags, uint32_t max_players
) {
    auto& loopback = self(context);
    if (loopback.hosted < 0 || loopback.hosted >= loopback.session_count)
        return;
    auto& session = loopback.sessions[loopback.hosted];
    std::snprintf(session.name, sizeof(session.name), "%s", name);
    std::memcpy(session.user, user, sizeof(session.user));
    session.flags = flags | (session.flags & kSessionFlagStarted);
    session.max_players = max_players;
}

void leave(void* context) {
    auto& loopback = self(context);
    loopback.hosted = -1;
    loopback.queue_head = 0;
    loopback.queue_count = 0;
}

void close(void* context) {
    auto& loopback = self(context);
    loopback.opened = false;
    loopback.queue_head = 0;
    loopback.queue_count = 0;
}

LobbyResult add_player(void* context, const char* name, const char*, uint32_t* id) {
    auto& loopback = self(context);
    if (!loopback.opened)
        return LobbyResult::failed;
    *id = loopback.next_player_id++;
    LobbyEvent event{};
    event.kind = LobbyEventKind::player_joined;
    event.player_id = *id;
    std::snprintf(event.name, sizeof(event.name), "%s", name);
    return loopback_inject(loopback, event) ? LobbyResult::ok : LobbyResult::failed;
}

void remove_player(void* context, uint32_t id) {
    self(context).removed_player = id;
}

} // namespace

void loopback_reset(LoopbackNet& loopback) noexcept {
    std::memset(&loopback, 0, sizeof(loopback));
    add_provider(loopback, "IPX Connection For DirectPlay", kProviderGuidIpx, ProviderKind::ipx);
    add_provider(
        loopback,
        "Internet TCP/IP Connection For DirectPlay",
        kProviderGuidTcpip,
        ProviderKind::tcpip
    );
    add_provider(
        loopback, "Modem Connection For DirectPlay", kProviderGuidModem, ProviderKind::modem
    );
    add_provider(
        loopback, "Serial Connection For DirectPlay", kProviderGuidSerial, ProviderKind::serial
    );
    loopback.next_player_id = kFirstLoopbackPlayerId;
    loopback.hosted = -1;
}

LobbyNet loopback_lobby_net(LoopbackNet& loopback) noexcept {
    return {
        &loopback,
        providers,
        open,
        enumerate,
        host,
        join,
        send,
        receive,
        describe,
        leave,
        close,
        add_player,
        remove_player,
        send_from,
        flush
    };
}

bool loopback_inject(LoopbackNet& loopback, const LobbyEvent& event) noexcept {
    if (loopback.queue_count == 32)
        return false;
    loopback.queue[(loopback.queue_head + loopback.queue_count) % 32] = event;
    ++loopback.queue_count;
    return true;
}

bool loopback_add_session(LoopbackNet& loopback, const SessionEntry& session) noexcept {
    if (loopback.session_count >= static_cast<int32_t>(kMaxSessions))
        return false;
    loopback.sessions[loopback.session_count++] = session;
    return true;
}

} // namespace oa::ui::frontend_multiplayer
