// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The wire forms a mod profile's network rules add: the version rule, the
// private chat channel, the recorder's records, messages and commands, the
// frame split with recorder records, and the commander start sync record.
// The byte vectors are records two machines of such a game sent each other.

#include "oa/netgame/frame.hpp"
#include "oa/netgame/private_channel.hpp"
#include "oa/netgame/recorder_messages.hpp"
#include "oa/netgame/recorder_session.hpp"
#include "oa/netgame/unit_state.hpp"
#include "oa/netgame/wire_rules.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string_view>
#include <vector>

using namespace oa::netgame;
using Bytes = std::vector<uint8_t>;

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

void start_case(const char* name) {
    current_test = name;
    std::printf("wire rules: %s\n", name);
    std::fflush(stdout);
}

// A whole 65-byte private record: the given head, then zeros.
Bytes private_record(std::initializer_list<uint8_t> head) {
    Bytes out(head);
    out.resize(private_record_bytes, 0);
    return out;
}

// 3.1c compares a game's major as at least the local one; a game whose
// version is not 3.1 needs the same major.
void version_rules() {
    start_case("version_rules");
    WireRules base{};
    CHECK(version_admits(base, 3));
    CHECK(version_admits(base, 10));
    CHECK(!version_admits(base, 2));
    WireRules equal{};
    equal.version_major = 10;
    equal.version_minor = 2;
    equal.version_rule = VersionRule::equal;
    CHECK(version_admits(equal, 10));
    CHECK(!version_admits(equal, 3));
    CHECK(!version_admits(equal, 11));
    CHECK(WireRules{} == base);
}

// A challenge one machine sent another at tick 180: sub-id 0x2b, the
// length word 0x41, op 1 and 32 random bytes.
void captured_challenge_decodes() {
    start_case("captured_challenge_decodes");
    const auto record = private_record({0x05, 0x00, 0x2b, 0x41, 0x00, 0x01, 0xb3, 0xe9, 0x24, 0x5a,
                                        0x1d, 0xb0, 0x73, 0x6c, 0x2a, 0x3d, 0xff, 0xa8, 0x7c, 0xae,
                                        0x04, 0x13, 0x80, 0xce, 0x7a, 0xbb, 0x2e, 0x9a, 0xf0, 0x96,
                                        0x35, 0x53, 0xa3, 0xd1, 0xcc, 0x1c, 0x8a, 0x46});
    CHECK(is_private_chat(record.data(), record.size()));
    PrivateMessage message{};
    CHECK(decode_private_message(record.data(), record.size(), &message) == WireError::ok);
    CHECK(message.sub_id == private_sub_integrity && message.op == integrity_op_challenge);
    CHECK(message.payload[0] == 0xb3 && message.payload[31] == 0x46 && message.payload[32] == 0);
    CHECK(private_message_accepted(PrivateChannel::sub_id_dispatch, message));
    CHECK(private_message_accepted(PrivateChannel::integrity_only, message));
    CHECK(!private_message_accepted(PrivateChannel::none, message));
    uint8_t again[private_record_bytes];
    std::size_t written = 0;
    CHECK(encode_private_message(message, again, sizeof again, &written) == WireError::ok);
    CHECK(written == record.size() && std::memcmp(again, record.data(), written) == 0);
}

// A timeout vote one machine proposed on a silent player: op 1, the
// target's id, flag 6.
void captured_vote_decodes() {
    start_case("captured_vote_decodes");
    const auto record =
        private_record({0x05, 0x00, 0x2c, 0x41, 0x00, 0x01, 0x18, 0x23, 0x47, 0x00, 0x06});
    PrivateMessage message{};
    CHECK(decode_private_message(record.data(), record.size(), &message) == WireError::ok);
    CHECK(message.sub_id == private_sub_vote && message.op == vote_op_propose);
    CHECK(
        load_u32(message.payload + vote_target_offset) == 4662040u
    ); // the silent player's transport id
    CHECK(message.payload[vote_flag_offset] == vote_flag_timeout);
    CHECK(private_message_accepted(PrivateChannel::sub_id_dispatch, message));
    // The integrity-only form takes integrity messages alone.
    CHECK(!private_message_accepted(PrivateChannel::integrity_only, message));
}

