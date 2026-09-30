// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The byte layout of the three zip records the reader and writer use: the
// local file header, the central directory record and the end record. Every
// field is little-endian.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace oa::formats::zip::detail {

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

/// Reads a little-endian 16-bit field.
///
/// @param bytes the bytes the field lies in
/// @param offset the field's offset in `bytes`; two bytes must follow it
/// @return the field's value
[[nodiscard]] inline uint16_t read_le16(std::span<const uint8_t> bytes, size_t offset) noexcept {
    return static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

/// Reads a little-endian 32-bit field.
///
/// @param bytes the bytes the field lies in
/// @param offset the field's offset in `bytes`; four bytes must follow it
/// @return the field's value
[[nodiscard]] inline uint32_t read_le32(std::span<const uint8_t> bytes, size_t offset) noexcept {
    return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

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
