// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/services/sqsh_writer.hpp"

#include "oa/formats/hpi.hpp"
#include "oa/formats/sqsh.hpp"

#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <span>

namespace oa::ui::services {
namespace {

constexpr uint8_t sqsh_version = 2;
constexpr int32_t first_invalid_compression = 4;

void store_le32(uint8_t* bytes, uint32_t value) noexcept {
    for (int i = 0; i < 4; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

} // namespace

SqshWriteStatus sqsh_write_chunk(
    uint8_t* out,
    uint32_t* out_size,
    const uint8_t* input,
    uint32_t input_size,
    int32_t compression,
    int32_t encrypt
) noexcept {
    if (out == nullptr || input == nullptr || input_size == 0) {
        return SqshWriteStatus::invalid_argument;
    }
    if (compression >= first_invalid_compression) {
        return SqshWriteStatus::bad_compression;
    }
    uint8_t* payload = out + formats::hpi::SQSHHeaderSize;
    const int64_t capacity = static_cast<int32_t>(*out_size);
    const std::size_t payload_room =
        capacity > formats::hpi::SQSHHeaderSize
            ? static_cast<std::size_t>(capacity - formats::hpi::SQSHHeaderSize)
            : 0;
    uint32_t stored = input_size;
    if (compression == formats::hpi::CompressionLZ77) {
        const auto encoded = formats::sqsh::encode_lz77(std::span(input, input_size), payload_room);
        if (!encoded.ok()) {
            return SqshWriteStatus::output_too_small;
        }
        std::memcpy(payload, encoded.value->data(), encoded.value->size());
        stored = static_cast<uint32_t>(encoded.value->size());
    } else if (compression == formats::hpi::CompressionZLib) {
        // A failed compression leaves the length at the whole output size,
        // which the size check below rejects.
        uLongf length = static_cast<uLongf>(payload_room);
        stored = *out_size;
        if (compress(payload, &length, input, input_size) == Z_OK) {
            stored = static_cast<uint32_t>(length);
        }
    }
    if (capacity <
        static_cast<int64_t>(static_cast<int32_t>(stored + formats::hpi::SQSHHeaderSize))) {
        return SqshWriteStatus::output_too_small;
    }
    if (encrypt != 0) {
        formats::sqsh::encrypt_chunk(std::span(payload, stored));
    }
    const uint32_t checksum =
        formats::sqsh::chunk_checksum(std::span<const uint8_t>(payload, stored));
    store_le32(out, formats::hpi::ChunkMarker);
    out[4] = sqsh_version;
    out[5] = static_cast<uint8_t>(compression);
    out[6] = static_cast<uint8_t>(encrypt);
    store_le32(out + 7, stored);
    store_le32(out + 11, input_size);
    store_le32(out + 15, checksum);
    *out_size = stored + formats::hpi::SQSHHeaderSize;
    return SqshWriteStatus::ok;
}

} // namespace oa::ui::services