// Malformed private records are refused with what is wrong.
void malformed_private_records() {
    start_case("malformed_private_records");
    PrivateMessage message{};
    auto record = private_record({0x05, 0x00, 0x2b, 0x41, 0x00, 0x01});
    CHECK(decode_private_message(record.data(), 40, &message) == WireError::truncated);
    record[3] = 0x40;
    CHECK(
        decode_private_message(record.data(), record.size(), &message) == WireError::length_mismatch
    );
    const auto plain = private_record({0x05, 'h', 'i'});
    CHECK(!is_private_chat(plain.data(), plain.size()));
    CHECK(decode_private_message(plain.data(), plain.size(), &message) == WireError::bad_argument);
    // The integrity-only form drops an op outside 1..7, 0x20, 0x21.
    PrivateMessage odd{};
    odd.sub_id = private_sub_integrity;
    odd.op = 0x08;
    CHECK(!private_message_accepted(PrivateChannel::integrity_only, odd));
    odd.op = integrity_op_data_reply;
    CHECK(private_message_accepted(PrivateChannel::integrity_only, odd));
    uint8_t small[10];
    std::size_t written = 0;
    CHECK(
        encode_private_message(odd, small, sizeof small, &written) == WireError::buffer_too_small
    );
}

// The recorder's record lengths: 0xfb by its length byte, the others fixed
// or by their word.
void recorder_record_lengths() {
    start_case("recorder_record_lengths");
    uint16_t length = 0;
    const uint8_t warp_done[] = {0xfb, 0x00, 0x01};
    CHECK(recorder_record_length(warp_done, sizeof warp_done, &length) == WireError::ok);
    CHECK(length == 3);
    const uint8_t options[] = {0xfb, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01, 0x14};
    CHECK(recorder_record_length(options, sizeof options, &length) == WireError::ok);
    CHECK(length == 9);
    const uint8_t camera[] = {0xfc, 0x00, 0x01, 0xd0, 0x00};
    CHECK(recorder_record_length(camera, sizeof camera, &length) == WireError::ok);
    CHECK(length == 5);
    const uint8_t chat[] = {0xf9};
    CHECK(recorder_record_length(chat, 1, &length) == WireError::ok && length == 0x49);
    const uint8_t replayer[] = {0xfa};
    CHECK(recorder_record_length(replayer, 1, &length) == WireError::ok && length == 1);
    const uint8_t state[] = {0xfd, 0x10, 0x00};
    CHECK(recorder_record_length(state, sizeof state, &length) == WireError::ok);
    CHECK(length == 0x0c);
    const uint8_t short_state[] = {0xfd, 0x03, 0x00};
    CHECK(
        recorder_record_length(short_state, sizeof short_state, &length) ==
        WireError::zero_length_record
    );
    CHECK(recorder_record_length(warp_done, 1, &length) == WireError::truncated);
    const uint8_t game[] = {0x2c};
    CHECK(recorder_record_length(game, 1, &length) == WireError::invalid_type);
    CHECK(is_recorder_record_type(0xf6) && !is_recorder_record_type(0xf7));
}

