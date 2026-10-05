// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Zip archives built field by field for tests, as writers other than the
// engine's make them: any name bytes, flag, method, host system and file
// attributes, extra fields, and the 64-bit extension's extra fields and end
// records. The streamed zip reader's test and the mod packages' tests build
// their archives with it. Link ZLIB::ZLIB.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

namespace oa::test::raw_zip {

/// One entry of a raw archive.
struct RawFile {
    std::string name{};             ///< the name's bytes as recorded
    std::vector<uint8_t> data{};    ///< the data as stored, compressed or not
    uint16_t method{};              ///< 0 stored, 8 deflated, or any other
    uint16_t flags{};               ///< the general purpose flags
    uint8_t host{};                 ///< "version made by"'s system: 0 MS-DOS, 3 Unix
    uint32_t external_attributes{}; ///< a Unix mode in the high 16 bits for host 3
    uint32_t crc32{};               ///< recorded CRC-32
    uint64_t bytes{};               ///< recorded uncompressed size
    /// The sizes and the local header's offset go in the 64-bit extra field,
    /// with sentinels in their 32-bit fields, in both records.
    bool zip64{};
    std::vector<uint8_t> central_extra{}; ///< more extra fields of the central record
    std::vector<uint8_t> local_extra{};   ///< more extra fields of the local header
};

/// A built archive and where its records lie.
struct RawArchive {
    std::vector<uint8_t> bytes{};            ///< from base_offset on
    std::vector<uint64_t> local_offsets{};   ///< each file's local header, from the archive's start
    std::vector<uint64_t> central_offsets{}; ///< each file's central record
    uint64_t directory_offset{};
    uint64_t end_offset{}; ///< the end record
};

/// How the archive is laid out.
struct ArchiveShape {
    /// Write the 64-bit end record and its locator, and sentinels in the
    /// end record.
    bool zip64_end{};
    /// Where `bytes` starts in the archive: what comes before it is left to
    /// the caller, as zeros of a sparse source.
    uint64_t base_offset{};
};

/// Appends a little-endian value of `size` bytes.
///
/// @param[in,out] out the bytes
/// @param value the value
/// @param size its width in bytes
inline void append_le(std::vector<uint8_t>& out, uint64_t value, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index)
        out.push_back(static_cast<uint8_t>(value >> (8 * index)));
}

/// Writes a little-endian value of `size` bytes over bytes already there.
///
/// @param[in,out] out the bytes
/// @param offset where the value goes
/// @param value the value
/// @param size its width in bytes
inline void
put_le(std::vector<uint8_t>& out, std::size_t offset, uint64_t value, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index)
        out[offset + index] = static_cast<uint8_t>(value >> (8 * index));
}

/// Deflates bytes as a raw stream, with no zlib header or trailer.
///
/// @param bytes the data
/// @return the deflated stream
inline std::vector<uint8_t> deflate_raw(std::span<const uint8_t> bytes) {
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

/// Returns the CRC-32 of bytes.
///
/// @param bytes the bytes
/// @return the CRC-32
inline uint32_t raw_crc32(std::span<const uint8_t> bytes) {
    return static_cast<uint32_t>(
        crc32(crc32(0L, Z_NULL, 0), bytes.data(), static_cast<uInt>(bytes.size()))
    );
}

/// Returns a file that stores bytes.
///
/// @param name the name
/// @param bytes the data
/// @return the file
inline RawFile stored_file(std::string name, std::span<const uint8_t> bytes) {
    RawFile file{};
    file.name = std::move(name);
    file.data.assign(bytes.begin(), bytes.end());
    file.crc32 = raw_crc32(bytes);
    file.bytes = bytes.size();
    return file;
}

/// Returns a file that stores a text.
///
/// @param name the name
/// @param text the data
/// @return the file
inline RawFile stored_text(std::string name, std::string_view text) {
    return stored_file(
        std::move(name),
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size())
    );
}

/// Returns a file that deflates bytes.
///
/// @param name the name
/// @param bytes the data
/// @return the file
inline RawFile deflated_file(std::string name, std::span<const uint8_t> bytes) {
    RawFile file = stored_file(std::move(name), bytes);
    file.method = 8;
    file.data = deflate_raw(bytes);
    return file;
}

