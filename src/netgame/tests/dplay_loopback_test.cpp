// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A host and a client on 127.0.0.1 enumerate, join, create
// players and exchange condenser frames carrying game records over real
// TCP and UDP sockets, driven through TA's session wrappers. Every message
// header carries its sender's address and stream port. A client that
// searches instead of naming an address finds a host on the same machine.
// When the host of three machines leaves, another takes its place and a
// fourth machine joins through it.

#include "oa/netgame/frame.hpp"
#include "oa/netgame/records.hpp"
#include "oa/netgame/session.hpp"
#include "oa/netgame/socket_host.hpp"
#include "oa/netgame/network.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace oa::netgame;
namespace sock = oa::netgame::sock;
namespace ses = oa::netgame::session;

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

constexpr uint32_t wait_limit_ms = 5000;

constexpr std::size_t max_machines = 4;
sock::Host* g_hosts[max_machines]{};

// Every machine lives in this process, so every wait services them all.
void pump_both(void*, uint32_t wait_ms) {
    uint32_t count = 0;
    for (sock::Host* host : g_hosts)
        count += host != nullptr ? 1 : 0;
    for (sock::Host* host : g_hosts)
        if (host != nullptr)
            sock::host_pump(host, wait_ms / (count != 0 ? count : 1));
}

ses::Backend backend_for(sock::Host* host) {
    ses::Backend b = sock::host_session_backend(host);
    b.pump = pump_both;
    return b;
}

// The machines' shared clock: DirectPlay's windows and timeouts run on it in
// simulated time, which moves on whenever a pump finds nothing to read.
uint32_t g_now_ms = 0;
int64_t g_in_flight = 0; // bytes sent between the machines and not yet read

oa::netgame::sock::HostClock shared_clock() {
    oa::netgame::sock::HostClock clock;
    clock.now_ms = [](void*) { return g_now_ms; };
    clock.advance = [](void*, uint32_t ms) { g_now_ms += ms; };
    clock.in_flight = &g_in_flight;
    return clock;
}

uint32_t elapsed_since(std::chrono::steady_clock::time_point start) {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - start
    )
                                     .count());
}

/// Tells whether the last control message an engine decoded is a command from an endpoint.
///
/// @param engine Engine that received the message.
/// @param command Command word expected.
/// @param sender Machine whose stream endpoint the header should carry.
/// @return True when the header names 127.0.0.1 and the sender's stream port.
bool last_heard(const dplay::Engine& engine, uint16_t command, const sock::Host& sender) {
    const auto& m = engine.message;
    const uint8_t loopback[4] = {127, 0, 0, 1};
    return m.command == command && std::memcmp(m.reply_to.ip, loopback, 4) == 0 &&
           m.reply_to.port == sender.engine.config.stream.port;
}

struct Received {
    uint32_t from{};
    uint32_t to{};
    std::vector<uint8_t> bytes;
};

// Next message through a transport, pumping both hosts until one arrives.
bool receive(const NetTransport& transport, Received* out) {
    const auto start = std::chrono::steady_clock::now();
    uint8_t buffer[0x10000];
    while (elapsed_since(start) < wait_limit_ms) {
        uint32_t size = sizeof buffer;
        const uint32_t r =
            transport.receive(transport.context, &out->from, &out->to, buffer, &size);
        if (r == transport_result::ok) {
            out->bytes.assign(buffer, buffer + size);
            return true;
        }
        pump_both(nullptr, 10);
    }
    return false;
}

bool receive_system(const NetTransport& transport, SystemMessageType type, Received* out) {
    while (receive(transport, out))
        if (out->from == system_message_sender_id && out->bytes.size() >= 4 &&
            load_u32(out->bytes.data()) == static_cast<uint32_t>(type))
            return true;
    return false;
}

struct FrameSender {
    const NetTransport* transport = nullptr;
    uint32_t flags = 0;
};

// Frame sink: condenser-wrap each frame and hand it to the transport.
void emit_frame(void* context, uint32_t from, uint32_t to, const uint8_t* frame, std::size_t size) {
    auto* sender = static_cast<FrameSender*>(context);
    const auto datagram = oa::netgame::network::encode_frame({frame, size});
    const uint32_t r = sender->transport->send(
        sender->transport->context,
        from,
        to,
        sender->flags,
        datagram.data(),
        static_cast<uint32_t>(datagram.size())
    );
    CHECK(r == transport_result::ok);
}