// The host's options a recorder sent a joiner before any lock was set, and
// a warp-done message, both as their records carry them.
void captured_recorder_messages() {
    start_case("captured_recorder_messages");
    const uint8_t options_record[] = {0xfb, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01, 0x14};
    RecorderMessage message{};
    CHECK(
        decode_recorder_message(options_record, sizeof options_record, &message) == WireError::ok
    );
    CHECK(message.kind == RecorderMessageKind::host_options && message.size == 6);
    RecorderHostOptions options{};
    CHECK(decode_recorder_host_options(message.payload, message.size, &options) == WireError::ok);
    CHECK(options.autopause == 0 && options.commander_warp == 0 && options.speed_lock == 0);
    CHECK(options.speed_high == 1 && options.speed_low == 20);
    uint8_t payload[recorder_host_options_bytes];
    encode_recorder_host_options(RecorderHostOptions{}, payload);
    uint8_t record[recorder_message_header_bytes + sizeof payload];
    std::size_t written = 0;
    CHECK(
        encode_recorder_message(
            RecorderMessageKind::host_options,
            payload,
            sizeof payload,
            record,
            sizeof record,
            &written
        ) == WireError::ok
    );
    CHECK(written == sizeof options_record);
    CHECK(std::memcmp(record, options_record, written) == 0);

    uint8_t warp[3];
    CHECK(
        encode_recorder_message(
            RecorderMessageKind::warp_done, nullptr, 0, warp, sizeof warp, &written
        ) == WireError::ok
    );
    CHECK(written == 3 && warp[0] == 0xfb && warp[1] == 0x00 && warp[2] == 0x01);
    const uint8_t longer[] = {0xfb, 0x00, 0x01, 0x00};
    CHECK(decode_recorder_message(longer, sizeof longer, &message) == WireError::length_mismatch);
    CHECK(decode_recorder_message(options_record, 2, &message) == WireError::truncated);

    uint8_t camera[recorder_camera_bytes];
    encode_recorder_camera(0x0100, 0x00d0, camera);
    const uint8_t captured_camera[] = {0xfc, 0x00, 0x01, 0xd0, 0x00};
    CHECK(std::memcmp(camera, captured_camera, sizeof camera) == 0);
}

// Chat lines carry the recorder's commands, with the sender's name first.
void recorder_commands_parse() {
    start_case("recorder_commands_parse");
    auto line = parse_recorder_command("<Alpha> .syncon 0 5");
    CHECK(line.command == RecorderCommand::speed_lock && line.first == 0 && line.second == 5);
    CHECK(parse_recorder_command(".AUTOPAUSE").command == RecorderCommand::autopause);
    CHECK(parse_recorder_command("<Bravo> .cmdwarp").command == RecorderCommand::commander_warp);
    line = parse_recorder_command(".crcreport");
    CHECK(line.command == RecorderCommand::integrity_report && line.integrity_op == 7);
    CHECK(parse_recorder_command(".record xrec").argument == std::string_view("xrec"));
    CHECK(parse_recorder_command("hello").command == RecorderCommand::none);
    CHECK(parse_recorder_command("<Alpha> hello .report").command == RecorderCommand::none);
    CHECK(parse_recorder_command(".nosuch").command == RecorderCommand::none);
    CHECK(parse_recorder_command(nullptr).command == RecorderCommand::none);
    CHECK(recorder_command_host_only(RecorderCommand::speed_lock));
    CHECK(!recorder_command_host_only(RecorderCommand::report));
    CHECK(recorder_session_command(RecorderCommand::random_map));
    CHECK(!recorder_session_command(RecorderCommand::commander_warp));

    CHECK(recorder_speed_command(RecorderCommand::speed_lock));
    CHECK(recorder_speed_command(RecorderCommand::speed_unlock));
    CHECK(!recorder_speed_command(RecorderCommand::autopause));

    RecorderSession session{};
    uint8_t low = 0;
    uint8_t high = 0;
    recorder_speed_range(session, 0, 20, &low, &high);
    CHECK(low == 0 && high == 20);
    recorder_speed_range(session, 1, 20, &low, &high);
    CHECK(low == 1 && high == 20);
    CHECK(recorder_apply_host_command(session, parse_recorder_command(".syncon 0 5")));
    recorder_speed_range(session, 0, 20, &low, &high);
    CHECK(low == 10 && high == 15);
    // The rules' range narrows the lock further.
    recorder_speed_range(session, 12, 18, &low, &high);
    CHECK(low == 12 && high == 15);
    recorder_speed_range(session, 0, 8, &low, &high);
    CHECK(low == 8 && high == 8);
    CHECK(session.options.speed_lock == 1 && session.options.speed_low == 0);
    CHECK(session.options.speed_high == 5);
    CHECK(recorder_apply_host_command(session, parse_recorder_command(".syncon -5 20")));
    recorder_speed_range(session, 0, 20, &low, &high);
    CHECK(low == 5 && high == 20);
    CHECK(!recorder_apply_host_command(session, parse_recorder_command(".syncon -5 20")));
    CHECK(recorder_apply_host_command(session, parse_recorder_command(".syncoff")));
    recorder_speed_range(session, 0, 20, &low, &high);
    CHECK(low == 0 && high == 20);
    CHECK(!recorder_apply_host_command(session, parse_recorder_command(".report")));
    // .cmdwarp turns the warp on, and the next one off.
    CHECK(recorder_apply_host_command(session, parse_recorder_command(".cmdwarp")));
    CHECK(session.options.commander_warp == 1);
    CHECK(recorder_apply_host_command(session, parse_recorder_command(".cmdwarp")));
    CHECK(session.options.commander_warp == 0);
}

