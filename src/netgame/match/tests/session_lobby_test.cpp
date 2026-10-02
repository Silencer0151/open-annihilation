// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// net-session-lobby: machines on 127.0.0.1 play the battle room through the
// session-backed LobbyNet: the description a host publishes from the
// moment it creates the session, who each record leaves from and how they
// are batched, pings and their echoes, probes and pings heard from their
// sender, and a joiner the host refuses.

#include "oa/netgame/match/session_lobby.hpp"
#include "oa/netgame/records.hpp"
#include "oa/ui/frontend_multiplayer/connect.hpp"
#include "oa/ui/frontend_multiplayer/lobby.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace oa;
using namespace oa::netgame;
using namespace oa::netgame::match;
namespace mp = oa::ui::frontend_multiplayer;

namespace {

int failures = 0;
const char* current_test = "";

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            ++failures;                                                                            \
            std::fprintf(stderr, "%s: %s:%d: %s\n", current_test, __FILE__, __LINE__, #condition); \
        }                                                                                          \
    } while (0)

constexpr uint8_t kLoopback[4] = {127, 0, 0, 1};
constexpr uint32_t kWaitLimitMs = 5000;
constexpr int32_t kMachines = 3;

uint32_t g_tick = 1000; // the battle rooms' 30 Hz clock

uint32_t tick(void*) {
    return g_tick;
}

uint32_t elapsed_ms(std::chrono::steady_clock::time_point since) {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - since
    )
                                     .count());
}

// The machines' shared clock. DirectPlay's windows and timeouts, and the
// lobbies' milliseconds, run on it in simulated time: it moves on whenever a
// pump finds nothing to read, so a 1500 ms search takes microseconds.
uint32_t g_now_ms = 0;
int64_t g_in_flight = 0; // bytes sent between the machines and not yet read

uint32_t milliseconds(void*) {
    return g_now_ms;
}

oa::netgame::sock::HostClock shared_clock() {
    oa::netgame::sock::HostClock clock;
    clock.now_ms = milliseconds;
    clock.advance = [](void*, uint32_t ms) { g_now_ms += ms; };
    clock.in_flight = &g_in_flight;
    return clock;
}

struct Machine {
    NetConnection connection{};
    mp::LobbyNet net{};
    std::unique_ptr<Game> game = std::make_unique<Game>();
    std::unique_ptr<mp::Lobby> lobby = std::make_unique<mp::Lobby>();
    std::unique_ptr<mp::ConnectState> connect = std::make_unique<mp::ConnectState>();
    mp::Panel panel;                   // the battle room's, with no layout
    std::vector<mp::LobbyEvent> heard; // every event received, in order

    ~Machine() { net_connection_destroy(&connection); }
};

Machine* g_machines[kMachines]{};

// Every machine lives in this process, so a machine waiting on a reply
// services the others.
void pump_others(void* context, uint32_t wait_ms) {
    for (auto* m : g_machines)
        if (m != nullptr && m != context && m->connection.opened)
            sock::host_pump(m->connection.host, wait_ms / kMachines);
}

void pump_all(uint32_t wait_ms) {
    for (auto* m : g_machines)
        if (m != nullptr && m->connection.opened)
            sock::host_pump(m->connection.host, wait_ms);
}

// Applies every event queued for a machine, keeping each.
void drain(Machine& m) {
    mp::LobbyEvent event{};
    while (m.net.receive(m.net.context, &event)) {
        m.heard.push_back(event);
        (void)mp::lobby_apply_event(*m.lobby, event);
    }
}

template <class Done>
bool wait_until(Done done) {
    const auto start = std::chrono::steady_clock::now();
    while (!done()) {
        if (elapsed_ms(start) > kWaitLimitMs)
            return false;
        pump_all(2);
        for (auto* m : g_machines)
            if (m != nullptr && m->connection.in_session)
                drain(*m);
    }
    return true;
}