/// Builds an archive: each file's local header and data, the central
/// directory, then, when asked, the 64-bit end record and locator, and the
/// end record.
///
/// @param files the files, in order
/// @param shape how the archive is laid out
/// @return the archive
inline RawArchive
build_raw_archive(const std::vector<RawFile>& files, const ArchiveShape& shape = {}) {
    constexpr uint16_t version = 20;
    constexpr uint16_t version_zip64 = 45;
    constexpr uint32_t sentinel_32 = 0xFFFFFFFFU;
    RawArchive archive{};
    std::vector<uint8_t>& out = archive.bytes;
    const auto here = [&] { return shape.base_offset + out.size(); };
    for (const RawFile& file : files) {
        archive.local_offsets.push_back(here());
        std::vector<uint8_t> extra{};
        if (file.zip64) {
            append_le(extra, 1, 2);
            append_le(extra, 16, 2);
            append_le(extra, file.bytes, 8);
            append_le(extra, file.data.size(), 8);
        }
        extra.insert(extra.end(), file.local_extra.begin(), file.local_extra.end());
        out.insert(out.end(), {'P', 'K', 3, 4});
        append_le(out, file.zip64 ? version_zip64 : version, 2);
        append_le(out, file.flags, 2);
        append_le(out, file.method, 2);
        append_le(out, 0, 4);
        append_le(out, file.crc32, 4);
        append_le(out, file.zip64 ? sentinel_32 : file.data.size(), 4);
        append_le(out, file.zip64 ? sentinel_32 : file.bytes, 4);
        append_le(out, file.name.size(), 2);
        append_le(out, extra.size(), 2);
        out.insert(out.end(), file.name.begin(), file.name.end());
        out.insert(out.end(), extra.begin(), extra.end());
        out.insert(out.end(), file.data.begin(), file.data.end());
    }
    archive.directory_offset = here();
    for (std::size_t index = 0; index < files.size(); ++index) {
        const RawFile& file = files[index];
        archive.central_offsets.push_back(here());
        std::vector<uint8_t> extra{};
        if (file.zip64) {
            append_le(extra, 1, 2);
            append_le(extra, 24, 2);
            append_le(extra, file.bytes, 8);
            append_le(extra, file.data.size(), 8);
            append_le(extra, archive.local_offsets[index], 8);
        }
        extra.insert(extra.end(), file.central_extra.begin(), file.central_extra.end());
        out.insert(out.end(), {'P', 'K', 1, 2});
        append_le(out, (uint32_t{file.host} << 8) | (file.zip64 ? version_zip64 : version), 2);
        append_le(out, file.zip64 ? version_zip64 : version, 2);
        append_le(out, file.flags, 2);
        append_le(out, file.method, 2);
        append_le(out, 0, 4);
        append_le(out, file.crc32, 4);
        append_le(out, file.zip64 ? sentinel_32 : file.data.size(), 4);
        append_le(out, file.zip64 ? sentinel_32 : file.bytes, 4);
        append_le(out, file.name.size(), 2);
        append_le(out, extra.size(), 2);
        append_le(out, 0, 2);
        append_le(out, 0, 2);
        append_le(out, 0, 2);
        append_le(out, file.external_attributes, 4);
        append_le(out, file.zip64 ? sentinel_32 : archive.local_offsets[index], 4);
        out.insert(out.end(), file.name.begin(), file.name.end());
        out.insert(out.end(), extra.begin(), extra.end());
    }
    const uint64_t directory_bytes = here() - archive.directory_offset;
    if (shape.zip64_end) {
        const uint64_t record = here();
        out.insert(out.end(), {'P', 'K', 6, 6});
        append_le(out, 44, 8);
        append_le(out, version_zip64, 2);
        append_le(out, version_zip64, 2);
        append_le(out, 0, 4);
        append_le(out, 0, 4);
        append_le(out, files.size(), 8);
        append_le(out, files.size(), 8);
        append_le(out, directory_bytes, 8);
        append_le(out, archive.directory_offset, 8);
        out.insert(out.end(), {'P', 'K', 6, 7});
        append_le(out, 0, 4);
        append_le(out, record, 8);
        append_le(out, 1, 4);
    }
    archive.end_offset = here();
    out.insert(out.end(), {'P', 'K', 5, 6});
    append_le(out, 0, 2);
    append_le(out, 0, 2);
    append_le(out, shape.zip64_end ? 0xFFFFU : files.size(), 2);
    append_le(out, shape.zip64_end ? 0xFFFFU : files.size(), 2);
    append_le(out, shape.zip64_end ? sentinel_32 : directory_bytes, 4);
    append_le(out, shape.zip64_end ? sentinel_32 : archive.directory_offset, 4);
    append_le(out, 0, 2);
    return archive;
}

} // namespace oa::test::raw_zip
