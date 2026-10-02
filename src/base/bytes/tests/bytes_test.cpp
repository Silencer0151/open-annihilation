// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// ByteReader on fields that fit and on every way a read can fall off the
// end, the sticky first failure, sub-readers and their offsets, bounded
// strings, the free little-endian helpers and Decoded.

#include "oa/base/bytes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

using oa::base::bytes::ByteReader;
using oa::base::bytes::DecodeCode;
using oa::base::bytes::DecodeError;
using oa::base::bytes::Decoded;

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

constexpr std::array<uint8_t, 12> sample{
    0x01, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0xFE, 0xFF, 0xFF, 0xFF, 0x80
};

void test_reads_that_fit() {
    ByteReader reader(sample);
    CHECK(reader.size() == sample.size() && reader.remaining() == sample.size());
    CHECK(reader.u8() == 0x01);
    CHECK(reader.u16() == 0x1234);
    CHECK(reader.u32() == 0x12345678u);
    CHECK(reader.i32() == -2);
    CHECK(reader.ok() && reader.position() == 11 && reader.remaining() == 1);
    CHECK(reader.u8_at(11) == 0x80);
    CHECK(reader.i16_at(10) == static_cast<int16_t>(0x80FF));
    CHECK(reader.u16_at(1) == 0x1234 && reader.u32_at(3) == 0x12345678u);
    CHECK(reader.i32_at(8) == static_cast<int32_t>(0x80FFFFFFu));
    CHECK(reader.position() == 11);
    CHECK(reader.seek(1) && reader.i16() == 0x1234);
    CHECK(reader.skip(4) && reader.position() == 7);
    const auto rest = reader.bytes(5);
    CHECK(rest.size() == 5 && rest[0] == 0xFE && reader.remaining() == 0);
    CHECK(reader.bytes(0).empty() && reader.ok());
    CHECK(reader.seek(sample.size()) && reader.ok());
    CHECK(reader.fits(12, 0) && !reader.fits(12, 1) && !reader.fits(13, 0));
}

void test_reads_past_the_end() {
    {
        ByteReader reader{std::span(sample).first(3)};
        CHECK(reader.u16() == 0x3401);
        CHECK(reader.u16() == 0);
        CHECK(!reader.ok() && reader.error().code == DecodeCode::truncated);
        CHECK(reader.error().offset == 2 && reader.error().message != nullptr);
        // The first failure is kept, and later reads that would fit fail too.
        CHECK(reader.u8() == 0);
        CHECK(reader.u8_at(0) == 0);
        CHECK(reader.error().offset == 2);
    }
    {
        ByteReader reader(sample);
        CHECK(reader.u32_at(9) == 0 && reader.error().offset == 9);
    }
    {
        ByteReader reader(sample);
        CHECK(reader.u32_at(UINT64_MAX - 1) == 0 && !reader.ok());
    }
    {
        ByteReader reader(sample);
        CHECK(!reader.seek(13) && reader.error().offset == 13);
    }
    {
        ByteReader reader(sample);
        CHECK(reader.skip(10) && !reader.skip(3) && reader.error().offset == 10);
    }
    {
        ByteReader reader(sample);
        CHECK(reader.bytes_at(8, 5).empty() && reader.error().offset == 8);
    }
    {
        ByteReader reader(sample);
        reader.skip(11);
        CHECK(reader.bytes(2).empty() && reader.error().offset == 11);
    }
    {
        ByteReader empty;
        CHECK(empty.size() == 0 && empty.u8() == 0 && !empty.ok());
    }
    {
        ByteReader reader(sample);
        CHECK(!reader.fail(DecodeCode::malformed, 4, "bad field", 7));
        CHECK(reader.error().code == DecodeCode::malformed && reader.error().detail == 7);
        CHECK(std::string_view(reader.error().message) == "bad field");
        CHECK(reader.u8() == 0 && reader.error().code == DecodeCode::malformed);
    }
}