// .give and .stopgive name players by whole words, matched without case;
// any player asks for its base with .dobase, while .base and .baseoff are
// the host's.
void recorder_take_and_base_commands_parse() {
    start_case("recorder_take_and_base_commands_parse");
    auto line = parse_recorder_command("<Alpha> .give bravo Charlie");
    CHECK(line.command == RecorderCommand::give);
    CHECK(recorder_arguments_name(line, "Bravo", 16));
    CHECK(recorder_arguments_name(line, "CHARLIE\0junk", 16));
    CHECK(!recorder_arguments_name(line, "Brav", 16));
    CHECK(!recorder_arguments_name(line, "Alpha", 16));
    CHECK(!recorder_arguments_name(line, "", 16));
    line = parse_recorder_command(".stopgive Bravo");
    CHECK(line.command == RecorderCommand::stop_give && recorder_arguments_name(line, "bravo", 5));
    CHECK(parse_recorder_command(".TAKECMD").command == RecorderCommand::take_commander);
    CHECK(parse_recorder_command(".take").command == RecorderCommand::take);
    CHECK(!recorder_command_host_only(RecorderCommand::do_base));
    CHECK(recorder_command_host_only(RecorderCommand::base_file));
    CHECK(recorder_command_host_only(RecorderCommand::base_off));
    CHECK(parse_recorder_command(".base base.txt").argument == std::string_view("base.txt"));
}

