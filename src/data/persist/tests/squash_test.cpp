// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"

#include "oa/data/persist/squash.hpp"
#include "oa/formats/sqsh.hpp"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

using namespace oa::data::persist;

namespace {

std::vector<uint8_t> pack(
    const std::vector<uint8_t>& input,
    SquashType type,
    bool scramble,
    SquashStatus* status = nullptr
) {
    uint32_t capacity = squash_bound(static_cast<int32_t>(input.size()), SquashType::lz77) + 64;
    std::vector<uint8_t> out(capacity);
    const auto result = squash_pack(
        out.data(), &capacity, input.data(), static_cast<uint32_t>(input.size()), type, scramble
    );
    if (status != nullptr)
        *status = result;
    out.resize(result == SquashStatus::ok ? capacity : 0);
    return out;
}

SquashStatus unpack(std::vector<uint8_t> block, std::vector<uint8_t>& out) {
    out.assign(squash_unpacked_size(block.data(), block.size()), 0);
    return squash_unpack(out.data(), out.size(), block.data(), block.size());
}

void bound_values() {
    CHECK(squash_bound(0, SquashType::lz77) == 0x77);
    CHECK(squash_bound(100, SquashType::lz77) == 120 + 0x77);
    CHECK(squash_bound(100, SquashType::zlib) == 110 + 0x77);
    CHECK(squash_bound(7, SquashType::lz77) == 8 + 0x77);
    CHECK(squash_bound(100, SquashType::stored) == 0);
    // Negative sizes wrap through the unsigned product.
    CHECK(squash_bound(-1, SquashType::lz77) == (0xffffffffu * 120u) / 100u + 0x77);
}

void round_trips() {
    std::mt19937 rng(7);
    for (const std::size_t size : {1u, 2u, 17u, 18u, 100u, 4096u, 5000u, 70000u}) {
        for (int pattern = 0; pattern < 3; ++pattern) {
            std::vector<uint8_t> input(size);
            for (std::size_t i = 0; i < size; ++i)
                input[i] = pattern == 0   ? static_cast<uint8_t>(rng())
                           : pattern == 1 ? static_cast<uint8_t>(i % 7)
                                          : static_cast<uint8_t>('a' + (i / 5) % 3);
            for (const auto type : {SquashType::lz77, SquashType::zlib}) {
                for (const bool scramble : {false, true}) {
                    SquashStatus status{};
                    auto block = pack(input, type, scramble, &status);
                    CHECK(status == SquashStatus::ok);
                    CHECK(block.size() >= squash_header_bytes);
                    CHECK(std::memcmp(block.data(), "SQSH", 4) == 0);
                    CHECK(block[squash_field::version] == squash_writer_version);
                    CHECK(block[squash_field::type] == static_cast<uint8_t>(type));
                    CHECK(block[squash_field::scrambled] == (scramble ? 1 : 0));
                    CHECK(squash_unpacked_size(block.data(), block.size()) == size);
                    std::vector<uint8_t> out;
                    CHECK(unpack(block, out) == SquashStatus::ok);
                    CHECK(out == input);
                }
            }
        }
    }
}

void lz77_payload_matches_the_lz77_encoder() {
    std::vector<uint8_t> input(300);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<uint8_t>(i * i);
    const auto block = pack(input, SquashType::lz77, false);
    const auto encoded = oa::formats::sqsh::encode_lz77(input, input.size() * 2).value.value();
    CHECK(block.size() == encoded.size() + squash_header_bytes);
    CHECK(std::equal(encoded.begin(), encoded.end(), block.begin() + squash_header_bytes));
    uint32_t sum = 0;
    for (const auto b : encoded)
        sum += b;
    uint32_t stored = 0;
    std::memcpy(&stored, block.data() + squash_field::checksum, 4);
    CHECK(stored == sum);
}

void pack_errors() {
    uint8_t out[64];
    uint8_t in[4] = {1, 2, 3, 4};
    uint32_t capacity = sizeof(out);
    CHECK(
        squash_pack(nullptr, &capacity, in, 4, SquashType::lz77, false) == SquashStatus::bad_params
    );
    CHECK(squash_pack(out, &capacity, in, 0, SquashType::lz77, false) == SquashStatus::bad_params);
    CHECK(
        squash_pack(out, &capacity, in, 4, static_cast<SquashType>(4), false) ==
        SquashStatus::bad_type
    );
    capacity = 10;
    CHECK(
        squash_pack(out, &capacity, in, 4, SquashType::lz77, false) ==
        SquashStatus::output_too_small
    );
    CHECK(capacity == 10);
    // Stored packing reports the input size but leaves the payload untouched.
    std::memset(out, 0xaa, sizeof(out));
    capacity = sizeof(out);
    CHECK(squash_pack(out, &capacity, in, 4, SquashType::stored, false) == SquashStatus::ok);
    CHECK(capacity == squash_header_bytes + 4);
    CHECK(out[squash_header_bytes] == 0xaa);
}

void unpack_errors() {
    std::vector<uint8_t> input(200, 3);
    const auto good = pack(input, SquashType::lz77, false);
    std::vector<uint8_t> out;

    auto block = good;
    block[0] = 'X';
    CHECK(unpack(block, out) == SquashStatus::bad_header);
    uint8_t tiny[4] = {'S', 'Q', 'S', 'H'};
    CHECK(squash_unpack(nullptr, 0, tiny, sizeof(tiny)) == SquashStatus::bad_header);

    block = good;
    block[squash_field::type] = 4;
    CHECK(unpack(block, out) == SquashStatus::bad_type);

    block = good;
    block.back() ^= 1;
    CHECK(unpack(block, out) == SquashStatus::bad_checksum);

    block = good;
    block[squash_field::packed_size] = 0xff;
    block[squash_field::packed_size + 1] = 0xff;
    CHECK(unpack(block, out) == SquashStatus::bad_checksum);

    block = good;
    block[squash_field::unpacked_size] =
        static_cast<uint8_t>(block[squash_field::unpacked_size] + 1);
    CHECK(unpack(block, out) == SquashStatus::bad_unpack_size);

    block = good;
    block[squash_field::type] = 0;
    CHECK(unpack(block, out) == SquashStatus::bad_unpack_size);

    CHECK(
        std::strcmp(squash_status_name(SquashStatus::bad_checksum), "SQUASHERR_BADCHECKSUM") == 0
    );
    CHECK(squash_status_name(static_cast<SquashStatus>(7)) == nullptr);
}

} // namespace

int main() {
    bound_values();
    round_trips();
    lz77_payload_matches_the_lz77_encoder();
    pack_errors();
    unpack_errors();
    return oa::data::persist::test::finish("persist-squash");
}
