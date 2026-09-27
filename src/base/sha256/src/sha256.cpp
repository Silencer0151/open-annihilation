// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The SHA-256 compression function, padding and running state.
#include "oa/base/sha256.hpp"

#include <algorithm>
#include <bit>

namespace oa::base::sha256 {
namespace {

/// Number of rounds of the compression function, one per message-schedule word.
constexpr size_t rounds = 64;

/// The round constants: the first 32 bits of the fractional parts of the
/// cube roots of the first 64 primes.
constexpr std::array<uint32_t, rounds> round_constants{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

/// Number of 32-bit words in one block.
constexpr size_t block_words = block_size / 4;
/// Size of the message length that ends the padding, in bytes.
constexpr size_t length_field_size = 8;
/// The byte that starts the padding: a single one bit, then zeros.
constexpr uint8_t padding_start = 0x80;

/// Reads a big-endian 32-bit word.
[[nodiscard]] uint32_t load_big_endian(const uint8_t* bytes) noexcept {
    return static_cast<uint32_t>(bytes[0]) << 24 | static_cast<uint32_t>(bytes[1]) << 16 |
           static_cast<uint32_t>(bytes[2]) << 8 | static_cast<uint32_t>(bytes[3]);
}

/// Runs the compression function over one block.
///
/// @param[in,out] state the hash value, updated with the block
/// @param block block_size message bytes
void compress(std::array<uint32_t, state_words>& state, const uint8_t* block) noexcept {
    std::array<uint32_t, rounds> schedule{};
    for (size_t index = 0; index < block_words; ++index)
        schedule[index] = load_big_endian(block + index * 4);
    for (size_t index = block_words; index < rounds; ++index) {
        const uint32_t early = schedule[index - 15];
        const uint32_t late = schedule[index - 2];
        const uint32_t sigma0 = std::rotr(early, 7) ^ std::rotr(early, 18) ^ (early >> 3);
        const uint32_t sigma1 = std::rotr(late, 17) ^ std::rotr(late, 19) ^ (late >> 10);
        schedule[index] = schedule[index - 16] + sigma0 + schedule[index - 7] + sigma1;
    }
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];
    for (size_t index = 0; index < rounds; ++index) {
        const uint32_t sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const uint32_t choice = (e & f) ^ (~e & g);
        const uint32_t first = h + sum1 + choice + round_constants[index] + schedule[index];
        const uint32_t sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t second = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

} // namespace

void update(Hasher& hasher, std::span<const uint8_t> bytes) noexcept {
    hasher.message_size += bytes.size();
    while (!bytes.empty()) {
        if (hasher.pending_size == 0 && bytes.size() >= block_size) {
            compress(hasher.state, bytes.data());
            bytes = bytes.subspan(block_size);
            continue;
        }
        const size_t taken = std::min(block_size - hasher.pending_size, bytes.size());
        std::copy_n(bytes.data(), taken, hasher.pending.data() + hasher.pending_size);
        hasher.pending_size += taken;
        bytes = bytes.subspan(taken);
        if (hasher.pending_size == block_size) {
            compress(hasher.state, hasher.pending.data());
            hasher.pending_size = 0;
        }
    }
}

Digest finish(Hasher hasher) noexcept {
    // The message length in bits, modulo 2^64, ends the last block.
    const uint64_t bit_length = hasher.message_size * 8;
    hasher.pending[hasher.pending_size++] = padding_start;
    if (hasher.pending_size > block_size - length_field_size) {
        std::fill(hasher.pending.begin() + hasher.pending_size, hasher.pending.end(), uint8_t{});
        compress(hasher.state, hasher.pending.data());
        hasher.pending_size = 0;
    }
    std::fill(
        hasher.pending.begin() + hasher.pending_size,
        hasher.pending.end() - length_field_size,
        uint8_t{}
    );
    for (size_t index = 0; index < length_field_size; ++index)
        hasher.pending[block_size - 1 - index] = static_cast<uint8_t>(bit_length >> (index * 8));
    compress(hasher.state, hasher.pending.data());
    Digest digest{};
    for (size_t word = 0; word < state_words; ++word)
        for (size_t index = 0; index < 4; ++index)
            digest[word * 4 + index] = static_cast<uint8_t>(hasher.state[word] >> (24 - index * 8));
    return digest;
}

Digest digest_of(std::span<const uint8_t> bytes) noexcept {
    Hasher hasher;
    update(hasher, bytes);
    return finish(hasher);
}

} // namespace oa::base::sha256
