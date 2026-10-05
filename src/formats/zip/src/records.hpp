// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The byte layout of the three zip records the reader and writer use: the
// local file header, the central directory record and the end record. Every
// field is little-endian.
#pragma once

#include "oa/base/bytes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace oa::formats::zip::detail {

using base::bytes::load_le16;
using base::bytes::load_le32;

/// Reads a 64-bit little-endian value; the caller guarantees eight bytes.
///
/// @param bytes the first of eight bytes
/// @return the value
[[nodiscard]] constexpr uint64_t load_le64(const uint8_t* bytes) noexcept {
    return uint64_t{load_le32(bytes)} | (uint64_t{load_le32(bytes + 4)} << 32U);
}

/// The four bytes that open a record: 'P', 'K' and two record bytes.
using Signature = std::array<uint8_t, 4>;

/// Opens a local file header.
inline constexpr Signature local_signature{'P', 'K', 3, 4};
/// Opens a central directory record.
inline constexpr Signature central_signature{'P', 'K', 1, 2};
/// Opens the end-of-central-directory record.
inline constexpr Signature end_signature{'P', 'K', 5, 6};

// Local file header: signature, version needed, flags, method, time, date,
// CRC-32, compressed size, size, name length, extra length, then the name
// and the extra field.
inline constexpr size_t local_bytes = 30;
inline constexpr size_t local_version_needed = 4;
inline constexpr size_t local_flags = 6;
inline constexpr size_t local_method = 8;
inline constexpr size_t local_time = 10;
inline constexpr size_t local_date = 12;
inline constexpr size_t local_crc32 = 14;
inline constexpr size_t local_compressed_bytes = 18;
inline constexpr size_t local_uncompressed_bytes = 22;
inline constexpr size_t local_name_length = 26;
inline constexpr size_t local_extra_length = 28;

// Central directory record: signature, version made by, version needed,
// flags, method, time, date, CRC-32, compressed size, size, name, extra and
// comment lengths, starting disk, internal and external attributes and the
// local header's offset, then the name, the extra field and the comment.
inline constexpr size_t central_bytes = 46;
inline constexpr size_t central_version_made_by = 4;
inline constexpr size_t central_version_needed = 6;
inline constexpr size_t central_flags = 8;
inline constexpr size_t central_method = 10;
inline constexpr size_t central_time = 12;
inline constexpr size_t central_date = 14;
inline constexpr size_t central_crc32 = 16;
inline constexpr size_t central_compressed_bytes = 20;
inline constexpr size_t central_uncompressed_bytes = 24;
inline constexpr size_t central_name_length = 28;
inline constexpr size_t central_extra_length = 30;
inline constexpr size_t central_comment_length = 32;
inline constexpr size_t central_start_disk = 34;
inline constexpr size_t central_internal_attributes = 36;
inline constexpr size_t central_external_attributes = 38;
inline constexpr size_t central_local_header_offset = 42;

// End record: signature, this disk's number, the disk the directory starts
// on, the entries on this disk, all entries, the directory's size and
// offset, and the comment length, then the comment.
inline constexpr size_t end_bytes = 22;
inline constexpr size_t end_disk = 4;
inline constexpr size_t end_directory_disk = 6;
inline constexpr size_t end_disk_entry_count = 8;
inline constexpr size_t end_entry_count = 10;
inline constexpr size_t end_directory_bytes = 12;
inline constexpr size_t end_directory_offset = 16;
inline constexpr size_t end_comment_length = 20;
/// The longest end record comment.
inline constexpr size_t max_comment_bytes = 65535;

// The 64-bit extension's end-of-central-directory locator: signature, the
// disk the 64-bit end record is on, that record's offset, and the number of
// disks. It lies just before the end record.
inline constexpr Signature zip64_locator_signature{'P', 'K', 6, 7};
inline constexpr size_t zip64_locator_bytes = 20;
inline constexpr size_t zip64_locator_disk = 4;
inline constexpr size_t zip64_locator_end_offset = 8;
inline constexpr size_t zip64_locator_disk_count = 16;

// The 64-bit end record: signature, the size of what follows that field,
// version made by and needed, this disk's number, the disk the directory
// starts on, the entries on this disk, all entries, the directory's size
// and offset, then an extensible part.
inline constexpr Signature zip64_end_signature{'P', 'K', 6, 6};
inline constexpr size_t zip64_end_bytes = 56;
inline constexpr size_t zip64_end_disk = 16;
inline constexpr size_t zip64_end_directory_disk = 20;
inline constexpr size_t zip64_end_disk_entry_count = 24;
inline constexpr size_t zip64_end_entry_count = 32;
inline constexpr size_t zip64_end_directory_bytes = 40;
inline constexpr size_t zip64_end_directory_offset = 48;

/// The extra field that holds an entry's 64-bit sizes, offset and disk.
inline constexpr uint16_t zip64_extra_id = 0x0001;
/// An extra field's header: its id and the length of its data.
inline constexpr size_t extra_header_bytes = 4;

// The system an entry was made on: the high byte of "version made by".
inline constexpr uint8_t host_ms_dos = 0;
inline constexpr uint8_t host_unix = 3;
inline constexpr uint8_t host_windows_ntfs = 10;
inline constexpr uint8_t host_vfat = 14;
inline constexpr uint8_t host_macos = 19;

// A Unix file type, in the high 16 bits of a Unix-made entry's external
// attributes.
inline constexpr uint32_t unix_type_mask = 0170000;
inline constexpr uint32_t unix_type_link = 0120000;
inline constexpr uint32_t unix_type_regular = 0100000;
inline constexpr uint32_t unix_type_directory = 0040000;

/// A 16-bit field that holds this value is found in the 64-bit extension.
inline constexpr uint16_t zip64_sentinel_16 = UINT16_MAX;
/// A 32-bit field that holds this value is found in the 64-bit extension.
inline constexpr uint32_t zip64_sentinel_32 = UINT32_MAX;

/// General purpose flag: the entry is encrypted.
inline constexpr uint16_t flag_encrypted = 1u << 0;
/// General purpose flag: the CRC-32 and sizes follow the data, and the local
/// header may hold zero for them.
inline constexpr uint16_t flag_data_descriptor = 1u << 3;
/// General purpose flag: the entry uses strong encryption.
inline constexpr uint16_t flag_strong_encryption = 1u << 6;
/// General purpose flag: the name and comment are UTF-8.
inline constexpr uint16_t flag_utf8_name = 1u << 11;
/// General purpose flag: the central directory is encrypted.
inline constexpr uint16_t flag_encrypted_directory = 1u << 13;
/// Every flag that marks some form of encryption.
inline constexpr uint16_t encryption_flags =
    flag_encrypted | flag_strong_encryption | flag_encrypted_directory;

/// Tells whether a record signature starts at an offset.
///
/// @param bytes the bytes to look in
/// @param offset where the signature would start; four bytes must follow it
/// @param signature the signature
/// @return true when the four bytes are the signature
[[nodiscard]] inline bool
signature_at(std::span<const uint8_t> bytes, size_t offset, const Signature& signature) noexcept {
    return bytes[offset] == signature[0] && bytes[offset + 1] == signature[1] &&
           bytes[offset + 2] == signature[2] && bytes[offset + 3] == signature[3];
}

} // namespace oa::formats::zip::detail