std::vector<uint8_t> chat_record(const char* text) {
    ChatRecord chat{};
    std::strncpy(chat.text, text, sizeof chat.text - 1);
    std::vector<uint8_t> bytes(record_length_table[static_cast<uint8_t>(RecordType::chat)]);
    std::size_t written = 0;
    CHECK(encode_record(chat, bytes.data(), bytes.size(), &written) == WireError::ok);
    return bytes;
}

std::vector<uint8_t> ping_record(uint32_t tick, uint32_t player) {
    PingRecord ping{};
    ping.origin_tick_count = tick;
    ping.origin_player_id = player;
    std::vector<uint8_t> bytes(record_length_table[static_cast<uint8_t>(RecordType::ping)]);
    std::size_t written = 0;
    CHECK(encode_record(ping, bytes.data(), bytes.size(), &written) == WireError::ok);
    return bytes;
}

// Send records as one frame through the frame layer and the condenser.
void send_records(
    const NetTransport& transport,
    bool guaranteed,
    uint32_t from,
    uint32_t to,
    const std::vector<std::vector<uint8_t>>& records
) {
    auto channel = std::make_unique<SendChannel>();
    send_channel_init(channel.get(), to, 1);
    FrameSender sender{&transport, guaranteed ? send_flag_guaranteed : 0u};
    const FrameSink sink{&sender, emit_frame};
    for (const auto& r : records)
        CHECK(
            send_channel_queue(channel.get(), from, r.data(), r.size(), 0, sink) == WireError::ok
        );
    CHECK(send_channel_flush(channel.get(), 1, true, sink));
}

// Receive one condenser frame and split it back into records.
std::vector<std::vector<uint8_t>>
receive_records(const NetTransport& transport, uint32_t expect_from, int32_t* sequence) {
    std::vector<std::vector<uint8_t>> out;
    Received message;
    while (receive(transport, &message) && message.from == system_message_sender_id) {
    }
    CHECK(message.from == expect_from);
    if (message.from != expect_from)
        return out;
    const auto frame = oa::netgame::network::decode_frame(message.bytes);
    CHECK(frame.size() >= frame_header_bytes);
    *sequence = static_cast<int32_t>(load_u32(frame.data()));
    auto receiver = std::make_unique<FrameReceiver>();
    auto records = std::make_unique<PeerRecords>();
    frame_receiver_init(receiver.get());
    peer_records_reset(records.get());
    FrameDeliveries deliveries{};
    CHECK(
        frame_receiver_accept(
            receiver.get(), message.from, message.to, frame.data(), frame.size(), &deliveries
        ) == WireError::ok
    );
    for (uint8_t i = 0; i < deliveries.count; ++i) {
        const FrameDelivery& d = deliveries.items[i];
        UnpackOutcome outcome{};
        CHECK(
            unpack_frame_records(
                records.get(), d.frame, d.size, 0, d.from_id, d.to_id, d.fresh, &outcome
            ) == WireError::ok
        );
    }
    const uint8_t* data = nullptr;
    uint16_t length = 0;
    while (pop_due_record(records.get(), 0, &data, &length))
        out.emplace_back(data, data + length);
    return out;
}

