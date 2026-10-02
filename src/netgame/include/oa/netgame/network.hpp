// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/base/bytes.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>
#include <vector>

namespace oa::netgame::network {
// Unit-created record payload before routing and transport framing.
struct UnitCreated {
    uint16_t type_index{}, unit_index{};
    std::array<uint32_t, 3> position{}; // signed16.16 bit patterns, X/Y/Z
    std::array<uint16_t, 3> angles{};   // Unit.bank, Unit.heading, Unit.pitch
};

inline constexpr uint8_t unit_created_opcode = 0x09;
inline constexpr std::size_t unit_created_bytes = 23;
inline constexpr uint8_t unit_finished_opcode = 0x12;
inline constexpr std::size_t unit_finished_bytes = 5;
static_assert(
    unit_created_bytes == sizeof(unit_created_opcode) + sizeof(UnitCreated::type_index) +
                              sizeof(UnitCreated::unit_index) + sizeof(UnitCreated::position) +
                              sizeof(UnitCreated::angles)
);
static_assert(
    unit_finished_bytes == sizeof(unit_finished_opcode) + sizeof(uint16_t) + sizeof(uint16_t)
);
/// Encodes a unit-created record (0x09): opcode, type and unit index, position, then angles, little-endian.
///
/// @param unit Payload to encode.
/// @return The 23-byte record.
std::array<uint8_t, unit_created_bytes> encode_unit_created(const UnitCreated& unit) noexcept;

/// Encodes a unit-finished record (0x12): opcode, then two little-endian unit indices.
///
/// The subject index comes before the source index on the wire.
///
/// @param subject_index Unit index written first.
/// @param source_index Unit index written second.
/// @return The 5-byte record.
std::array<uint8_t, unit_finished_bytes>
encode_unit_finished(uint16_t subject_index, uint16_t source_index) noexcept;

namespace condenser {
inline constexpr uint8_t stored_frame = 3;
inline constexpr uint8_t compressed_frame = 4;

// Condenser prefix only. Offsets match the wire; multi-byte fields are still
// decoded little-endian rather than overlaid as a host integer.
#pragma pack(push, 1)

struct FrameHeader {
    uint8_t type{};
    uint16_t checksum{};
};

#pragma pack(pop)

inline constexpr std::size_t header_bytes = sizeof(FrameHeader);
inline constexpr std::size_t clear_tail_bytes = 3;
inline constexpr std::size_t maximum_frame_bytes = 65536;
inline constexpr std::size_t maximum_payload_bytes = maximum_frame_bytes - header_bytes;
static_assert(header_bytes == 3);
static_assert(offsetof(FrameHeader, type) == 0);
static_assert(offsetof(FrameHeader, checksum) == 1);
static_assert(maximum_payload_bytes == 65533);

/// Reads the condenser prefix: type byte and little-endian checksum word.
///
/// @param bytes Frame start; must hold at least header_bytes.
/// @return The decoded prefix.
[[nodiscard]] inline FrameHeader read_frame_header(std::span<const uint8_t> bytes) noexcept {
    FrameHeader header;
    header.type = bytes[offsetof(FrameHeader, type)];
    const auto checksum_at = offsetof(FrameHeader, checksum);
    header.checksum = static_cast<uint16_t>(
        bytes[checksum_at] | (static_cast<uint16_t>(bytes[checksum_at + 1]) << 8)
    );
    return header;
}

// HRESULT bit patterns the condenser receiver returns.
inline constexpr uint32_t success = 0;
inline constexpr uint32_t no_message = 0x887700be;
inline constexpr uint32_t buffer_too_small = 0x8877001e;
inline constexpr std::size_t receive_storage_bytes = 28000;
inline constexpr std::size_t compression_minimum_payload_bytes = 13;
} // namespace condenser

// TA condenser frames only: DirectPlay envelopes are NOT accepted here.

/// What unwrap_frame reports in DecodeError::detail.
enum class FrameProblem : uint16_t {
    none,
    bad_length,        ///< the frame is outside 4..65536 bytes
    unsupported_type,  ///< the type byte is neither stored nor compressed
    checksum_mismatch, ///< the checksum does not match the obfuscated body
    bad_body,          ///< the compressed body is malformed or decodes past 65536 bytes
};

/// Verifies a condenser frame, clears its obfuscation and returns the payload, decompressed when needed.
///
/// Throws nothing but an allocation failure; the output never exceeds 65536 bytes.
///
/// @param packet Whole frame, 4..65536 bytes.
/// @return The payload; or truncated (a frame of 3 bytes or fewer) or limit_exceeded (over 65536
///         bytes), malformed for an unsupported type or a checksum mismatch, or the LZ77 decoder's
///         error for a bad compressed body; detail holds the FrameProblem.
[[nodiscard]] base::bytes::Decoded<std::vector<uint8_t>>
unwrap_frame(std::span<const uint8_t> packet);

/// Verifies a condenser frame and returns its payload, as unwrap_frame does, for tools and tests.
///
/// @param packet Whole frame, 4..65536 bytes.
/// @return The payload.
/// @throws std::runtime_error with unwrap_frame's message when it refuses the frame.
std::vector<uint8_t> decode_frame(std::span<const uint8_t> packet);

/// Wraps a payload as a stored (type 3) condenser frame.
///
/// @param payload Payload, 1..65533 bytes.
/// @return The sealed frame.
/// @throws std::runtime_error for a payload length outside 1..65533.
std::vector<uint8_t> encode_stored_frame(std::span<const uint8_t> payload);

/// Wraps a payload as a condenser frame, compressing it when that pays.
///
/// Compression is chosen, as the game chooses it, only when it is enabled,
/// the payload is at least 13 bytes and the compressed bytes plus header are
/// strictly smaller than the raw payload.
///
/// @param payload Payload, 1..65533 bytes.
/// @param compression_enabled Whether LZ77 compression may be used.
/// @return The sealed frame.
/// @throws std::runtime_error for a payload length outside 1..65533.
std::vector<uint8_t>
encode_frame(std::span<const uint8_t> payload, bool compression_enabled = true);

/// Verifies a frame, then removes and reapplies its XOR and checksum without recompressing.
///
/// This preserves the encoded payload; it is not a canonical compressor test.
///
/// @param packet Whole frame, 4..65536 bytes.
/// @return The resealed frame.
/// @throws std::runtime_error when decode_frame rejects the packet.
std::vector<uint8_t> reencode_frame(std::span<const uint8_t> packet);

// Platform transport boundary used by the condenser receiver. A successful
// transport call must fit its result in both capacity and buffer.
class ReceiveTransport {
  public:

