// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the reader and writer share: status messages, the name rule, the
// entry lookup and the CRC-32.

#include <algorithm>
#include <climits>

#include <zlib.h>

#include "oa/formats/zip.hpp"

namespace oa::formats::zip {

namespace {

/// Bytes handed to one zlib crc32 call, whose length is an unsigned int.
constexpr size_t crc_piece_bytes = size_t{1} << 30;
static_assert(crc_piece_bytes <= UINT_MAX);

/// Tells whether a character is an ASCII letter, as a drive letter is.
///
/// @param c the character
/// @return true for 'A' to 'Z' and 'a' to 'z'
bool is_ascii_letter(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

} // namespace

const char* zip_status_message(ZipStatus status) noexcept {
    switch (status) {
    case ZipStatus::ok:
        return "ok";
    case ZipStatus::too_large:
        return "the archive is too large";
    case ZipStatus::no_end_record:
        return "no end-of-central-directory record";
    case ZipStatus::truncated:
        return "a record or an entry's data runs past the end of the archive";
    case ZipStatus::several_disks:
        return "the archive spans several disks";
    case ZipStatus::too_many_entries:
        return "the archive has too many entries";
    case ZipStatus::bad_central_record:
        return "a central directory record is malformed";
    case ZipStatus::bad_local_record:
        return "a local header is malformed or unlike its central record";
    case ZipStatus::name_too_long:
        return "an entry name is too long";
    case ZipStatus::unsafe_name:
        return "an entry name is unsafe";
    case ZipStatus::duplicate_name:
        return "two entries have the same name";
    case ZipStatus::encrypted:
        return "an entry is encrypted";
    case ZipStatus::unsupported_method:
        return "an entry uses a compression method other than stored or deflated";
    case ZipStatus::zip64:
        return "the archive needs the 64-bit extension";
    case ZipStatus::entry_too_large:
        return "an entry is too large";
    case ZipStatus::inflate_failed:
        return "an entry's deflated data does not inflate";
    case ZipStatus::size_mismatch:
        return "an entry's data is not its recorded size";
    case ZipStatus::crc_mismatch:
        return "an entry's CRC-32 is not its recorded one";
    }
    return "unknown zip status";
}

bool name_is_safe(std::string_view name) noexcept {
    if (name.empty() || name.front() == '/')
        return false;
    if (name.size() >= 2 && is_ascii_letter(name[0]) && name[1] == ':')
        return false;
    if (name.find('\0') != std::string_view::npos || name.find('\\') != std::string_view::npos)
        return false;
    // A trailing '/' names a directory; every component before it must be
    // a real name.
    std::string_view rest = name.back() == '/' ? name.substr(0, name.size() - 1) : name;
    while (true) {
        const size_t separator = rest.find('/');
        const std::string_view component = rest.substr(0, separator);
        if (component.empty() || component == "." || component == "..")
            return false;
        if (separator == std::string_view::npos)
            return true;
        rest.remove_prefix(separator + 1);
    }
}

const Entry* find_entry(const CentralDirectory& directory, std::string_view name) noexcept {
    const auto found =
        std::find_if(directory.entries.begin(), directory.entries.end(), [&](const Entry& entry) {
            return entry.name == name;
        });
    return found == directory.entries.end() ? nullptr : &*found;
}

uint32_t crc32_of(std::span<const uint8_t> bytes) noexcept {
    uLong crc = crc32(0L, Z_NULL, 0);
    while (!bytes.empty()) {
        const size_t piece = std::min(bytes.size(), crc_piece_bytes);
        crc = crc32(crc, bytes.data(), static_cast<uInt>(piece));
        bytes = bytes.subspan(piece);
    }
    return static_cast<uint32_t>(crc);
}

} // namespace oa::formats::zip
