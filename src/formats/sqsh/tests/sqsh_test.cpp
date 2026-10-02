// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The SQSH codec on malformed and edge-case input: truncated and oversized
// LZ77 streams, references into the fresh window, arbitrary bytes, the
// encoder's output limit, and empty chunks for the scramble and checksum.

#include "oa/formats/sqsh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;
using oa::base::bytes::DecodeCode;
using oa::formats::sqsh::Lz77Status;

int failures = 0;

/// Records a failed check.
///
/// @param line source line of the check
/// @param what text of the failed condition
void report_failure(int line, const char* what) {
    std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, what);
    ++failures;
}

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            report_failure(__LINE__, #condition);                                                  \
        }                                                                                          \
    } while (false)

/// Reports whether decode_lz77 refuses a stream.
///
/// @param input the stream
/// @param limit the output limit
/// @return true when decoding fails
bool decode_refuses(const Bytes& input, std::size_t limit) {
    return !oa::formats::sqsh::decode_lz77(input, limit).ok();
}

/// Reports whether decode_lz77 refuses a stream with a code at an offset.
///
/// @param input the stream
/// @param limit the output limit
/// @param code the error code expected
/// @param offset the input offset expected
/// @return true when decoding fails so
bool decode_fails_with(const Bytes& input, std::size_t limit, DecodeCode code, uint64_t offset) {
    const auto decoded = oa::formats::sqsh::decode_lz77(input, limit);
    return !decoded.ok() && decoded.error.code == code && decoded.error.offset == offset &&
           decoded.error.message != nullptr;
}

/// Reports whether encode_lz77 refuses an output limit.
///
/// @param input bytes to compress
/// @param limit the output limit
/// @return true when encoding fails with limit_exceeded
bool encode_refuses(const Bytes& input, std::size_t limit) {
    const auto encoded = oa::formats::sqsh::encode_lz77(input, limit);
    return !encoded.ok() && encoded.error.code == DecodeCode::limit_exceeded;
}

// Linear congruential step that fills the arbitrary streams.
constexpr uint32_t pattern_multiplier = 1103515245u;
constexpr uint32_t pattern_increment = 12345u;
/// Arbitrary streams decoded, and the length of each.
constexpr int arbitrary_streams = 2000;
constexpr std::size_t arbitrary_stream_bytes = 64;
/// Output buffer the arbitrary streams decode into, with guard bytes after it.
constexpr std::size_t arbitrary_output_bytes = 256;
constexpr std::size_t guard_bytes = 16;
constexpr uint8_t guard_value = 0xA5;

void test_truncated_streams() {
    CHECK(decode_refuses({}, 10));           // no flag byte
    CHECK(decode_refuses({0}, 10));          // a literal flag and no literal
    CHECK(decode_refuses({0, 'a'}, 10));     // eight literals announced, one given
    CHECK(decode_refuses({1}, 10));          // a token flag and no token
    CHECK(decode_refuses({1, 0x10}, 10));    // half a token
    CHECK(decode_refuses({2, 'a', 0}, 10));  // a literal, then half a token
    CHECK(decode_refuses({0xFF, 0x12}, 10)); // tokens only, cut in the first

    // The error lies where the input ended or the token was cut.
    CHECK(decode_fails_with({0, 'a'}, 10, DecodeCode::truncated, 2));
    CHECK(decode_fails_with({1, 0x10}, 10, DecodeCode::truncated, 1));
    CHECK(decode_fails_with({}, 10, DecodeCode::truncated, 0));

    // The bytes decoded before the stream ends are kept, and the rest of
    // the buffer is left as it was.
    std::array<uint8_t, 8> output{};
    output.fill(guard_value);
    const auto decoded =
        oa::formats::sqsh::decode_lz77_into(std::array<uint8_t, 3>{0, 'a', 'b'}, output);
    CHECK(decoded.status == Lz77Status::truncated && decoded.written == 2);
    CHECK(output[0] == 'a' && output[1] == 'b' && output[2] == guard_value);
}