void setup(Machine& m, int32_t index, const char* nickname, const char* password, uint32_t seed) {
    g_machines[index] = &m;
    SessionLobbyConfig config{};
    std::memcpy(config.host.bind_ip, kLoopback, 4);
    std::memcpy(config.host.enum_target, kLoopback, 4);
    config.host.stream_port_first = config.host.datagram_port_first = config.host.enum_port = 0;
    config.host.seed = seed;
    config.host.clock = shared_clock();
    CHECK(net_connection_create(&m.connection, config));
    m.connection.pump_context = &m;
    m.connection.pump_other = pump_others;
    m.net = session_lobby_net(&m.connection);
    mp::lobby_reset(*m.lobby, *m.game);
    m.lobby->net = m.net;
    m.lobby->services.tick = tick;
    m.lobby->services.milliseconds = milliseconds;
    m.lobby->local_version_major = 3;
    std::snprintf(mp::lobby_nickname(*m.game), 17, "%s", nickname);
    std::snprintf(mp::lobby_game_name(*m.game), 17, "%s", "Session Game");
    std::snprintf(mp::lobby_password(*m.game), 11, "%s", password);
    mp::Provider providers[1]{};
    CHECK(m.net.providers(m.net.context, providers, 1) == 1);
    CHECK(m.net.open(m.net.context, &providers[0], "127.0.0.1"));
}

void teardown() {
    for (auto*& m : g_machines)
        m = nullptr;
}

// Lists the sessions a machine finds at the host's enumeration port.
int32_t search(Machine& m, const Machine& host, mp::SessionEntry (&sessions)[mp::kMaxSessions]) {
    m.connection.host->engine.config.enum_port = host.connection.host->enum_port_bound;
    return m.net.enumerate(m.net.context, sessions, mp::kMaxSessions);
}

bool join(Machine& m, const Machine& host) {
    mp::SessionEntry sessions[mp::kMaxSessions]{};
    if (search(m, host, sessions) != 1)
        return false;
    m.connect->chosen = sessions[0];
    return mp::connect_join(*m.lobby, *m.connect, false);
}

bool seated(Machine& m, uint32_t id) {
    return mp::slot_for_player_id(*m.lobby, id) >= 0;
}

// A host and a client seated in each other's battle rooms.
void host_and_join(Machine& host, Machine& client) {
    CHECK(mp::connect_host(*host.lobby, *host.connect));
    CHECK(join(client, host));
    const auto host_id = host.connection.local_id;
    const auto client_id = client.connection.local_id;
    CHECK(wait_until([&] { return seated(host, client_id) && seated(client, host_id); }));
}

// ---- tests ----

// The session is created with its full name and user bytes, found without
// a republish; it publishes flags 4 with a password, a 0x20 that stays
// once the game starts, and the player limit closing slots lowers.
void the_published_description() {
    current_test = "the_published_description";
    auto host = std::make_unique<Machine>();
    auto client = std::make_unique<Machine>();
    auto late = std::make_unique<Machine>();
    setup(*host, 0, "hoster", "secret", 0x51);
    setup(*client, 1, "joiner", "secret", 0x77);
    setup(*late, 2, "late", "secret", 0x99);
    CHECK(mp::connect_host(*host->lobby, *host->connect));

    mp::SessionEntry sessions[mp::kMaxSessions]{};
    CHECK(search(*client, *host, sessions) == 1);
    char expected[mp::kSessionNameBytes];
    std::snprintf(expected, sizeof expected, "%-16s%-15s", "Session Game", "");
    CHECK(std::memcmp(sessions[0].name, expected, sizeof expected) == 0);
    const auto& info = mp::local_info(*host->lobby);
    const auto* user =
        reinterpret_cast<const uint8_t*>(&info) + offsetof(mp::PlayerSetupInfo, memory_mb);
    CHECK(std::memcmp(sessions[0].user, user, sizeof sessions[0].user) == 0);
    CHECK(
        sessions[0].flags == 0x4 && sessions[0].max_players == 10 &&
        sessions[0].current_players == 1
    );

    // Closing every seat but the host's hides the full game; reopening one shows it.
    mp::lobby_session_players(*host->game) = 1;
    mp::lobby_publish_session(*host->lobby);
    CHECK(search(*client, *host, sessions) == 0);
    mp::lobby_session_players(*host->game) = 2;
    mp::lobby_publish_session(*host->lobby);
    CHECK(search(*client, *host, sessions) == 1 && sessions[0].max_players == 2);
    CHECK(search(*late, *host, sessions) == 1);
    late->connect->chosen = sessions[0];
    client->connect->chosen = sessions[0];
    CHECK(mp::connect_join(*client->lobby, *client->connect, false));
    // The late machine's copy of the list still shows a seat, but the host refuses it as full.
    CHECK(!mp::connect_join(*late->lobby, *late->connect, false));
    late->net.leave(late->net.context);

    const auto host_id = host->connection.local_id;
    CHECK(wait_until([&] { return seated(*client, host_id); }));
    mp::local_info(*host->lobby).options |= mp::option::started;
    mp::lobby_publish_session(*host->lobby);
    const auto& seen = client->connection.host->engine.desc;
    CHECK(wait_until([&] { return seen.flags == 0x24; }));
    mp::local_info(*host->lobby).options &= static_cast<uint16_t>(~mp::option::started);
    mp::local_info(*host->lobby).max_units = 77;
    mp::lobby_publish_session(*host->lobby);
    // The limit sits in the fourth user dword's low half.
    CHECK(wait_until([&] { return (seen.user[3] & 0xffff) == 77; }));
    CHECK(seen.flags == 0x24);
    teardown();
}

