// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// SQSH block codec used by the savegame bank: a 19-byte header followed by an
// LZ77 or zlib payload, optionally byte-scrambled.
#pragma once

#include <cstddef>
#include <cstdint>

namespace oa::data::persist {

inline constexpr uint32_t squash_magic = 0x48535153u; // "SQSH"
inline constexpr uint32_t squash_header_bytes = 0x13;
inline constexpr uint8_t squash_writer_version = 2;

enum class SquashType : int32_t { stored = 0, lz77 = 1, zlib = 2 };
inline constexpr int32_t squash_type_limit = 4; // types >= 4 are rejected

// Result codes shared by pack and unpack; squash_status_name gives each its
// SQUASHERR_* diagnostic name.
enum class SquashStatus : uint8_t {
    ok = 0,
    bad_header = 1,
    bad_checksum = 2,
    bad_unpack_size = 3,
    bad_type = 4,
    output_too_small = 5,
    bad_params = 6,
};

// Byte layout of the 19-byte block header.
namespace squash_field {
inline constexpr std::size_t magic = 0x0, version = 0x4, type = 0x5, scrambled = 0x6,
                             packed_size = 0x7, unpacked_size = 0xb, checksum = 0xf;
} // namespace squash_field

/// Returns the SQUASHERR_* diagnostic name of a status.
///
/// @param status status code
/// @return the name, or null past the table
const char* squash_status_name(SquashStatus status);

/// Returns the worst-case packed size budget for an input.
///
/// The budget is 120% (LZ77) or 110% (zlib) of the input size plus 0x77 bytes.
///
/// @param size input size in bytes
/// @param type codec the budget is for
/// @return the budget in bytes; 0 for other types
uint32_t squash_bound(int32_t size, SquashType type);

/// Returns the unpacked size recorded in a block header.
///
/// @param block block bytes; may be null
/// @param block_bytes number of bytes available at `block`
/// @return the recorded size, or 0 when the block is shorter than a header or
///     lacks the magic
uint32_t squash_unpacked_size(const uint8_t* block, std::size_t block_bytes);

/// Packs an input into one block (header + payload).
///
/// @param[out] out destination for the block
/// @param[in,out] out_bytes capacity of `out` on entry; the block size on success
/// @param input bytes to pack
/// @param input_bytes number of input bytes; 0 is rejected
/// @param type codec: LZ77 or zlib; types 0 and 3 record the input size as the
///     payload size but leave the payload bytes untouched
/// @param scramble true to scramble the payload
/// @return ok; bad_params for a null pointer or empty input; bad_type for
///     types of 4 and above; output_too_small when the block does not fit
SquashStatus squash_pack(
    uint8_t* out,
    uint32_t* out_bytes,
    const uint8_t* input,
    uint32_t input_bytes,
    SquashType type,
    bool scramble
);

/// Validates and unpacks one block.
///
/// @param[out] out destination for the unpacked bytes
/// @param out_capacity size of `out`; must hold the header's unpacked size
/// @param[in,out] block block bytes; the payload is unscrambled in place when
///     scrambled, as the game does
/// @param block_bytes number of bytes available at `block`
/// @return ok; bad_header without a header or magic; bad_type for types of 4
///     and above; bad_checksum when the checksum differs or the payload runs
///     past the block; bad_unpack_size when `out` is too small, the decoded
///     length differs from the header's, or a zlib stream does not reach its
///     end
/// @quirk Stored and type-3 blocks never unpack.
SquashStatus
squash_unpack(uint8_t* out, std::size_t out_capacity, uint8_t* block, std::size_t block_bytes);

} // namespace oa::data::persist
