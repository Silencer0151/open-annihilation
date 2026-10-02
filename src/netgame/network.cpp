// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/network.hpp"
#include "oa/formats/sqsh.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace oa::netgame::network {
std::array<uint8_t, unit_created_bytes> encode_unit_created(const UnitCreated& unit) noexcept {
    std::array<uint8_t, unit_created_bytes> bytes{};
    std::size_t offset = 0;
    const auto write = [&](uint32_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i)
            bytes[offset++] = static_cast<uint8_t>(value >> (i * 8));
    };
    write(unit_created_opcode, sizeof(unit_created_opcode));
    write(unit.type_index, sizeof(unit.type_index));
    write(unit.unit_index, sizeof(unit.unit_index));
    for (auto position : unit.position)
        write(position, sizeof(position));
    for (auto angle : unit.angles)
        write(angle, sizeof(angle));
    return bytes;
}

std::array<uint8_t, unit_finished_bytes>
encode_unit_finished(uint16_t subject_index, uint16_t source_index) noexcept {
    return {
        unit_finished_opcode,
        static_cast<uint8_t>(subject_index),
        static_cast<uint8_t>(subject_index >> 8),
        static_cast<uint8_t>(source_index),
        static_cast<uint8_t>(source_index >> 8)
    };
}

namespace {
using namespace condenser;
using base::bytes::DecodeCode;
using base::bytes::DecodeError;

DecodeError
frame_error(DecodeCode code, uint64_t offset, const char* message, FrameProblem problem) {
    return {code, offset, message, static_cast<uint16_t>(problem)};
}

void wrap(std::vector<uint8_t>& out) {
    uint16_t sum = 0;
    for (std::size_t j = header_bytes; j < out.size() - clear_tail_bytes; ++j) {
        out[j] ^= static_cast<uint8_t>(j);
        sum = static_cast<uint16_t>(sum + out[j]);
    }
    const auto checksum_at = offsetof(FrameHeader, checksum);
    out[checksum_at] = static_cast<uint8_t>(sum);
    out[checksum_at + 1] = static_cast<uint8_t>(sum >> 8);
}

/// Returns the bytes an LZ77 call produced, or throws its error.
///
/// @param decoded the call's result
/// @return the bytes
std::vector<uint8_t> decoded_or_throw(base::bytes::Decoded<std::vector<uint8_t>>&& decoded) {
    if (!decoded.ok())
        throw std::runtime_error(decoded.error.message);
    return std::move(*decoded.value);
}
} // namespace

base::bytes::Decoded<std::vector<uint8_t>> unwrap_frame(std::span<const uint8_t> packet) {
    if (packet.size() <= header_bytes)
        return frame_error(
            DecodeCode::truncated,
            0,
            "condenser: frame length outside 4..65536",
            FrameProblem::bad_length
        );
    if (packet.size() > maximum_frame_bytes)
        return frame_error(
            DecodeCode::limit_exceeded,
            0,
            "condenser: frame length outside 4..65536",
            FrameProblem::bad_length
        );
    const auto header = read_frame_header(packet);
    if (header.type != stored_frame && header.type != compressed_frame)
        return frame_error(
            DecodeCode::malformed, 0, "condenser: unsupported type", FrameProblem::unsupported_type
        );
    std::vector<uint8_t> out(packet.begin() + header_bytes, packet.end());
    uint16_t sum = 0;
    for (std::size_t j = header_bytes; j + clear_tail_bytes < packet.size(); ++j) {
        sum = static_cast<uint16_t>(sum + packet[j]);
        out[j - header_bytes] ^= static_cast<uint8_t>(j);
    }
    if (sum != header.checksum)
        return frame_error(
            DecodeCode::malformed,
            offsetof(FrameHeader, checksum),
            "condenser: checksum mismatch",
            FrameProblem::checksum_mismatch
        );
    if (header.type != compressed_frame)
        return out;
    auto decoded = formats::sqsh::decode_lz77(out, maximum_frame_bytes);
    if (!decoded.ok()) {
        auto error = decoded.error;
        error.offset += header_bytes;
        error.detail = static_cast<uint16_t>(FrameProblem::bad_body);
        return error;
    }
    return std::move(*decoded.value);
}

