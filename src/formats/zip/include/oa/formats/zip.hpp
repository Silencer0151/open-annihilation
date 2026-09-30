// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Zip archives as the PKWARE application note describes them: the reader a
// director bundle (.oamovie) is opened with, in memory, and the writer that
// makes one.
//
// The reader takes one archive on one disk, without the 64-bit extension,
// whose entries are stored (method 0) or deflated (method 8). It walks the
// central directory from the end record, checks every entry's local header
// against its central record, and checks each entry's CRC-32 as it reads
// it. It refuses, by name and with the byte offset of the record at fault:
// encrypted entries, other methods, unsafe names (see name_is_safe),
// duplicate names, sizes past the limits below and records that run past
// the end. Entries that only name a directory (a name ending in '/') are
// listed and hold nothing.
//
// The writer stores its entries uncompressed, with a fixed time (1980-01-01
// 00:00) and fixed attributes, so the same entries always give the same
// bytes on every platform.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::formats::zip {

/// The largest archive read or written, in bytes.
inline constexpr size_t max_archive_bytes = size_t{1} << 30;
/// The most entries an archive holds.
inline constexpr size_t max_entry_count = 256;
/// The longest entry name, in bytes.
inline constexpr size_t max_name_bytes = 512;
/// The largest entry, uncompressed, in bytes.
inline constexpr uint64_t max_entry_bytes = uint64_t{1} << 30;

/// Why an archive or entry could not be read or written.
enum class ZipStatus : uint8_t {
    ok,
    too_large,          ///< the archive is larger than max_archive_bytes
    no_end_record,      ///< no end-of-central-directory record in the last 65,557 bytes
    truncated,          ///< a record or an entry's data runs past the end
    several_disks,      ///< the archive spans more than one disk
    too_many_entries,   ///< more than max_entry_count entries
    bad_central_record, ///< a central directory record without its signature
    bad_local_record,   ///< a local header without its signature, or unlike its central record
    name_too_long,      ///< a name longer than max_name_bytes
    unsafe_name,        ///< a name name_is_safe refuses
    duplicate_name,     ///< two entries with one name
    encrypted,          ///< an encrypted entry
    unsupported_method, ///< a compression method other than stored or deflated
    zip64,              ///< a size or offset that needs the 64-bit extension
    entry_too_large,    ///< an entry larger than max_entry_bytes
    inflate_failed,     ///< deflated data that does not inflate
    size_mismatch,      ///< an entry whose data is not its recorded size
    crc_mismatch,       ///< an entry whose CRC-32 is not its recorded one
};

/// What went wrong, where, and in which entry.
struct ZipError {
    ZipStatus status{ZipStatus::ok};
    uint64_t offset{};   ///< byte offset of the record at fault
    std::string entry{}; ///< the entry's name; empty for the archive itself
};

/// How an entry's data is stored.
enum class Method : uint16_t {
    stored = 0,
    deflated = 8,
};

/// One entry of the central directory.
struct Entry {
    std::string name{}; ///< as recorded, '/' separating directories
    Method method{Method::stored};
    uint32_t crc32{}; ///< of the uncompressed data
    uint32_t compressed_bytes{};
    uint32_t bytes{}; ///< uncompressed
    uint32_t local_header_offset{};
    bool directory{}; ///< the name ends in '/'
};

/// The entries of an archive, in central directory order.
struct CentralDirectory {
    std::vector<Entry> entries{};
};

/// Returns a short English description of a status.
///
/// @param status the status
/// @return a static string
[[nodiscard]] const char* zip_status_message(ZipStatus status) noexcept;

/// Tells whether an entry name is safe to read.
///
/// A safe name is not empty, holds no NUL and no backslash, does not start
/// with '/' or a drive letter and colon, and has no empty, "." or ".."
/// component between its '/' separators, a trailing '/' excepted.
///
/// @param name the entry name
/// @return true when the name is safe
[[nodiscard]] bool name_is_safe(std::string_view name) noexcept;

/// Reads an archive's central directory.
///
/// @param archive the whole archive, at most max_archive_bytes
/// @param[out] directory its entries; left empty on failure
/// @param[out] error the status, and the record at fault
/// @return true when every entry was read
[[nodiscard]] bool
read_directory(std::span<const uint8_t> archive, CentralDirectory& directory, ZipError& error);

/// Returns the entry with a name.
///
/// @param directory the entries
/// @param name the name, compared byte for byte
/// @return the entry, or null when there is none
[[nodiscard]] const Entry*
find_entry(const CentralDirectory& directory, std::string_view name) noexcept;

/// Reads one entry's data.
///
/// Checks the local header against the entry, inflates deflated data and
/// checks the size and CRC-32.
///
/// @param archive the whole archive `entry` was read from
/// @param entry the entry
/// @param[out] bytes the uncompressed data; left empty on failure
/// @param[out] error the status, and the record at fault
/// @return true when the data was read and checked
[[nodiscard]] bool read_entry(
    std::span<const uint8_t> archive,
    const Entry& entry,
    std::vector<uint8_t>& bytes,
    ZipError& error
);

/// An entry the writer stores.
struct NewEntry {
    std::string_view name{};          ///< a name name_is_safe accepts
    std::span<const uint8_t> bytes{}; ///< the entry's data
};

/// Writes an archive of stored entries, in the order given.
///
/// @param entries the entries; at most max_entry_count, each at most
///        max_entry_bytes, with unique safe names
/// @param[out] archive the archive's bytes; left empty on failure
/// @param[out] error the status, and the entry at fault
/// @return true when the archive was written
[[nodiscard]] bool
write_archive(std::span<const NewEntry> entries, std::vector<uint8_t>& archive, ZipError& error);

/// Computes the CRC-32 of the zip format (the reflected polynomial of
/// ISO 3309, starting from all ones and inverted at the end).
///
/// @param bytes the data
/// @return its CRC-32
[[nodiscard]] uint32_t crc32_of(std::span<const uint8_t> bytes) noexcept;

} // namespace oa::formats::zip