void test_output_limits() {
    // A literal into no room, a match longer than the room, and an exact fit.
    CHECK(decode_refuses({0, 'a'}, 0));
    const Bytes repeat{6, 'a', 0x1F, 0x00, 0, 0}; // 'a', then 17 more from offset 1, then the end
    CHECK(decode_refuses(repeat, 17));
    CHECK(oa::formats::sqsh::decode_lz77(repeat, 18).value == Bytes(18, 'a'));
    // The match that passes the limit starts at input byte 2.
    CHECK(decode_fails_with(repeat, 17, DecodeCode::limit_exceeded, 2));
    CHECK(decode_fails_with({0, 'a'}, 0, DecodeCode::limit_exceeded, 1));
    std::array<uint8_t, 4> room{};
    const auto full = oa::formats::sqsh::decode_lz77_into(repeat, room);
    CHECK(full.status == Lz77Status::output_full && full.written == room.size());
}

void test_fresh_window() {
    // A stream may start with a reference: the window starts zero-filled, so
    // any offset reads zeros, the last slot of the window included.
    const Bytes last_slot{3, 0xF3, 0xFF, 0, 0}; // offset 4095, 5 bytes, then the end
    CHECK(oa::formats::sqsh::decode_lz77(last_slot, 5).value == Bytes(5, 0));
    const Bytes middle{3, 0x40, 0x06, 0, 0}; // offset 100, 2 bytes, then the end
    CHECK(oa::formats::sqsh::decode_lz77(middle, 2).value == Bytes(2, 0));
    // A seeded window of the wrong size writes nothing.
    std::array<uint8_t, 4> output{};
    output.fill(guard_value);
    const auto refused =
        oa::formats::sqsh::decode_lz77_into(middle, output, std::array<uint8_t, 4095>{});
    CHECK(refused.status == Lz77Status::bad_dictionary && refused.written == 0);
    CHECK(output[0] == guard_value);
}

void test_arbitrary_bytes() {
    // Arbitrary streams never write past the output, and report how many
    // bytes they wrote.
    uint32_t state = 0x13579BDFu;
    for (int stream = 0; stream < arbitrary_streams; ++stream) {
        std::array<uint8_t, arbitrary_stream_bytes> input{};
        for (auto& byte : input) {
            state = state * pattern_multiplier + pattern_increment;
            byte = static_cast<uint8_t>(state >> 16);
        }
        std::array<uint8_t, arbitrary_output_bytes + guard_bytes> output{};
        output.fill(guard_value);
        const auto decoded = oa::formats::sqsh::decode_lz77_into(
            input, std::span(output).first(arbitrary_output_bytes)
        );
        CHECK(decoded.written <= arbitrary_output_bytes);
        bool guard_kept = true;
        for (std::size_t i = arbitrary_output_bytes; i < output.size(); ++i)
            guard_kept = guard_kept && output[i] == guard_value;
        CHECK(guard_kept);
    }
}

void test_encoder_limit() {
    const Bytes text{'S', 'Q', 'S', 'H', ' ', 'S', 'Q', 'S', 'H', ' ', 'S', 'Q', 'S', 'H'};
    const Bytes packed = oa::formats::sqsh::encode_lz77(text, 64).value.value();
    CHECK(oa::formats::sqsh::decode_lz77(packed, text.size()).value == text);
    // The exact size fits, one byte less does not, and neither does none.
    CHECK(oa::formats::sqsh::encode_lz77(text, packed.size()).value == packed);
    CHECK(encode_refuses(text, packed.size() - 1));
    CHECK(encode_refuses(text, 0));
    CHECK(encode_refuses({}, 0));
}

void test_empty_chunks() {
    Bytes empty;
    oa::formats::sqsh::decrypt_chunk(empty);
    oa::formats::sqsh::encrypt_chunk(empty);
    CHECK(empty.empty());
    CHECK(oa::formats::sqsh::chunk_checksum({}) == 0);
    // The checksum is a byte sum that wraps at 32 bits.
    const Bytes ones(0x0101'0102, 0xFF);
    CHECK(
        oa::formats::sqsh::chunk_checksum(ones) ==
        uint32_t((0xFFull * 0x0101'0102ull) & 0xFFFF'FFFFull)
    );
}

} // namespace

int main() {
    test_truncated_streams();
    test_output_limits();
    test_fresh_window();
    test_arbitrary_bytes();
    test_encoder_limit();
    test_empty_chunks();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
