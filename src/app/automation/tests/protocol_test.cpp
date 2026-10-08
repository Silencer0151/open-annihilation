// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The automation protocol's codec: every test vector in the folder given
// (tests/vectors, written by make_vectors.py there) read whole, byte by byte
// and in chunks, each giving the frames and skipped runs vectors.json lists;
// frames the codec writes read back; malformed input refused where it
// should be (a bad magic, a CRC that does not match, lengths above the
// limits, a header cut short, a stream that ends inside a frame); the work
// of one call bounded, and limits changed under bytes already fed; and the
// JSON reader and writer, malformed texts and text that is not UTF-8 among
// them.
//
// usage: oa-app-automation-protocol-test VECTORS_FOLDER
#include "oa/app/automation/json.hpp"
#include "oa/app/automation/protocol.hpp"
#include "oa/test/check.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace automation = oa::app::automation;
namespace fs = std::filesystem;

// The chunk sizes each vector is fed in besides whole: byte by byte, and
// sizes that split headers, JSON parts and payloads at different places.
constexpr size_t kChunkSizes[] = {1, 2, 7, 64};

/// Reads a whole file.
///
/// @param path the file
/// @return its bytes; empty when it cannot be read
std::vector<uint8_t> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// Returns a CRC-32 as the vectors write it: eight lowercase hexadecimal digits.
///
/// @param crc the CRC
/// @return the digits
std::string crc_text(uint32_t crc) {
    char text[9]{};
    std::snprintf(text, sizeof text, "%08x", crc);
    return text;
}