// A base file: comments and blank lines skipped, the count first, then one
// building a line, "entry type x z health;", numbers signed or '$' hex.
// Reading stops at the first line in error, keeping what came before.
void recorder_base_files_read() {
    start_case("recorder_base_files_read");
    auto base = std::make_unique<RecorderBase>();
    CHECK(
        recorder_read_base("; buildings\r\n\r\n15\r\n1 132 -100 140 2500;\r\n", *base) ==
        RecorderBaseRead::read
    );
    CHECK(base->per_side == 15);
    CHECK(base->entries[1].unit_type == 132 && base->entries[1].offset_x == -100);
    CHECK(base->entries[1].offset_z == 140 && base->entries[1].health == 2500);
    CHECK(
        recorder_read_base("2\n1000 $1F +5 -32768 65535; trailing\n", *base) ==
        RecorderBaseRead::read
    );
    CHECK(base->per_side == 2 && base->entries[1000].unit_type == 31);
    CHECK(base->entries[1000].offset_x == 5 && base->entries[1000].offset_z == -32768);
    CHECK(base->entries[1000].health == 65535 && base->entries[1].unit_type == 132);
    CHECK(recorder_read_base("fifteen\n", *base) == RecorderBaseRead::bad_count);
    CHECK(recorder_read_base("15 \n", *base) == RecorderBaseRead::bad_count);
    CHECK(recorder_read_base("2\n1001 1 0 0 1;\n", *base) == RecorderBaseRead::bad_entry);
    CHECK(recorder_read_base("2\n1  1 0 0 1;\n", *base) == RecorderBaseRead::bad_type);
    CHECK(recorder_read_base("2\n1 70000 0 0 1;\n", *base) == RecorderBaseRead::bad_type);
    CHECK(recorder_read_base("2\n1 1 x 0 1;\n", *base) == RecorderBaseRead::bad_offset_x);
    CHECK(recorder_read_base("2\n1 1 0 40000 1;\n", *base) == RecorderBaseRead::bad_offset_z);
    CHECK(recorder_read_base("2\n1 1 0 0 1\n", *base) == RecorderBaseRead::bad_health);
    CHECK(recorder_read_base("2\n1 1 0 0 -1;\n", *base) == RecorderBaseRead::bad_health);
    // A line in error keeps what the lines before it read.
    // The third number needs the space after it.
    CHECK(recorder_read_base("3\n2 7 1 1 1;\n3 8 1\n", *base) == RecorderBaseRead::bad_offset_x);
    CHECK(base->per_side == 3 && base->entries[2].unit_type == 7);
    CHECK(
        recorder_base_read_text(RecorderBaseRead::bad_count) ==
        std::string_view("Erroneous number of possible buildings")
    );
    CHECK(
        recorder_base_read_text(RecorderBaseRead::bad_health) ==
        std::string_view("Erroneous base file5")
    );
    CHECK(recorder_base_read_text(RecorderBaseRead::read)[0] == '\0');

    auto standard = std::make_unique<RecorderBase>();
    recorder_standard_base(*standard);
    CHECK(standard->per_side == 15);
    CHECK(standard->entries[1].unit_type == 132 && standard->entries[15].unit_type == 52);
    CHECK(standard->entries[16].unit_type == 274 && standard->entries[30].unit_type == 184);
    CHECK(standard->entries[3].offset_z == 40 && standard->entries[18].offset_z == 60);
    CHECK(standard->entries[30].offset_x == 190 && standard->entries[30].offset_z == -160);
    CHECK(standard->entries[30].health == 1700 && standard->entries[31].unit_type == 0);
}

// A frame whose last record is a recorder's camera: 3.1c's split stops at
// it; a recorder's split takes it as a record of its own. A frame of the
// recorder's own message alone gives 3.1c nothing.
void frames_split_recorder_records() {
    start_case("frames_split_recorder_records");
    const Bytes frame{0xff, 0xff, 0xff, 0xff, 0x06, 0xfc, 0x00, 0x01, 0xd0, 0x00};
    auto records = std::make_unique<PeerRecords>();
    UnpackOutcome outcome{};
    CHECK(
        unpack_frame_records(records.get(), frame.data(), frame.size(), 5, 1, 0, false, &outcome) ==
        WireError::ok
    );
    CHECK(records->count == 1);
    peer_records_reset(records.get());
    records->recorder_records = true;
    CHECK(
        unpack_frame_records(records.get(), frame.data(), frame.size(), 5, 1, 0, false, &outcome) ==
        WireError::ok
    );
    CHECK(records->count == 2);
    const uint8_t* data = nullptr;
    uint16_t length = 0;
    CHECK(pop_due_record(records.get(), 5, &data, &length) && length == 1);
    CHECK(pop_due_record(records.get(), 5, &data, &length) && length == 5 && data[0] == 0xfc);

    const Bytes alone{0xff, 0xff, 0xff, 0xff, 0xfb, 0x00, 0x01};
    peer_records_reset(records.get());
    records->recorder_records = false;
    CHECK(
        unpack_frame_records(records.get(), alone.data(), alone.size(), 5, 1, 0, false, &outcome) ==
        WireError::ok
    );
    CHECK(records->count == 0);
    peer_records_reset(records.get());
    records->recorder_records = true;
    CHECK(
        unpack_frame_records(records.get(), alone.data(), alone.size(), 5, 1, 0, false, &outcome) ==
        WireError::ok
    );
    CHECK(records->count == 1);
    // A recorder record whose length cannot be read ends the split quietly.
    const Bytes cut{0xff, 0xff, 0xff, 0xff, 0x06, 0xfd, 0x02, 0x00};
    peer_records_reset(records.get());
    CHECK(
        unpack_frame_records(records.get(), cut.data(), cut.size(), 5, 1, 0, false, &outcome) ==
        WireError::ok
    );
    CHECK(records->count == 1);
}

