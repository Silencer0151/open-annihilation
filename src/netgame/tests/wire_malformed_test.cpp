// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Malformed and mutated network input. Every decoder of bytes that another
// player sent, or that a .tad recording holds, refuses what it cannot read
// and never reads outside its input: DirectPlay messages and their envelope,
// the system message images, condenser frames, game frames and records,
// unit states and the recording's chunks and records. The mutation pass
// changes, truncates and splices real messages; .tad recordings named on the
// command line join its seeds.

#include "oa/formats/sqsh.hpp"
#include "oa/formats/tad.hpp"
#include "oa/netgame/condenser.hpp"
#include "oa/netgame/dplay.hpp"
#include "oa/netgame/dplay/engine.hpp"
#include "oa/netgame/dplay/protocol.hpp"
#include "oa/netgame/frame.hpp"
#include "oa/netgame/network.hpp"
#include "oa/netgame/records.hpp"
#include "oa/netgame/unit_state.hpp"
#include "oa/test/check.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace oa::netgame;
using namespace oa::netgame::dplay;
using Bytes = std::vector<uint8_t>;

namespace {

namespace tad = oa::formats::tad;

// Mutations made of each seed; the pass stays well under a second.
constexpr int mutations_per_seed = 300;
// Packets and status datagrams taken from each recording named on the command line.
constexpr std::size_t recorded_seeds_per_file = 400;

const Address host_endpoint{{192, 168, 1, 10}, 2300};

/// Encodes a control message with a writer such as encode_ping.
template <class F>
Bytes control(F&& encode) {
    Bytes out(max_message_bytes);
    const std::size_t n = encode(out.data(), out.size());
    OA_CHECK(n != 0);
    out.resize(n);
    return out;
}

PlayerInfo sample_player(uint32_t id) {
    PlayerInfo p{};
    p.id = id;
    p.system_id = id + 1;
    p.has_short_name = true;
    std::snprintf(p.short_name, sizeof p.short_name, "Player%u", id);
    p.has_long_name = true;
    std::snprintf(p.long_name, sizeof p.long_name, "Long name %u", id);
    p.has_addresses = true;
    p.stream = {{10, 0, 0, 2}, 2300};
    p.datagram = {{10, 0, 0, 2}, 2350};
    p.data_size = create_player_data_bytes;
    CreatePlayerData block{};
    std::snprintf(block.tag, sizeof block.tag, "secret");
    block.version_high = create_player_version_high;
    (void)encode_create_player_data(block, p.data, sizeof p.data);
    return p;
}

SessionDesc sample_desc() {
    SessionDesc desc{};
    desc.flags = session_flag::migrate_host;
    desc.max_players = 10;
    desc.current_players = 2;
    std::memcpy(desc.application_guid, application_guid, 16);
    desc.instance_guid[0] = 0x42;
    return desc;
}

// One encoded message of every command the decoder reads.
std::vector<Bytes> control_seeds() {
    std::vector<Bytes> seeds;
    const auto player = sample_player(0x101);
    const PlayerInfo roster[2] = {sample_player(0x201), sample_player(0x301)};
    const auto desc = sample_desc();
    Guid app{};
    std::memcpy(app.bytes, application_guid, 16);
    seeds.push_back(control([&](uint8_t* out, std::size_t cap) {
        return encode_enum_sessions(host_endpoint, app, "pw", 0x81, out, cap);
    }));
    seeds.push_back(control([&](uint8_t* out, std::size_t cap) {
        return encode_enum_sessions_reply(host_endpoint, desc, "Game\tMap", out, cap);
    }));
    seeds.push_back(control([&](uint8_t* out, std::size_t cap) {
        return encode_request_player_id(host_endpoint, 8, out, cap);
    }));
    seeds.push_back(control([&](uint8_t* out, std::size_t cap) {
        return encode_request_player_reply(host_endpoint, 0x101, 0, out, cap);
    }));
    for (const uint16_t cmd : {command::create_player, command::add_forward_request}) {
        seeds.push_back(control([&](uint8_t* out, std::size_t cap) {
            return encode_player_message(host_endpoint, cmd, 0, player, "pw", 77, out, cap);
        }));
    }
    seeds.push_back(control([&](uint8_t* out, std::size_t cap) {
        return encode_delete_player(host_endpoint, 0, 0x101, out, cap);
    }));
    seeds.push_back(control([&](uint8_t* out, std::size_t cap) {
        return encode_ping(host_endpoint, command::ping, 0x101, 1234, out, cap);
    }));
    seeds.push_back(control([&](uint8_t* out, std::size_t cap) {
        return encode_i_am_name_server(
            host_endpoint, 0, 0x101, 3, player.stream, player.datagram, out, cap
        );
    }));
    seeds.push_back(control([&](uint8_t* out, std::size_t cap) {
        return encode_player_data_changed(host_endpoint, 0, 0x101, player.data, 21, out, cap);
    }));
    seeds.push_back(control([&](uint8_t* out, std::size_t cap) {
        return encode_player_name_changed(host_endpoint, 0, 0x101, "Short", "Long", out, cap);
    }));
    seeds.push_back(control([&](uint8_t* out, std::size_t cap) {
        return encode_session_desc_changed(host_endpoint, 0, desc, "Game", "pw", out, cap);
    }));
    seeds.push_back(control([&](uint8_t* out, std::size_t cap) {
        return encode_super_enum_players_reply(
            host_endpoint, desc, "Game", "pw", roster, std::size(roster), out, cap
        );
    }));
    const uint8_t payload[] = {1, 2, 3, 4, 5};
    seeds.push_back(control([&](uint8_t* out, std::size_t cap) {
        return encode_stream_data(host_endpoint, 0x101, 0, payload, sizeof payload, out, cap);
    }));
    seeds.push_back(control([&](uint8_t* out, std::size_t cap) {
        return encode_datagram_data(0x101, 0, payload, sizeof payload, out, cap);
    }));
    return seeds;
}

/// Appends one encoded record to a frame.
template <class R>
void append_record(Bytes& frame, const R& record) {
    uint8_t bytes[256];
    std::size_t written = 0;
    OA_CHECK(encode_record(record, bytes, sizeof bytes, &written) == WireError::ok);
    frame.insert(frame.end(), bytes, bytes + written);
}

// A unit state with two list entries and a full record.
Bytes sample_unit_state(unsigned def_bits) {
    Bytes storage(unit_state_writer_words * bit_stream_word_bytes);
    BitWriter writer;
    bit_writer_init(&writer, storage.data(), unit_state_writer_words);
    unit_state_begin(&writer, 1234);
    for (const uint16_t unit : {uint16_t{3}, uint16_t{7}}) {
        unit_state_write_entry_header(&writer, unit, 1, def_bits);
        WaypointDelta delta{};
        delta.count = 2;
        delta.points[0][0] = 100;
        delta.points[1][1] = -5;
        write_waypoint_delta(&writer, delta);
    }
    FullUnitRecord full{};
    full.unit_def_index = 1;
    full.health = 300;
    full.position[0] = 0x100000;
    uint16_t length = 0;
    OA_CHECK(unit_state_finish(&writer, full, def_bits, &length) == WireError::ok);
    storage.resize(length);
    return storage;
}

// A broadcast game frame holding records of several types and a unit state.
Bytes sample_frame(unsigned def_bits) {
    Bytes frame(frame_header_bytes, 0);
    store_u32(frame.data(), static_cast<uint32_t>(first_broadcast_frame_sequence));
    ChatRecord chat{};
    std::memcpy(chat.text, "<Player> hello", 14);
    append_record(frame, chat);
    UnitDamageRecord damage{};
    damage.target_unit_index = 5;
    damage.amount = 40;
    append_record(frame, damage);
    PlayerInfoRecord info{};
    info.player_id = 0x101;
    append_record(frame, info);
    UnitDefHandshakeRecord handshake{};
    handshake.subtype = 2;
    handshake.key = 0x1234;
    append_record(frame, handshake);
    const auto state = sample_unit_state(def_bits);
    frame.insert(frame.end(), state.begin(), state.end());
    EconomyRecord economy{};
    economy.metal = 100.0f;
    append_record(frame, economy);
    return frame;
}

// ---- mutation ----

void mutate(Bytes& b, std::mt19937& rng) {
    if (b.empty()) {
        b.push_back(static_cast<uint8_t>(rng()));
        return;
    }
    const auto pick = [&] { return static_cast<std::size_t>(rng() % b.size()); };
    switch (rng() % 7) {
    case 0:
        b[pick()] ^= static_cast<uint8_t>(1U << (rng() % 8));
        break;
    case 1:
        b[pick()] = static_cast<uint8_t>(rng());
        break;
    case 2: {
        static constexpr uint32_t extremes[] = {
            0, 1, 0x7f, 0xff, 0x7fff, 0xffff, 0x7fffffff, 0x80000000, 0xffffffec, 0xffffffff
        };
        const auto value = extremes[rng() % std::size(extremes)];
        const auto at = pick();
        const std::size_t width = std::size_t{1} << (rng() % 3);
        for (std::size_t i = 0; i < width && at + i < b.size(); ++i)
            b[at + i] = static_cast<uint8_t>(value >> (8 * i));
        break;
    }
    case 3:
        b.resize(rng() % (b.size() + 1));
        break;
    case 4: {
        const auto from = pick();
        const auto count = std::min<std::size_t>(1 + rng() % 32, b.size() - from);
        const Bytes piece(
            b.begin() + static_cast<std::ptrdiff_t>(from),
            b.begin() + static_cast<std::ptrdiff_t>(from + count)
        );
        b.insert(b.begin() + static_cast<std::ptrdiff_t>(pick()), piece.begin(), piece.end());
        break;
    }
    case 5:
        b.erase(b.begin() + static_cast<std::ptrdiff_t>(pick()));
        break;
    default:
        // A record type in place of another byte.
        b[pick()] = static_cast<uint8_t>(first_record_type + rng() % record_type_count);
        break;
    }
}

Bytes mutated(const Bytes& seed, std::mt19937& rng) {
    Bytes out = seed;
    const auto times = 1 + rng() % 4;
    for (unsigned i = 0; i < times; ++i)
        mutate(out, rng);
    return out;
}

// ---- decoders the mutation pass feeds ----

bool no_send_stream(void*, const Address&, const uint8_t*, std::size_t) {
    return true;
}

bool no_send_datagram(void*, const Address&, const uint8_t*, std::size_t) {
    return true;
}

// A hosting engine that has heard from one remote player, so data messages
// from it reach the receive queue.
std::unique_ptr<Engine> hosting_engine() {
    auto engine = std::make_unique<Engine>();
    EngineConfig config{};
    config.stream = {{10, 0, 0, 1}, 2300};
    config.datagram = {{10, 0, 0, 1}, 2350};
    engine_init(engine.get(), config, EngineIo{nullptr, no_send_stream, no_send_datagram});
    OA_CHECK(engine_host(engine.get(), sample_desc(), "Game", nullptr, 0) == result::ok);
    return engine;
}

void drain(Engine* engine) {
    Bytes buffer(receive_queue_bytes);
    for (int i = 0; i < 4096; ++i) {
        uint32_t from = 0;
        uint32_t to = 0;
        auto size = static_cast<uint32_t>(buffer.size());
        if (engine_receive(engine, &from, &to, buffer.data(), &size) != result::ok)
            return;
        OA_CHECK(size <= buffer.size());
        uint32_t type = 0;
        if (from == system_message_sender_id &&
            read_system_message_type(buffer.data(), size, &type)) {
            CreatePlayerView create{};
            PlayerDataView data{};
            PlayerNameView name{};
            PlayerDestroyedView destroyed{};
            if (decode_create_player_image(buffer.data(), size, &create))
                OA_CHECK(
                    create.data == nullptr || create.data + create.data_size <= buffer.data() + size
                );
            if (decode_player_data_image(buffer.data(), size, &data))
                OA_CHECK(
                    data.data == nullptr || data.data + data.data_size <= buffer.data() + size
                );
            (void)decode_player_name_image(buffer.data(), size, &name);
            (void)decode_player_destroyed_image(buffer.data(), size, &destroyed);
        }
    }
}

void feed_control(const Bytes& bytes, Engine* engine, uint32_t now) {
    auto message = std::make_unique<Message>();
    const auto parsed = decode_message(bytes.data(), bytes.size(), message.get());
    if (parsed == ParseError::ok) {
        OA_CHECK(message->roster_count <= max_roster_players);
        OA_CHECK(message->player.data_size <= max_player_data_bytes);
        OA_CHECK(std::memchr(message->session_name, 0, sizeof message->session_name) != nullptr);
        OA_CHECK(
            std::memchr(message->player.short_name, 0, sizeof message->player.short_name) != nullptr
        );
    }
    DataMessage data{};
    if (decode_stream_data(bytes.data(), bytes.size(), &data) == ParseError::ok)
        OA_CHECK(data.payload + data.payload_size <= bytes.data() + bytes.size());
    DplayEnvelope envelope{};
    std::size_t next = 0;
    if (decode_dplay_envelope(bytes.data(), bytes.size(), &envelope, &next) == WireError::ok) {
        OA_CHECK(next <= bytes.size());
        OA_CHECK(envelope.body + envelope.body_size <= bytes.data() + bytes.size());
    }
    if (bytes.size() >= 4)
        OA_CHECK(stream_message_size(bytes.data()) <= max_message_bytes);
    engine_on_stream(engine, host_endpoint, bytes.data(), bytes.size(), now);
    engine_on_datagram(engine, host_endpoint, bytes.data(), bytes.size(), now);
    drain(engine);
}

// Reads every unit's delta as a waypoint delta, the ground layout.
WireError read_ground(void*, uint16_t, uint16_t, BitReader* reader) {
    WaypointDelta delta{};
    read_waypoint_delta(reader, &delta);
    return bit_reader_overrun(reader) ? WireError::truncated : WireError::ok;
}

void feed_record(const uint8_t* bytes, std::size_t size, unsigned def_bits) {
    uint16_t length = 0;
    const auto measured = record_wire_length(bytes, size, &length);
    if (measured == WireError::ok)
        OA_CHECK(length != 0);
    AnyRecord any{};
    if (decode_any_record(bytes, size, &any) == WireError::ok && any.type == RecordType::unit_state)
        OA_CHECK(any.unit_state.body + any.unit_state.body_size <= bytes + size);
    if (size != 0 && bytes[0] == static_cast<uint8_t>(RecordType::unit_state)) {
        const UnitDeltaCodec codec{nullptr, read_ground, nullptr};
        UnitStateDecodeOptions options{};
        options.def_index_bits = def_bits;
        options.delta_codec = &codec;
        auto body = std::make_unique<UnitStateBody>();
        if (decode_unit_state(bytes, size, options, body.get()) == WireError::ok) {
            OA_CHECK(body->entry_count <= unit_state_max_entries);
            OA_CHECK(body->bits_consumed <= std::size_t{body->length} * 8);
        }
    }
}

// Splits a frame as the receive side does and decodes each record it yields.
void feed_frame(const Bytes& frame, unsigned def_bits) {
    auto receiver = std::make_unique<FrameReceiver>();
    frame_receiver_init(receiver.get());
    FrameDeliveries deliveries{};
    if (frame_receiver_accept(receiver.get(), 7, 0, frame.data(), frame.size(), &deliveries) !=
        WireError::ok)
        return;
    OA_CHECK(deliveries.count <= std::size(deliveries.items));
    auto* peer = frame_receiver_find_peer(receiver.get(), 7);
    OA_CHECK(peer != nullptr);
    for (uint8_t i = 0; i < deliveries.count && peer != nullptr; ++i) {
        const auto& d = deliveries.items[i];
        UnpackOutcome outcome{};
        if (unpack_frame_records(&peer->records, d.frame, d.size, 100, 7, 0, d.fresh, &outcome) !=
            WireError::ok)
            continue;
        const uint8_t* data = nullptr;
        uint16_t length = 0;
        while (pop_due_record(&peer->records, 0, &data, &length)) {
            OA_CHECK(data >= peer->records.copy);
            OA_CHECK(data + length <= peer->records.copy + peer->records.copy_size);
            feed_record(data, length, def_bits);
        }
    }
}

// Unwraps a condenser datagram both ways the engine does.
struct OneDatagram {
    const Bytes* datagram{};
    bool taken{};
};

uint32_t one_receive(void* context, uint32_t* from, uint32_t* to, uint8_t* buffer, uint32_t* size) {
    auto* one = static_cast<OneDatagram*>(context);
    if (one->taken)
        return transport_result::no_messages;
    one->taken = true;
    *from = 7;
    *to = 0;
    if (one->datagram->size() > *size) {
        *size = static_cast<uint32_t>(one->datagram->size());
        return transport_result::buffer_too_small;
    }
    std::copy(one->datagram->begin(), one->datagram->end(), buffer);
    *size = static_cast<uint32_t>(one->datagram->size());
    return transport_result::ok;
}

void feed_datagram(const Bytes& datagram, unsigned def_bits) {
    const auto unwrapped = network::unwrap_frame(datagram);
    if (unwrapped.ok())
        OA_CHECK(unwrapped.value->size() <= network::condenser::maximum_frame_bytes);
    auto condenser = std::make_unique<Condenser>();
    condenser_init(condenser.get());
    OneDatagram one{&datagram};
    const NetTransport transport{&one, nullptr, one_receive};
    Bytes buffer(max_receive_frame_bytes);
    uint32_t from = 0;
    uint32_t to = 0;
    auto size = static_cast<uint32_t>(buffer.size());
    if (condenser_receive(condenser.get(), transport, nullptr, &from, &to, buffer.data(), &size) ==
        transport_result::ok) {
        OA_CHECK(size <= buffer.size());
        buffer.resize(size);
        feed_frame(buffer, def_bits);
    }
}

void feed_recording(const Bytes& file, unsigned def_bits) {
    const auto parsed = tad::parse(file);
    if (!parsed.ok())
        return;
    for (const auto& status : parsed.demo->statuses)
        feed_datagram(Bytes(status.datagram.begin(), status.datagram.end()), def_bits);
    for (const auto& packet : parsed.demo->packets) {
        const auto decoded = tad::decode_payload(packet.payload);
        if (!decoded.ok())
            continue;
        OA_CHECK(decoded.bytes.size() <= tad::limit::decoded_payload_bytes + 1);
        const auto split = tad::split_records(decoded.bytes);
        for (const auto& record : split.records) {
            OA_CHECK(record.bytes.data() >= decoded.bytes.data());
            OA_CHECK(
                record.bytes.data() + record.bytes.size() <=
                decoded.bytes.data() + decoded.bytes.size()
            );
            const auto expanded = tad::expand_unit_state(record);
            if (!expanded.empty())
                feed_record(expanded.data(), expanded.size(), def_bits);
            else
                feed_record(record.bytes.data(), record.bytes.size(), def_bits);
        }
    }
}

// ---- malformed input, case by case ----

void control_messages_refuse_bad_offsets_and_counts() {
    auto m = std::make_unique<Message>();
    const auto player = sample_player(0x101);
    auto forward = control([&](uint8_t* out, std::size_t cap) {
        return encode_player_message(
            host_endpoint, command::add_forward_request, 0, player, "pw", 7, out, cap
        );
    });
    OA_CHECK(decode_message(forward.data(), forward.size(), m.get()) == ParseError::ok);
    // The packed player's offset: past the end, and so large that adding the
    // envelope to it would wrap a 32-bit size.
    constexpr std::size_t create_offset_at = header_bytes + 12;
    for (const uint32_t offset : {0x00010000u, 0xffffffecu, 0xffffffffu}) {
        auto bad = forward;
        store_u32(bad.data() + create_offset_at, offset);
        OA_CHECK(decode_message(bad.data(), bad.size(), m.get()) == ParseError::bad_offset);
    }
    // The packed player's fixed size and name lengths point past the message.
    const std::size_t packed_at =
        dplay_envelope_bytes + load_u32(forward.data() + create_offset_at);
    for (const std::size_t field : {std::size_t{36}, std::size_t{12}, std::size_t{24}}) {
        auto bad = forward;
        store_u32(bad.data() + packed_at + field, 0xfffffff0u);
        OA_CHECK(decode_message(bad.data(), bad.size(), m.get()) != ParseError::ok);
    }
    // Player data larger than a player's data block.
    {
        auto bad = forward;
        store_u32(bad.data() + packed_at + 24, max_player_data_bytes + 1);
        store_u32(bad.data() + packed_at, 0);
        const auto result = decode_message(bad.data(), bad.size(), m.get());
        OA_CHECK(result == ParseError::truncated || result == ParseError::unsupported);
    }
    // Every copy cut inside the packed player is refused.
    const std::size_t packed_end = packed_at + load_u32(forward.data() + packed_at);
    OA_CHECK(packed_end <= forward.size());
    for (std::size_t n = header_bytes; n < packed_end; ++n) {
        Bytes cut(forward.begin(), forward.begin() + static_cast<std::ptrdiff_t>(n));
        store_u32(cut.data(), (dplay_envelope_token << 20) | static_cast<uint32_t>(n));
        OA_CHECK(decode_message(cut.data(), cut.size(), m.get()) != ParseError::ok);
    }

    const PlayerInfo roster[2] = {sample_player(0x201), sample_player(0x301)};
    auto players = control([&](uint8_t* out, std::size_t cap) {
        return encode_super_enum_players_reply(
            host_endpoint, sample_desc(), "Game", nullptr, roster, 2, out, cap
        );
    });
    OA_CHECK(decode_message(players.data(), players.size(), m.get()) == ParseError::ok);
    OA_CHECK(m->roster_count == 2);
    {
        auto bad = players;
        store_u32(bad.data() + header_bytes, max_roster_players + 1);
        OA_CHECK(decode_message(bad.data(), bad.size(), m.get()) == ParseError::too_many_players);
        store_u32(bad.data() + header_bytes, 3); // a third player the message does not hold
        OA_CHECK(decode_message(bad.data(), bad.size(), m.get()) == ParseError::truncated);
    }
    {
        // A super-packed player whose fixed size wraps the offset.
        auto bad = players;
        const std::size_t first = dplay_envelope_bytes + load_u32(bad.data() + header_bytes + 8);
        store_u32(bad.data() + first, 0xfffffff8u);
        OA_CHECK(decode_message(bad.data(), bad.size(), m.get()) == ParseError::bad_offset);
    }

    // A name change whose names have no terminator before the end.
    auto names = control([&](uint8_t* out, std::size_t cap) {
        return encode_player_name_changed(host_endpoint, 0, 0x101, "Short", "Long", out, cap);
    });
    OA_CHECK(decode_message(names.data(), names.size(), m.get()) == ParseError::ok);
    {
        auto bad = names;
        bad.resize(bad.size() - 2); // drops the long name's terminator
        store_u32(bad.data(), (dplay_envelope_token << 20) | static_cast<uint32_t>(bad.size()));
        OA_CHECK(decode_message(bad.data(), bad.size(), m.get()) == ParseError::truncated);
    }

    // The envelope: a size past the input or below the envelope, or no token.
    {
        auto bad = names;
        store_u32(bad.data(), (dplay_envelope_token << 20) | static_cast<uint32_t>(bad.size() + 1));
        OA_CHECK(decode_message(bad.data(), bad.size(), m.get()) == ParseError::truncated);
        store_u32(bad.data(), (dplay_envelope_token << 20) | 19u);
        OA_CHECK(decode_message(bad.data(), bad.size(), m.get()) == ParseError::truncated);
        store_u32(bad.data(), 0);
        OA_CHECK(decode_message(bad.data(), bad.size(), m.get()) == ParseError::not_dplay);
        OA_CHECK(stream_message_size(bad.data()) == 0);
        store_u32(
            bad.data(), (dplay_envelope_token << 20) | static_cast<uint32_t>(max_message_bytes + 1)
        );
        OA_CHECK(stream_message_size(bad.data()) == 0);
    }
    DataMessage data{};
    const uint8_t short_data[] = {0x18, 0x00, 0xb0, 0xfa};
    OA_CHECK(decode_stream_data(short_data, sizeof short_data, &data) == ParseError::truncated);
}

void envelope_and_blocks_refuse_short_input() {
    DplayEnvelope envelope{};
    std::size_t next = 0;
    const uint8_t three[3] = {};
    OA_CHECK(decode_dplay_envelope(three, sizeof three, &envelope, &next) == WireError::truncated);
    uint8_t word[24] = {};
    store_u32(word, (dplay_envelope_token << 20) | 24u);
    OA_CHECK(decode_dplay_envelope(word, 12, &envelope, &next) == WireError::truncated);
    OA_CHECK(decode_dplay_envelope(word, 24, &envelope, &next) == WireError::ok);
    OA_CHECK(envelope.body_size == 4 && next == 24);
    store_u32(word, (dplay_envelope_token << 20) | 0xfffffu); // past the input
    OA_CHECK(decode_dplay_envelope(word, 24, &envelope, &next) == WireError::ok);
    OA_CHECK(next == 24 && envelope.body_size == 4);
    store_u32(word, 0x12345678);
    OA_CHECK(decode_dplay_envelope(word, 24, &envelope, &next) == WireError::invalid_type);

    uint8_t block[create_player_data_bytes + 1] = {};
    CreatePlayerData decoded{};
    OA_CHECK(
        decode_create_player_data(block, create_player_data_bytes - 1, &decoded) ==
        WireError::truncated
    );
    OA_CHECK(
        decode_create_player_data(block, sizeof block, &decoded) == WireError::length_mismatch
    );
    OA_CHECK(check_create_player_data(nullptr, 0, false, nullptr) == JoinVerdict::version_mismatch);
    // A tag of 17 bytes with no terminator still compares within its bound.
    std::memset(block, 'A', 17);
    store_u16(block + 0x13, create_player_version_high);
    OA_CHECK(
        check_create_player_data(block, create_player_data_bytes, true, "aaaaaaaaaaaaaaaaa") ==
        JoinVerdict::accepted
    );
    OA_CHECK(
        check_create_player_data(block, create_player_data_bytes, true, "A") ==
        JoinVerdict::wrong_password
    );

    uint8_t desc[session_desc_bytes] = {};
    SessionDesc session{};
    OA_CHECK(decode_session_desc(desc, session_desc_bytes - 1, &session) == WireError::truncated);
    OA_CHECK(decode_session_desc(desc, session_desc_bytes, &session) == WireError::ok);
}

void system_images_refuse_short_and_outside_data() {
    uint8_t image[0x60] = {};
    store_u32(image, static_cast<uint32_t>(SystemMessageType::player_created));
    store_u32(image + 4, system_message::player_type_player);
    store_u32(image + system_message::create_id, 0x101);
    CreatePlayerView create{};
    OA_CHECK(decode_create_player_image(image, system_message::create_bytes, &create));
    OA_CHECK(!decode_create_player_image(image, system_message::create_bytes - 1, &create));
    // Data that runs past the image, and a size with no data.
    store_u32(image + system_message::create_data, system_message::create_bytes);
    store_u32(image + system_message::create_data_size, 0x31);
    OA_CHECK(!decode_create_player_image(image, sizeof image, &create));
    store_u32(image + system_message::create_data, 0);
    store_u32(image + system_message::create_data_size, 4);
    OA_CHECK(!decode_create_player_image(image, sizeof image, &create));
    store_u32(image + system_message::create_data_size, 0);
    // A name outside the image, then one with no terminator inside it.
    const std::size_t short_at = system_message::create_name + system_message::name_short;
    store_u32(image + short_at, sizeof image);
    OA_CHECK(!decode_create_player_image(image, sizeof image, &create));
    std::memset(image + 0x50, 'x', 0x10);
    store_u32(image + short_at, 0x50);
    OA_CHECK(!decode_create_player_image(image, sizeof image, &create));
    image[0x5f] = '\0';
    OA_CHECK(decode_create_player_image(image, sizeof image, &create));
    OA_CHECK(create.short_name != nullptr && std::strlen(create.short_name) == 0x0f);

    uint8_t data[system_message::player_data_bytes + 8] = {};
    store_u32(data, static_cast<uint32_t>(SystemMessageType::player_data_changed));
    store_u32(data + 4, system_message::player_type_player);
    store_u32(data + system_message::player_data_data, system_message::player_data_bytes);
    store_u32(data + system_message::player_data_size, 9);
    PlayerDataView data_view{};
    OA_CHECK(!decode_player_data_image(data, sizeof data, &data_view));
    store_u32(data + system_message::player_data_size, 8);
    OA_CHECK(decode_player_data_image(data, sizeof data, &data_view));
    OA_CHECK(!decode_player_data_image(data, system_message::player_data_bytes - 1, &data_view));

    uint8_t name[system_message::player_name_bytes + 4] = {};
    store_u32(name, static_cast<uint32_t>(SystemMessageType::player_name_changed));
    store_u32(name + 4, system_message::player_type_player);
    store_u32(name + system_message::player_name_name + system_message::name_long, sizeof name);
    PlayerNameView name_view{};
    OA_CHECK(!decode_player_name_image(name, sizeof name, &name_view));
    OA_CHECK(!decode_player_name_image(name, system_message::player_name_bytes - 1, &name_view));

    uint32_t type = 0;
    OA_CHECK(!read_system_message_type(image, 3, &type));
    OA_CHECK(read_system_message_type(image, 4, &type) && type == 3);
    uint8_t destroyed[system_message::destroy_bytes] = {};
    store_u32(destroyed, static_cast<uint32_t>(SystemMessageType::player_destroyed));
    store_u32(destroyed + 4, system_message::player_type_player);
    store_u32(destroyed + system_message::destroy_id, 0x202);
    PlayerDestroyedView view{};
    OA_CHECK(!decode_player_destroyed_image(destroyed, system_message::destroy_id_end - 1, &view));
    OA_CHECK(decode_player_destroyed_image(destroyed, system_message::destroy_id_end, &view));
    OA_CHECK(view.id == 0x202 && view.player_type == system_message::player_type_player);
    OA_CHECK(!decode_player_destroyed_image(image, sizeof image, &view));
}

void records_refuse_wrong_lengths() {
    uint16_t length = 0;
    const uint8_t none[1] = {};
    OA_CHECK(record_wire_length(none, 0, &length) == WireError::truncated);
    for (const uint8_t type :
         {uint8_t{0x00}, uint8_t{0x01}, uint8_t{0x2d}, uint8_t{0xfe}, uint8_t{0xff}}) {
        const uint8_t bytes[4] = {type};
        OA_CHECK(record_wire_length(bytes, sizeof bytes, &length) == WireError::invalid_type);
        AnyRecord any{};
        OA_CHECK(decode_any_record(bytes, sizeof bytes, &any) == WireError::invalid_type);
    }
    for (const uint8_t type : {uint8_t{0x04}, uint8_t{0x2b}}) {
        const uint8_t bytes[4] = {type};
        OA_CHECK(record_wire_length(bytes, sizeof bytes, &length) == WireError::zero_length_record);
    }
    // Each fixed record one byte short and one byte long.
    for (uint8_t type = first_record_type; type < last_record_type; ++type) {
        const auto table = record_length_table[type];
        if (table == 0)
            continue;
        Bytes bytes(table + 1u, 0);
        bytes[0] = type;
        AnyRecord any{};
        OA_CHECK(decode_any_record(bytes.data(), table, &any) == WireError::ok);
        OA_CHECK(decode_any_record(bytes.data(), table - 1u, &any) == WireError::truncated);
        OA_CHECK(decode_any_record(bytes.data(), table + 1u, &any) == WireError::length_mismatch);
    }
    // A unit state's own length below its header, past the input, or 0.
    uint8_t state[16] = {0x2c};
    UnitStateRecord header{};
    for (const uint16_t declared : {uint16_t{0}, uint16_t{6}, uint16_t{17}, uint16_t{0xffff}}) {
        store_u16(state + 1, declared);
        OA_CHECK(decode_unit_state_record(state, sizeof state, &header) != WireError::ok);
    }
    store_u16(state + 1, 0);
    OA_CHECK(record_wire_length(state, sizeof state, &length) == WireError::zero_length_record);
    OA_CHECK(record_wire_length(state, 2, &length) == WireError::truncated);
}

void unit_states_refuse_overruns_and_endless_lists() {
    constexpr unsigned def_bits = 4;
    const auto good = sample_unit_state(def_bits);
    const UnitDeltaCodec codec{nullptr, read_ground, nullptr};
    UnitStateDecodeOptions options{};
    options.def_index_bits = def_bits;
    options.delta_codec = &codec;
    auto body = std::make_unique<UnitStateBody>();
    OA_CHECK(decode_unit_state(good.data(), good.size(), options, body.get()) == WireError::ok);
    OA_CHECK(body->entry_count == 2 && body->has_full_record);
    // Cut short with its length field following: the body runs out.
    for (std::size_t n = unit_state_header_bytes; n + 1 < good.size(); ++n) {
        Bytes cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(n));
        store_u16(cut.data() + 1, static_cast<uint16_t>(n));
        OA_CHECK(
            decode_unit_state(cut.data(), cut.size(), options, body.get()) == WireError::truncated
        );
    }
    // Without a codec a listed unit cannot be read.
    UnitStateDecodeOptions no_codec = options;
    no_codec.delta_codec = nullptr;
    OA_CHECK(
        decode_unit_state(good.data(), good.size(), no_codec, body.get()) ==
        WireError::unsupported_delta_layout
    );
    UnitStateDecodeOptions wide = options;
    wide.def_index_bits = unit_state_max_def_index_bits + 1;
    OA_CHECK(
        decode_unit_state(good.data(), good.size(), wide, body.get()) == WireError::bad_argument
    );
    // A list longer than any player's units: stopped at the entry limit.
    Bytes storage(unit_state_writer_words * bit_stream_word_bytes * 4);
    BitWriter writer;
    bit_writer_init(&writer, storage.data(), storage.size() / bit_stream_word_bytes);
    unit_state_begin(&writer, 1);
    for (std::size_t i = 0; i <= unit_state_max_entries; ++i) {
        unit_state_write_entry_header(&writer, 1, 1, def_bits);
        write_waypoint_delta(&writer, WaypointDelta{});
    }
    uint16_t length = 0;
    OA_CHECK(unit_state_finish(&writer, FullUnitRecord{}, def_bits, &length) == WireError::ok);
    OA_CHECK(decode_unit_state(storage.data(), length, options, body.get()) == WireError::overflow);
}