// Each player's records leave from that player's own id, a computer
// player's included, and are batched: one frame per player per flush. A
// record from an id this machine did not create leaves from the local
// player. The receiver stamps the sender's slot as heard from.
void records_leave_from_each_player() {
    current_test = "records_leave_from_each_player";
    auto host = std::make_unique<Machine>();
    auto client = std::make_unique<Machine>();
    setup(*host, 0, "hoster", "", 0x53);
    setup(*client, 1, "joiner", "", 0x79);
    host_and_join(*host, *client);
    const auto host_id = host->connection.local_id;

    mp::lobby_add_computer(*host->lobby, 2);
    const auto computer_id = mp::slot_player(*host->lobby, 2).player_id;
    CHECK(computer_id != 0 && computer_id != host_id);
    CHECK(wait_until([&] {
        return mp::slot_player(*host->lobby, 2).in_use != 0 && seated(*client, computer_id);
    }));

    client->heard.clear();
    g_tick += 100;
    const auto frames = host->connection.packets->sent_frames;
    mp::lobby_send_player_info(*host->lobby);
    // Two players' lobby blocks and teams: four records in two frames.
    CHECK(host->connection.packets->sent_frames - frames == 2);
    const auto heard_from = [&](uint32_t id, RecordType type) {
        for (const auto& event : client->heard)
            if (event.kind == mp::LobbyEventKind::record && event.player_id == id &&
                event.size != 0 && event.data[0] == static_cast<uint8_t>(type))
                return true;
        return false;
    };
    CHECK(wait_until([&] {
        return heard_from(computer_id, RecordType::player_info) &&
               heard_from(computer_id, RecordType::player_team) &&
               heard_from(host_id, RecordType::player_info);
    }));
    const auto computer_slot = mp::slot_for_player_id(*client->lobby, computer_id);
    CHECK(
        computer_slot >= 0 &&
        mp::slot_player(*client->lobby, computer_slot).last_update_time == g_tick
    );

    ChatRecord chat{};
    std::snprintf(chat.text, sizeof chat.text, "%s", "<hoster> from nobody");
    uint8_t bytes[80];
    std::size_t written = 0;
    CHECK(encode_record(chat, bytes, sizeof bytes, &written) == WireError::ok);
    // The broadcast frames share one sequence, so a player's frame that
    // follows another player's waits at the receiver for that player's next
    // frame: a second one follows.
    for (int frame = 0; frame < 2; ++frame) {
        host->net.send_from(host->net.context, 0xdeadbeef, 0, bytes, written, false);
        host->net.flush(host->net.context);
    }
    CHECK(wait_until([&] { return heard_from(host_id, RecordType::chat); }));
    teardown();
}