// The start sync records two machines sent at tick 90 for their
// commanders (type 54 of a unit table 10 bits wide), byte for byte.
void captured_start_positions() {
    start_case("captured_start_positions");

    struct Case {
        StartPosition position;
        Bytes bytes;
    };

    const Case cases[] = {
        {{54, 9072, 992, 9072, 992},
         {0x2c, 0x15, 0x00, 0x5a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x10,
          0x6e, 0x04, 0x7c, 0x00, 0x6e, 0x04, 0x7c, 0xe0, 0xff, 0x1f}},
        {{54, 14352, 18176, 14352, 18176},
         {0x2c, 0x15, 0x00, 0x5a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x10,
          0x02, 0x07, 0xe0, 0x08, 0x02, 0x07, 0xe0, 0xe8, 0xff, 0x1f}},
    };
    constexpr unsigned def_bits = 10;
    for (const auto& c : cases) {
        uint8_t storage[unit_state_writer_words * bit_stream_word_bytes];
        BitWriter writer;
        bit_writer_init(&writer, storage, unit_state_writer_words);
        uint16_t length = 0;
        CHECK(
            unit_state_write_start_position(&writer, 90, c.position, def_bits, &length) ==
            WireError::ok
        );
        CHECK(length == c.bytes.size());
        CHECK(std::memcmp(storage, c.bytes.data(), c.bytes.size()) == 0);
        int16_t x = 0;
        int16_t z = 0;
        CHECK(unit_state_start_position(c.bytes.data(), c.bytes.size(), def_bits, &x, &z));
        CHECK(x == c.position.x && z == c.position.z);
    }
    // An ordinary record with an empty list gives no position, nor does one
    // for another unit index or with fewer than two points.
    const Bytes empty{0x2c, 0x0b, 0x00, 0x5a, 0x00, 0x00, 0x00, 0xff, 0xff, 0x01, 0x00};
    int16_t x = 0;
    int16_t z = 0;
    CHECK(!unit_state_start_position(empty.data(), empty.size(), def_bits, &x, &z));
    auto other = cases[0].bytes;
    other[7] = 0x01;
    CHECK(!unit_state_start_position(other.data(), other.size(), def_bits, &x, &z));
    auto one_point = cases[0].bytes;
    one_point[10] = static_cast<uint8_t>(one_point[10] & ~0x10); // count 2 becomes 0
    CHECK(!unit_state_start_position(one_point.data(), one_point.size(), def_bits, &x, &z));
    CHECK(!unit_state_start_position(cases[0].bytes.data(), 12, def_bits, &x, &z));
}

} // namespace

int main() {
    version_rules();
    captured_challenge_decodes();
    captured_vote_decodes();
    malformed_private_records();
    recorder_record_lengths();
    captured_recorder_messages();
    recorder_commands_parse();
    recorder_take_and_base_commands_parse();
    recorder_base_files_read();
    frames_split_recorder_records();
    captured_start_positions();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("wire rules: all tests passed");
    return 0;
}