/// Returns the bytes of a text.
///
/// @param text the text
/// @return a view of its bytes
std::span<const uint8_t> bytes_of(std::string_view text) {
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

// What reading a stream gave: its frames and skipped runs, in order.
struct Reading {
    std::vector<std::string> results;      ///< one line for each, comparable across splits
    std::vector<automation::Frame> frames; ///< the frames
};

/// Describes a frame in one line.
///
/// @param frame the frame
/// @return "frame <route> <JSON part> <payload length> <payload CRC>"
std::string describe(const automation::Frame& frame) {
    return "frame " + frame.route + " " + frame.json + " " + std::to_string(frame.payload.size()) +
           " " + crc_text(automation::crc32(frame.payload));
}

/// Describes a skipped run in one line.
///
/// @param skipped the run
/// @return "bad_frame <reason> <count>"
std::string describe(const automation::SkippedBytes& skipped) {
    return "bad_frame " + std::string(automation::skip_reason_name(skipped.reason)) + " " +
           std::to_string(skipped.count);
}

/// Reads a stream fed in chunks, to its end.
///
/// @param bytes the stream
/// @param chunk the bytes fed at a time; 0 feeds them all at once
/// @param reader the reader, with the limits to read with
/// @return what reading gave
Reading read_stream(std::span<const uint8_t> bytes, size_t chunk, automation::FrameReader reader) {
    Reading reading;
    const auto take = [&reader, &reading]() {
        automation::Frame frame;
        automation::SkippedBytes skipped;
        for (;;) {
            const auto result = reader.next(frame, skipped);
            if (result == automation::ReadResult::need_more)
                return;
            if (result == automation::ReadResult::paused)
                continue;
            if (result == automation::ReadResult::frame) {
                reading.results.push_back(describe(frame));
                reading.frames.push_back(frame);
            } else {
                reading.results.push_back(describe(skipped));
            }
        }
    };
    const size_t step = chunk == 0 ? std::max<size_t>(bytes.size(), 1) : chunk;
    for (size_t at = 0; at < bytes.size(); at += step) {
        reader.feed(bytes.subspan(at, std::min(step, bytes.size() - at)));
        take();
    }
    automation::SkippedBytes skipped;
    if (reader.finish(skipped) == automation::ReadResult::skipped)
        reading.results.push_back(describe(skipped));
    return reading;
}

/// Reads a stream fed all at once, with the protocol's limits.
///
/// @param bytes the stream
/// @return what reading gave
Reading read_whole(std::span<const uint8_t> bytes) {
    return read_stream(bytes, 0, automation::FrameReader{});
}

/// Returns a string member of a parsed object.
///
/// @param object the object
/// @param name the member's name
/// @return the member's text; empty when it has none
std::string member_text(const automation::Json& object, std::string_view name) {
    const auto* member = object.find(name);
    const auto* text = member != nullptr ? member->string() : nullptr;
    return text != nullptr ? *text : std::string();
}

/// Checks one vector against what vectors.json says reading it gives.
///
/// @param folder the vectors' folder
/// @param vector the vector's entry in vectors.json
void check_vector(const fs::path& folder, const automation::Json& vector) {
    const std::string file = member_text(vector, "file");
    const auto bytes = read_file(folder / file);
    OA_CHECK(!bytes.empty());
    std::vector<std::string> expected;
    const auto* expect = vector.find("expect");
    OA_CHECK(expect != nullptr && !expect->elements().empty());
    if (expect == nullptr)
        return;
    for (const auto& result : expect->elements()) {
        if (const auto* frame = result.find("frame")) {
            const auto* length = frame->find("payload_len");
            expected.push_back(
                "frame " + member_text(*frame, "route") + " " + member_text(*frame, "json_text") +
                " " + std::to_string(length != nullptr ? length->integer().value_or(-1) : -1) +
                " " + member_text(*frame, "payload_crc32")
            );
        } else if (const auto* bad = result.find("bad_frame")) {
            const auto* count = bad->find("skipped");
            expected.push_back(
                "bad_frame " + member_text(*bad, "reason") + " " +
                std::to_string(count != nullptr ? count->integer().value_or(-1) : -1)
            );
        }
    }
    const Reading whole = read_whole(bytes);
    if (whole.results != expected) {
        std::fprintf(stderr, "%s read whole gives:\n", file.c_str());
        for (const auto& line : whole.results)
            std::fprintf(stderr, "  %s\n", line.c_str());
    }
    OA_CHECK(whole.results == expected);
    for (const size_t chunk : kChunkSizes) {
        const Reading split = read_stream(bytes, chunk, automation::FrameReader{});
        if (split.results != whole.results)
            std::fprintf(stderr, "%s read %zu bytes at a time differs\n", file.c_str(), chunk);
        OA_CHECK(split.results == whole.results);
    }
    // The strings a frame's JSON part must read back unchanged.
    size_t frame_index = 0;
    for (const auto& result : expect->elements()) {
        const auto* frame = result.find("frame");
        if (frame == nullptr)
            continue;
        const auto* strings = frame->find("strings");
        if (strings != nullptr && frame_index < whole.frames.size()) {
            automation::JsonError error;
            const auto parsed = automation::parse_json(whole.frames[frame_index].json, error);
            OA_CHECK(parsed.has_value());
            for (size_t member = 0; parsed && member < strings->names().size(); ++member) {
                const auto* wanted = strings->values()[member].string();
                OA_CHECK(wanted != nullptr);
                OA_CHECK(member_text(*parsed, strings->names()[member]) == *wanted);
            }
        }
        ++frame_index;
    }
}

/// Checks every vector the folder's vectors.json lists.
///
/// @param folder the vectors' folder
void check_vectors(const fs::path& folder) {
    const auto index = read_file(folder / "vectors.json");
    automation::JsonError error;
    const auto parsed =
        automation::parse_json({reinterpret_cast<const char*>(index.data()), index.size()}, error);
    if (!parsed)
        std::fprintf(stderr, "vectors.json: %s at byte %zu\n", error.message.c_str(), error.offset);
    OA_CHECK(parsed.has_value());
    if (!parsed)
        return;
    const auto* vectors = parsed->find("vectors");
    OA_CHECK(vectors != nullptr && vectors->elements().size() == 15);
    if (vectors == nullptr)
        return;
    for (const auto& vector : vectors->elements())
        check_vector(folder, vector);
}

/// Frames the codec writes read back as written, and the header is as the
/// framing says; a route or a part the framing refuses writes nothing.
void check_encoding() {
    std::vector<uint8_t> stream;
    const std::string json = R"({"id":7,"op":"key","keys":["Return"]})";
    OA_CHECK(automation::encode_frame("game", json, {}, stream));
    const std::string written(stream.begin(), stream.end());
    OA_CHECK(written == "AUTO/1 game 37 0 4ae1fb5a\n" + json);
    const std::vector<uint8_t> payload = {0, 1, 2, 255};
    OA_CHECK(automation::encode_frame(automation::endpoint_route, R"({"id":1})", payload, stream));
    const Reading reading = read_whole(stream);
    OA_CHECK(reading.frames.size() == 2);
    if (reading.frames.size() == 2) {
        OA_CHECK(reading.frames[0].route == "game" && reading.frames[0].json == json);
        OA_CHECK(reading.frames[1].route == "-" && reading.frames[1].payload == payload);
    }
    const size_t before = stream.size();
    OA_CHECK(!automation::encode_frame("Upper", "{}", {}, stream));
    OA_CHECK(!automation::encode_frame("", "{}", {}, stream));
    OA_CHECK(!automation::encode_frame(std::string(49, 'a'), "{}", {}, stream));
    OA_CHECK(automation::encode_frame(std::string(48, 'a'), "{}", {}, stream));
    OA_CHECK(
        !automation::encode_frame("-", std::string(automation::max_json_bytes + 1, ' '), {}, stream)
    );
    OA_CHECK(stream.size() > before);
    OA_CHECK(automation::route_valid("relay/echo.2_x-y"));
    OA_CHECK(!automation::route_valid("a b"));
    OA_CHECK(automation::crc32(bytes_of("123456789")) == 0xCBF43926U);
    OA_CHECK(automation::crc32(bytes_of("")) == 0);
}

/// Encodes one frame.
///
/// @param route the route
/// @param json the JSON part
/// @return the frame's bytes
std::vector<uint8_t> one_frame(std::string_view route, std::string_view json) {
    std::vector<uint8_t> out;
    (void)automation::encode_frame(route, json, {}, out);
    return out;
}

/// Appends bytes to a stream.
///
/// @param[in,out] stream the stream
/// @param bytes the bytes
void append(std::vector<uint8_t>& stream, std::span<const uint8_t> bytes) {
    stream.insert(stream.end(), bytes.begin(), bytes.end());
}

/// The reader refuses each kind of malformed input as the framing says and
/// goes on to the next good frame: a bad magic, a CRC that does not match,
/// lengths above its limits, a header too long or cut short, a JSON part not
/// where the header says, and a stream that ends inside a frame.
void check_malformed() {
    const auto good = one_frame("-", R"({"id":1,"op":"ping"})");
    const std::string good_line = describe(read_whole(good).frames.at(0));
    {
        // A bad magic: a lower-case one, and another version.
        std::vector<uint8_t> stream;
        append(stream, bytes_of("auto/1 - 2 0 00000000\n{}AUTO/2 - 2 0 00000000\n{}"));
        append(stream, good);
        const Reading reading = read_whole(stream);
        OA_CHECK(reading.results.size() == 2 && reading.results[0] == "bad_frame noise 48");
        OA_CHECK(reading.results.size() == 2 && reading.results[1] == good_line);
    }
    {
        // A CRC that does not match: one flipped bit of the JSON part.
        auto damaged = one_frame("-", R"({"id":2,"op":"screen"})");
        damaged.back() ^= 0x01;
        std::vector<uint8_t> stream = damaged;
        append(stream, good);
        const Reading reading = read_whole(stream);
        OA_CHECK(reading.results.size() == 2);
        OA_CHECK(reading.results.at(0) == "bad_frame crc " + std::to_string(damaged.size()));
    }
    {
        // Lengths above the reader's limits are refused before the body is
        // read: nothing waits for the 64 MiB the header announces.
        std::vector<uint8_t> stream;
        append(stream, bytes_of("AUTO/1 - 2 67108865 00000000\n"));
        append(stream, good);
        const Reading reading = read_whole(stream);
        OA_CHECK(reading.results.size() == 2 && reading.results[0] == "bad_frame too_long 29");
        const auto limited = read_stream(good, 0, automation::FrameReader(8, 0));
        OA_CHECK(limited.results.size() == 1);
        OA_CHECK(
            limited.results.size() == 1 &&
            limited.results[0] == "bad_frame too_long " + std::to_string(good.size())
        );
        automation::FrameReader waiting;
        waiting.feed(bytes_of("AUTO/1 - 1048577 0 00000000\n{"));
        automation::Frame frame;
        automation::SkippedBytes skipped;
        OA_CHECK(waiting.next(frame, skipped) == automation::ReadResult::need_more);
        OA_CHECK(waiting.buffered() == 0);
    }
    {
        // A header line longer than the framing allows, and a header whose
        // CRC is not eight lowercase digits or whose length has a leading
        // zero.
        std::vector<uint8_t> stream;
        std::string long_line = "AUTO/1 - 2 0 " + std::string(100, '0') + "\n";
        append(stream, bytes_of(long_line));
        append(stream, bytes_of("AUTO/1 - 2 0 0000000A\n{}"));
        append(stream, bytes_of("AUTO/1 - 02 0 00000000\n{}"));
        append(stream, good);
        const Reading reading = read_whole(stream);
        OA_CHECK(
            reading.results.size() == 2 && reading.results.at(0).starts_with("bad_frame header ")
        );
        OA_CHECK(reading.results.size() == 2 && reading.results[1] == good_line);
    }
    {
        // A JSON part that does not start where the header says.
        std::vector<uint8_t> stream;
        append(stream, bytes_of("AUTO/1 - 3 0 00000000\nxyz"));
        append(stream, good);
        const Reading reading = read_whole(stream);
        OA_CHECK(reading.results.size() == 2 && reading.results[0] == "bad_frame header 25");
    }
    {
        // A header cut short where the stream ends, and a frame whose
        // payload never came.
        const Reading cut = read_whole(bytes_of("AUTO/1 - 20 0"));
        OA_CHECK(cut.results.size() == 1 && cut.results[0] == "bad_frame stalled 13");
        const Reading magic = read_whole(bytes_of("AUTO/"));
        OA_CHECK(magic.results.size() == 1 && magic.results[0] == "bad_frame stalled 5");
        std::vector<uint8_t> stream = good;
        stream.resize(stream.size() - 1);
        const Reading body = read_whole(stream);
        OA_CHECK(body.results.size() == 1 && body.results[0] == "bad_frame stalled 42");
    }
    {
        // A frame given up on is skipped as stalled, and the next good frame read.
        automation::FrameReader reader;
        reader.feed(bytes_of("AUTO/1 - 20 0 00000000\n{\"id\""));
        automation::Frame frame;
        automation::SkippedBytes skipped;
        OA_CHECK(reader.next(frame, skipped) == automation::ReadResult::need_more);
        reader.abandon();
        reader.feed(good);
        OA_CHECK(reader.next(frame, skipped) == automation::ReadResult::skipped);
        OA_CHECK(skipped.reason == automation::SkipReason::stalled && skipped.count == 28);
        OA_CHECK(reader.next(frame, skipped) == automation::ReadResult::frame);
        OA_CHECK(describe(frame) == good_line);
        OA_CHECK(reader.next(frame, skipped) == automation::ReadResult::need_more);
    }
}

/// One call of the reader looks at no more than twice the largest frame it
/// takes, and pauses instead, giving the same frames and runs; a reader
/// whose limits change keeps the bytes fed and not yet taken.
void check_bounds() {
    const auto good = one_frame("-", R"({"id":1,"op":"ping"})");
    const std::string good_line = describe(read_whole(good).frames.at(0));
    // Headers each followed by '{' and announcing a JSON part that ends
    // where the last one's does: every one's CRC is checked, and fails.
    constexpr size_t headers = 2000;
    constexpr size_t json_limit = 4096;
    std::vector<std::string> fakes;
    size_t after = 1; // the bytes from the last header's end to where every JSON part ends
    for (size_t made = 0; made < headers; ++made) {
        const std::string header = "AUTO/1 - " + std::to_string(after) + " 0 00000000\n";
        fakes.push_back(header);
        after += header.size() + 1;
        if (after > json_limit)
            break;
    }
    std::vector<uint8_t> stream;
    for (auto fake = fakes.rbegin(); fake != fakes.rend(); ++fake) {
        append(stream, bytes_of(*fake));
        stream.push_back('{');
    }
    const size_t fake_bytes = stream.size();
    append(stream, good);
    automation::FrameReader reader(json_limit, 0);
    reader.feed(stream);
    std::vector<std::string> results;
    size_t paused = 0;
    for (size_t call = 0; call < stream.size(); ++call) {
        automation::Frame frame;
        automation::SkippedBytes skipped;
        const auto result = reader.next(frame, skipped);
        if (result == automation::ReadResult::need_more)
            break;
        if (result == automation::ReadResult::paused) {
            ++paused;
            OA_CHECK(reader.skipping());
        } else if (result == automation::ReadResult::frame) {
            results.push_back(describe(frame));
        } else {
            results.push_back(describe(skipped));
        }
    }
    OA_CHECK(fakes.size() > 100 && paused > 0);
    OA_CHECK(
        results.size() == 2 && results[0] == "bad_frame crc " + std::to_string(fake_bytes) &&
        results[1] == good_line
    );
    OA_CHECK(!reader.skipping() && reader.buffered() == 0);
    OA_CHECK(reader.largest_frame_bytes() == automation::max_header_bytes + json_limit);

    // Noise far longer than a frame is skipped a part at a time.
    automation::FrameReader noisy(16, 0);
    std::vector<uint8_t> noise(4 * noisy.largest_frame_bytes(), 'x');
    noisy.feed(noise);
    automation::Frame frame;
    automation::SkippedBytes skipped;
    OA_CHECK(noisy.next(frame, skipped) == automation::ReadResult::paused);
    OA_CHECK(noisy.skipping() && noisy.buffered() > 0);
    while (noisy.next(frame, skipped) == automation::ReadResult::paused) {
    }
    OA_CHECK(noisy.buffered() == 0 && noisy.finish(skipped) == automation::ReadResult::skipped);
    OA_CHECK(skipped.reason == automation::SkipReason::noise && skipped.count == noise.size());

    // A first frame read with small limits, then the rest with larger ones.
    const auto large = one_frame("-", R"({"id":2,"op":"prefs","names":["a","b","c"]})");
    automation::FrameReader first(24, 0);
    first.feed(good);
    first.feed(large);
    OA_CHECK(first.next(frame, skipped) == automation::ReadResult::frame);
    OA_CHECK(describe(frame) == good_line && !first.skipping());
    first.set_limits(automation::max_json_bytes, 0);
    OA_CHECK(first.next(frame, skipped) == automation::ReadResult::frame);
    OA_CHECK(frame.json == R"({"id":2,"op":"prefs","names":["a","b","c"]})");
}

/// Parses a JSON text.
///
/// @param text the text
/// @return the value, or nothing when it is refused
std::optional<automation::Json> parse(std::string_view text) {
    automation::JsonError error;
    return automation::parse_json(text, error);
}

/// The JSON reader takes RFC 8259's values and refuses malformed texts, and
/// the writer writes them compactly.
void check_json() {
    const auto request = parse(
        R"( {"id": -42, "op": "prefs", "names": ["a", "b"], "on": true, "off": false,)"
        R"( "none": null, "ratio": 1.5e3, "nested": {"x": [[], {}]}} )"
    );
    OA_CHECK(request.has_value() && request->type() == automation::JsonType::object);
    if (request) {
        OA_CHECK(request->find("id")->integer() == -42);
        OA_CHECK(member_text(*request, "op") == "prefs");
        OA_CHECK(request->find("names")->elements().size() == 2);
        OA_CHECK(request->find("on")->boolean() == true);
        OA_CHECK(request->find("off")->boolean() == false);
        OA_CHECK(request->find("none")->type() == automation::JsonType::null);
        OA_CHECK(!request->find("ratio")->integer().has_value());
        OA_CHECK(*request->find("ratio")->number_text() == "1.5e3");
        OA_CHECK(request->find("missing") == nullptr);
        OA_CHECK(!request->find("op")->integer().has_value());
    }
    OA_CHECK(parse("9223372036854775807")->integer() == INT64_MAX);
    OA_CHECK(!parse("9223372036854775808")->integer().has_value());
    OA_CHECK(parse("[]")->type() == automation::JsonType::array);
    OA_CHECK(*parse(R"("é😀\/")")->string() == "\xC3\xA9\xF0\x9F\x98\x80/");
    // Half a surrogate pair stands for U+FFFD. The escapes are spelled with
    // a doubled backslash, not in raw strings, which some compilers read
    // as invalid characters of the source.
    OA_CHECK(*parse("\"\\ud800x\"")->string() == "\xEF\xBF\xBDx");
    OA_CHECK(*parse("\"\\udc00\"")->string() == "\xEF\xBF\xBD");
    // Nested 64 deep is taken; 65 is refused.
    OA_CHECK(parse(std::string(64, '[') + std::string(64, ']')).has_value());
    OA_CHECK(!parse(std::string(65, '[') + std::string(65, ']')).has_value());
    for (const std::string_view malformed : {
             "",
             "{",
             "}",
             R"({"a":})",
             R"({"a" 1})",
             R"({"a":1,})",
             R"({a:1})",
             "[1,]",
             "[1 2]",
             "01",
             "1.",
             "-",
             "1e",
             "+1",
             "tru",
             "nul",
             "{} {}",
             "{}x",
             "\"open",
             "\"tab\there\"",
             R"("\x")",
             R"("\u12")",
             R"("\u12G4")",
             "\"\xC3\"",
             "\"\xC0\xAF\"",
             "\"\xED\xA0\x80\"",
             "\"\xF4\x90\x80\x80\"",
             "\xEF\xBB\xBF{}",
         }) {
        automation::JsonError error;
        const bool refused = !automation::parse_json(malformed, error).has_value();
        if (!refused)
            std::fprintf(
                stderr, "taken: %.*s\n", static_cast<int>(malformed.size()), malformed.data()
            );
        OA_CHECK(refused && !error.message.empty());
    }
    automation::JsonError error;
    OA_CHECK(!automation::parse_json(R"({"a":1} x)", error).has_value());
    OA_CHECK(error.offset == 8);

    automation::JsonWriter writer;
    writer.begin_object();
    writer.key("id");
    writer.integer(9);
    writer.key("ok");
    writer.boolean(true);
    writer.key("list");
    writer.begin_array();
    writer.string("a\"b\\c\n\x01");
    writer.null();
    writer.begin_object();
    writer.end_object();
    writer.end_array();
    writer.key("raw");
    writer.raw(R"({"x":1})");
    writer.key("text");
    writer.string(
        "Ren\xC3\xA9"
        "e"
    );
    writer.end_object();
    OA_CHECK(
        writer.text() == "{\"id\":9,\"ok\":true,\"list\":[\"a\\\"b\\\\c\\n\\u0001\",null,{}],"
                         "\"raw\":{\"x\":1},\"text\":\"Ren\xC3\xA9"
                         "e\"}"
    );
    const auto again = parse(writer.text());
    OA_CHECK(again.has_value());
    OA_CHECK(again && *again->find("list")->elements()[0].string() == "a\"b\\c\n\x01");

    // Text that is UTF-8 is written as it is; text that is not, such as the
    // game's own text in its code page, whole as the marker and the base64
    // of its bytes, so that what is written is always UTF-8.
    const std::string accented = "Ar\xC3\xA8ne \xE4\xB8\xAD";
    OA_CHECK(automation::unicode_text(accented) == accented);
    OA_CHECK(automation::unicode_text("") == "");
    OA_CHECK(automation::unicode_text("D\xE9marrer") == "Non-Unicode Text Error::ROltYXJyZXI=");
    automation::JsonWriter marked;
    marked.begin_object();
    marked.key("caf\xE9");
    marked.string("D\xE9marrer \"1\"");
    marked.key("name");
    marked.string(accented + " \"1\"");
    marked.end_object();
    OA_CHECK(
        marked.text() == "{\"Non-Unicode Text Error::Y2Fm6Q==\":"
                         "\"Non-Unicode Text Error::ROltYXJyZXIgIjEi\","
                         "\"name\":\"Ar\xC3\xA8ne \xE4\xB8\xAD \\\"1\\\"\"}"
    );
    const auto read_back = parse(marked.text());
    OA_CHECK(
        read_back &&
        member_text(*read_back, "Non-Unicode Text Error::Y2Fm6Q==") ==
            "Non-Unicode Text Error::ROltYXJyZXIgIjEi" &&
        member_text(*read_back, "name") == accented + " \"1\""
    );
    // The test vectors of RFC 4648, section 10, and every byte value.
    const auto base64 = [](std::string_view text) {
        return automation::base64_text(
            {reinterpret_cast<const uint8_t*>(text.data()), text.size()}
        );
    };
    OA_CHECK(base64("") == "" && base64("f") == "Zg==" && base64("fo") == "Zm8=");
    OA_CHECK(base64("foo") == "Zm9v" && base64("foob") == "Zm9vYg==");
    OA_CHECK(base64("fooba") == "Zm9vYmE=" && base64("foobar") == "Zm9vYmFy");
    OA_CHECK(base64(std::string_view("\x00\xFF\xFE\xFB\xEF\xBE", 6)) == "AP/+++++");
    // Two CJK characters, three bytes each.
    OA_CHECK(automation::utf8_valid("\344\270\255\346\226\207"));
    // Malformed: a stray continuation byte, a sequence cut short, an
    // overlong form, a surrogate and a code point above U+10FFFF.
    for (const std::string_view bad :
         {"\x80", "\xe4\xb8", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xff"}) {
        OA_CHECK(!automation::utf8_valid(bad));
        OA_CHECK(automation::unicode_text(bad) == "Non-Unicode Text Error::" + base64(bad));
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s VECTORS_FOLDER\n", argv[0]);
        return 2;
    }
    check_vectors(argv[1]);
    check_encoding();
    check_malformed();
    check_bounds();
    check_json();
    return oa::test::check_exit_status();
}
