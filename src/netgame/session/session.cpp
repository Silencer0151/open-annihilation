// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/session.hpp"

#include <cstdint>
#include <cstring>

namespace oa::netgame::session {
namespace {

using namespace dplay;

constexpr uint32_t pump_slice_ms = 10;
constexpr uint8_t reject_game_closed = 3;
constexpr uint8_t reject_wrong_password = 4;
constexpr uint8_t reject_version_mismatch = 8;
constexpr const char* computer_player_name = "COMPUTER";

void copy_bounded(char* out, const char* in, std::size_t bound) {
    std::size_t n = 0;
    for (; in != nullptr && n < bound && in[n] != '\0'; ++n)
        out[n] = in[n];
    for (std::size_t i = n; i < bound; ++i)
        out[i] = '\0';
}

bool connected(const Session* s) {
    return s->initialized && s->backend.engine != nullptr;
}

uint32_t now(const Session* s) {
    return s->backend.now_ms != nullptr ? s->backend.now_ms(s->backend.context) : 0;
}

void pump(Session* s, uint32_t wait_ms) {
    if (s->backend.pump != nullptr)
        s->backend.pump(s->backend.context, wait_ms);
}

// Pump until status() stops returning `connecting`.
template <typename Status>
uint32_t wait_for(Session* s, Status status) {
    uint32_t r = status();
    while (r == result::connecting) {
        pump(s, pump_slice_ms);
        r = status();
    }
    return r;
}

void refresh_instance(Session* s) {
    std::memcpy(s->desc.instance_guid, s->backend.engine->desc.instance_guid, 16);
}

uint32_t transport_send(
    void* context, uint32_t from, uint32_t to, uint32_t flags, const uint8_t* data, uint32_t size
) {
    auto* s = static_cast<Session*>(context);
    if (!connected(s))
        return transport_result::no_connection;
    return engine_send(s->backend.engine, from, to, flags, data, size);
}

uint32_t
transport_receive(void* context, uint32_t* from, uint32_t* to, uint8_t* buffer, uint32_t* size) {
    auto* s = static_cast<Session*>(context);
    const uint32_t r = session_receive(s, buffer, size);
    if (from != nullptr)
        *from = s->recv_from;
    if (to != nullptr)
        *to = s->recv_to;
    return r;
}

} // namespace

void session_init_multiplay(Session* s, const Backend& backend, const Guid& application) noexcept {
    *s = Session{};
    s->backend = backend;
    s->application = application;
    std::memcpy(s->desc.application_guid, application.bytes, 16);
    s->guaranteed = false;
    s->initialized = backend.engine != nullptr;
}

void session_init_defaults(Session* s) noexcept {
    s->max_players = default_max_players;
    s->enum_timeout_ms = default_enum_timeout_ms;
}

void session_uninit(Session* s) noexcept {
    if (connected(s))
        engine_close(s->backend.engine);
    s->initialized = false;
}

int32_t session_get_games(Session* s, GameEntry* entries, std::size_t capacity) noexcept {
    s->session_count = 0;
    if (!connected(s))
        return -1;
    Engine* e = s->backend.engine;
    if (engine_enum_sessions(e, s->application, s->backend.enum_target, now(s)) != result::ok)
        return -1;
    const uint32_t start = now(s);
    while (now(s) - start < s->enum_timeout_ms)
        pump(s, pump_slice_ms);
    for (uint32_t i = 0; i < e->session_count && s->session_count < capacity; ++i) {
        const SessionEntry& found = e->sessions[i];
        GameEntry& out = entries[s->session_count++];
        out = GameEntry{};
        for (std::size_t u = 0; u < 4; ++u)
            out.user[u] = found.desc.user[u];
        out.max_players = found.desc.max_players;
        copy_bounded(out.session_name, found.name, game_entry_name_bytes - 1);
        out.instance = found.instance;
    }
    return static_cast<int32_t>(s->session_count);
}

bool session_create_game(
    Session* s,
    const char* name,
    const char* password,
    uint32_t user1,
    uint32_t user2,
    uint32_t user3,
    uint32_t user4
) noexcept {
    if (!connected(s))
        return false;
    copy_bounded(s->session_name, name, session_name_bytes - 1);
    s->desc = SessionDesc{};
    s->desc.size = static_cast<uint32_t>(session_desc_bytes);
    s->desc.flags = session_flag_game;
    std::memcpy(s->desc.application_guid, s->application.bytes, 16);
    s->desc.max_players = s->max_players;
    s->desc.user[0] = user1;
    s->desc.user[1] = user2;
    s->desc.user[2] = user3;
    s->desc.user[3] = user4;
    if (s->backend.listen_enumeration != nullptr &&
        !s->backend.listen_enumeration(s->backend.context))
        return false;
    if (engine_host(s->backend.engine, s->desc, name, password, now(s)) != result::ok)
        return false;
    refresh_instance(s);
    return true;
}

bool session_join_game(Session* s, const Guid& instance) noexcept {
    s->last_join_result = result::no_connection;
    if (!connected(s))
        return false;
    s->desc = SessionDesc{};
    s->desc.size = static_cast<uint32_t>(session_desc_bytes);
    s->desc.flags = session_flag_game;
    std::memcpy(s->desc.application_guid, s->application.bytes, 16);
    std::memcpy(s->desc.instance_guid, instance.bytes, 16);
    Engine* e = s->backend.engine;
    uint32_t r = engine_join(e, instance, nullptr, now(s));
    if (r == result::ok)
        r = wait_for(s, [e] { return engine_join_status(e); });
    s->last_join_result = r;
    if (r != result::ok)
        return false;
    refresh_instance(s);
    copy_bounded(s->session_name, e->session_name, session_name_bytes - 1);
    return true;
}

uint32_t session_last_join_result(const Session* s) noexcept {
    return s->last_join_result;
}

bool session_add_player(
    Session* s,
    uint32_t* id,
    const char* short_name,
    const char* long_name,
    const char* tag,
    uint16_t version_low,
    uint16_t version_high
) noexcept {
    if (!connected(s))
        return false;
    copy_bounded(s->player_long_name, long_name, player_name_bytes);
    copy_bounded(s->player_short_name, short_name, player_name_bytes);
    CreatePlayerData block{};
    copy_bounded(block.tag, tag, sizeof block.tag - 1);
    block.version_low = version_low;
    block.version_high = version_high;
    uint8_t data[create_player_data_bytes]{};
    if (encode_create_player_data(block, data, sizeof data) != WireError::ok)
        return false;
    Engine* e = s->backend.engine;
    uint32_t new_id = 0;
    uint32_t r = engine_create_player(e, short_name, long_name, data, sizeof data, now(s), &new_id);
    if (r == result::connecting)
        r = wait_for(s, [e, &new_id] { return engine_create_player_status(e, &new_id); });
    if (r != result::ok)
        return false;
    if (id != nullptr)
        *id = new_id;
    return true;
}

uint32_t session_remove_player(Session* s, uint32_t id) noexcept {
    if (!connected(s))
        return result::uninitialized;
    return engine_destroy_player(s->backend.engine, id);
}

uint32_t session_get_player_name(
    const Session* s, uint32_t id, char* short_name, char* long_name, size_t capacity
) noexcept {
    if (!connected(s))
        return result::uninitialized;
    const EnginePlayer* player = engine_find_player(s->backend.engine, id);
    if (player == nullptr)
        return result::invalid_player;
    if (capacity == 0)
        return result::buffer_too_small;
    copy_bounded(short_name, player->info.short_name, capacity - 1);
    short_name[capacity - 1] = '\0';
    copy_bounded(long_name, player->info.long_name, capacity - 1);
    long_name[capacity - 1] = '\0';
    return result::ok;
}

bool session_player_names(
    const Session* s, uint32_t id, char* short_name, char* long_name, size_t capacity
) noexcept {
    if (id == computer_player_id) {
        copy_bounded(short_name, computer_player_name, capacity);
        copy_bounded(long_name, computer_player_name, capacity);
        return capacity != 0;
    }
    return session_get_player_name(s, id, short_name, long_name, capacity) == result::ok;
}

uint32_t session_enum_players(
    const Session* s, void (*visit)(void* context, uint32_t id), void* context
) noexcept {
    if (!connected(s))
        return result::uninitialized;
    for (const auto& player : s->backend.engine->players)
        if (player.used && !player.local && (player.info.flags & player_flag::system_player) == 0 &&
            visit != nullptr)
            visit(context, player.info.id);
    return result::ok;
}

bool session_quit_game(Session* s) noexcept {
    if (connected(s)) {
        engine_close(s->backend.engine);
        pump(s, 0);
    }
    return true;
}

uint32_t session_update_game_info(Session* s, const char* name) noexcept {
    if (!connected(s))
        return result::no_connection;
    copy_bounded(s->session_name, name, session_name_bytes - 1);
    Engine* e = s->backend.engine;
    e->publish_without_password = s->publish_without_password;
    return engine_set_session_desc(
        e, s->desc, name, s->publish_without_password ? "" : e->password
    );
}

bool session_password_required(const Session* s) noexcept {
    const uint32_t flags = connected(s) ? s->backend.engine->desc.flags : s->desc.flags;
    return (flags & session_flag_password_required) != 0;
}

bool session_set_guaranteed(Session* s, bool guaranteed) noexcept {
    const bool old = s->guaranteed;
    s->guaranteed = guaranteed;
    return old;
}

uint32_t
session_send(Session* s, uint32_t from, uint32_t to, const uint8_t* data, uint32_t size) noexcept {
    if (!connected(s))
        return transport_result::no_connection;
    return engine_send(
        s->backend.engine, from, to, s->guaranteed ? send_flag_guaranteed : 0, data, size
    );
}

uint32_t session_receive(Session* s, uint8_t* buffer, uint32_t* size) noexcept {
    if (!connected(s))
        return transport_result::no_connection;
    pump(s, 0);
    return engine_receive(s->backend.engine, &s->recv_from, &s->recv_to, buffer, size);
}

NetTransport session_transport(Session* s) noexcept {
    return NetTransport{s, transport_send, transport_receive};
}

uint8_t session_join_reject_reason(
    const uint8_t* image,
    std::size_t size,
    bool game_closed,
    bool password_required,
    const char* expected_tag
) noexcept {
    CreatePlayerView view{};
    if (!decode_create_player_image(image, size, &view))
        return reject_version_mismatch;
    if (game_closed)
        return reject_game_closed;
    switch (check_create_player_data(view.data, view.data_size, password_required, expected_tag)) {
    case JoinVerdict::accepted:
        return 0;
    case JoinVerdict::wrong_password:
        return reject_wrong_password;
    case JoinVerdict::version_mismatch:
        return reject_version_mismatch;
    }
    return reject_version_mismatch;
}

} // namespace oa::netgame::session
