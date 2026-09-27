// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/sqsh.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace oa::formats::sqsh {
std::vector<uint8_t> decode_lz77(
    std::span<const uint8_t> input,
    std::size_t output_limit,
    std::span<const uint8_t> initial_dictionary
) {
    std::array<uint8_t, lz77::dictionary_bytes> dictionary{};
    if (!initial_dictionary.empty()) {
        if (initial_dictionary.size() != dictionary.size())
            throw std::invalid_argument("LZ77 dictionary must contain 4096 bytes");
        std::copy(initial_dictionary.begin(), initial_dictionary.end(), dictionary.begin());
    }
    std::vector<uint8_t> output;
    std::size_t cursor = 0;
    std::size_t write = lz77::initial_write_offset;
    const auto read = [&]() {
        if (cursor == input.size())
            throw std::runtime_error("truncated LZ77 stream");
        return input[cursor++];
    };
    const auto emit = [&](uint8_t value) {
        if (output.size() == output_limit)
            throw std::runtime_error("LZ77 output limit exceeded");
        output.push_back(value);
        dictionary[write] = value;
        write = (write + 1) & lz77::dictionary_mask;
    };
    for (;;) {
        const auto flags = read();
        for (unsigned mask = 1; mask <= lz77::last_flag_bit; mask <<= 1) {
            if ((flags & mask) == 0) {
                emit(read());
                continue;
            }
            const auto low = read();
            const auto high = read();
            const auto token = static_cast<uint16_t>(low | (static_cast<unsigned>(high) << 8));
            const std::size_t offset = token >> lz77::token_length_bits;
            if (offset == lz77::terminator_offset)
                return output;
            const std::size_t count = (token & lz77::token_length_mask) + lz77::minimum_match_bytes;
            // Read after each preceding write: overlapping copies are intentional.
            for (std::size_t i = 0; i < count; ++i)
                emit(dictionary[(offset + i) & lz77::dictionary_mask]);
        }
    }
}

void decrypt_chunk(std::span<uint8_t> bytes) noexcept {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto index = static_cast<uint8_t>(i);
        bytes[i] = static_cast<uint8_t>(static_cast<uint8_t>(bytes[i] - index) ^ index);
    }
}

void encrypt_chunk(std::span<uint8_t> bytes) noexcept {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto index = static_cast<uint8_t>(i);
        bytes[i] = static_cast<uint8_t>((bytes[i] ^ index) + index);
    }
}

uint32_t chunk_checksum(std::span<const uint8_t> bytes) noexcept {
    uint32_t sum = 0;
    for (const auto byte : bytes)
        sum += byte;
    return sum;
}
} // namespace oa::formats::sqsh
