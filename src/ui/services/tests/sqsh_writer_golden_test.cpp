// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The engine services' SQSH chunk writer, pinned byte for byte: for a
// one-byte, a highly repetitive and a 64 KiB pseudo-random payload, the LZ77
// chunk's size and 64-bit FNV-1a, scrambled and plain, must equal the values
// below on every platform, and the archive chunk reader must decode each
// back. An empty payload is refused. zlib chunks are only decoded back:
// their bytes come from the host zlib.
#include "oa/formats/hpi.hpp"
#include "oa/ui/services/sqsh_writer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;
using oa::ui::services::SqshWriteStatus;

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
// Room for any payload's chunk: twice the input plus slack.
constexpr size_t chunk_slack = 64;
constexpr int32_t scrambled = 1;
constexpr int32_t plain = 0;

constexpr uint64_t fnv_basis = 0xcbf29ce484222325ull;
constexpr uint64_t fnv_prime = 0x100000001b3ull;

struct PinnedBytes {
    uint64_t size{};
    uint64_t hash{}; // 64-bit FNV-1a
};

struct PinnedChunks {
    PinnedBytes scrambled{};
    PinnedBytes plain{};
};

constexpr std::array<PinnedChunks, payload_count> pinned{{
    {{24, 0x3fb65b1e9f31da64ull}, {24, 0x11922e4f2da73af5ull}},
    {{1065, 0xbfd69d6ab8852c33ull}, {1065, 0x0db7e9d2c5ec38a7ull}},
    {{73280, 0xb101064c6b0058f4ull}, {73280, 0x45101215eff78ceaull}},
}};

int failures = 0;

/// Records a failed check.
///
/// @param held whether the check held
/// @param what description printed when it did not
void check(bool held, const std::string& what) {
    if (!held) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

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

/// Returns a payload's name.
///
/// @param kind payload
/// @return its lower-case name
const char* payload_name(Payload kind) {
    constexpr std::array<const char*, payload_count> names{"one_byte", "repetitive", "random"};
    return names[static_cast<size_t>(kind)];
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

/// Checks bytes against a pinned size and hash, printing the values found when they differ.
///
/// @param what name printed
/// @param bytes bytes produced
/// @param expected pinned size and hash
void matches_pinned(
    const std::string& what, std::span<const uint8_t> bytes, const PinnedBytes& expected
) {
    const PinnedBytes found{bytes.size(), fnv1a(bytes)};
    char text[96];
    std::snprintf(
        text,
        sizeof text,
        "{%llu, 0x%016llxull}",
        static_cast<unsigned long long>(found.size),
        static_cast<unsigned long long>(found.hash)
    );
    check(found.size == expected.size && found.hash == expected.hash, what + " is " + text);
}

/// Writes one chunk.
///
/// @param input payload
/// @param compression 1 = LZ77, 2 = zlib
/// @param encrypt 1 to scramble the payload
/// @return the chunk, or nothing when the writer refused it
Bytes write_chunk(const Bytes& input, int32_t compression, int32_t encrypt) {
    Bytes chunk(oa::formats::hpi::SQSHHeaderSize + input.size() * 2 + chunk_slack);
    auto size = static_cast<uint32_t>(chunk.size());
    const auto status = oa::ui::services::sqsh_write_chunk(
        chunk.data(), &size, input.data(), static_cast<uint32_t>(input.size()), compression, encrypt
    );
    if (status != SqshWriteStatus::ok)
        return {};
    chunk.resize(size);
    return chunk;
}

/// Decodes a chunk with the archive chunk reader.
///
/// @param chunk chunk bytes; copied, since the reader descrambles in place
/// @param size decoded size expected
/// @return the decoded bytes, or nothing when the reader refused the chunk
Bytes read_chunk(Bytes chunk, size_t size) {
    Bytes out(size);
    if (oa::unsquash_archive_block(out, chunk) != oa::formats::hpi::SquashStatus::ok)
        return {};
    return out;
}

/// Checks each payload's LZ77 chunks against their pins and decodes them and zlib chunks back.
void chunks_match_pins() {
    for (size_t index = 0; index < payload_count; ++index) {
        const auto kind = static_cast<Payload>(index);
        const std::string name = payload_name(kind);
        const Bytes input = payload(kind);
        const Bytes locked = write_chunk(input, oa::formats::hpi::CompressionLZ77, scrambled);
        matches_pinned("scrambled LZ77 chunk of " + name, locked, pinned[index].scrambled);
        check(
            read_chunk(locked, input.size()) == input,
            "scrambled LZ77 chunk of " + name + " decodes back"
        );
        const Bytes open = write_chunk(input, oa::formats::hpi::CompressionLZ77, plain);
        matches_pinned("plain LZ77 chunk of " + name, open, pinned[index].plain);
        check(
            read_chunk(open, input.size()) == input, "plain LZ77 chunk of " + name + " decodes back"
        );
        const Bytes zlib = write_chunk(input, oa::formats::hpi::CompressionZLib, scrambled);
        check(
            !zlib.empty() && read_chunk(zlib, input.size()) == input,
            "zlib chunk of " + name + " decodes back"
        );
    }
}

/// Checks that an empty payload is refused.
void empty_payload_is_refused() {
    uint8_t chunk[oa::formats::hpi::SQSHHeaderSize + chunk_slack]{};
    uint32_t size = sizeof chunk;
    const uint8_t input[1]{};
    check(
        oa::ui::services::sqsh_write_chunk(
            chunk, &size, input, 0, oa::formats::hpi::CompressionLZ77, plain
        ) == SqshWriteStatus::invalid_argument,
        "an empty payload is refused"
    );
}

} // namespace

int main() {
    try {
        chunks_match_pins();
        empty_payload_is_refused();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0)
        return 1;
    std::cout << "SQSH chunk writer golden bytes passed\n";
    return 0;
}