// The battle room's periodic pings fill each machine's Ping column; a ping
// addressed to a computer player is echoed from that player.
void pings_fill_the_ping_column() {
    current_test = "pings_fill_the_ping_column";
    auto host = std::make_unique<Machine>();
    auto client = std::make_unique<Machine>();
    setup(*host, 0, "hoster", "", 0x55);
    setup(*client, 1, "joiner", "", 0x81);
    host_and_join(*host, *client);
    const auto host_id = host->connection.local_id;
    const auto client_id = client->connection.local_id;
    mp::lobby_add_computer(*host->lobby, 2);
    const auto computer_id = mp::slot_player(*host->lobby, 2).player_id;
    CHECK(wait_until([&] { return seated(*client, computer_id); }));

    auto& host_on_client =
        mp::slot_player(*client->lobby, mp::slot_for_player_id(*client->lobby, host_id));
    auto& client_on_host =
        mp::slot_player(*host->lobby, mp::slot_for_player_id(*host->lobby, client_id));
    mp::lobby_player_ping(host_on_client) = -1;
    mp::lobby_player_ping(client_on_host) = -1;
    CHECK(wait_until([&] {
        g_tick += 61;
        (void)mp::lobby_tick(*host->lobby, host->panel);
        (void)mp::lobby_tick(*client->lobby, client->panel);
        return mp::lobby_player_ping(host_on_client) >= 0 &&
               mp::lobby_player_ping(client_on_host) >= 0;
    }));

    PingRecord ping{};
    ping.origin_tick_count = milliseconds(nullptr);
    ping.origin_player_id = client_id;
    uint8_t bytes[16];
    std::size_t written = 0;
    CHECK(encode_record(ping, bytes, sizeof bytes, &written) == WireError::ok);
    client->heard.clear();
    client->net.send_from(client->net.context, client_id, computer_id, bytes, written, true);
    const auto echoed_by_computer = [&] {
        for (const auto& event : client->heard) {
            PingRecord echo{};
            if (event.kind == mp::LobbyEventKind::record && event.player_id == computer_id &&
                decode_record(event.data, event.size, &echo) == WireError::ok &&
                echo.echo_tick_count != 0 && echo.origin_player_id == client_id)
                return true;
        }
        return false;
    };
    CHECK(wait_until(echoed_by_computer));
    teardown();
}

// The connection answers a probe (0x07 to the prober) and a ping request
// (its echo) by itself and still hands both on, so the battle room hears
// from their sender: every record keeps a player from falling silent.
void probes_and_pings_are_heard() {
    current_test = "probes_and_pings_are_heard";
    auto host = std::make_unique<Machine>();
    auto client = std::make_unique<Machine>();
    setup(*host, 0, "hoster", "", 0x59);
    setup(*client, 1, "joiner", "", 0x85);
    host_and_join(*host, *client);
    const auto host_id = host->connection.local_id;
    const auto client_id = client->connection.local_id;
    const auto& host_on_client =
        mp::slot_player(*client->lobby, mp::slot_for_player_id(*client->lobby, host_id));
    // Whether a machine heard a record of a type from a player; for a
    // ping, a request (echo false) or an echo (echo true).
    const auto heard = [](const Machine& m, uint32_t from, RecordType type, bool echo = false) {
        for (const auto& event : m.heard) {
            if (event.kind != mp::LobbyEventKind::record || event.player_id != from ||
                event.size == 0 || event.data[0] != static_cast<uint8_t>(type))
                continue;
            PingRecord ping{};
            if (type != RecordType::ping ||
                (decode_record(event.data, event.size, &ping) == WireError::ok &&
                 (ping.echo_tick_count != 0) == echo))
                return true;
        }
        return false;
    };

    g_tick += 100;
    host->heard.clear();
    client->heard.clear();
    const auto probe = static_cast<uint8_t>(RecordType::probe);
    host->net.send_from(host->net.context, host_id, 0, &probe, 1, false);
    host->net.flush(host->net.context);
    CHECK(wait_until([&] {
        return heard(*client, host_id, RecordType::probe) &&
               heard(*host, client_id, RecordType::probe_reply);
    }));
    CHECK(host_on_client.last_update_time == g_tick);

    g_tick += 100;
    PingRecord ping{};
    ping.origin_tick_count = milliseconds(nullptr);
    ping.origin_player_id = host_id;
    uint8_t bytes[16];
    std::size_t written = 0;
    CHECK(encode_record(ping, bytes, sizeof bytes, &written) == WireError::ok);
    host->net.send_from(host->net.context, host_id, 0, bytes, written, true);
    CHECK(wait_until([&] {
        return heard(*client, host_id, RecordType::ping, false) &&
               heard(*host, client_id, RecordType::ping, true);
    }));
    CHECK(host_on_client.last_update_time == g_tick);
    teardown();
}