void native_host_and_client_play_over_loopback() {
    current_test = "native_host_and_client_play_over_loopback";
    auto host_machine = std::make_unique<sock::Host>();
    auto client_machine = std::make_unique<sock::Host>();
    g_hosts[0] = host_machine.get();
    g_hosts[1] = client_machine.get();

    sock::HostConfig config{};
    config.clock = shared_clock();
    const uint8_t loopback[4] = {127, 0, 0, 1};
    std::memcpy(config.bind_ip, loopback, 4);
    std::memcpy(config.enum_target, loopback, 4);
    config.stream_port_first = config.datagram_port_first = config.enum_port = 0;
    config.seed = 0x51;
    CHECK(sock::host_open(host_machine.get(), config));
    config.seed = 0x77;
    CHECK(sock::host_open(client_machine.get(), config));

    dplay::Guid app{};
    std::memcpy(app.bytes, application_guid, 16);
    auto host = std::make_unique<ses::Session>();
    auto client = std::make_unique<ses::Session>();
    ses::session_init_multiplay(host.get(), backend_for(host_machine.get()), app);
    ses::session_init_defaults(host.get());
    host->max_players = 4;
    ses::session_init_multiplay(client.get(), backend_for(client_machine.get()), app);
    ses::session_init_defaults(client.get());

    // Host creates the game and its player.
    CHECK(
        ses::session_create_game(
            host.get(), "Loopback Game", "", 0x0e010400, 0xd, 0x000a000a, 0x016701f4
        )
    );
    CHECK(host_machine->enum_port_bound != 0);
    client_machine->engine.config.enum_port = host_machine->enum_port_bound;
    uint32_t host_player = 0;
    CHECK(
        ses::session_add_player(
            host.get(), &host_player, "hoster", "hoster", "", 0, create_player_version_high
        )
    );

    // Client enumerates and joins.
    ses::GameEntry games[ses::max_game_entries]{};
    CHECK(ses::session_get_games(client.get(), games, ses::max_game_entries) == 1);
    CHECK(std::strcmp(games[0].session_name, "Loopback Game") == 0);
    CHECK(
        games[0].max_players == 4 && games[0].user[0] == 0x0e010400 &&
        games[0].user[3] == 0x016701f4
    );
    // Each header names the sender's address and stream port, never 0.0.0.0.
    CHECK(last_heard(host_machine->engine, dplay::command::enum_sessions, *client_machine));
    CHECK(last_heard(client_machine->engine, dplay::command::enum_sessions_reply, *host_machine));
    CHECK(ses::session_join_game(client.get(), games[0].instance));
    CHECK(last_heard(host_machine->engine, dplay::command::add_forward_request, *client_machine));
    // The join request ends with the session's first reserved dword.
    CHECK(host_machine->engine.message.tick == host_machine->engine.desc.reserved[0]);
    CHECK(
        last_heard(client_machine->engine, dplay::command::super_enum_players_reply, *host_machine)
    );
    CHECK(std::strcmp(client->session_name, "Loopback Game") == 0);
    CHECK(std::memcmp(client->desc.instance_guid, host->desc.instance_guid, 16) == 0);
    CHECK(!ses::session_password_required(client.get()));
    CHECK(client_machine->engine.name_server_id == host_machine->engine.system_id);
    CHECK(dplay::engine_find_player(&client_machine->engine, host_player) != nullptr);

    uint32_t client_player = 0;
    CHECK(
        ses::session_add_player(
            client.get(), &client_player, "joiner", "joiner", "TAG", 0, create_player_version_high
        )
    );
    CHECK(client_player != 0 && client_player != host_player);

    const NetTransport host_transport = ses::session_transport(host.get());
    const NetTransport client_transport = ses::session_transport(client.get());

    // The host sees the new player with the 21-byte join block.
    Received created;
    CHECK(receive_system(host_transport, SystemMessageType::player_created, &created));
    CHECK(created.to == host_player);
    dplay::CreatePlayerView view{};
    CHECK(dplay::decode_create_player_image(created.bytes.data(), created.bytes.size(), &view));
    CHECK(view.id == client_player && view.data_size == create_player_data_bytes);
    CHECK(view.short_name != nullptr && std::strcmp(view.short_name, "joiner") == 0);
    CHECK(
        ses::session_join_reject_reason(
            created.bytes.data(), created.bytes.size(), false, true, "tag"
        ) == 0
    );
    CHECK(
        ses::session_join_reject_reason(
            created.bytes.data(), created.bytes.size(), false, true, "nope"
        ) == 4
    );

    // Client -> all, guaranteed (stream): chat + ping in one condenser frame.
    const auto chat = chat_record("hello from the joiner");
    const auto ping = ping_record(1234, client_player);
    send_records(client_transport, true, client_player, broadcast_destination_id, {chat, ping});
    int32_t sequence = 0;
    auto records = receive_records(host_transport, client_player, &sequence);
    CHECK(sequence == first_broadcast_frame_sequence);
    CHECK(records.size() == 2 && records[0] == chat && records[1] == ping);
    ChatRecord decoded{};
    CHECK(
        records.size() == 2 &&
        decode_record(records[0].data(), records[0].size(), &decoded) == WireError::ok &&
        std::strcmp(decoded.text, "hello from the joiner") == 0
    );

    // Host -> client, unguaranteed (datagram), unicast.
    const auto reply = ping_record(5678, host_player);
    send_records(host_transport, false, host_player, client_player, {reply});
    records = receive_records(client_transport, host_player, &sequence);
    CHECK(sequence == unicast_frame_sequence);
    CHECK(records.size() == 1 && records[0] == reply);

    // A session description change reaches the client as 0x0104.
    host->desc.user[0] = 0x0e020400;
    CHECK(ses::session_update_game_info(host.get(), "Loopback Game") == dplay::result::ok);
    Received changed;
    CHECK(receive_system(client_transport, SystemMessageType::session_desc_changed, &changed));
    SessionDesc desc{};
    CHECK(
        changed.bytes.size() >= 4 + session_desc_bytes &&
        decode_session_desc(changed.bytes.data() + 4, session_desc_bytes, &desc) == WireError::ok
    );
    CHECK(desc.user[0] == 0x0e020400 && desc.current_players == 2 && desc.max_players == 4);

    // The client leaves; the host sees the player destroyed.
    CHECK(ses::session_quit_game(client.get()));
    Received destroyed;
    CHECK(receive_system(host_transport, SystemMessageType::player_destroyed, &destroyed));
    CHECK(destroyed.bytes.size() >= 12 && load_u32(destroyed.bytes.data() + 8) == client_player);
    CHECK(dplay::engine_player_count(&host_machine->engine) == 1);

    ses::session_uninit(host.get());
    ses::session_uninit(client.get());
    sock::host_close(host_machine.get());
    sock::host_close(client_machine.get());
    for (auto& host : g_hosts)
        host = nullptr;
}

