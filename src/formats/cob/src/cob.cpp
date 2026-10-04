// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/cob.hpp"
#include "oa/base/bytes.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace oa::formats::cob {
namespace {

using base::bytes::DecodeCode;
using base::bytes::DecodeError;
using base::bytes::Decoded;
using base::bytes::load_le32;

constexpr std::size_t kHeaderBytes = header_bytes;

struct DiskHeader {
    uint32_t version_signature{};
    uint32_t script_count{};
    uint32_t piece_count{};
    uint32_t code_word_count{};
    uint32_t static_variable_count{};
    uint32_t sound_count{};
    uint32_t script_entry_offset{};
    uint32_t script_name_offset_table{};
    uint32_t piece_name_offset_table{};
    uint32_t code_offset{};
    uint32_t name_pool_offset{};
};

static_assert(sizeof(DiskHeader) == header_bytes);

bool add_overflow(std::size_t left, std::size_t right, std::size_t& result) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return true;
    }
    result = left + right;
    return false;
}

/// Decodes the eleven header words; the caller has checked that they fit.
DiskHeader read_disk_header(std::span<const uint8_t> bytes) noexcept {
    DiskHeader header{};
    header.version_signature = load_le32(bytes.data() + 0);
    header.script_count = load_le32(bytes.data() + 4);
    header.piece_count = load_le32(bytes.data() + 8);
    header.code_word_count = load_le32(bytes.data() + 12);
    header.static_variable_count = load_le32(bytes.data() + 16);
    header.sound_count = load_le32(bytes.data() + 20);
    header.script_entry_offset = load_le32(bytes.data() + 24);
    header.script_name_offset_table = load_le32(bytes.data() + 28);
    header.piece_name_offset_table = load_le32(bytes.data() + 32);
    header.code_offset = load_le32(bytes.data() + 36);
    header.name_pool_offset = load_le32(bytes.data() + 40);
    return header;
}

bool table_end(uint32_t offset, uint32_t count, std::size_t& end) noexcept {
    std::size_t bytes = 0;
    if (static_cast<std::size_t>(count) >
        std::numeric_limits<std::size_t>::max() / sizeof(uint32_t)) {
        return false;
    }
    bytes = static_cast<std::size_t>(count) * sizeof(uint32_t);
    return !add_overflow(static_cast<std::size_t>(offset), bytes, end);
}

bool read_name(
    std::span<const uint8_t> bytes,
    uint32_t offset,
    uint32_t pool_offset,
    const ParseLimits& limits,
    std::string& output
) {
    if (offset < pool_offset || offset >= bytes.size()) {
        return false;
    }
    const std::size_t remaining = bytes.size() - static_cast<std::size_t>(offset);
    const std::size_t limit = std::min(remaining, limits.max_name_bytes);
    std::size_t length = 0;
    while (length < limit && bytes[static_cast<std::size_t>(offset) + length] != 0U)
        ++length;
    if (length == limit) {
        return false;
    }
    output.assign(reinterpret_cast<const char*>(bytes.data() + offset), length);
    return true;
}

} // namespace

