// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace oa::formats::sqsh {
namespace lz77 {
inline constexpr std::size_t dictionary_bytes = 4096;
inline constexpr std::size_t dictionary_mask = dictionary_bytes - 1;
inline constexpr std::size_t initial_write_offset = 1;
inline constexpr unsigned token_length_bits = 4;
inline constexpr unsigned token_length_mask = (1u << token_length_bits) - 1;
inline constexpr unsigned minimum_match_bytes = 2;
inline constexpr unsigned last_flag_bit = 0x80;
inline constexpr std::size_t terminator_offset = 0;
} // namespace lz77

/// Decompresses one LZ77 stream into a 4096-byte window starting at offset 1.
///
/// Each flag byte selects, bit 0 first, a literal byte or a two-byte token of
/// a 12-bit window offset and a 4-bit length (plus 2); offset 0 ends the
/// stream. Overlapping copies read each byte after the preceding write.
/// Throws std::runtime_error on truncated input or when output_limit is
/// exceeded, and std::invalid_argument for a dictionary of the wrong size.
///
/// @param input compressed bytes
/// @param output_limit most bytes the output may hold
/// @param initial_dictionary empty for the game's fresh zero-filled window,
///        otherwise exactly 4096 bytes to seed it
/// @return the decompressed bytes
std::vector<uint8_t> decode_lz77(
    std::span<const uint8_t> input,
    std::size_t output_limit,
    std::span<const uint8_t> initial_dictionary = {}
);

/// How a decode_lz77_into call ended.
enum class Lz77Status : uint8_t {
    ok,             ///< the stream reached its end marker
    truncated,      ///< the input ends before the end marker
    output_full,    ///< the stream produces more bytes than the output holds
    bad_dictionary, ///< the initial dictionary is neither empty nor 4096 bytes
};

/// What a decode_lz77_into call wrote, and how it ended.
struct Lz77Decoded {
    std::size_t written{}; ///< bytes written to the start of the output
    Lz77Status status{};
};

/// Decompresses one LZ77 stream, as decode_lz77 does, into a buffer the caller owns.
///
/// The size of `output` is the output limit. Bytes past the written count
/// are left as they were. A stream that fails has written the bytes it
/// decoded before the failure; a dictionary of the wrong size writes none.
///
/// @param input compressed bytes
/// @param[out] output receives the decompressed bytes; its size is the most the stream may produce
/// @param initial_dictionary empty for the game's fresh zero-filled window,
///        otherwise exactly 4096 bytes to seed it
/// @return the count of bytes written, and ok or why the stream stopped
[[nodiscard]] Lz77Decoded decode_lz77_into(
    std::span<const uint8_t> input,
    std::span<uint8_t> output,
    std::span<const uint8_t> initial_dictionary = {}
) noexcept;

/// Compresses bytes with the game's LZ77 encoder, starting from a fresh tree.
///
/// The output decodes with decode_lz77. Throws std::runtime_error when
/// output_limit is exceeded. The byte after the end marker is the last
/// literal an earlier flag group held in the token position that follows the
/// marker's, or 0 when none did.
///
/// @param input bytes to compress
/// @param output_limit most bytes the output may hold
/// @return the compressed stream, byte for byte as 3.1c produces it
/// @quirk Like 3.1c, the encoder emits one trailing byte after the end marker.
std::vector<uint8_t> encode_lz77(std::span<const uint8_t> input, std::size_t output_limit);

/// Undoes the SQSH index scramble in place: (byte - index) ^ index.
///
/// @param[in,out] bytes chunk payload; index is each byte's position modulo 256
void decrypt_chunk(std::span<uint8_t> bytes) noexcept;
/// Applies the SQSH index scramble in place, the inverse of decrypt_chunk.
///
/// @param[in,out] bytes chunk payload; each byte becomes (byte ^ index) + index
void encrypt_chunk(std::span<uint8_t> bytes) noexcept;
/// Returns the SQSH chunk checksum.
///
/// @param bytes stored chunk payload
/// @return the byte sum modulo 2^32
uint32_t chunk_checksum(std::span<const uint8_t> bytes) noexcept;
} // namespace oa::formats::sqsh
