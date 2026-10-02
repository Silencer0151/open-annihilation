// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The LZ77 encoder and the HPI archive writer, pinned byte for byte: for an
// empty, a one-byte, a highly repetitive and a 64 KiB pseudo-random payload,
// the compressed stream's size and 64-bit FNV-1a, and two archives of those
// payloads (scrambled chunks under key 0, plain chunks under a nonzero key)
// must equal the values below on every platform, and everything must read
// back unchanged. zlib entries are only read back: their bytes come from
// the host zlib, which may encode differently while decoding identically.
// Usage: oa-hpi-writer-golden-test SCRATCH_ARCHIVE_PATH
#include "oa/formats/hpi.hpp"
#include "oa/formats/sqsh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

enum class Payload : uint8_t { empty, one_byte, repetitive, random, count };
constexpr size_t payload_count = static_cast<size_t>(Payload::count);

constexpr uint8_t one_byte_value = 'A';
constexpr size_t repetitive_bytes = 8192;
constexpr std::string_view repetitive_cycle = "OPEN ANNIHILATION ";
constexpr size_t random_bytes = 0x10000;
constexpr uint32_t random_seed = 0x5eed0001u;
constexpr uint32_t random_multiplier = 1664525u;
constexpr uint32_t random_increment = 1013904223u;
constexpr unsigned random_byte_shift = 24;
// The archive writer's LZ77 output budget: twice the input plus slack.
constexpr size_t lz77_slack = 64;
constexpr uint8_t plain_archive_key = 0x42;

constexpr uint64_t fnv_basis = 0xcbf29ce484222325ull;
constexpr uint64_t fnv_prime = 0x100000001b3ull;

struct PinnedBytes {
    uint64_t size{};
    uint64_t hash{}; // 64-bit FNV-1a
};

constexpr std::array<PinnedBytes, payload_count> pinned_lz77{{
    {4, 0xad2aca7747985764ull},
    {5, 0x5f7468103c25dd44ull},
    {1046, 0x7828abada39e57feull},
    {73261, 0x1860d95ba077e1d0ull},
}};
constexpr PinnedBytes pinned_scrambled_archive{
    82815, 0xaea51955ff4f87dcull
}; // key 0, scrambled chunks
constexpr PinnedBytes pinned_plain_archive{
    82815, 0x8e603b8635fd6dadull
}; // plain_archive_key, plain chunks

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
    case Payload::empty:
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
    constexpr std::array<const char*, payload_count> names{
        "empty", "one_byte", "repetitive", "random"
    };
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

/// Checks each payload's LZ77 stream against its pin and decodes it back.
void lz77_streams_match_pins() {
    for (size_t index = 0; index < payload_count; ++index) {
        const auto kind = static_cast<Payload>(index);
        const Bytes input = payload(kind);
        const Bytes encoded =
            oa::formats::sqsh::encode_lz77(input, input.size() * 2 + lz77_slack).value.value();
        matches_pinned(std::string("LZ77 of ") + payload_name(kind), encoded, pinned_lz77[index]);
        check(
            oa::formats::sqsh::decode_lz77(encoded, input.size()).value == input,
            std::string("LZ77 of ") + payload_name(kind) + " decodes back"
        );
    }
}

/// Builds the archive entries: each payload with the given compression, then the repetitive one stored.
///
/// @param compression chunk compression of the payload entries
/// @return the entries, "<payload>.bin" at the root and "stored/repetitive.bin"
std::vector<oa::HpiWriteFile> archive_files(uint8_t compression) {
    std::vector<oa::HpiWriteFile> files;
    for (size_t index = 0; index < payload_count; ++index) {
        const auto kind = static_cast<Payload>(index);
        files.push_back({std::string(payload_name(kind)) + ".bin", payload(kind), compression});
    }
    files.push_back(
        {"stored/repetitive.bin", payload(Payload::repetitive), oa::formats::hpi::CompressionNone}
    );
    return files;
}

/// Writes an archive to disk and reads every entry back.
///
/// @param what name printed
/// @param archive archive bytes
/// @param files entries it was written from
/// @param path scratch file the archive is written to
void reads_back(
    const std::string& what,
    const Bytes& archive,
    const std::vector<oa::HpiWriteFile>& files,
    const std::filesystem::path& path
) {
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(
            reinterpret_cast<const char*>(archive.data()),
            static_cast<std::streamsize>(archive.size())
        );
        check(out.good(), what + " written to " + path.string());
    }
    const auto opened = oa::open_hpi_file(path);
    check(opened.ok(), what + " opens");
    if (!opened.ok())
        return;
    const oa::HpiArchive& reader = *opened.value;
    check(reader.entries().size() == files.size(), what + " lists every entry");
    for (const auto& file : files)
        check(reader.read(file.path).value == file.bytes, what + " reads back " + file.path);
}

/// Checks the two pinned archives, and reads them and a zlib archive back.
///
/// @param path scratch file the archives are written to
void archives_match_pins(const std::filesystem::path& path) {
    const auto lz77_files = archive_files(oa::formats::hpi::CompressionLZ77);
    const Bytes scrambled = oa::write_hpi(lz77_files);
    matches_pinned("scrambled archive", scrambled, pinned_scrambled_archive);
    reads_back("scrambled archive", scrambled, lz77_files, path);

    const oa::HpiWriteOptions plain_options{
        .header_key = plain_archive_key, .scramble_chunks = false
    };
    const Bytes plain = oa::write_hpi(lz77_files, plain_options);
    matches_pinned("plain archive", plain, pinned_plain_archive);
    reads_back("plain archive", plain, lz77_files, path);

    const auto zlib_files = archive_files(oa::formats::hpi::CompressionZLib);
    reads_back("zlib archive", oa::write_hpi(zlib_files), zlib_files, path);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: oa-hpi-writer-golden-test SCRATCH_ARCHIVE_PATH\n";
        return 2;
    }
    try {
        lz77_streams_match_pins();
        archives_match_pins(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0)
        return 1;
    std::cout << "HPI writer golden bytes passed\n";
    return 0;
}
