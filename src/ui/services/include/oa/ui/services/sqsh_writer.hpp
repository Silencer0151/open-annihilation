// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Encoder for one SQSH chunk (the compressed block format of HPI archives and
// saved data): a 19-byte header followed by the payload.

#include <cstdint>

namespace oa::ui::services {

enum class SqshWriteStatus : uint32_t {
    ok = 0,
    bad_compression = 4,
    output_too_small = 5,
    invalid_argument = 6,
};

/// Encodes one SQSH chunk: compresses, optionally encrypts, then writes the header.
///
/// The header carries the byte-sum checksum of the stored payload. A zlib
/// payload's bytes depend on the host zlib version; every version decodes
/// them identically.
///
/// @param[out] out chunk buffer: 19-byte header, then the payload; null is refused
/// @param[in,out] out_size capacity of out on entry, chunk length on success
/// @param input bytes to compress; null is refused
/// @param input_size bytes of input; 0 is refused
/// @param compression 1 = LZ77, 2 = zlib; 0 and 3 describe an uncompressed payload
/// @param encrypt nonzero to encrypt the payload; its low byte is stored as the flag
/// @return ok, or why the chunk could not be written
/// @quirk Types 0 and 3 are accepted but, as in 3.1c, do not copy the input:
///        the header describes an uncompressed payload the caller must already
///        have placed after it. Types 4 and above are refused.
SqshWriteStatus sqsh_write_chunk(
    uint8_t* out,
    uint32_t* out_size,
    const uint8_t* input,
    uint32_t input_size,
    int32_t compression,
    int32_t encrypt
) noexcept;

} // namespace oa::ui::services
