// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The zip reader and writer: round trips, the writer's exact bytes, archives
// made by the zip command and by zlib, every refusal, and malformed and
// hostile archives.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

#include "oa/base/sha256.hpp"
#include "oa/formats/zip.hpp"
#include "tool_archives.hpp"

#include <cstdlib>

// The address sanitizer keeps its own allocator, which this program's must
// not replace.
#if defined(__SANITIZE_ADDRESS__)
#define OA_ZIP_TEST_OWN_ALLOCATOR 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define OA_ZIP_TEST_OWN_ALLOCATOR 0
#endif
#endif
#ifndef OA_ZIP_TEST_OWN_ALLOCATOR
#define OA_ZIP_TEST_OWN_ALLOCATOR 1
#endif

#if OA_ZIP_TEST_OWN_ALLOCATOR
/// The largest allocation this program makes: a larger one fails, as on a
/// machine without the memory for it.
constexpr std::size_t refused_allocation_bytes = std::size_t{256} << 20;

/// Allocates as the standard allocator does, up to refused_allocation_bytes.
///
/// @param size bytes wanted
/// @return the block
void* operator new(std::size_t size) {
    if (size <= refused_allocation_bytes)
        if (void* block = std::malloc(size == 0 ? 1 : size))
            return block;
    throw std::bad_alloc();
}

/// Frees a block operator new gave.
///
/// @param block the block; null does nothing
void operator delete(void* block) noexcept {
    std::free(block);
}

/// Frees a block operator new gave.
///
/// @param block the block; null does nothing
void operator delete(void* block, std::size_t) noexcept {
    std::free(block);
}
#endif