    virtual ~ReceiveTransport() = default;

    /// Receives one datagram.
    ///
    /// @param[out] buffer Destination storage.
    /// @param[in,out] size Capacity on entry; received, or required, bytes on return.
    /// @return A condenser HRESULT bit pattern.
    virtual uint32_t receive(std::span<uint8_t> buffer, uint32_t& size) = 0;

    /// Tells whether received datagrams carry the condenser envelope.
    ///
    /// @return True when the condenser is in use.
    virtual bool condenser_enabled() const = 0;

    /// Counts one received datagram into the traffic counters.
    ///
    /// @param size Datagram length in bytes.
    virtual void record_received_bytes(uint32_t size) = 0;
};

// Receive lifecycle: expanded messages survive a too-small caller buffer and
// are delivered on retry without receiving another transport packet.
class CondenserReceiver {
  public:

    /// Creates a receiver with an optional LZ77 dictionary seed.
    ///
    /// @param dictionary_seed Empty, or exactly one LZ77 dictionary of initial bytes.
    /// @throws std::invalid_argument for a seed of any other size.
    explicit CondenserReceiver(std::span<const uint8_t> dictionary_seed = {});

    /// Receives and unwraps one message, or delivers the message a previous call could not.
    ///
    /// Datagrams pass through raw when the transport does not use the condenser
    /// or their type byte is neither stored nor compressed.
    ///
    /// @param transport Transport to receive from when nothing is pending.
    /// @param[out] buffer Destination storage; unwrapped in place.
    /// @param[in,out] size Capacity on entry (at most buffer.size()); message length, or the length
    ///        needed, on return.
    /// @return success; buffer_too_small with the needed size, keeping the message pending; no_message for
    ///         an empty datagram, a short frame, a checksum mismatch, a stored body over 28000 bytes
    ///         or a compressed body that is malformed or decodes past 28000 bytes; the transport's
    ///         status otherwise.
    /// @throws std::invalid_argument when size exceeds buffer; std::runtime_error when the transport
    ///         overfills the buffer.
    uint32_t receive(ReceiveTransport& transport, std::span<uint8_t> buffer, uint32_t& size);

    /// Tells whether a decoded message is waiting for a larger buffer.
    [[nodiscard]] bool has_pending_message() const noexcept { return pending_; }

    /// Returns the length of the last decoded message.
    [[nodiscard]] std::size_t decoded_size() const noexcept { return decoded_.size(); }

  private:

    bool pending_{};
    std::vector<uint8_t> decoded_;
    std::vector<uint8_t> dictionary_seed_;
};
} // namespace oa::netgame::network
