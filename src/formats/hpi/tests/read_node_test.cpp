// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Characterisation of HpiArchive::read_node, which reads a file record by
// node index: a stored entry, LZ77 entries of exactly one chunk and of
// several, a zlib entry just over one chunk, an empty entry, an encrypted
// directory, the index checks and a chunk whose checksum no longer matches.
// Archives are written by the engine's own writer into a temporary
// directory.

#include "oa/formats/hpi.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

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

// Several 64 KiB chunks, exactly one, and a little over one.
constexpr std::size_t three_chunks = 2 * oa::formats::hpi::BlockBytes + 12345;
constexpr std::size_t one_full_chunk = oa::formats::hpi::BlockBytes;
constexpr std::size_t just_over_one_chunk = oa::formats::hpi::BlockBytes + 1;
constexpr uint8_t encrypting_header_key = 0x7D;
constexpr std::string_view chunk_marker = "SQSH";
// A payload byte some way into the first chunk, past its header, and the
// bits flipped in it.
constexpr std::size_t corrupted_payload_byte = oa::formats::hpi::SQSHHeaderSize + 7;
constexpr uint8_t corrupting_mask = 0x5A;

// Linear congruential step used to fill test entries.
constexpr uint32_t pattern_multiplier = 1103515245u;
constexpr uint32_t pattern_increment = 12345u;
constexpr uint32_t pattern_seed = 0x2F6B2A1Du;

/// Builds compressible bytes: a low-entropy pseudo-random pattern.
///
/// @param size length in bytes
/// @return the bytes
Bytes pattern_bytes(std::size_t size) {
    Bytes bytes(size);
    uint32_t state = pattern_seed;
    for (auto& byte : bytes) {
        state = state * pattern_multiplier + pattern_increment;
        byte = static_cast<uint8_t>((state >> 16) & 0x1F);
    }
    return bytes;
}

// A scratch directory removed when the test ends.
class TempDir {
  public:

    /// Creates a uniquely named directory under the system temporary directory.
    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("oa-hpi-read-node-" + std::to_string(stamp));
        fs::create_directories(path_);
    }

    /// Removes the directory and everything in it.
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    /// Writes a file into the directory.
    ///
    /// @param name file name
    /// @param bytes file contents
    /// @return the file's path
    fs::path write(const std::string& name, const Bytes& bytes) const {
        const fs::path target = path_ / name;
        std::ofstream stream(target, std::ios::binary);
        stream.write(
            reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
        );
        return target;
    }

  private:

    fs::path path_;
};

/// Returns the entries every archive in this test holds.
///
/// @return the files, one per compression kind and size
std::vector<oa::HpiWriteFile> sample_files() {
    return {
        {"readme.txt",
         Bytes{'h', 'e', 'l', 'l', 'o'},
         static_cast<uint8_t>(oa::formats::hpi::CompressionNone)},
        {"data/lz77.bin",
         pattern_bytes(three_chunks),
         static_cast<uint8_t>(oa::formats::hpi::CompressionLZ77)},
        {"data/zlib.bin",
         pattern_bytes(just_over_one_chunk),
         static_cast<uint8_t>(oa::formats::hpi::CompressionZLib)},
        {"data/empty.bin", Bytes{}, static_cast<uint8_t>(oa::formats::hpi::CompressionLZ77)},
        {"data/single.bin",
         pattern_bytes(one_full_chunk),
         static_cast<uint8_t>(oa::formats::hpi::CompressionLZ77)},
    };
}

/// Reports whether read_node rejects an index with std::invalid_argument.
///
/// @param archive archive to read
/// @param index node index
/// @return true when the read is refused
bool refuses(const oa::HpiArchive& archive, uint32_t index) {
    try {
        (void)archive.read_node(index);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

/// Checks that every file record reads back as written and that other indices are refused.
///
/// @param path archive to open
void check_round_trip(const fs::path& path) {
    const oa::HpiArchive archive(path);
    const auto files = sample_files();
    int32_t files_read = 0;
    for (const auto& file : files) {
        const auto index = archive.lookup(file.path);
        CHECK(index.has_value());
        if (!index) {
            continue;
        }
        const Bytes bytes = archive.read_node(*index);
        CHECK(bytes == file.bytes);
        CHECK(bytes.size() == archive.nodes()[*index].size);
        CHECK(archive.read(file.path) == bytes);
        ++files_read;
    }
    CHECK(files_read == static_cast<int32_t>(files.size()));
    const auto directory = archive.lookup("data");
    CHECK(directory.has_value() && archive.nodes()[*directory].directory());
    CHECK(refuses(archive, 0));
    if (directory) {
        CHECK(refuses(archive, *directory));
    }
    CHECK(refuses(archive, static_cast<uint32_t>(archive.nodes().size())));
}

/// Checks read_node over plain and encrypted archives.
///
/// @param scratch directory the archives are written into
void test_round_trips(const TempDir& scratch) {
    const auto files = sample_files();
    check_round_trip(scratch.write("plain.hpi", oa::write_hpi(files)));

    oa::HpiWriteOptions encrypted;
    encrypted.header_key = encrypting_header_key;
    encrypted.scramble_chunks = false;
    check_round_trip(scratch.write("encrypted.hpi", oa::write_hpi(files, encrypted)));
}

/// Checks that a chunk whose checksum no longer matches its payload fails the read.
///
/// @param scratch directory the archive is written into
void test_corrupt_chunk(const TempDir& scratch) {
    Bytes bytes = oa::write_hpi(sample_files());
    // The stored entry has no chunks, so the first marker opens lz77.bin.
    const std::string_view view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const std::size_t marker = view.find(chunk_marker);
    CHECK(marker != std::string_view::npos);
    if (marker == std::string_view::npos) {
        return;
    }
    bytes[marker + corrupted_payload_byte] ^= corrupting_mask;
    const oa::HpiArchive archive(scratch.write("corrupt.hpi", bytes));
    const auto corrupted = archive.lookup("data/lz77.bin");
    const auto readme = archive.lookup("readme.txt");
    const auto zlib = archive.lookup("data/zlib.bin");
    CHECK(corrupted.has_value() && readme.has_value() && zlib.has_value());
    if (!corrupted || !readme || !zlib) {
        return;
    }
    bool failed = false;
    try {
        (void)archive.read_node(*corrupted);
    } catch (const std::runtime_error&) {
        failed = true;
    }
    CHECK(failed);
    // Other entries still read.
    CHECK(archive.read_node(*readme).size() == 5);
    CHECK(archive.read_node(*zlib) == pattern_bytes(just_over_one_chunk));
}

} // namespace

int main() {
    const TempDir scratch;
    test_round_trips(scratch);
    test_corrupt_chunk(scratch);
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
