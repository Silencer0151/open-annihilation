// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The zip writer: stored entries with a fixed time and fixed attributes, the
// central directory and the end record, so the same entries always give the
// same bytes.

#include <algorithm>
#include <string>
#include <vector>

#include "oa/formats/zip.hpp"
#include "records.hpp"

namespace oa::formats::zip {

using namespace detail;

namespace {

/// Version 2.0 of the format, which stored and deflated entries and
/// directories need; also the version the writer declares it made them with.
constexpr uint16_t written_version = 20;
/// The time field of every entry: 00:00:00.
constexpr uint16_t written_time = 0;
/// The date field of every entry: 1980-01-01 (day 1, month 1, year 0 of
/// the format's 1980-based years, packed as day | month << 5 | year << 9).
constexpr uint16_t written_date = 1 | (1 << 5);
/// The highest byte value of 7-bit ASCII.
constexpr uint8_t last_ascii_byte = 127;

/// Appends a little-endian 16-bit field.
///
/// @param[in,out] out the bytes to append to
/// @param value the value
void append_le16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

/// Appends a little-endian 32-bit field.
///
/// @param[in,out] out the bytes to append to
/// @param value the value
void append_le32(std::vector<uint8_t>& out, uint32_t value) {
    append_le16(out, static_cast<uint16_t>(value));
    append_le16(out, static_cast<uint16_t>(value >> 16));
}

/// Appends bytes.
///
/// @param[in,out] out the bytes to append to
/// @param bytes the bytes
void append_bytes(std::vector<uint8_t>& out, std::span<const uint8_t> bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

/// Appends a name's bytes.
///
/// @param[in,out] out the bytes to append to
/// @param name the name
void append_name(std::vector<uint8_t>& out, std::string_view name) {
    out.insert(out.end(), name.begin(), name.end());
}

/// Returns the general purpose flags of an entry: the UTF-8 flag when its
/// name holds a byte outside 7-bit ASCII, nothing otherwise.
///
/// @param name the entry's name
/// @return the flags
uint16_t flags_for(std::string_view name) noexcept {
    const bool ascii = std::all_of(name.begin(), name.end(), [](char c) {
        return static_cast<uint8_t>(c) <= last_ascii_byte;
    });
    return ascii ? uint16_t{0} : flag_utf8_name;
}

/// Records a failure.
///
/// @param[out] error the error to fill
/// @param status why the archive could not be written
/// @param entry the name of the entry at fault; empty for the archive
/// @return false, for the caller to return
bool fail(ZipError& error, ZipStatus status, std::string_view entry = {}) {
    error = ZipError{status, 0, std::string{entry}};
    return false;
}

/// Checks the entries against the limits and the name rule.
///
/// @param entries the entries
/// @param[out] error the status, and the entry at fault
/// @return true when every entry can be written
bool check_entries(std::span<const NewEntry> entries, ZipError& error) {
    if (entries.size() > max_entry_count)
        return fail(error, ZipStatus::too_many_entries);
    uint64_t archive_bytes = end_bytes;
    for (size_t index = 0; index < entries.size(); ++index) {
        const NewEntry& entry = entries[index];
        if (entry.name.size() > max_name_bytes)
            return fail(error, ZipStatus::name_too_long, entry.name);
        if (!name_is_safe(entry.name))
            return fail(error, ZipStatus::unsafe_name, entry.name);
        const auto earlier = entries.first(index);
        if (std::any_of(earlier.begin(), earlier.end(), [&](const NewEntry& other) {
                return other.name == entry.name;
            }))
            return fail(error, ZipStatus::duplicate_name, entry.name);
        if (entry.bytes.size() > max_entry_bytes)
            return fail(error, ZipStatus::entry_too_large, entry.name);
        if (entry.name.back() == '/' && !entry.bytes.empty())
            return fail(error, ZipStatus::size_mismatch, entry.name);
        archive_bytes +=
            local_bytes + central_bytes + 2 * uint64_t{entry.name.size()} + entry.bytes.size();
        if (archive_bytes > max_archive_bytes)
            return fail(error, ZipStatus::too_large, entry.name);
    }
    return true;
}

} // namespace

bool write_archive(
    std::span<const NewEntry> entries, std::vector<uint8_t>& archive, ZipError& error
) {
    error = ZipError{};
    archive.clear();
    if (!check_entries(entries, error))
        return false;
    // check_entries bounded the archive by max_archive_bytes, so every size
    // and offset below fits its 16- or 32-bit field.
    std::vector<uint8_t> out{};
    std::vector<uint32_t> crcs(entries.size());
    std::vector<uint32_t> offsets(entries.size());
    for (size_t index = 0; index < entries.size(); ++index) {
        const NewEntry& entry = entries[index];
        crcs[index] = crc32_of(entry.bytes);
        offsets[index] = static_cast<uint32_t>(out.size());
        const auto size = static_cast<uint32_t>(entry.bytes.size());
        append_bytes(out, local_signature);
        append_le16(out, written_version);
        append_le16(out, flags_for(entry.name));
        append_le16(out, static_cast<uint16_t>(Method::stored));
        append_le16(out, written_time);
        append_le16(out, written_date);
        append_le32(out, crcs[index]);
        append_le32(out, size);
        append_le32(out, size);
        append_le16(out, static_cast<uint16_t>(entry.name.size()));
        append_le16(out, 0);
        append_name(out, entry.name);
        append_bytes(out, entry.bytes);
    }
    const auto directory_offset = static_cast<uint32_t>(out.size());
    for (size_t index = 0; index < entries.size(); ++index) {
        const NewEntry& entry = entries[index];
        const auto size = static_cast<uint32_t>(entry.bytes.size());
        append_bytes(out, central_signature);
        append_le16(out, written_version);
        append_le16(out, written_version);
        append_le16(out, flags_for(entry.name));
        append_le16(out, static_cast<uint16_t>(Method::stored));
        append_le16(out, written_time);
        append_le16(out, written_date);
        append_le32(out, crcs[index]);
        append_le32(out, size);
        append_le32(out, size);
        append_le16(out, static_cast<uint16_t>(entry.name.size()));
        append_le16(out, 0); // extra field
        append_le16(out, 0); // comment
        append_le16(out, 0); // starting disk
        append_le16(out, 0); // internal attributes
        append_le32(out, 0); // external attributes
        append_le32(out, offsets[index]);
        append_name(out, entry.name);
    }
    const auto directory_bytes = static_cast<uint32_t>(out.size() - directory_offset);
    const auto entry_count = static_cast<uint16_t>(entries.size());
    append_bytes(out, end_signature);
    append_le16(out, 0); // this disk
    append_le16(out, 0); // the directory's disk
    append_le16(out, entry_count);
    append_le16(out, entry_count);
    append_le32(out, directory_bytes);
    append_le32(out, directory_offset);
    append_le16(out, 0); // comment
    archive = std::move(out);
    return true;
}

} // namespace oa::formats::zip