// A joiner the host refuses (a wrong password) hears the host's lobby
// blocks, then its rejection, and leaves by itself; the host destroys
// nothing.
void a_refused_joiner_leaves_by_itself() {
    current_test = "a_refused_joiner_leaves_by_itself";
    auto host = std::make_unique<Machine>();
    auto client = std::make_unique<Machine>();
    setup(*host, 0, "hoster", "secret", 0x57);
    setup(*client, 1, "joiner", "wrong", 0x83);
    CHECK(mp::connect_host(*host->lobby, *host->connect));
    CHECK(join(*client, *host));
    const auto host_id = host->connection.local_id;
    const auto client_id = client->connection.local_id;
    CHECK(wait_until([&] { return mp::local_player(*client->lobby).reject_reason == 4; }));
    int32_t info_at = -1;
    int32_t reject_at = -1;
    for (int32_t i = 0; i < static_cast<int32_t>(client->heard.size()); ++i) {
        const auto& event = client->heard[static_cast<std::size_t>(i)];
        if (event.kind != mp::LobbyEventKind::record || event.player_id != host_id ||
            event.size == 0)
            continue;
        RejectRecord reject{};
        if (event.data[0] == static_cast<uint8_t>(RecordType::player_info) && info_at < 0)
            info_at = i;
        if (decode_record(event.data, event.size, &reject) == WireError::ok &&
            reject.player_id == client_id && reject.reason == 4)
            reject_at = i;
    }
    CHECK(info_at >= 0 && reject_at > info_at);
    CHECK(!seated(*host, client_id));
    CHECK(dplay::engine_find_player(&host->connection.host->engine, client_id) != nullptr);
    CHECK(std::strcmp(mp::reject_reason_text(4), "You did not have the correct password") == 0);

    client->net.leave(client->net.context);
    CHECK(wait_until([&] {
        return dplay::engine_find_player(&host->connection.host->engine, client_id) == nullptr;
    }));
    teardown();
}

bool g_launch_active = false;

bool answer_launch_active(void*) {
    return g_launch_active;
}

// A closed game refuses a joiner (reason 3) unless its host has a launch
// active; the password and the other checks stay.
void a_launched_host_admits_joiners_to_a_closed_game() {
    current_test = "a_launched_host_admits_joiners_to_a_closed_game";
    for (const bool launched : {false, true}) {
        g_launch_active = launched;
        auto host = std::make_unique<Machine>();
        auto client = std::make_unique<Machine>();
        setup(*host, 0, "hoster", "", launched ? 0x61 : 0x59);
        setup(*client, 1, "joiner", "", launched ? 0x87 : 0x85);
        host->connection.launch_active = answer_launch_active;
        CHECK(mp::connect_host(*host->lobby, *host->connect));
        mp::local_info(*host->lobby).options |= mp::option::game_closed;
        mp::lobby_publish_session(*host->lobby);
        CHECK(join(*client, *host));
        const auto host_id = host->connection.local_id;
        const auto client_id = client->connection.local_id;
        if (launched) {
            CHECK(wait_until([&] { return seated(*host, client_id) && seated(*client, host_id); }));
            CHECK(mp::local_player(*client->lobby).reject_reason == 0);
        } else {
            CHECK(wait_until([&] { return mp::local_player(*client->lobby).reject_reason == 3; }));
            CHECK(!seated(*host, client_id));
        }
        client->net.leave(client->net.context);
        teardown();
    }
    g_launch_active = false;
}

} // namespace

int main() {
    the_published_description();
    records_leave_from_each_player();
    pings_fill_the_ping_column();
    probes_and_pings_are_heard();
    a_refused_joiner_leaves_by_itself();
    a_launched_host_admits_joiners_to_a_closed_game();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("session lobby: all tests passed");
    return 0;
}
