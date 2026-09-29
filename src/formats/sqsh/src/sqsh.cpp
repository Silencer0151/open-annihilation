// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/sqsh.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace oa::formats::sqsh {
namespace {

// A match token's two input bytes produce at most 17 output bytes and a
// literal's one byte produces one, so no stream yields more than 9 bytes for
// each input byte.
constexpr std::size_t max_output_per_input_byte =
    (lz77::token_length_mask + lz77::minimum_match_bytes + 1) / 2;

} // namespace

std::vector<uint8_t> decode_lz77(
    std::span<const uint8_t> input,
    std::size_t output_limit,
    std::span<const uint8_t> initial_dictionary
) {
    const std::size_t most = input.size() > output_limit / max_output_per_input_byte
                                 ? output_limit
                                 : input.size() * max_output_per_input_byte;
    std::vector<uint8_t> output(most);
    const Lz77Decoded decoded = decode_lz77_into(input, output, initial_dictionary);
    switch (decoded.status) {
    case Lz77Status::ok:
        break;
    case Lz77Status::truncated:
        throw std::runtime_error("truncated LZ77 stream");
    case Lz77Status::output_full:
        throw std::runtime_error("LZ77 output limit exceeded");
    case Lz77Status::bad_dictionary:
        throw std::invalid_argument("LZ77 dictionary must contain 4096 bytes");
    }
    output.resize(decoded.written);
    return output;
}

Lz77Decoded decode_lz77_into(
    std::span<const uint8_t> input,
    std::span<uint8_t> output,
    std::span<const uint8_t> initial_dictionary
) noexcept {
    std::array<uint8_t, lz77::dictionary_bytes> window{};
    if (!initial_dictionary.empty()) {
        if (initial_dictionary.size() != window.size())
            return {0, Lz77Status::bad_dictionary};
        std::memcpy(window.data(), initial_dictionary.data(), window.size());
    }
    // Plain pointers keep the per-byte loop free of calls in unoptimised builds.
    uint8_t* const dictionary = window.data();
    const uint8_t* const in = input.data();
    const std::size_t in_size = input.size();
    uint8_t* const out = output.data();
    const std::size_t out_size = output.size();
    std::size_t cursor = 0;
    std::size_t written = 0;
    std::size_t write = lz77::initial_write_offset;
    for (;;) {
        if (cursor == in_size)
            return {written, Lz77Status::truncated};
        const uint8_t flags = in[cursor++];
        for (unsigned mask = 1; mask <= lz77::last_flag_bit; mask <<= 1) {
            if ((flags & mask) == 0) {
                if (cursor == in_size)
                    return {written, Lz77Status::truncated};
                if (written == out_size)
                    return {written, Lz77Status::output_full};
                const uint8_t value = in[cursor++];
                out[written++] = value;
                dictionary[write] = value;
                write = (write + 1) & lz77::dictionary_mask;
                continue;
            }
            if (in_size - cursor < 2)
                return {written, Lz77Status::truncated};
            const auto token =
                static_cast<uint16_t>(in[cursor] | (static_cast<unsigned>(in[cursor + 1]) << 8));
            cursor += 2;
            const std::size_t offset = token >> lz77::token_length_bits;
            if (offset == lz77::terminator_offset)
                return {written, Lz77Status::ok};
            const std::size_t count = (token & lz77::token_length_mask) + lz77::minimum_match_bytes;
            // Read after each preceding write: overlapping copies are intentional.
            for (std::size_t i = 0; i < count; ++i) {
                if (written == out_size)
                    return {written, Lz77Status::output_full};
                const uint8_t value = dictionary[(offset + i) & lz77::dictionary_mask];
                out[written++] = value;
                dictionary[write] = value;
                write = (write + 1) & lz77::dictionary_mask;
            }
        }
    }
}

void decrypt_chunk(std::span<uint8_t> bytes) noexcept {
    uint8_t* const data = bytes.data();
    const std::size_t size = bytes.size();
    for (std::size_t i = 0; i < size; ++i) {
        const auto index = static_cast<uint8_t>(i);
        data[i] = static_cast<uint8_t>(static_cast<uint8_t>(data[i] - index) ^ index);
    }
}

void encrypt_chunk(std::span<uint8_t> bytes) noexcept {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto index = static_cast<uint8_t>(i);
        bytes[i] = static_cast<uint8_t>((bytes[i] ^ index) + index);
    }
}

uint32_t chunk_checksum(std::span<const uint8_t> bytes) noexcept {
    const uint8_t* const data = bytes.data();
    const std::size_t size = bytes.size();
    uint32_t sum = 0;
    for (std::size_t i = 0; i < size; ++i)
        sum += data[i];
    return sum;
}
} // namespace oa::formats::sqsh
