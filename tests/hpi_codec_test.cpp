// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/sqsh.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* description) {
    if (!condition) {
        std::cerr << description << '\n';
        std::exit(1);
    }
}

void rejects(std::vector<uint8_t> input, std::size_t limit) {
    try {
        (void)oa::formats::sqsh::decode_lz77(input, limit);
    } catch (const std::runtime_error&) {
        return;
    }
    require(false, "malformed or oversized stream was accepted");
}
} // namespace

int main() {
    using oa::formats::sqsh::decode_lz77;
    using oa::formats::sqsh::encode_lz77;
    require(
        encode_lz77({}, 4) == std::vector<uint8_t>({1, 0, 0, 0}),
        "empty compression terminator plus initialized trailing literal"
    );
    require(
        encode_lz77(std::array<uint8_t, 1>{'A'}, 5) == std::vector<uint8_t>({2, 'A', 0, 0, 0}),
        "literal compression"
    );
    bool compression_bounded = false;
    try {
        (void)encode_lz77({}, 3);
    } catch (const std::runtime_error&) {
        compression_bounded = true;
    }
    require(compression_bounded, "compression output allocation bound");
    require(
        decode_lz77(std::array<uint8_t, 3>{1, 15, 0}, 0).empty(), "any zero-offset token terminates"
    );
    const std::array<uint8_t, 6> overlap{6, 'A', 0x1f, 0, 0, 0};
    require(
        decode_lz77(overlap, 18) == std::vector<uint8_t>(18, 'A'),
        "overlap and maximum match length"
    );
    const std::array<uint8_t, 5> zero_seed{3, 0x10, 0, 0, 0};
    require(
        decode_lz77(zero_seed, 2) == std::vector<uint8_t>(2, 0), "fresh dictionary is zero filled"
    );
    std::array<uint8_t, 4096> seed{};
    seed[4095] = 'X';
    seed[0] = 'Y';
    seed[1] = 'Z';
    const std::array<uint8_t, 5> wrap{3, 0xf1, 0xff, 0, 0};
    // Third byte sees the first emitted byte overwrite dictionary slot 1.
    require(
        decode_lz77(wrap, 3, seed) == std::vector<uint8_t>({'X', 'Y', 'X'}),
        "dictionary read wrap and write overlap"
    );
    const std::array<uint8_t, 12> next_flags{0, 1, 2, 3, 4, 5, 6, 7, 8, 1, 0, 0};
    require(
        decode_lz77(next_flags, 8) == std::vector<uint8_t>({1, 2, 3, 4, 5, 6, 7, 8}),
        "flag reload after eight tokens"
    );
    rejects({}, 10);
    rejects({0}, 10);
    rejects({1, 0}, 10);
    rejects({0, 1}, 10);
    rejects({0, 1}, 0);
    rejects(std::vector<uint8_t>(overlap.begin(), overlap.end()), 17);
    bool dictionary_checked = false;
    try {
        (void)decode_lz77(overlap, 18, std::array<uint8_t, 3>{});
    } catch (const std::invalid_argument&) {
        dictionary_checked = true;
    }
    require(dictionary_checked, "a dictionary of the wrong size is refused");
    // Decoding into a caller's buffer: its size is the limit, a failure is
    // returned with the count written before it, and bytes past the count
    // are left as they were.
    using oa::formats::sqsh::decode_lz77_into;
    using oa::formats::sqsh::Lz77Status;
    constexpr uint8_t untouched = 0x5a;
    const auto is_a = [](uint8_t b) { return b == 'A'; };
    std::array<uint8_t, 20> into{};
    into.fill(untouched);
    const auto whole = decode_lz77_into(overlap, into);
    require(
        whole.status == Lz77Status::ok && whole.written == 18 &&
            std::all_of(into.begin(), into.begin() + 18, is_a) && into[18] == untouched &&
            into[19] == untouched,
        "decode into a buffer returns its count and keeps the rest"
    );
    into.fill(untouched);
    const auto bounded = decode_lz77_into(overlap, std::span(into).first(17));
    require(
        bounded.status == Lz77Status::output_full && bounded.written == 17 &&
            std::all_of(into.begin(), into.begin() + 17, is_a) && into[17] == untouched,
        "decode into a buffer stops at its size"
    );
    into.fill(untouched);
    const auto truncated = decode_lz77_into(std::array<uint8_t, 2>{0, 1}, into);
    require(
        truncated.status == Lz77Status::truncated && truncated.written == 1 && into[0] == 1 &&
            into[1] == untouched,
        "decode into a buffer reports a truncated stream"
    );
    into.fill(untouched);
    const auto wrong_dictionary = decode_lz77_into(overlap, into, std::array<uint8_t, 3>{});
    require(
        wrong_dictionary.status == Lz77Status::bad_dictionary && wrong_dictionary.written == 0 &&
            into[0] == untouched,
        "decode into a buffer refuses a dictionary of the wrong size"
    );
    std::array<uint8_t, 6> bytes{0, 255, 128, 1, 254, 42};
    const auto original = bytes;
    oa::formats::sqsh::encrypt_chunk(bytes);
    require(bytes == std::array<uint8_t, 6>{0, 255, 132, 5, 254, 52}, "encrypt known vector");
    require(oa::formats::sqsh::chunk_checksum(bytes) == 698, "unsigned byte sum");
    oa::formats::sqsh::decrypt_chunk(bytes);
    require(bytes == original, "decrypt known vector");
    std::vector<uint8_t> period(258, 0);
    oa::formats::sqsh::encrypt_chunk(period);
    require(
        period[127] == 254 && period[128] == 0 && period[255] == 254 && period[256] == 0 &&
            period[257] == 2,
        "byte index arithmetic wraps"
    );
    std::cout << "LZ77, scramble and random-generator vectors passed\n";
}