std::vector<uint8_t> decode_frame(std::span<const uint8_t> packet) {
    auto decoded = unwrap_frame(packet);
    if (!decoded.ok())
        throw std::runtime_error(decoded.error.message);
    return std::move(*decoded.value);
}

std::vector<uint8_t> encode_stored_frame(std::span<const uint8_t> payload) {
    if (payload.empty() || payload.size() > maximum_payload_bytes)
        throw std::runtime_error("condenser: stored payload length outside 1..65533");
    std::vector<uint8_t> out(header_bytes + payload.size(), 0);
    out[offsetof(FrameHeader, type)] = stored_frame;
    std::copy(payload.begin(), payload.end(), out.begin() + header_bytes);
    wrap(out);
    return out;
}

std::vector<uint8_t> reencode_frame(std::span<const uint8_t> packet) {
    (void)decode_frame(packet);
    // Clear the obfuscation in place, then seal it again.
    std::vector<uint8_t> out(packet.begin(), packet.end());
    for (std::size_t j = header_bytes; j + clear_tail_bytes < out.size(); ++j)
        out[j] ^= static_cast<uint8_t>(j);
    wrap(out);
    return out;
}

std::vector<uint8_t> encode_frame(std::span<const uint8_t> payload, bool compression_enabled) {
    if (payload.empty() || payload.size() > maximum_payload_bytes)
        throw std::runtime_error("condenser: payload length outside 1..65533");
    if (payload.size() >= compression_minimum_payload_bytes && compression_enabled) {
        auto compressed =
            decoded_or_throw(formats::sqsh::encode_lz77(payload, payload.size() * 2 + 16));
        if (compressed.size() + header_bytes < payload.size()) {
            std::vector<uint8_t> result(header_bytes, 0);
            result[offsetof(FrameHeader, type)] = compressed_frame;
            result.insert(result.end(), compressed.begin(), compressed.end());
            wrap(result);
            return result;
        }
    }
    return encode_stored_frame(payload);
}

CondenserReceiver::CondenserReceiver(std::span<const uint8_t> seed) {
    if (!seed.empty() && seed.size() != formats::sqsh::lz77::dictionary_bytes)
        throw std::invalid_argument("condenser: invalid dictionary seed size");
    dictionary_seed_.assign(seed.begin(), seed.end());
}

uint32_t
CondenserReceiver::receive(ReceiveTransport& transport, std::span<uint8_t> buffer, uint32_t& size) {
    const auto capacity = size;
    if (capacity > buffer.size())
        throw std::invalid_argument("condenser: capacity exceeds supplied storage");
    if (!pending_) {
        const auto status = transport.receive(buffer.first(capacity), size);
        if (!transport.condenser_enabled() || status != success)
            return status;
        if (size > capacity)
            throw std::runtime_error("condenser: transport exceeded receive capacity");
        transport.record_received_bytes(size);
        // An empty transport result holds no frame: it is reported as no message.
        if (size == 0)
            return no_message;
        if (buffer[offsetof(FrameHeader, type)] != stored_frame &&
            buffer[offsetof(FrameHeader, type)] != compressed_frame)
            return success;
        if (size <= header_bytes)
            return no_message;
        uint16_t checksum = 0;
        for (std::size_t index = header_bytes; index < size - clear_tail_bytes; ++index) {
            checksum = static_cast<uint16_t>(checksum + buffer[index]);
            buffer[index] ^= static_cast<uint8_t>(index);
        }
        const auto header = read_frame_header(buffer.first(header_bytes));
        if (checksum != header.checksum)
            return no_message;
        const auto body =
            std::span<const uint8_t>(buffer).subspan(header_bytes, size - header_bytes);
        // A body that is malformed or larger than the receive storage is
        // dropped as the game's receiver drops it.
        if (header.type == compressed_frame) {
            auto decoded =
                formats::sqsh::decode_lz77(body, receive_storage_bytes, dictionary_seed_);
            if (!decoded.ok())
                return no_message;
            decoded_ = std::move(*decoded.value);
        } else {
            if (body.size() > receive_storage_bytes)
                return no_message;
            decoded_.assign(body.begin(), body.end());
        }
    }
    size = static_cast<uint32_t>(decoded_.size());
    if (capacity < size) {
        pending_ = true;
        return buffer_too_small;
    }
    std::copy(decoded_.begin(), decoded_.end(), buffer.begin());
    pending_ = false;
    return success;
}
} // namespace oa::netgame::network