namespace {

using namespace oa::formats::zip;

int g_failures = 0;

/// Reports a failed check with its line.
///
/// @param condition the checked condition
/// @param what the condition's source text
/// @param line the line of the check
void check(bool condition, const char* what, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// Offsets of the fields the tests patch, from the format's record layouts.
constexpr size_t local_flags = 6;
constexpr size_t local_method = 8;
constexpr size_t local_crc32 = 14;
constexpr size_t local_compressed_bytes = 18;
constexpr size_t local_uncompressed_bytes = 22;
constexpr size_t local_name = 30;
constexpr size_t central_flags = 8;
constexpr size_t central_method = 10;
constexpr size_t central_compressed_bytes = 20;
constexpr size_t central_uncompressed_bytes = 24;
constexpr size_t central_start_disk = 34;
constexpr size_t central_local_header_offset = 42;
constexpr size_t end_bytes = 22;
constexpr size_t end_disk = 4;
constexpr size_t end_directory_disk = 6;
constexpr size_t end_disk_entry_count = 8;
constexpr size_t end_entry_count = 10;
constexpr size_t end_directory_bytes = 12;
constexpr size_t end_directory_offset = 16;
constexpr size_t end_comment_length = 20;

constexpr uint16_t flag_encrypted = 1u << 0;
constexpr uint16_t flag_data_descriptor = 1u << 3;
constexpr uint16_t flag_utf8_name = 1u << 11;
constexpr uint16_t method_bzip2 = 12;

/// Returns the bytes of a string.
///
/// @param text the string
/// @return its bytes
std::vector<uint8_t> bytes_of(std::string_view text) {
    return {text.begin(), text.end()};
}

/// Returns deterministic bytes that do not deflate well.
///
/// @param size how many bytes
/// @param seed the generator's starting value
/// @return the bytes
std::vector<uint8_t> noise(size_t size, uint32_t seed) {
    std::vector<uint8_t> out(size);
    for (uint8_t& b : out) {
        seed = seed * 1103515245u + 12345u;
        b = static_cast<uint8_t>(seed >> 16);
    }
    return out;
}

/// Reads a little-endian 16-bit field.
///
/// @param bytes the bytes
/// @param offset the field's offset
/// @return its value
uint16_t get16(std::span<const uint8_t> bytes, size_t offset) {
    return static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

/// Reads a little-endian 32-bit field.
///
/// @param bytes the bytes
/// @param offset the field's offset
/// @return its value
uint32_t get32(std::span<const uint8_t> bytes, size_t offset) {
    return get16(bytes, offset) | (static_cast<uint32_t>(get16(bytes, offset + 2)) << 16);
}

/// Writes a little-endian 16-bit field.
///
/// @param[in,out] bytes the bytes
/// @param offset the field's offset
/// @param value the value
void put16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

/// Writes a little-endian 32-bit field.
///
/// @param[in,out] bytes the bytes
/// @param offset the field's offset
/// @param value the value
void put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    put16(bytes, offset, static_cast<uint16_t>(value));
    put16(bytes, offset + 2, static_cast<uint16_t>(value >> 16));
}

/// Appends a little-endian 16-bit field.
///
/// @param[in,out] out the bytes
/// @param value the value
void add16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

/// Appends a little-endian 32-bit field.
///
/// @param[in,out] out the bytes
/// @param value the value
void add32(std::vector<uint8_t>& out, uint32_t value) {
    add16(out, static_cast<uint16_t>(value));
    add16(out, static_cast<uint16_t>(value >> 16));
}

/// Deflates bytes as a raw stream, with no zlib header or trailer.
///
/// @param bytes the data
/// @return the deflated stream
std::vector<uint8_t> deflate_raw(std::span<const uint8_t> bytes) {
    z_stream stream{};
    constexpr int memory_level = 8;
    if (deflateInit2(
            &stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, memory_level, Z_DEFAULT_STRATEGY
        ) != Z_OK)
        return {};
    std::vector<uint8_t> out(deflateBound(&stream, static_cast<uLong>(bytes.size())));
    stream.next_in = const_cast<Bytef*>(bytes.data());
    stream.avail_in = static_cast<uInt>(bytes.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    const int result = deflate(&stream, Z_FINISH);
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return result == Z_STREAM_END ? out : std::vector<uint8_t>{};
}

/// One entry of an archive the test builds field by field.
struct RawEntry {
    std::string name{};
    uint16_t method{};
    uint16_t flags{};
    uint32_t crc32{};
    uint32_t bytes{};               ///< the recorded uncompressed size
    std::vector<uint8_t> data{};    ///< as stored, compressed or not
    bool zero_local_fields{};       ///< CRC-32 and sizes zero in the local header
    std::vector<uint8_t> extra{};   ///< the extra field of both records
    std::vector<uint8_t> trailer{}; ///< bytes after the data, such as a data descriptor
};

/// Returns a raw entry that stores bytes.
///
/// @param name the name
/// @param bytes the data
/// @return the entry
RawEntry stored_entry(std::string name, std::span<const uint8_t> bytes) {
    RawEntry entry{};
    entry.name = std::move(name);
    entry.crc32 = crc32_of(bytes);
    entry.bytes = static_cast<uint32_t>(bytes.size());
    entry.data.assign(bytes.begin(), bytes.end());
    return entry;
}

/// Returns a raw entry that deflates bytes.
///
/// @param name the name
/// @param bytes the data
/// @return the entry
RawEntry deflated_entry(std::string name, std::span<const uint8_t> bytes) {
    RawEntry entry = stored_entry(std::move(name), bytes);
    entry.method = static_cast<uint16_t>(Method::deflated);
    entry.data = deflate_raw(bytes);
    return entry;
}

/// Builds an archive from raw entries, as a writer other than this
/// module's would: version 2.0 records, any method and flags.
///
/// @param entries the entries
/// @return the archive
std::vector<uint8_t> build_archive(const std::vector<RawEntry>& entries) {
    std::vector<uint8_t> out{};
    std::vector<uint32_t> offsets{};
    constexpr uint16_t version = 20;
    for (const RawEntry& entry : entries) {
        offsets.push_back(static_cast<uint32_t>(out.size()));
        out.insert(out.end(), {'P', 'K', 3, 4});
        add16(out, version);
        add16(out, entry.flags);
        add16(out, entry.method);
        add32(out, 0);
        add32(out, entry.zero_local_fields ? 0 : entry.crc32);
        add32(out, entry.zero_local_fields ? 0 : static_cast<uint32_t>(entry.data.size()));
        add32(out, entry.zero_local_fields ? 0 : entry.bytes);
        add16(out, static_cast<uint16_t>(entry.name.size()));
        add16(out, static_cast<uint16_t>(entry.extra.size()));
        out.insert(out.end(), entry.name.begin(), entry.name.end());
        out.insert(out.end(), entry.extra.begin(), entry.extra.end());
        out.insert(out.end(), entry.data.begin(), entry.data.end());
        out.insert(out.end(), entry.trailer.begin(), entry.trailer.end());
    }
    const auto directory_offset = static_cast<uint32_t>(out.size());
    for (size_t index = 0; index < entries.size(); ++index) {
        const RawEntry& entry = entries[index];
        out.insert(out.end(), {'P', 'K', 1, 2});
        add16(out, version);
        add16(out, version);
        add16(out, entry.flags);
        add16(out, entry.method);
        add32(out, 0);
        add32(out, entry.crc32);
        add32(out, static_cast<uint32_t>(entry.data.size()));
        add32(out, entry.bytes);
        add16(out, static_cast<uint16_t>(entry.name.size()));
        add16(out, static_cast<uint16_t>(entry.extra.size()));
        add16(out, 0);
        add16(out, 0);
        add16(out, 0);
        add32(out, 0);
        add32(out, offsets[index]);
        out.insert(out.end(), entry.name.begin(), entry.name.end());
        out.insert(out.end(), entry.extra.begin(), entry.extra.end());
    }
    const auto directory_bytes = static_cast<uint32_t>(out.size() - directory_offset);
    out.insert(out.end(), {'P', 'K', 5, 6});
    add16(out, 0);
    add16(out, 0);
    add16(out, static_cast<uint16_t>(entries.size()));
    add16(out, static_cast<uint16_t>(entries.size()));
    add32(out, directory_bytes);
    add32(out, directory_offset);
    add16(out, 0);
    return out;
}

/// Writes an archive with write_archive, failing the test when it cannot.
///
/// @param entries the entries
/// @return the archive
std::vector<uint8_t> written(std::span<const NewEntry> entries) {
    std::vector<uint8_t> archive{};
    ZipError error{};
    CHECK(write_archive(entries, archive, error));
    CHECK(error.status == ZipStatus::ok);
    return archive;
}

/// Returns the offset of an archive's end record, which has no comment.
///
/// @param archive the archive
/// @return the offset
size_t end_offset(std::span<const uint8_t> archive) {
    return archive.size() - end_bytes;
}

/// Returns the offset of an archive's first central directory record.
///
/// @param archive the archive, without a comment
/// @return the offset
size_t directory_offset(std::span<const uint8_t> archive) {
    return get32(archive, end_offset(archive) + end_directory_offset);
}

/// Reads an archive's directory and returns the status.
///
/// @param archive the archive
/// @param[out] error the error, when the caller wants it
/// @return the status
ZipStatus directory_status(std::span<const uint8_t> archive, ZipError* error = nullptr) {
    CentralDirectory directory{};
    ZipError local{};
    const bool read = read_directory(archive, directory, local);
    CHECK(read == (local.status == ZipStatus::ok));
    if (!read)
        CHECK(directory.entries.empty());
    if (error != nullptr)
        *error = local;
    return local.status;
}

/// Reads an archive's directory and its first entry, and returns the
/// status of whichever failed.
///
/// @param archive the archive
/// @param[out] error the error, when the caller wants it
/// @return the status
ZipStatus first_entry_status(std::span<const uint8_t> archive, ZipError* error = nullptr) {
    CentralDirectory directory{};
    ZipError local{};
    if (read_directory(archive, directory, local) && !directory.entries.empty()) {
        std::vector<uint8_t> bytes{};
        const bool read = read_entry(archive, directory.entries.front(), bytes, local);
        CHECK(read == (local.status == ZipStatus::ok));
        if (!read)
            CHECK(bytes.empty());
    }
    if (error != nullptr)
        *error = local;
    return local.status;
}

/// Reads every entry of an archive, and tells whether all of them read.
///
/// @param archive the archive
/// @return true when the directory and every entry read
bool read_everything(std::span<const uint8_t> archive) {
    CentralDirectory directory{};
    ZipError error{};
    if (!read_directory(archive, directory, error))
        return false;
    for (const Entry& entry : directory.entries) {
        std::vector<uint8_t> bytes{};
        if (!read_entry(archive, entry, bytes, error))
            return false;
        if (bytes.size() != entry.bytes || crc32_of(bytes) != entry.crc32)
            return false;
    }
    return true;
}

/// Reads one entry by name, failing the test when it cannot.
///
/// @param archive the archive
/// @param directory its directory
/// @param name the entry's name
/// @return the entry's data
std::vector<uint8_t> entry_bytes(
    std::span<const uint8_t> archive, const CentralDirectory& directory, std::string_view name
) {
    const Entry* entry = find_entry(directory, name);
    CHECK(entry != nullptr);
    if (entry == nullptr)
        return {};
    std::vector<uint8_t> bytes{};
    ZipError error{};
    CHECK(read_entry(archive, *entry, bytes, error));
    CHECK(error.status == ZipStatus::ok);
    return bytes;
}

/// The entries the writer's golden archive holds.
struct GoldenEntries {
    std::vector<uint8_t> script = bytes_of("oascript: 1\ninput:\n  demo: game.rec\n");
    std::vector<uint8_t> recording = noise(300, 7);
    std::array<NewEntry, 3> entries{{
        {"game.oascript", script},
        {"recordings/", {}},
        {"recordings/game.rec", recording},
    }};
};

void test_name_rule() {
    struct Case {
        std::string_view name;
        bool safe;
    };

    const Case cases[] = {
        {"game.oascript", true},
        {"a/b/c.txt", true},
        {"dir/", true},
        {"a/b/", true},
        {"..a", true},
        {"a..", true},
        {"a/...", true},
        {"ab:c", true},
        {"1:y", true},
        {"caf\xc3\xa9.txt", true},
        {"", false},
        {"/", false},
        {"/etc/passwd", false},
        {"..", false},
        {"../a", false},
        {"a/../b", false},
        {"a/..", false},
        {".", false},
        {"./a", false},
        {"a/./b", false},
        {"a//b", false},
        {"a//", false},
        {"dir//", false},
        {"a\\b", false},
        {"..\\a", false},
        {"C:", false},
        {"C:/a", false},
        {"c:a", false},
        {"x:y", false},
        {std::string_view{"a\0b", 3}, false},
    };
    for (const Case& c : cases) {
        if (name_is_safe(c.name) != c.safe) {
            std::fprintf(stderr, "name_is_safe(\"%s\") is wrong\n", std::string{c.name}.c_str());
            CHECK(false);
        }
    }
}

void test_status_messages() {
    std::vector<std::string_view> seen{};
    for (uint8_t value = 0; value <= static_cast<uint8_t>(ZipStatus::crc_mismatch); ++value) {
        const std::string_view message = zip_status_message(static_cast<ZipStatus>(value));
        CHECK(!message.empty());
        for (const std::string_view other : seen)
            CHECK(other != message);
        seen.push_back(message);
    }
}

void test_crc32() {
    CHECK(crc32_of({}) == 0);
    const std::vector<uint8_t> check_input = bytes_of("123456789");
    CHECK(crc32_of(check_input) == 0xcbf43926u);
}

void test_round_trip() {
    const std::vector<uint8_t> text = bytes_of("director:\n  shots: []\n");
    const std::vector<uint8_t> large = noise(70000, 1);
    const std::vector<uint8_t> empty{};
    const std::string accented = "caf\xc3\xa9.txt";
    const std::array<NewEntry, 5> entries{{
        {"game.oascript", text},
        {"data/", {}},
        {"data/large.bin", large},
        {"data/empty", empty},
        {accented, text},
    }};
    const std::vector<uint8_t> archive = written(entries);

    CentralDirectory directory{};
    ZipError error{};
    CHECK(read_directory(archive, directory, error));
    CHECK(directory.entries.size() == entries.size());
    for (size_t index = 0; index < entries.size() && index < directory.entries.size(); ++index) {
        const Entry& entry = directory.entries[index];
        CHECK(entry.name == entries[index].name);
        CHECK(entry.method == Method::stored);
        CHECK(entry.bytes == entries[index].bytes.size());
        CHECK(entry.compressed_bytes == entry.bytes);
        CHECK(entry.crc32 == crc32_of(entries[index].bytes));
        CHECK(entry.directory == (index == 1));
        const std::vector<uint8_t> bytes = entry_bytes(archive, directory, entry.name);
        CHECK(
            std::equal(
                bytes.begin(), bytes.end(), entries[index].bytes.begin(), entries[index].bytes.end()
            )
        );
    }
    CHECK(find_entry(directory, "missing") == nullptr);
    CHECK(find_entry(directory, "data") == nullptr);
    CHECK(find_entry(directory, "data/") == &directory.entries[1]);

    // Only the entry whose name leaves 7-bit ASCII carries the UTF-8 flag.
    const Entry* ascii = find_entry(directory, "game.oascript");
    const Entry* utf8 = find_entry(directory, accented);
    CHECK(ascii != nullptr && utf8 != nullptr);
    if (ascii != nullptr && utf8 != nullptr) {
        CHECK(get16(archive, ascii->local_header_offset + local_flags) == 0);
        CHECK(get16(archive, utf8->local_header_offset + local_flags) == flag_utf8_name);
    }

    // An archive of no entries is its end record alone.
    const std::vector<uint8_t> nothing = written({});
    CHECK(nothing.size() == end_bytes);
    CHECK(read_directory(nothing, directory, error));
    CHECK(directory.entries.empty());
}

void test_writer_golden() {
    const GoldenEntries golden{};
    const std::vector<uint8_t> archive = written(golden.entries);
    // Local headers (30 bytes and the name), the data, central records (46
    // bytes and the name) and the end record.
    constexpr size_t expected_size =
        (30 + 13 + 36) + (30 + 11) + (30 + 19 + 300) + (46 + 13) + (46 + 11) + (46 + 19) + 22;
    CHECK(archive.size() == expected_size);
    // Version 2.0, no flags, stored, 00:00 on 1980-01-01.
    const std::array<uint8_t, 14> first_header{'P', 'K', 3, 4, 20, 0, 0, 0, 0, 0, 0, 0, 33, 0};
    CHECK(
        archive.size() >= first_header.size() &&
        std::equal(first_header.begin(), first_header.end(), archive.begin())
    );
    const auto digest = oa::base::sha256::to_hex(oa::base::sha256::digest_of(archive));
    const std::string_view hex{digest.data(), digest.size()};
    CHECK(hex == "362bda48337aa935654418b0e84bd48ade74964d3d2f7dbbd6a8e30275c10440");
    // The same entries give the same bytes every time.
    CHECK(written(golden.entries) == archive);
    CHECK(read_everything(archive));
}

void test_deflated_archives() {
    const std::vector<uint8_t> text = bytes_of(std::string(4000, 'x') + "end\n");
    const std::vector<uint8_t> random = noise(5000, 3);
    std::vector<RawEntry> raw{
        deflated_entry("text.txt", text),
        deflated_entry("random.bin", random),
        deflated_entry("empty", {}),
        stored_entry("plain", text),
    };
    CHECK(!raw[0].data.empty() && raw[0].data.size() < text.size());
    const std::vector<uint8_t> archive = build_archive(raw);
    CentralDirectory directory{};
    ZipError error{};
    CHECK(read_directory(archive, directory, error));
    CHECK(directory.entries.size() == raw.size());
    CHECK(directory.entries[0].method == Method::deflated);
    CHECK(entry_bytes(archive, directory, "text.txt") == text);
    CHECK(entry_bytes(archive, directory, "random.bin") == random);
    CHECK(entry_bytes(archive, directory, "empty").empty());
    CHECK(entry_bytes(archive, directory, "plain") == text);

    // A data descriptor: the local header holds zero, and the central
    // record the real values.
    RawEntry described = deflated_entry("described.txt", text);
    described.flags = flag_data_descriptor;
    described.zero_local_fields = true;
    described.trailer = {'P', 'K', 7, 8};
    add32(described.trailer, described.crc32);
    add32(described.trailer, static_cast<uint32_t>(described.data.size()));
    add32(described.trailer, described.bytes);
    described.extra = {1, 2, 3, 4, 5};
    const std::vector<uint8_t> descriptor_archive = build_archive({described, raw[3]});
    CHECK(read_directory(descriptor_archive, directory, error));
    CHECK(entry_bytes(descriptor_archive, directory, "described.txt") == text);
    CHECK(entry_bytes(descriptor_archive, directory, "plain") == text);
}

void test_tool_archives() {
    CentralDirectory directory{};
    ZipError error{};
    CHECK(read_directory(test::tool_archive, directory, error));
    CHECK(directory.entries.size() == 4);
    const std::vector<uint8_t> script = bytes_of("oascript: 1\ninput:\n  demo: game.rec\n");
    std::string list{};
    for (int line = 0; line < 40; ++line)
        list += "shot line\n";
    if (directory.entries.size() == 4) {
        CHECK(directory.entries[0].name == "game.oascript");
        CHECK(directory.entries[0].method == Method::stored);
        CHECK(directory.entries[1].name == "shots/");
        CHECK(directory.entries[1].directory);
        CHECK(directory.entries[2].name == "shots/list.txt");
        CHECK(directory.entries[2].method == Method::deflated);
        CHECK(directory.entries[3].name == "shots/tiny.bin");
    }
    CHECK(entry_bytes(test::tool_archive, directory, "game.oascript") == script);
    CHECK(entry_bytes(test::tool_archive, directory, "shots/").empty());
    CHECK(entry_bytes(test::tool_archive, directory, "shots/list.txt") == bytes_of(list));
    CHECK(entry_bytes(test::tool_archive, directory, "shots/tiny.bin") == bytes_of("ab"));

    CHECK(read_directory(test::descriptor_archive, directory, error));
    CHECK(directory.entries.size() == 1);
    CHECK(entry_bytes(test::descriptor_archive, directory, "game.oascript") == script);

    // The zip command's own 64-bit local header, written to a pipe.
    CHECK(directory_status(test::zip64_archive, &error) == ZipStatus::zip64);
    CHECK(error.offset == 0);
    CHECK(error.entry == "-");
}

void test_end_record() {
    const GoldenEntries golden{};
    const std::vector<uint8_t> archive = written(golden.entries);
    CHECK(directory_status({}) == ZipStatus::no_end_record);
    CHECK(directory_status(noise(100, 5)) == ZipStatus::no_end_record);

    // A comment, even one that holds an end record signature of its own.
    std::vector<uint8_t> commented = archive;
    const std::vector<uint8_t> comment = bytes_of("made for the test PK\x05\x06 of the reader");
    put16(
        commented, end_offset(archive) + end_comment_length, static_cast<uint16_t>(comment.size())
    );
    commented.insert(commented.end(), comment.begin(), comment.end());
    CHECK(read_everything(commented));

    // The longest comment still finds the record; one byte more, and the
    // record's comment length no longer reaches the end.
    std::vector<uint8_t> longest = archive;
    put16(longest, end_offset(archive) + end_comment_length, UINT16_MAX);
    longest.resize(longest.size() + UINT16_MAX, 'c');
    CHECK(read_everything(longest));
    longest.push_back('c');
    CHECK(directory_status(longest) == ZipStatus::no_end_record);

    // A comment length that does not reach the end, or runs past it.
    std::vector<uint8_t> short_comment = commented;
    put16(short_comment, end_offset(archive) + end_comment_length, 3);
    CHECK(directory_status(short_comment) == ZipStatus::no_end_record);
    std::vector<uint8_t> long_comment = archive;
    put16(long_comment, end_offset(archive) + end_comment_length, 1);
    CHECK(directory_status(long_comment) == ZipStatus::no_end_record);
}

void test_refusals() {
    const GoldenEntries golden{};
    const std::vector<uint8_t> archive = written(golden.entries);
    const size_t end = end_offset(archive);
    const size_t central = directory_offset(archive);
    ZipError error{};

    // Several disks, and the sentinels of the 64-bit extension.
    for (const size_t field : {end_disk, end_directory_disk, end_disk_entry_count}) {
        std::vector<uint8_t> patched = archive;
        put16(patched, end + field, 1);
        CHECK(directory_status(patched, &error) == ZipStatus::several_disks);
        CHECK(error.offset == end);
        CHECK(error.entry.empty());
        put16(patched, end + field, UINT16_MAX);
        CHECK(directory_status(patched) == ZipStatus::zip64);
    }
    {
        std::vector<uint8_t> patched = archive;
        put16(patched, end + end_disk_entry_count, UINT16_MAX);
        put16(patched, end + end_entry_count, UINT16_MAX);
        CHECK(directory_status(patched) == ZipStatus::zip64);
    }
    for (const size_t field : {end_directory_bytes, end_directory_offset}) {
        std::vector<uint8_t> patched = archive;
        put32(patched, end + field, UINT32_MAX);
        CHECK(directory_status(patched) == ZipStatus::zip64);
    }
    for (const size_t field : {central_compressed_bytes, central_uncompressed_bytes}) {
        std::vector<uint8_t> patched = archive;
        put32(patched, central + field, UINT32_MAX);
        CHECK(directory_status(patched, &error) == ZipStatus::zip64);
        CHECK(error.offset == central);
        CHECK(error.entry == "game.oascript");
    }
    {
        std::vector<uint8_t> patched = archive;
        put32(patched, central + central_local_header_offset, UINT32_MAX);
        CHECK(directory_status(patched) == ZipStatus::zip64);
        put16(patched, central + central_start_disk, UINT16_MAX);
        CHECK(directory_status(patched) == ZipStatus::zip64);
    }
    for (const size_t field : {local_compressed_bytes, local_uncompressed_bytes}) {
        std::vector<uint8_t> patched = archive;
        put32(patched, field, UINT32_MAX);
        CHECK(directory_status(patched, &error) == ZipStatus::zip64);
        CHECK(error.offset == 0);
    }
    {
        std::vector<uint8_t> patched = archive;
        put16(patched, central + central_start_disk, 1);
        CHECK(directory_status(patched) == ZipStatus::several_disks);
    }

    // Too many entries, counted before any record is read.
    {
        std::vector<uint8_t> patched = archive;
        put16(patched, end + end_disk_entry_count, max_entry_count + 1);
        put16(patched, end + end_entry_count, max_entry_count + 1);
        CHECK(directory_status(patched) == ZipStatus::too_many_entries);
    }

    // Records that run past the directory or the archive.
    {
        std::vector<uint8_t> patched = archive;
        put32(patched, end + end_directory_offset, static_cast<uint32_t>(central + 1));
        CHECK(directory_status(patched) == ZipStatus::truncated);
        patched = archive;
        put32(patched, end + end_directory_bytes, static_cast<uint32_t>(end - central + 1));
        CHECK(directory_status(patched) == ZipStatus::truncated);
        patched = archive;
        put32(patched, central + central_local_header_offset, static_cast<uint32_t>(central));
        CHECK(directory_status(patched, &error) == ZipStatus::bad_local_record);
        CHECK(error.offset == central);
        put32(patched, central + central_local_header_offset, static_cast<uint32_t>(end));
        CHECK(directory_status(patched) == ZipStatus::truncated);
        put32(patched, central + central_local_header_offset, static_cast<uint32_t>(end + 10));
        CHECK(directory_status(patched) == ZipStatus::truncated);
        // More entries counted than records in the directory.
        patched = archive;
        put16(patched, end + end_disk_entry_count, 4);
        put16(patched, end + end_entry_count, 4);
        CHECK(directory_status(patched) == ZipStatus::truncated);
        // Fewer: the directory holds bytes no record accounts for.
        put16(patched, end + end_disk_entry_count, 2);
        put16(patched, end + end_entry_count, 2);
        CHECK(directory_status(patched) == ZipStatus::bad_central_record);
    }

    // Signatures.
    {
        std::vector<uint8_t> patched = archive;
        patched[central + 3] = 3;
        CHECK(directory_status(patched, &error) == ZipStatus::bad_central_record);
        CHECK(error.offset == central);
        patched = archive;
        patched[2] = 1;
        CHECK(directory_status(patched, &error) == ZipStatus::bad_local_record);
        CHECK(error.offset == 0);
        CHECK(error.entry == "game.oascript");
    }

    // A local header unlike its central record.
    {
        std::vector<uint8_t> patched = archive;
        patched[local_name] = 'G';
        CHECK(directory_status(patched) == ZipStatus::bad_local_record);
        patched = archive;
        put16(patched, local_method, static_cast<uint16_t>(Method::deflated));
        CHECK(directory_status(patched) == ZipStatus::bad_local_record);
        patched = archive;
        put32(patched, local_crc32, 1);
        CHECK(directory_status(patched) == ZipStatus::bad_local_record);
        patched = archive;
        put32(patched, local_uncompressed_bytes, 35);
        CHECK(directory_status(patched) == ZipStatus::bad_local_record);
        // Zero sizes in the local header need the data descriptor flag.
        patched = archive;
        put32(patched, local_crc32, 0);
        put32(patched, local_compressed_bytes, 0);
        put32(patched, local_uncompressed_bytes, 0);
        CHECK(directory_status(patched) == ZipStatus::bad_local_record);
        put16(patched, local_flags, flag_data_descriptor);
        CHECK(read_everything(patched));
        // With the flag, a value that is neither zero nor the central one.
        put32(patched, local_compressed_bytes, 7);
        CHECK(directory_status(patched) == ZipStatus::bad_local_record);
    }

    // Encryption, in the central record or the local header alone.
    {
        std::vector<uint8_t> patched = archive;
        put16(patched, central + central_flags, flag_encrypted);
        put16(patched, local_flags, flag_encrypted);
        CHECK(directory_status(patched, &error) == ZipStatus::encrypted);
        CHECK(error.offset == central);
        patched = archive;
        put16(patched, local_flags, flag_encrypted);
        CHECK(directory_status(patched, &error) == ZipStatus::encrypted);
        CHECK(error.offset == 0);
    }

    // Methods other than stored and deflated.
    {
        std::vector<uint8_t> patched = archive;
        put16(patched, central + central_method, method_bzip2);
        put16(patched, local_method, method_bzip2);
        CHECK(directory_status(patched) == ZipStatus::unsupported_method);
        Entry entry{};
        entry.method = static_cast<Method>(method_bzip2);
        std::vector<uint8_t> bytes{};
        CHECK(!read_entry(archive, entry, bytes, error));
        CHECK(error.status == ZipStatus::unsupported_method);
    }

    // Names: unsafe, too long, repeated.
    const std::vector<uint8_t> data = bytes_of("data");
    for (const std::string_view name : {"../evil", "/etc/passwd", "C:/x", "a\\b", "a//b", "."}) {
        CHECK(
            directory_status(build_archive({stored_entry(std::string{name}, data)}), &error) ==
            ZipStatus::unsafe_name
        );
        CHECK(error.entry == name);
    }
    CHECK(
        directory_status(build_archive({stored_entry(std::string(max_name_bytes, 'n'), data)})) ==
        ZipStatus::ok
    );
    CHECK(
        directory_status(
            build_archive({stored_entry(std::string(max_name_bytes + 1, 'n'), data)})
        ) == ZipStatus::name_too_long
    );
    CHECK(
        directory_status(
            build_archive(
                {stored_entry("a", data), stored_entry("b", data), stored_entry("a", data)}
            ),
            &error
        ) == ZipStatus::duplicate_name
    );
    CHECK(error.entry == "a");

    // Sizes.
    {
        RawEntry entry = stored_entry("big", data);
        entry.bytes = static_cast<uint32_t>(max_entry_bytes + 1);
        CHECK(directory_status(build_archive({entry})) == ZipStatus::entry_too_large);
        // A stored entry whose two sizes differ.
        entry = stored_entry("odd", data);
        entry.bytes = 3;
        CHECK(directory_status(build_archive({entry})) == ZipStatus::size_mismatch);
        // A directory that holds data.
        CHECK(
            directory_status(build_archive({stored_entry("dir/", data)})) ==
            ZipStatus::size_mismatch
        );
        // More than any deflate stream of this length can hold: refused
        // before a buffer of that size is allocated.
        entry = deflated_entry("bomb", data);
        entry.bytes = static_cast<uint32_t>(max_entry_bytes);
        CHECK(directory_status(build_archive({entry})) == ZipStatus::size_mismatch);
    }
}

void test_entry_data() {
    const std::vector<uint8_t> text = bytes_of(std::string(2000, 'y') + "tail\n");
    ZipError error{};

    // A CRC-32 recorded wrong in both records.
    {
        RawEntry entry = stored_entry("wrong-crc", text);
        entry.crc32 ^= 1;
        CHECK(first_entry_status(build_archive({entry}), &error) == ZipStatus::crc_mismatch);
        CHECK(error.entry == "wrong-crc");
        CHECK(error.offset == 0);
        entry = deflated_entry("wrong-crc", text);
        entry.crc32 ^= 1;
        CHECK(first_entry_status(build_archive({entry})) == ZipStatus::crc_mismatch);
    }

    // Deflated data that is not deflate, that stops early, or that holds
    // more or less than recorded.
    {
        RawEntry entry = deflated_entry("garbage", text);
        std::fill(entry.data.begin(), entry.data.end(), uint8_t{0xff});
        CHECK(first_entry_status(build_archive({entry})) == ZipStatus::inflate_failed);

        entry = deflated_entry("cut", text);
        entry.data.resize(entry.data.size() / 2);
        CHECK(first_entry_status(build_archive({entry})) == ZipStatus::inflate_failed);

        entry = deflated_entry("fewer", text);
        entry.bytes -= 1;
        CHECK(first_entry_status(build_archive({entry})) == ZipStatus::size_mismatch);

        entry = deflated_entry("more", text);
        entry.bytes += 1;
        CHECK(first_entry_status(build_archive({entry})) == ZipStatus::size_mismatch);

        entry = deflated_entry("trailing", text);
        entry.data.push_back(0);
        CHECK(first_entry_status(build_archive({entry})) == ZipStatus::size_mismatch);

        // A deflated entry of no bytes whose stream holds one.
        entry = deflated_entry("empty", bytes_of("z"));
        entry.bytes = 0;
        entry.crc32 = 0;
        CHECK(first_entry_status(build_archive({entry})) == ZipStatus::size_mismatch);

#if OA_ZIP_TEST_OWN_ALLOCATOR
        // A deflated entry claiming more than its buffer can be given, with
        // data enough for the claim: refused, not fatal.
        entry = deflated_entry("claimed", bytes_of("z"));
        entry.bytes = static_cast<uint32_t>(refused_allocation_bytes + 1);
        entry.data.assign(entry.bytes / 1032 + 1, 0);
        CHECK(first_entry_status(build_archive({entry})) == ZipStatus::entry_too_large);
#endif
    }

    // An entry given to read_entry that the directory would not hold.
    {
        const std::vector<uint8_t> archive = build_archive({stored_entry("a", text)});
        CentralDirectory directory{};
        CHECK(read_directory(archive, directory, error));
        Entry entry = directory.entries.front();
        entry.bytes = static_cast<uint32_t>(max_entry_bytes + 1);
        std::vector<uint8_t> bytes{1, 2, 3};
        CHECK(!read_entry(archive, entry, bytes, error));
        CHECK(error.status == ZipStatus::entry_too_large);
        CHECK(bytes.empty());
        entry = directory.entries.front();
        entry.name = "b";
        CHECK(!read_entry(archive, entry, bytes, error));
        CHECK(error.status == ZipStatus::bad_local_record);
        entry = directory.entries.front();
        entry.local_header_offset = static_cast<uint32_t>(archive.size());
        CHECK(!read_entry(archive, entry, bytes, error));
        CHECK(error.status == ZipStatus::truncated);
        entry = directory.entries.front();
        CHECK(!read_entry(std::span{archive}.first(archive.size() / 2), entry, bytes, error));
        CHECK(error.status == ZipStatus::truncated);
    }
}

void test_writer_refusals() {
    const std::vector<uint8_t> data = bytes_of("data");
    std::vector<uint8_t> archive{1, 2, 3};
    ZipError error{};
    const auto refused = [&](std::span<const NewEntry> entries) {
        archive = {1, 2, 3};
        const bool wrote = write_archive(entries, archive, error);
        CHECK(!wrote);
        CHECK(archive.empty());
        return error.status;
    };
    const std::string long_name(max_name_bytes + 1, 'n');
    {
        const std::array<NewEntry, 1> entries{{{long_name, data}}};
        CHECK(refused(entries) == ZipStatus::name_too_long);
    }
    for (const std::string_view name : {"", "../x", "/x", "D:x", "a\\b", "x/./y"}) {
        const std::array<NewEntry, 1> entries{{{name, data}}};
        CHECK(refused(entries) == ZipStatus::unsafe_name);
        CHECK(error.entry == name);
    }
    {
        const std::array<NewEntry, 3> entries{{{"a", data}, {"b", data}, {"b", data}}};
        CHECK(refused(entries) == ZipStatus::duplicate_name);
        CHECK(error.entry == "b");
    }
    {
        const std::array<NewEntry, 1> entries{{{"dir/", data}}};
        CHECK(refused(entries) == ZipStatus::size_mismatch);
    }
    {
        std::vector<std::string> names{};
        for (size_t index = 0; index <= max_entry_count; ++index)
            names.push_back("entry" + std::to_string(index));
        std::vector<NewEntry> entries{};
        for (const std::string& name : names)
            entries.push_back({name, data});
        CHECK(refused(entries) == ZipStatus::too_many_entries);
        entries.pop_back();
        CHECK(write_archive(entries, archive, error));
        CentralDirectory directory{};
        CHECK(read_directory(archive, directory, error));
        CHECK(directory.entries.size() == max_entry_count);
    }

    // Sizes past the limits, checked before any byte of the data is read:
    // the buffer below is allocated and never touched.
    std::unique_ptr<uint8_t[]> big{};
    try {
        big.reset(new uint8_t[max_archive_bytes + 1]);
    } catch (const std::bad_alloc&) {
        std::printf("skipped the size limits: no room for %zu bytes\n", max_archive_bytes + 1);
        return;
    }
    const std::span<const uint8_t> over_archive{big.get(), max_archive_bytes + 1};
    const std::span<const uint8_t> over_entry{big.get(), static_cast<size_t>(max_entry_bytes + 1)};
    const std::span<const uint8_t> half{big.get(), max_archive_bytes / 2};
    {
        const std::array<NewEntry, 1> entries{{{"big", over_entry}}};
        CHECK(refused(entries) == ZipStatus::entry_too_large);
    }
    {
        const std::array<NewEntry, 2> entries{{{"half", half}, {"other half", half}}};
        CHECK(refused(entries) == ZipStatus::too_large);
        CHECK(error.entry == "other half");
    }
    CHECK(directory_status(over_archive) == ZipStatus::too_large);
    std::vector<uint8_t> bytes{};
    CHECK(!read_entry(over_archive, Entry{}, bytes, error));
    CHECK(error.status == ZipStatus::too_large);
}

void test_malformed_sweep() {
    const GoldenEntries golden{};
    const std::vector<uint8_t> stored = written(golden.entries);
    const std::vector<uint8_t> text = bytes_of(std::string(600, 'q') + "tail\n");
    const std::vector<uint8_t> deflated =
        build_archive({deflated_entry("a.txt", text), stored_entry("b/", {})});
    for (const std::vector<uint8_t>* archive : {&stored, &deflated}) {
        CHECK(read_everything(*archive));
        // Every prefix, and every suffix, lacks an end record that fits.
        for (size_t size = 0; size < archive->size(); ++size) {
            CHECK(directory_status(std::span{*archive}.first(size)) != ZipStatus::ok);
            CHECK(directory_status(std::span{*archive}.subspan(size + 1)) != ZipStatus::ok);
        }
    }
    // Random bytes changed under a fixed seed: whatever is read must be
    // refused or read whole, never read past the end.
    const std::array<std::span<const uint8_t>, 5> sources{
        stored, deflated, test::tool_archive, test::descriptor_archive, test::zip64_archive
    };
    uint32_t seed = 12345;
    const auto next = [&seed] {
        seed = seed * 1103515245u + 12345u;
        return seed >> 8;
    };
    for (const std::span<const uint8_t> source : sources) {
        for (int round = 0; round < 3000; ++round) {
            std::vector<uint8_t> mutated{source.begin(), source.end()};
            const uint32_t changes = 1 + next() % 4;
            for (uint32_t change = 0; change < changes; ++change)
                mutated[next() % mutated.size()] = static_cast<uint8_t>(next());
            (void)read_everything(mutated);
            (void)first_entry_status(mutated);
        }
    }
}

} // namespace

int main() {
    test_name_rule();
    test_status_messages();
    test_crc32();
    test_round_trip();
    test_writer_golden();
    test_deflated_archives();
    test_tool_archives();
    test_end_record();
    test_refusals();
    test_entry_data();
    test_writer_refusals();
    test_malformed_sweep();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("zip: all checks passed\n");
    return 0;
}
