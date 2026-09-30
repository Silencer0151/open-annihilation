// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The zip reader: finds the end record, walks the central directory, checks
// each local header against its central record and reads stored or deflated
// entries.

#include <algorithm>
#include <string>
#include <vector>

#include <zlib.h>

#include "oa/formats/zip.hpp"
#include "records.hpp"

namespace oa::formats::zip {

using namespace detail;

namespace {

/// The most bytes one byte of deflated data can inflate to: a 258-byte
/// match coded in two bits. Every deflate stream stays within this ratio,
/// so an entry that claims more is refused before its buffer is allocated.
constexpr uint64_t max_inflate_ratio = 1032;

static_assert(max_archive_bytes <= UINT32_MAX, "offsets are 32-bit fields");
static_assert(max_entry_bytes <= UINT32_MAX, "sizes are 32-bit fields");

/// Records a failure.
///
/// @param[out] error the error to fill
/// @param status why the read failed
/// @param offset the byte offset of the record at fault
/// @param entry the entry's name; empty for the archive itself
/// @return false, for the caller to return
bool fail(ZipError& error, ZipStatus status, uint64_t offset, std::string_view entry = {}) {
    error = ZipError{status, offset, std::string{entry}};
    return false;
}

/// Tells whether a method is one the reader takes.
///
/// @param method the recorded method
/// @return true for stored and deflated
bool method_supported(uint16_t method) noexcept {
    return method == static_cast<uint16_t>(Method::stored) ||
           method == static_cast<uint16_t>(Method::deflated);
}

/// Checks an entry's sizes against its method and the limits.
///
/// A stored entry's two sizes are equal, a directory holds nothing and a
/// deflated entry claims no more than its data can inflate to.
///
/// @param entry the entry
/// @param offset the offset of the record the sizes were read from
/// @param[out] error the status, and `offset`
/// @return true when the sizes are consistent
bool check_sizes(const Entry& entry, uint64_t offset, ZipError& error) {
    if (entry.bytes > max_entry_bytes)
        return fail(error, ZipStatus::entry_too_large, offset, entry.name);
    const bool consistent =
        entry.method == Method::stored
            ? entry.compressed_bytes == entry.bytes
            : uint64_t{entry.bytes} <= uint64_t{entry.compressed_bytes} * max_inflate_ratio;
    if (!consistent || (entry.directory && entry.bytes != 0))
        return fail(error, ZipStatus::size_mismatch, offset, entry.name);
    return true;
}

/// Tells whether a local header field agrees with its central record.
///
/// @param local the local header's value
/// @param central the central record's value
/// @param deferred the local header defers the value to a data descriptor,
///        so zero also agrees
/// @return true when the two agree
bool local_field_agrees(uint32_t local, uint32_t central, bool deferred) noexcept {
    return local == central || (deferred && local == 0);
}

/// Checks an entry's local header against the entry and finds its data.
///
/// @param archive the whole archive
/// @param entry the entry, as its central record gives it
/// @param[out] data_offset where the entry's data starts
/// @param[out] error the status, and the local header's offset
/// @return true when the local header agrees and the data lies in the archive
bool locate_data(
    std::span<const uint8_t> archive, const Entry& entry, size_t& data_offset, ZipError& error
) {
    const uint64_t header = entry.local_header_offset;
    if (header + local_bytes > archive.size())
        return fail(error, ZipStatus::truncated, header, entry.name);
    const std::span<const uint8_t> record =
        archive.subspan(static_cast<size_t>(header), local_bytes);
    if (!signature_at(record, 0, local_signature))
        return fail(error, ZipStatus::bad_local_record, header, entry.name);
    const uint16_t name_length = read_le16(record, local_name_length);
    const uint16_t extra_length = read_le16(record, local_extra_length);
    const uint64_t data = header + local_bytes + name_length + extra_length;
    if (data > archive.size())
        return fail(error, ZipStatus::truncated, header, entry.name);
    const std::span<const uint8_t> name_bytes =
        archive.subspan(static_cast<size_t>(header) + local_bytes, name_length);
    const std::string_view name{reinterpret_cast<const char*>(name_bytes.data()), name_length};
    if (name != entry.name)
        return fail(error, ZipStatus::bad_local_record, header, entry.name);
    const uint16_t flags = read_le16(record, local_flags);
    if ((flags & encryption_flags) != 0)
        return fail(error, ZipStatus::encrypted, header, entry.name);
    if (read_le16(record, local_method) != static_cast<uint16_t>(entry.method))
        return fail(error, ZipStatus::bad_local_record, header, entry.name);
    const uint32_t compressed_bytes = read_le32(record, local_compressed_bytes);
    const uint32_t bytes = read_le32(record, local_uncompressed_bytes);
    if (compressed_bytes == zip64_sentinel_32 || bytes == zip64_sentinel_32)
        return fail(error, ZipStatus::zip64, header, entry.name);
    const bool deferred = (flags & flag_data_descriptor) != 0;
    if (!local_field_agrees(read_le32(record, local_crc32), entry.crc32, deferred) ||
        !local_field_agrees(compressed_bytes, entry.compressed_bytes, deferred) ||
        !local_field_agrees(bytes, entry.bytes, deferred))
        return fail(error, ZipStatus::bad_local_record, header, entry.name);
    if (data + entry.compressed_bytes > archive.size())
        return fail(error, ZipStatus::truncated, header, entry.name);
    data_offset = static_cast<size_t>(data);
    return true;
}

/// Inflates raw deflated data into a buffer of exactly its recorded size.
///
/// @param input the deflated data, all of which the stream must use
/// @param[in,out] output sized to the recorded size; filled on success
/// @return ok; size_mismatch when the stream inflates to another size or
///         ends before the data does; inflate_failed when the data is not
///         deflate or ends before the stream does
ZipStatus inflate_exactly(std::span<const uint8_t> input, std::vector<uint8_t>& output) {
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return ZipStatus::inflate_failed;
    // zlib refuses a null output pointer even when there is no room to fill.
    uint8_t no_output{};
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = output.empty() ? &no_output : output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    const int result = inflate(&stream, Z_FINISH);
    const uLong produced = stream.total_out;
    const uLong consumed = stream.total_in;
    const uInt room_left = stream.avail_out;
    inflateEnd(&stream);
    if (result == Z_STREAM_END)
        return produced == output.size() && consumed == input.size() ? ZipStatus::ok
                                                                     : ZipStatus::size_mismatch;
    // The buffer is full and the stream has not ended: it holds more than
    // recorded. With room left, the data ended before the stream did.
    if (result == Z_BUF_ERROR && room_left == 0)
        return ZipStatus::size_mismatch;
    return ZipStatus::inflate_failed;
}

/// Finds the end record: the last signature in the final 65,557 bytes whose
/// comment reaches exactly to the end of the archive.
///
/// @param archive the whole archive
/// @param[out] end_offset where the end record starts
/// @return true when there is one
bool find_end_record(std::span<const uint8_t> archive, size_t& end_offset) noexcept {
    if (archive.size() < end_bytes)
        return false;
    const size_t last = archive.size() - end_bytes;
    const size_t first = last - std::min(last, max_comment_bytes);
    for (size_t offset = last + 1; offset-- > first;) {
        if (signature_at(archive, offset, end_signature) &&
            offset + end_bytes + read_le16(archive, offset + end_comment_length) ==
                archive.size()) {
            end_offset = offset;
            return true;
        }
    }
    return false;
}

/// Reads the central directory records into `directory`.
///
/// @param archive the whole archive
/// @param start where the directory starts
/// @param end where it ends, at or before the end record
/// @param entry_count the entries the end record counts
/// @param[out] directory the entries read
/// @param[out] error the status, and the record at fault
/// @return true when every record was read and checked
bool read_records(
    std::span<const uint8_t> archive,
    size_t start,
    size_t end,
    uint16_t entry_count,
    CentralDirectory& directory,
    ZipError& error
) {
    std::vector<std::string_view> names{};
    names.reserve(entry_count);
    directory.entries.reserve(entry_count);
    size_t offset = start;
    for (uint16_t index = 0; index < entry_count; ++index) {
        if (end - offset < central_bytes)
            return fail(error, ZipStatus::truncated, offset);
        const std::span<const uint8_t> record = archive.subspan(offset, central_bytes);
        if (!signature_at(record, 0, central_signature))
            return fail(error, ZipStatus::bad_central_record, offset);
        const size_t name_length = read_le16(record, central_name_length);
        const size_t tail_length = name_length + read_le16(record, central_extra_length) +
                                   read_le16(record, central_comment_length);
        if (end - offset - central_bytes < tail_length)
            return fail(error, ZipStatus::truncated, offset);
        const std::span<const uint8_t> name_bytes =
            archive.subspan(offset + central_bytes, name_length);
        const std::string_view name{
            reinterpret_cast<const char*>(name_bytes.data()), name_bytes.size()
        };
        const uint16_t flags = read_le16(record, central_flags);
        const uint16_t method = read_le16(record, central_method);
        const uint16_t start_disk = read_le16(record, central_start_disk);
        Entry entry{};
        entry.name = std::string{name};
        entry.crc32 = read_le32(record, central_crc32);
        entry.compressed_bytes = read_le32(record, central_compressed_bytes);
        entry.bytes = read_le32(record, central_uncompressed_bytes);
        entry.local_header_offset = read_le32(record, central_local_header_offset);
        entry.directory = !name.empty() && name.back() == '/';
        if (entry.compressed_bytes == zip64_sentinel_32 || entry.bytes == zip64_sentinel_32 ||
            entry.local_header_offset == zip64_sentinel_32 || start_disk == zip64_sentinel_16)
            return fail(error, ZipStatus::zip64, offset, name);
        if (start_disk != 0)
            return fail(error, ZipStatus::several_disks, offset, name);
        if ((flags & encryption_flags) != 0)
            return fail(error, ZipStatus::encrypted, offset, name);
        if (!method_supported(method))
            return fail(error, ZipStatus::unsupported_method, offset, name);
        entry.method = static_cast<Method>(method);
        if (name_length > max_name_bytes)
            return fail(error, ZipStatus::name_too_long, offset, name);
        if (!name_is_safe(name))
            return fail(error, ZipStatus::unsafe_name, offset, name);
        if (std::find(names.begin(), names.end(), name) != names.end())
            return fail(error, ZipStatus::duplicate_name, offset, name);
        names.push_back(name);
        size_t data_offset{};
        if (!check_sizes(entry, offset, error) || !locate_data(archive, entry, data_offset, error))
            return false;
        directory.entries.push_back(std::move(entry));
        offset += central_bytes + tail_length;
    }
    if (offset != end)
        return fail(error, ZipStatus::bad_central_record, offset);
    return true;
}

} // namespace

bool read_directory(
    std::span<const uint8_t> archive, CentralDirectory& directory, ZipError& error
) {
    error = ZipError{};
    directory.entries.clear();
    if (archive.size() > max_archive_bytes)
        return fail(error, ZipStatus::too_large, 0);
    size_t end_offset{};
    if (!find_end_record(archive, end_offset))
        return fail(error, ZipStatus::no_end_record, 0);
    const std::span<const uint8_t> end = archive.subspan(end_offset, end_bytes);
    const uint16_t disk = read_le16(end, end_disk);
    const uint16_t directory_disk = read_le16(end, end_directory_disk);
    const uint16_t disk_entry_count = read_le16(end, end_disk_entry_count);
    const uint16_t entry_count = read_le16(end, end_entry_count);
    const uint32_t directory_bytes = read_le32(end, end_directory_bytes);
    const uint32_t directory_offset = read_le32(end, end_directory_offset);
    if (disk == zip64_sentinel_16 || directory_disk == zip64_sentinel_16 ||
        disk_entry_count == zip64_sentinel_16 || entry_count == zip64_sentinel_16 ||
        directory_bytes == zip64_sentinel_32 || directory_offset == zip64_sentinel_32)
        return fail(error, ZipStatus::zip64, end_offset);
    if (disk != 0 || directory_disk != 0 || disk_entry_count != entry_count)
        return fail(error, ZipStatus::several_disks, end_offset);
    if (entry_count > max_entry_count)
        return fail(error, ZipStatus::too_many_entries, end_offset);
    if (uint64_t{directory_offset} + directory_bytes > end_offset)
        return fail(error, ZipStatus::truncated, end_offset);
    if (!read_records(
            archive,
            directory_offset,
            size_t{directory_offset} + directory_bytes,
            entry_count,
            directory,
            error
        )) {
        directory.entries.clear();
        return false;
    }
    return true;
}

bool read_entry(
    std::span<const uint8_t> archive,
    const Entry& entry,
    std::vector<uint8_t>& bytes,
    ZipError& error
) {
    error = ZipError{};
    bytes.clear();
    if (archive.size() > max_archive_bytes)
        return fail(error, ZipStatus::too_large, 0);
    if (!method_supported(static_cast<uint16_t>(entry.method)))
        return fail(error, ZipStatus::unsupported_method, entry.local_header_offset, entry.name);
    size_t data_offset{};
    if (!check_sizes(entry, entry.local_header_offset, error) ||
        !locate_data(archive, entry, data_offset, error))
        return false;
    const std::span<const uint8_t> data = archive.subspan(data_offset, entry.compressed_bytes);
    std::vector<uint8_t> read(entry.bytes);
    if (entry.method == Method::stored) {
        std::copy(data.begin(), data.end(), read.begin());
    } else {
        const ZipStatus status = inflate_exactly(data, read);
        if (status != ZipStatus::ok)
            return fail(error, status, entry.local_header_offset, entry.name);
    }
    if (crc32_of(read) != entry.crc32)
        return fail(error, ZipStatus::crc_mismatch, entry.local_header_offset, entry.name);
    bytes = std::move(read);
    return true;
}

} // namespace oa::formats::zip
