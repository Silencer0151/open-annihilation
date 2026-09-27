// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace oa::formats::cob {

// COB header: 11 little-endian 32-bit words, 44 bytes. TA is version 4.
// Version 6 is TA: Kingdoms and is not accepted here.
inline constexpr uint32_t version_ta = 4;
inline constexpr uint32_t version_kingdoms = 6;
inline constexpr std::size_t header_dwords = 11;
inline constexpr std::size_t header_bytes = header_dwords * sizeof(uint32_t);

struct CobHeader {
    uint32_t version_signature = 0;
    uint32_t script_count = 0;
    uint32_t piece_count = 0;
    uint32_t code_word_count = 0;
    uint32_t static_variable_count = 0;
    // ? Entries in a sound-name table, which would start at name_pool_offset
    // (the word CobScriptHeader names sounds). Every shipped v4 file stores
    // zero, and this reader accepts only zero.
    uint32_t sound_count = 0;
    uint32_t script_entry_offset = 0;
    uint32_t script_name_offset_table = 0;
    uint32_t piece_name_offset_table = 0;
    uint32_t code_offset = 0;
    uint32_t name_pool_offset = 0;
};

struct Script {
    std::string name;
    // Code is indexed in 32-bit words, not bytes.
    uint32_t entry_word = 0;
};

struct CobProgram {
    CobHeader header;
    std::vector<uint32_t> code;
    std::vector<uint32_t> entry_points;
    std::vector<Script> scripts;
    std::vector<std::string> piece_names;
    // Hash of the whole COB file that the unit loader records; a saved
    // script state carries it as the script's identity.
    uint32_t file_hash = 0;
};

/// Returns the identity a saved script state carries.
///
/// @param program loaded COB program
/// @return the file hash recorded when the script was loaded
[[nodiscard]] inline uint32_t script_identity(const CobProgram& program) noexcept {
    return program.file_hash;
}

struct ParseLimits {
    std::size_t max_file_bytes = 64U * 1024U * 1024U;
    uint32_t max_scripts = 65536;
    uint32_t max_pieces = 65536;
    uint32_t max_code_words = 1U << 20;
    uint32_t max_static_variables = 65536;
    std::size_t max_name_bytes = 4096;
};

struct ParseResult {
    std::optional<CobProgram> program;
    std::string error;

    /// Returns whether a program was parsed.
    explicit operator bool() const noexcept { return program.has_value(); }
};

/// Decodes a COB image into its code, entry, name and piece tables.
///
/// The table offsets are copied into bounded tables; overlapping or
/// out-of-range sections are rejected.
/// Only VersionSignature 4 with a sound count of zero is accepted.
///
/// @param bytes the whole COB file
/// @param limits allocation and count bounds
/// @return the program and its file hash, or an error message
[[nodiscard]] ParseResult parse_cob(std::span<const uint8_t> bytes, const ParseLimits& limits = {});

/// Loads and decodes a COB from a host file path.
///
/// @param path host file path
/// @param limits allocation and count bounds; max_file_bytes also bounds the read
/// @return the program, or an error message when the file cannot be read or parsed
[[nodiscard]] ParseResult load_cob_file(const char* path, const ParseLimits& limits = {});

} // namespace oa::formats::cob