// A client that searches (a blank address) finds a game hosted on the same
// machine. Both machines are bound to 127.0.0.1, so the search has no
// network to broadcast to and finds the host through its copy to 127.0.0.1.
void client_search_finds_host_on_this_machine() {
    current_test = "client_search_finds_host_on_this_machine";
    auto host_machine = std::make_unique<sock::Host>();
    auto client_machine = std::make_unique<sock::Host>();
    g_hosts[0] = host_machine.get();
    g_hosts[1] = client_machine.get();

    sock::HostConfig config{};
    config.clock = shared_clock();
    const uint8_t loopback[4] = {127, 0, 0, 1};
    std::memcpy(config.bind_ip, loopback, 4);
    std::memcpy(config.enum_target, loopback, 4);
    config.stream_port_first = config.datagram_port_first = config.enum_port = 0;
    config.seed = 0x53;
    CHECK(sock::host_open(host_machine.get(), config));
    std::memcpy(config.enum_target, sock::local_networks_ip, 4);
    config.seed = 0x79;
    CHECK(sock::host_open(client_machine.get(), config));

    dplay::Guid app{};
    std::memcpy(app.bytes, application_guid, 16);
    auto host = std::make_unique<ses::Session>();
    auto client = std::make_unique<ses::Session>();
    ses::session_init_multiplay(host.get(), backend_for(host_machine.get()), app);
    ses::session_init_defaults(host.get());
    ses::session_init_multiplay(client.get(), backend_for(client_machine.get()), app);
    ses::session_init_defaults(client.get());
    CHECK(std::memcmp(client->backend.enum_target, sock::local_networks_ip, 4) == 0);

    CHECK(ses::session_create_game(host.get(), "Search Game", "", 0, 0, 0, 0));
    CHECK(host_machine->enum_port_bound != 0);
    client_machine->engine.config.enum_port = host_machine->enum_port_bound;
    ses::GameEntry games[ses::max_game_entries]{};
    CHECK(ses::session_get_games(client.get(), games, ses::max_game_entries) == 1);
    CHECK(std::strcmp(games[0].session_name, "Search Game") == 0);
    CHECK(ses::session_join_game(client.get(), games[0].instance));

    ses::session_uninit(host.get());
    ses::session_uninit(client.get());
    sock::host_close(host_machine.get());
    sock::host_close(client_machine.get());
    for (auto& host : g_hosts)
        host = nullptr;
}

/// Pumps every machine until a condition holds or the wait limit passes.
///
/// @param done The condition.
/// @return True when the condition held in time.
template <typename Done>
bool pump_until(Done done) {
    const auto start = std::chrono::steady_clock::now();
    while (!done()) {
        if (elapsed_since(start) >= wait_limit_ms)
            return false;
        pump_both(nullptr, 10);
    }
    return true;
}