void test_sub_readers() {
    ByteReader reader(sample, 100);
    auto inner = reader.sub(3, 4);
    CHECK(reader.ok() && inner.ok() && inner.size() == 4);
    CHECK(inner.u32() == 0x12345678u && inner.remaining() == 0);
    CHECK(inner.u8() == 0 && inner.error().offset == 107);
    CHECK(reader.ok());
    auto outside = reader.sub(10, 3);
    CHECK(!reader.ok() && reader.error().offset == 110);
    CHECK(!outside.ok() && outside.u8() == 0 && outside.error().offset == 110);
}

void test_strings() {
    const std::array<uint8_t, 9> text{'a', 'b', 'c', 0, 'd', 'e', 'f', 'g', 'h'};
    {
        ByteReader reader(text);
        CHECK(reader.c_string_at(0, 3) == "abc");
        CHECK(reader.c_string_at(3, 0).empty() && reader.ok());
        CHECK(reader.c_string_at(1, 16) == "bc");
    }
    {
        ByteReader reader(text);
        CHECK(reader.c_string_at(0, 2).empty());
        CHECK(reader.error().code == DecodeCode::limit_exceeded && reader.error().offset == 0);
    }
    {
        ByteReader reader(text);
        CHECK(reader.c_string_at(4, 16).empty());
        CHECK(reader.error().code == DecodeCode::truncated && reader.error().offset == 4);
    }
    {
        ByteReader reader(text);
        CHECK(reader.c_string_at(9, 16).empty() && reader.error().code == DecodeCode::truncated);
    }
}

void test_free_helpers() {
    static_assert(oa::base::bytes::load_le16(sample.data() + 1) == 0x1234);
    static_assert(oa::base::bytes::load_le32(sample.data() + 3) == 0x12345678u);
    std::array<uint8_t, 6> written{};
    oa::base::bytes::store_le16(written.data(), 0xBEEF);
    oa::base::bytes::store_le32(written.data() + 2, 0xCAFEF00Du);
    CHECK(written == (std::array<uint8_t, 6>{0xEF, 0xBE, 0x0D, 0xF0, 0xFE, 0xCA}));
    const std::array<std::byte, 2> as_bytes{std::byte{0x34}, std::byte{0x12}};
    ByteReader reader{std::span<const std::byte>(as_bytes)};
    CHECK(reader.u16() == 0x1234);
}

/// Decodes a 16-bit length and that many bytes.
///
/// @param bytes the input
/// @return the bytes, or where the input ends
Decoded<std::vector<uint8_t>> length_prefixed(std::span<const uint8_t> bytes) {
    ByteReader reader(bytes);
    const auto length = reader.u16();
    const auto body = reader.bytes(length);
    if (!reader.ok())
        return reader.error();
    return std::vector<uint8_t>(body.begin(), body.end());
}

void test_decoded() {
    const std::array<uint8_t, 4> whole{2, 0, 'o', 'k'};
    const auto decoded = length_prefixed(whole);
    CHECK(decoded.ok() && decoded.error.code == DecodeCode::none);
    CHECK(decoded.value == (std::vector<uint8_t>{'o', 'k'}));
    const auto cut = length_prefixed(std::span(whole).first(3));
    CHECK(!cut.ok() && cut.error.code == DecodeCode::truncated && cut.error.offset == 2);
    const Decoded<int> missing = DecodeError{DecodeCode::not_found, 0, "absent"};
    CHECK(!missing.ok() && std::strcmp(missing.error.message, "absent") == 0);
    CHECK(oa::base::bytes::decode_code_name(DecodeCode::truncated) == "truncated");
    CHECK(oa::base::bytes::decode_code_name(DecodeCode::none) == "no error");
}

} // namespace

int main() {
    test_reads_that_fit();
    test_reads_past_the_end();
    test_sub_readers();
    test_strings();
    test_free_helpers();
    test_decoded();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
