// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The savegame bank's SQSH packer, pinned byte for byte: for a one-byte, a
// highly repetitive and a 64 KiB pseudo-random payload, the LZ77 block's
// size and 64-bit FNV-1a, scrambled and plain, must equal the values below
// on every platform (the same bytes the engine services' chunk writer is
// pinned to), and each must unpack back. An empty payload is refused. zlib
// blocks are only unpacked back: their bytes come from the host zlib.
#include "check.hpp"

#include "oa/data/persist/squash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>
#include <vector>

using namespace oa::data::persist;

namespace {

using Bytes = std::vector<uint8_t>;

enum class Payload : uint8_t { one_byte, repetitive, random, count };
constexpr size_t payload_count = static_cast<size_t>(Payload::count);

constexpr uint8_t one_byte_value = 'A';
constexpr size_t repetitive_bytes = 8192;
constexpr std::string_view repetitive_cycle = "OPEN ANNIHILATION ";
constexpr size_t random_bytes = 0x10000;
constexpr uint32_t random_seed = 0x5eed0001u;
constexpr uint32_t random_multiplier = 1664525u;
constexpr uint32_t random_increment = 1013904223u;
constexpr unsigned random_byte_shift = 24;
// Room for any payload's block: twice the input plus slack.
constexpr size_t block_slack = 64;

constexpr uint64_t fnv_basis = 0xcbf29ce484222325ull;
constexpr uint64_t fnv_prime = 0x100000001b3ull;

struct PinnedBytes {
    uint64_t size{};
    uint64_t hash{}; // 64-bit FNV-1a
};

struct PinnedBlocks {
    PinnedBytes scrambled{};
    PinnedBytes plain{};
};

constexpr std::array<PinnedBlocks, payload_count> pinned{{
    {{24, 0x3fb65b1e9f31da64ull}, {24, 0x11922e4f2da73af5ull}},
    {{1065, 0xbfd69d6ab8852c33ull}, {1065, 0x0db7e9d2c5ec38a7ull}},
    {{73280, 0xb101064c6b0058f4ull}, {73280, 0x45101215eff78ceaull}},
}};

/// Returns the bytes of a test payload.
///
/// @param kind payload
/// @return its bytes; the random one comes from a 32-bit LCG's top byte
Bytes payload(Payload kind) {
    Bytes bytes;
    switch (kind) {
    case Payload::count:
        break;
    case Payload::one_byte:
        bytes.push_back(one_byte_value);
        break;
    case Payload::repetitive:
        for (size_t i = 0; i < repetitive_bytes; ++i)
            bytes.push_back(static_cast<uint8_t>(repetitive_cycle[i % repetitive_cycle.size()]));
        break;
    case Payload::random: {
        uint32_t state = random_seed;
        for (size_t i = 0; i < random_bytes; ++i) {
            state = state * random_multiplier + random_increment;
            bytes.push_back(static_cast<uint8_t>(state >> random_byte_shift));
        }
        break;
    }
    }
    return bytes;
}

/// Returns the 64-bit FNV-1a of bytes.
///
/// @param bytes bytes to hash
/// @return the hash
uint64_t fnv1a(std::span<const uint8_t> bytes) {
    uint64_t hash = fnv_basis;
    for (const uint8_t byte : bytes) {
        hash ^= byte;
        hash *= fnv_prime;
    }
    return hash;
}

/// Tells whether bytes match a pinned size and hash, printing the values found when they differ.
///
/// @param what name printed
/// @param bytes bytes produced
/// @param expected pinned size and hash
/// @return whether they match
bool matches_pinned(const char* what, std::span<const uint8_t> bytes, const PinnedBytes& expected) {
    const PinnedBytes found{bytes.size(), fnv1a(bytes)};
    if (found.size != expected.size || found.hash != expected.hash)
        std::fprintf(
            stderr,
            "%s is {%llu, 0x%016llxull}\n",
            what,
            static_cast<unsigned long long>(found.size),
            static_cast<unsigned long long>(found.hash)
        );
    return found.size == expected.size && found.hash == expected.hash;
}

/// Packs one block.
///
/// @param input payload
/// @param type compression
/// @param scramble whether the payload is scrambled
/// @return the block, or nothing when the packer refused it
Bytes pack(const Bytes& input, SquashType type, bool scramble) {
    Bytes block(squash_header_bytes + input.size() * 2 + block_slack);
    auto size = static_cast<uint32_t>(block.size());
    if (squash_pack(
            block.data(), &size, input.data(), static_cast<uint32_t>(input.size()), type, scramble
        ) != SquashStatus::ok)
        return {};
    block.resize(size);
    return block;
}

/// Unpacks a block.
///
/// @param block block bytes; copied, since unpacking descrambles in place
/// @param size unpacked size expected
/// @return the unpacked bytes, or nothing when the block is refused
Bytes unpack(Bytes block, size_t size) {
    Bytes out(size);
    if (squash_unpack(out.data(), out.size(), block.data(), block.size()) != SquashStatus::ok)
        return {};
    return out;
}

/// Checks each payload's LZ77 blocks against their pins and unpacks them and zlib blocks back.
void blocks_match_pins() {
    for (size_t index = 0; index < payload_count; ++index) {
        const Bytes input = payload(static_cast<Payload>(index));
        const Bytes scrambled = pack(input, SquashType::lz77, true);
        CHECK(matches_pinned("scrambled LZ77 block", scrambled, pinned[index].scrambled));
        CHECK(unpack(scrambled, input.size()) == input);
        const Bytes plain = pack(input, SquashType::lz77, false);
        CHECK(matches_pinned("plain LZ77 block", plain, pinned[index].plain));
        CHECK(unpack(plain, input.size()) == input);
        const Bytes zlib = pack(input, SquashType::zlib, true);
        CHECK(!zlib.empty() && unpack(zlib, input.size()) == input);
    }
}

/// Checks that an empty payload is refused.
void empty_payload_is_refused() {
    uint8_t block[squash_header_bytes + block_slack]{};
    uint32_t size = sizeof block;
    const uint8_t input[1]{};
    CHECK(squash_pack(block, &size, input, 0, SquashType::lz77, false) == SquashStatus::bad_params);
}

} // namespace

int main() {
    blocks_match_pins();
    empty_payload_is_refused();
    return oa::data::persist::test::finish("persist-squash-golden");
}