// Three machines on 127.0.0.1 play a game; the host leaves. The machine
// whose system player has the lower id takes the name server's place: its
// player hears 0x0101, the other machine takes it as the name server, it
// answers searches on an enumeration port of its own, and a fourth machine
// joins the game through it.
void host_leaves_and_another_machine_hosts_over_loopback() {
    current_test = "host_leaves_and_another_machine_hosts_over_loopback";
    std::unique_ptr<sock::Host> machines[max_machines];
    std::unique_ptr<ses::Session> sessions[max_machines];
    sock::HostConfig config{};
    config.clock = shared_clock();
    const uint8_t loopback[4] = {127, 0, 0, 1};
    std::memcpy(config.bind_ip, loopback, 4);
    std::memcpy(config.enum_target, loopback, 4);
    config.stream_port_first = config.datagram_port_first = config.enum_port = 0;
    dplay::Guid app{};
    std::memcpy(app.bytes, application_guid, 16);
    for (std::size_t i = 0; i < max_machines; ++i) {
        machines[i] = std::make_unique<sock::Host>();
        g_hosts[i] = machines[i].get();
        config.seed = static_cast<uint32_t>(0x61 + 2 * i);
        CHECK(sock::host_open(machines[i].get(), config));
        sessions[i] = std::make_unique<ses::Session>();
        ses::session_init_multiplay(sessions[i].get(), backend_for(machines[i].get()), app);
        ses::session_init_defaults(sessions[i].get());
    }
    uint32_t players[max_machines]{};
    CHECK(ses::session_create_game(sessions[0].get(), "Loopback Game", "", 0, 0, 0, 0));
    CHECK(
        ses::session_add_player(
            sessions[0].get(), &players[0], "hoster", "hoster", "", 0, create_player_version_high
        )
    );
    const auto join = [&](std::size_t i, uint16_t enum_port) {
        machines[i]->engine.config.enum_port = enum_port;
        ses::GameEntry games[ses::max_game_entries]{};
        CHECK(ses::session_get_games(sessions[i].get(), games, ses::max_game_entries) == 1);
        CHECK(std::strcmp(games[0].session_name, "Loopback Game") == 0);
        CHECK(ses::session_join_game(sessions[i].get(), games[0].instance));
        CHECK(
            ses::session_add_player(
                sessions[i].get(),
                &players[i],
                "joiner",
                "joiner",
                "",
                0,
                create_player_version_high
            )
        );
    };
    join(1, machines[0]->enum_port_bound);
    join(2, machines[0]->enum_port_bound);
    const std::size_t lower = machines[1]->engine.system_id < machines[2]->engine.system_id ? 1 : 2;
    const std::size_t higher = lower == 1 ? 2 : 1;
    const NetTransport lower_transport = ses::session_transport(sessions[lower].get());
    const NetTransport higher_transport = ses::session_transport(sessions[higher].get());

    CHECK(ses::session_quit_game(sessions[0].get()));
    Received message;
    CHECK(receive_system(lower_transport, SystemMessageType::host_changed, &message));
    CHECK(machines[lower]->engine.state == dplay::EngineState::open_host);
    CHECK(machines[lower]->enum_port_bound != 0);
    CHECK(pump_until([&] {
        return machines[higher]->engine.name_server_id == machines[lower]->engine.system_id;
    }));
    CHECK(receive_system(higher_transport, SystemMessageType::player_destroyed, &message));
    CHECK(message.bytes.size() >= 12 && load_u32(message.bytes.data() + 8) == players[0]);

    join(3, machines[lower]->enum_port_bound);
    CHECK(machines[3]->engine.name_server_id == machines[lower]->engine.system_id);
    // Both machines hear of the new player, among the players created
    // since they last read their messages.
    for (const NetTransport& transport : {lower_transport, higher_transport}) {
        bool heard = false;
        while (!heard && receive_system(transport, SystemMessageType::player_created, &message)) {
            dplay::CreatePlayerView view{};
            heard = dplay::decode_create_player_image(
                        message.bytes.data(), message.bytes.size(), &view
                    ) &&
                    view.id == players[3];
        }
        CHECK(heard);
    }
    CHECK(dplay::engine_find_player(&machines[higher]->engine, players[3]) != nullptr);

    for (std::size_t i = 0; i < max_machines; ++i) {
        ses::session_uninit(sessions[i].get());
        sock::host_close(machines[i].get());
    }
    for (auto& host : g_hosts)
        host = nullptr;
}

} // namespace

int main() {
    native_host_and_client_play_over_loopback();
    client_search_finds_host_on_this_machine();
    host_leaves_and_another_machine_hosts_over_loopback();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("dplay loopback: all tests passed");
    return 0;
}