void frames_refuse_oversized_and_overrunning_records() {
    auto receiver = std::make_unique<FrameReceiver>();
    frame_receiver_init(receiver.get());
    FrameDeliveries deliveries{};
    Bytes frame(frame_header_bytes, 0);
    OA_CHECK(
        frame_receiver_accept(receiver.get(), 7, 0, frame.data(), frame.size(), &deliveries) ==
        WireError::truncated
    );
    Bytes huge(max_receive_frame_bytes + 1, 0x02);
    OA_CHECK(
        frame_receiver_accept(receiver.get(), 7, 0, huge.data(), huge.size(), &deliveries) ==
        WireError::overflow
    );
    OA_CHECK(
        frame_receiver_accept(receiver.get(), 0, 0, huge.data(), 8, &deliveries) ==
        WireError::bad_argument
    );

    auto peer = std::make_unique<PeerRecords>();
    peer_records_reset(peer.get());
    UnpackOutcome outcome{};
    OA_CHECK(
        unpack_frame_records(peer.get(), huge.data(), huge.size(), 0, 7, 0, true, &outcome) ==
        WireError::overflow
    );
    // A unit state whose length runs past the frame ends the frame there.
    Bytes overrun(frame_header_bytes, 0);
    overrun.insert(overrun.end(), {0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    overrun.insert(overrun.end(), {0x2c, 0xff, 0x7f, 0, 0, 0, 0});
    OA_CHECK(
        unpack_frame_records(peer.get(), overrun.data(), overrun.size(), 0, 7, 0, true, &outcome) ==
        WireError::ok
    );
    OA_CHECK(peer->count == 1);
    const uint8_t* data = nullptr;
    uint16_t length = 0;
    OA_CHECK(pop_due_record(peer.get(), 0, &data, &length) && length == 13 && data[0] == 0x02);
    OA_CHECK(!pop_due_record(peer.get(), 0, &data, &length));
    // A unit state at the very end of the frame, its length bytes missing.
    Bytes tail(frame_header_bytes, 0);
    tail.push_back(0x2c);
    // Its length comes from what an earlier frame left after it, as in 3.1c,
    // but no record it yields reaches past the frame.
    const auto tail_result =
        unpack_frame_records(peer.get(), tail.data(), tail.size(), 0, 7, 0, true, &outcome);
    OA_CHECK(tail_result == WireError::ok || tail_result == WireError::zero_length_record);
    while (pop_due_record(peer.get(), 0, &data, &length))
        OA_CHECK(data + length <= peer->copy + peer->copy_size);
    // A type with no length stops the frame.
    peer_records_reset(peer.get());
    Bytes zero(frame_header_bytes, 0);
    zero.push_back(0x04);
    OA_CHECK(
        unpack_frame_records(peer.get(), zero.data(), zero.size(), 0, 7, 0, true, &outcome) ==
        WireError::zero_length_record
    );
}

void condenser_frames_are_dropped_not_thrown() {
    namespace cd = network::condenser;
    const Bytes payload(40, 'A');
    const auto sealed = network::encode_frame(payload, true);
    OA_CHECK(sealed[0] == cd::compressed_frame);
    OA_CHECK(network::unwrap_frame(sealed).ok());
    const auto problem = [](const Bytes& frame) {
        const auto result = network::unwrap_frame(frame);
        return result.ok() ? network::FrameProblem::none
                           : static_cast<network::FrameProblem>(result.error.detail);
    };
    OA_CHECK(problem(Bytes{3, 0, 0}) == network::FrameProblem::bad_length);
    OA_CHECK(problem(Bytes(cd::maximum_frame_bytes + 1, 3)) == network::FrameProblem::bad_length);
    OA_CHECK(problem(Bytes{9, 0, 0, 1}) == network::FrameProblem::unsupported_type);
    auto bad_sum = sealed;
    bad_sum[1] ^= 1;
    OA_CHECK(problem(bad_sum) == network::FrameProblem::checksum_mismatch);
    // A compressed body cut before its end marker, resealed so the checksum holds.
    Bytes cut = network::encode_stored_frame(Bytes{0x00, 'A'});
    cut[0] = cd::compressed_frame;
    OA_CHECK(problem(cut) == network::FrameProblem::bad_body);

    struct Fixed final : network::ReceiveTransport {
        Bytes datagram;

        uint32_t receive(std::span<uint8_t> buffer, uint32_t& size) override {
            if (datagram.size() > size)
                return cd::buffer_too_small;
            std::copy(datagram.begin(), datagram.end(), buffer.begin());
            size = static_cast<uint32_t>(datagram.size());
            return cd::success;
        }

        bool condenser_enabled() const override { return true; }

        void record_received_bytes(uint32_t) override {}
    };

    Fixed transport;
    network::CondenserReceiver receiver;
    Bytes buffer(cd::maximum_frame_bytes);
    transport.datagram = cut;
    auto size = static_cast<uint32_t>(buffer.size());
    OA_CHECK(receiver.receive(transport, buffer, size) == cd::no_message);
    OA_CHECK(!receiver.has_pending_message());
    transport.datagram = network::encode_stored_frame(Bytes(cd::receive_storage_bytes + 1, 'B'));
    size = static_cast<uint32_t>(buffer.size());
    OA_CHECK(receiver.receive(transport, buffer, size) == cd::no_message);

    // The game's own receiver drops the same frames.
    for (const Bytes& frame : {cut, transport.datagram, bad_sum}) {
        auto condenser = std::make_unique<Condenser>();
        condenser_init(condenser.get());
        OneDatagram one{&frame};
        const NetTransport net{&one, nullptr, one_receive};
        Bytes out(max_receive_frame_bytes + cd::maximum_frame_bytes);
        uint32_t from = 0;
        uint32_t to = 0;
        auto out_size = static_cast<uint32_t>(out.size());
        OA_CHECK(
            condenser_receive(condenser.get(), net, nullptr, &from, &to, out.data(), &out_size) ==
            transport_result::no_messages
        );
    }
}

void recordings_refuse_bad_chunks_and_records() {
    // A payload's 0x2c with a length below its header, a tick base cut
    // short and an elided state shorter than its header.
    for (const Bytes& payload :
         {Bytes{tad::payload_marker, 0x2c, 6, 0},
          Bytes{tad::payload_marker, 0xfe, 1, 2},
          Bytes{tad::payload_marker, 0xfd, 2, 0},
          Bytes{tad::payload_marker, 0x2c, 0xff, 0xff, 0}}) {
        const auto split = tad::split_records(payload);
        OA_CHECK(split.error.has_value() && split.records.empty());
    }
    // An elided state whose full length disagrees with its own is not expanded.
    tad::Record record{0xfd, {}, 5};
    const uint8_t elided[] = {0xfd, 0x20, 0x00, 1, 2, 3};
    record.bytes = elided;
    OA_CHECK(tad::expand_unit_state(record).empty());
    // A compressed payload that decodes past the condenser's storage.
    Bytes long_stream{tad::compressed_payload_marker};
    const auto many =
        oa::formats::sqsh::encode_lz77(Bytes(tad::limit::decoded_payload_bytes + 1, 0), 1 << 20);
    OA_CHECK(many.ok());
    long_stream.insert(long_stream.end(), many.value->begin(), many.value->end());
    OA_CHECK(!tad::decode_payload(long_stream).ok());
}

// ---- the mutation pass ----

void mutated_input_stays_in_bounds(const std::vector<std::filesystem::path>& recordings) {
    constexpr unsigned def_bits = 4;
    std::mt19937 rng(0x0a7a5eed);
    auto engine = hosting_engine();
    uint32_t now = 1;
    for (const auto& seed : control_seeds()) {
        feed_control(seed, engine.get(), now);
        for (int i = 0; i < mutations_per_seed; ++i)
            feed_control(mutated(seed, rng), engine.get(), ++now);
    }

    std::vector<Bytes> frames{sample_frame(def_bits)};
    std::vector<Bytes> datagrams{
        network::encode_frame(frames[0], true), network::encode_frame(frames[0], false)
    };
    for (const auto& path : recordings) {
        std::ifstream in(path, std::ios::binary);
        const Bytes file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        OA_CHECK(!file.empty());
        feed_recording(file, def_bits);
        const auto parsed = tad::parse(file);
        OA_CHECK(parsed.ok());
        if (!parsed.ok())
            continue;
        for (const auto& status : parsed.demo->statuses)
            datagrams.emplace_back(status.datagram.begin(), status.datagram.end());
        const auto& packets = parsed.demo->packets;
        const std::size_t step = std::max<std::size_t>(1, packets.size() / recorded_seeds_per_file);
        for (std::size_t i = 0; i < packets.size(); i += step) {
            const auto decoded = tad::decode_payload(packets[i].payload);
            if (!decoded.ok() || decoded.bytes.size() < 2)
                continue;
            Bytes frame(frame_header_bytes, 0);
            frame.insert(frame.end(), decoded.bytes.begin() + 1, decoded.bytes.end());
            frames.push_back(std::move(frame));
        }
        // The recording itself, mutated byte by byte, as a damaged file.
        for (int i = 0; i < 20; ++i)
            feed_recording(mutated(file, rng), def_bits);
    }
    for (const auto& frame : frames) {
        feed_frame(frame, def_bits);
        for (int i = 0; i < mutations_per_seed / 10 + 1; ++i)
            feed_frame(mutated(frame, rng), def_bits);
    }
    for (const auto& datagram : datagrams) {
        feed_datagram(datagram, def_bits);
        for (int i = 0; i < mutations_per_seed / 10 + 1; ++i)
            feed_datagram(mutated(datagram, rng), def_bits);
    }
    std::printf(
        "net-wire-malformed: %zu control seeds, %zu frames, %zu datagrams, %zu recordings "
        "mutated\n",
        control_seeds().size(),
        frames.size(),
        datagrams.size(),
        recordings.size()
    );
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::filesystem::path> recordings;
    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path path = argv[i];
        if (std::filesystem::is_directory(path)) {
            for (const auto& entry : std::filesystem::directory_iterator(path))
                if (entry.path().extension() == ".tad")
                    recordings.push_back(entry.path());
        } else {
            recordings.push_back(path);
        }
    }
    std::sort(recordings.begin(), recordings.end());
    control_messages_refuse_bad_offsets_and_counts();
    envelope_and_blocks_refuse_short_input();
    system_images_refuse_short_and_outside_data();
    records_refuse_wrong_lengths();
    unit_states_refuse_overruns_and_endless_lists();
    frames_refuse_oversized_and_overrunning_records();
    condenser_frames_are_dropped_not_thrown();
    recordings_refuse_bad_chunks_and_records();
    mutated_input_stays_in_bounds(recordings);
    return oa::test::check_exit_status();
}