Decoded<CobProgram> parse_cob(std::span<const uint8_t> bytes, const ParseLimits& limits) {
    if (bytes.size() < kHeaderBytes) {
        return DecodeError{
            DecodeCode::truncated, bytes.size(), "COB is smaller than its 44-byte header"
        };
    }
    if (bytes.size() > limits.max_file_bytes) {
        return DecodeError{DecodeCode::limit_exceeded, 0, "COB exceeds configured file bound"};
    }

    const DiskHeader disk = read_disk_header(bytes);
    const CobHeader header{
        disk.version_signature,
        disk.script_count,
        disk.piece_count,
        disk.code_word_count,
        disk.static_variable_count,
        disk.sound_count,
        disk.script_entry_offset,
        disk.script_name_offset_table,
        disk.piece_name_offset_table,
        disk.code_offset,
        disk.name_pool_offset
    };
    // The VersionSignature is not checked: every version is read by the same
    // header offsets.
    if (header.sound_count != 0U) {
        return DecodeError{
            DecodeCode::unsupported_version,
            offsetof(DiskHeader, sound_count),
            "unsupported non-zero TA COB sound count"
        };
    }
    if (header.script_count > limits.max_scripts || header.piece_count > limits.max_pieces ||
        header.code_word_count > limits.max_code_words ||
        header.static_variable_count > limits.max_static_variables) {
        return DecodeError{
            DecodeCode::limit_exceeded,
            offsetof(DiskHeader, script_count),
            "COB count exceeds configured bounds"
        };
    }
    if (header.code_offset < kHeaderBytes || header.script_entry_offset < header.code_offset ||
        header.script_name_offset_table < header.script_entry_offset ||
        header.piece_name_offset_table < header.script_name_offset_table ||
        header.name_pool_offset < header.piece_name_offset_table) {
        return DecodeError{
            DecodeCode::malformed,
            offsetof(DiskHeader, script_entry_offset),
            "COB sections are not in increasing order"
        };
    }
    std::size_t code_end = 0;
    if (!table_end(header.code_offset, header.code_word_count, code_end) ||
        code_end != header.script_entry_offset || code_end > bytes.size()) {
        return DecodeError{
            DecodeCode::out_of_range,
            offsetof(DiskHeader, code_offset),
            "COB code section bounds disagree with entry table"
        };
    }
    std::size_t script_table_end = 0;
    std::size_t script_name_end = 0;
    std::size_t piece_name_end = 0;
    if (!table_end(header.script_entry_offset, header.script_count, script_table_end) ||
        !table_end(header.script_name_offset_table, header.script_count, script_name_end) ||
        !table_end(header.piece_name_offset_table, header.piece_count, piece_name_end) ||
        script_table_end > bytes.size() || script_name_end > bytes.size() ||
        piece_name_end > bytes.size() || script_table_end > header.script_name_offset_table ||
        script_name_end > header.piece_name_offset_table ||
        piece_name_end > header.name_pool_offset) {
        return DecodeError{
            DecodeCode::out_of_range,
            offsetof(DiskHeader, script_entry_offset),
            "COB name or entry table bounds are invalid"
        };
    }

    CobProgram result;
    result.header = header;
    result.code.reserve(header.code_word_count);
    for (uint32_t i = 0; i < header.code_word_count; ++i) {
        result.code.push_back(load_le32(
            bytes.data() + static_cast<std::size_t>(header.code_offset) +
            static_cast<std::size_t>(i) * sizeof(uint32_t)
        ));
    }
    result.entry_points.reserve(header.script_count);
    result.scripts.reserve(header.script_count);
    for (uint32_t i = 0; i < header.script_count; ++i) {
        const std::size_t entry_at = static_cast<std::size_t>(header.script_entry_offset) +
                                     static_cast<std::size_t>(i) * sizeof(uint32_t);
        const auto entry = load_le32(bytes.data() + entry_at);
        if (entry >= header.code_word_count) {
            return DecodeError{
                DecodeCode::out_of_range, entry_at, "COB script entry points outside code section"
            };
        }
        const std::size_t name_at = static_cast<std::size_t>(header.script_name_offset_table) +
                                    static_cast<std::size_t>(i) * sizeof(uint32_t);
        const auto name_offset = load_le32(bytes.data() + name_at);
        Script script;
        if (!read_name(bytes, name_offset, header.name_pool_offset, limits, script.name)) {
            return DecodeError{
                DecodeCode::out_of_range, name_at, "COB script name is outside bounded name pool"
            };
        }
        script.entry_word = entry;
        result.entry_points.push_back(entry);
        result.scripts.push_back(std::move(script));
    }
    result.piece_names.reserve(header.piece_count);
    for (uint32_t i = 0; i < header.piece_count; ++i) {
        const std::size_t name_at = static_cast<std::size_t>(header.piece_name_offset_table) +
                                    static_cast<std::size_t>(i) * sizeof(uint32_t);
        const auto name_offset = load_le32(bytes.data() + name_at);
        std::string name;
        if (!read_name(bytes, name_offset, header.name_pool_offset, limits, name)) {
            return DecodeError{
                DecodeCode::out_of_range, name_at, "COB piece name is outside bounded name pool"
            };
        }
        result.piece_names.push_back(std::move(name));
    }
    return result;
}

} // namespace oa::formats::cob
