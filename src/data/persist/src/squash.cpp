// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/persist/squash.hpp"

#include "oa/formats/sqsh.hpp"

#include "bank_util.hpp"

#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <span>

namespace oa::data::persist {
namespace {

using detail::load_le32;
using detail::store_le32;

// Budget percentages the game allows over the input size, plus slack.
constexpr uint32_t lz77_budget_percent = 120;
constexpr uint32_t zlib_budget_percent = 110;
constexpr uint32_t budget_slack = 0x77;

constexpr const char* status_names[] = {
    "SQUASHERR_OK",
    "SQUASHERR_BADHEADER",
    "SQUASHERR_BADCHECKSUM",
    "SQUASHERR_BADUNPACKSIZE",
    "SQUASHERR_BADUNPACKTYPE",
    "SQUASHERR_BADPACKTYPE",
    "SQUASHERR_BADPARAMS",
};

bool has_magic(const uint8_t* block) {
    return load_le32(block + squash_field::magic) == squash_magic;
}

} // namespace

const char* squash_status_name(SquashStatus status) {
    const auto index = static_cast<std::size_t>(status);
    return index < sizeof(status_names) / sizeof(status_names[0]) ? status_names[index] : nullptr;
}

uint32_t squash_bound(int32_t size, SquashType type) {
    uint32_t scaled = 0;
    if (type == SquashType::lz77)
        scaled = static_cast<uint32_t>(size) * lz77_budget_percent;
    else if (type == SquashType::zlib)
        scaled = static_cast<uint32_t>(size) * zlib_budget_percent;
    else
        return 0;
    return scaled / 100u + budget_slack;
}

uint32_t squash_unpacked_size(const uint8_t* block, std::size_t block_bytes) {
    if (block == nullptr || block_bytes < squash_header_bytes || !has_magic(block))
        return 0;
    return load_le32(block + squash_field::unpacked_size);
}

SquashStatus squash_pack(
    uint8_t* out,
    uint32_t* out_bytes,
    const uint8_t* input,
    uint32_t input_bytes,
    SquashType type,
    bool scramble
) {
    if (out == nullptr || out_bytes == nullptr || input == nullptr || input_bytes == 0)
        return SquashStatus::bad_params;
    if (static_cast<int32_t>(type) >= squash_type_limit)
        return SquashStatus::bad_type;
    const uint32_t capacity = *out_bytes;
    const uint32_t payload_room =
        capacity > squash_header_bytes ? capacity - squash_header_bytes : 0;
    uint8_t* payload = out + squash_header_bytes;
    // Types other than LZ77 and zlib leave the payload bytes untouched, so the
    // block describes whatever the output buffer already held.
    uint32_t packed = input_bytes;
    if (type == SquashType::lz77) {
        const auto encoded =
            formats::sqsh::encode_lz77(std::span(input, input_bytes), payload_room);
        if (!encoded.ok())
            return SquashStatus::output_too_small;
        std::memcpy(payload, encoded.value->data(), encoded.value->size());
        packed = static_cast<uint32_t>(encoded.value->size());
    } else if (type == SquashType::zlib) {
        uLongf length = payload_room;
        packed = compress2(payload, &length, input, input_bytes, Z_DEFAULT_COMPRESSION) == Z_OK
                     ? static_cast<uint32_t>(length)
                     : capacity;
    }
    if (static_cast<int32_t>(capacity) < static_cast<int32_t>(packed + squash_header_bytes))
        return SquashStatus::output_too_small;
    if (scramble)
        formats::sqsh::encrypt_chunk(std::span(payload, packed));
    store_le32(out + squash_field::magic, squash_magic);
    out[squash_field::version] = squash_writer_version;
    out[squash_field::type] = static_cast<uint8_t>(type);
    out[squash_field::scrambled] = scramble ? 1 : 0;
    store_le32(out + squash_field::packed_size, packed);
    store_le32(out + squash_field::unpacked_size, input_bytes);
    store_le32(
        out + squash_field::checksum, formats::sqsh::chunk_checksum(std::span(payload, packed))
    );
    *out_bytes = packed + squash_header_bytes;
    return SquashStatus::ok;
}

SquashStatus
squash_unpack(uint8_t* out, std::size_t out_capacity, uint8_t* block, std::size_t block_bytes) {
    if (block == nullptr || block_bytes < squash_header_bytes || !has_magic(block))
        return SquashStatus::bad_header;
    const uint8_t type = block[squash_field::type];
    if (type >= squash_type_limit)
        return SquashStatus::bad_type;
    const uint32_t packed = load_le32(block + squash_field::packed_size);
    const uint32_t unpacked = load_le32(block + squash_field::unpacked_size);
    uint8_t* payload = block + squash_header_bytes;
    // A payload running past the block is reported as a checksum mismatch.
    if (packed > block_bytes - squash_header_bytes ||
        formats::sqsh::chunk_checksum(std::span(payload, packed)) !=
            load_le32(block + squash_field::checksum))
        return SquashStatus::bad_checksum;
    if (block[squash_field::scrambled] != 0)
        formats::sqsh::decrypt_chunk(std::span(payload, packed));
    if (out == nullptr || out_capacity < unpacked)
        return SquashStatus::bad_unpack_size;
    std::size_t produced = 0;
    if (type == static_cast<uint8_t>(SquashType::lz77)) {
        const auto decoded = formats::sqsh::decode_lz77(std::span(payload, packed), out_capacity);
        if (!decoded.ok())
            return SquashStatus::bad_unpack_size;
        std::memcpy(out, decoded.value->data(), decoded.value->size());
        produced = decoded.value->size();
    } else if (type == static_cast<uint8_t>(SquashType::zlib)) {
        // The zlib result code is ignored; only the produced length is checked.
        uLongf length = unpacked;
        uncompress(out, &length, payload, packed);
        produced = length;
    } else {
        // Stored and type-3 blocks never unpack.
        return SquashStatus::bad_unpack_size;
    }
    return produced == unpacked ? SquashStatus::ok : SquashStatus::bad_unpack_size;
}

} // namespace oa::data::persist
