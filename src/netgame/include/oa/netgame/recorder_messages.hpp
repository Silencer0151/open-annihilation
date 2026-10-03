// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The recorder's own records, which travel inside game frames between
// machines that run the recorder (record types 0xf6 and 0xf9..0xff, past
// 3.1c's last type 0x2c). 3.1c's splitter stops at them; a recorder splits
// them out by their own lengths:
//
//   0xf6  1 byte
//   0xf9  0x49 bytes: from id u32, to id u32, 64 bytes of chat text (ally chat relayed)
//   0xfa  1 byte: the sender plays a recording back
//   0xfb  3 + n bytes: u8 n, u8 kind, n payload bytes (one message to one machine)
//   0xfc  5 bytes: camera x u16, y u16 (0xffff, 0xffff: no longer shared)
//   0xfd  the u16 at +1 less 4 bytes; 0xfe 5 bytes; 0xff 1 byte (recordings only)

#include "oa/netgame/wire.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::netgame {

enum class RecorderRecordType : uint8_t {
    spare = 0xf6,          ///< in the recorder's length table; never sent
    ally_chat = 0xf9,      ///< a chat line addressed to one player, relayed to the others
    replayer = 0xfa,       ///< the sender is a replayer playing a recording
    message = 0xfb,        ///< a recorder message, by kind
    camera = 0xfc,         ///< the sender's camera position
    recorded_state = 0xfd, ///< a 0x2c without its tick, in recordings
    recorded_tick = 0xfe,  ///< the tick the following recorded states belong to
    recorded_empty = 0xff, ///< an empty 0x2c, in recordings
};

/// Kinds of the 0xfb recorder message.
enum class RecorderMessageKind : uint8_t {
    whiteboard = 0,   ///< up to 100 bytes of whiteboard marks
    warp_done = 1,    ///< the sender's commander warp is done; no payload
    cheat_mask = 2,   ///< u32 mask of cheats the sender's recorder found
    shared_sight = 3, ///< u8 0 or 1
    host_options = 4, ///< the host's session options, 6 bytes
};

inline constexpr std::size_t recorder_ally_chat_bytes = 0x49;
inline constexpr std::size_t recorder_camera_bytes = 5;
inline constexpr std::size_t recorder_message_header_bytes = 3;
inline constexpr std::size_t recorder_message_max_payload = 0xff;
inline constexpr std::size_t recorder_host_options_bytes = 6;
/// The most bytes of whiteboard marks one kind 0 message carries.
inline constexpr std::size_t recorder_whiteboard_max_payload = 100;
/// The lowest recorder protocol a machine sends 0xfb messages to.
inline constexpr uint8_t recorder_protocol_messages = 2;
inline constexpr uint16_t recorder_camera_off = 0xffff;

/// The host's session options a recorder sends each joiner (message kind 4).
///
/// The speed lock limits travel as the host typed them; the lock admits
/// speeds speed_low + 10 to speed_high + 10. Before any lock is set a
/// recorder sends high 1 and low 20.
struct RecorderHostOptions {
    uint8_t autopause{};      ///< the game starts paused and only the host may unpause it
    uint8_t f1_off{};         ///< the unit help key is off
    uint8_t commander_warp{}; ///< the game starts paused until every commander is placed
    uint8_t speed_lock{};     ///< the speed lock below is on
    uint8_t speed_high{1};
    uint8_t speed_low{20};
};

/// Tells whether a byte heads a recorder record.
[[nodiscard]] constexpr bool is_recorder_record_type(uint8_t type) noexcept {
    return type == static_cast<uint8_t>(RecorderRecordType::spare) ||
           type >= static_cast<uint8_t>(RecorderRecordType::ally_chat);
}

/// Returns the length of the recorder record at bytes[0], as a recorder's splitter reads it.
///
/// @param bytes record start
/// @param available bytes readable from bytes
/// @param[out] length record length including the type byte
/// @return ok; bad_argument for a null pointer; invalid_type for a byte that heads no recorder
///         record; truncated when the bytes giving the length are missing; zero_length_record for a
///         0xfd whose length word gives none
[[nodiscard]] WireError
recorder_record_length(const uint8_t* bytes, std::size_t available, uint16_t* length) noexcept;

/// Writes a 0xfb recorder message.
///
/// @param kind the message kind
/// @param payload its payload; may be null when size is 0
/// @param size payload length, at most 255
/// @param[out] out storage for the record
/// @param capacity bytes available at out
/// @param[out] written the record length, 3 + size
/// @return ok; bad_argument for a null pointer or a payload above 255 bytes; buffer_too_small
[[nodiscard]] WireError encode_recorder_message(
    RecorderMessageKind kind,
    const uint8_t* payload,
    std::size_t size,
    uint8_t* out,
    std::size_t capacity,
    std::size_t* written
) noexcept;

/// A 0xfb recorder message read in place.
struct RecorderMessage {
    RecorderMessageKind kind{};
    const uint8_t* payload{}; ///< inside the record
    std::size_t size{};
};

/// Reads a 0xfb recorder message.
///
/// @param record the record, type byte first
/// @param size its length in bytes
/// @param[out] out the message, pointing into the record
/// @return ok; bad_argument for a null pointer or another type; truncated or length_mismatch when the
///         record and its length byte disagree
[[nodiscard]] WireError
decode_recorder_message(const uint8_t* record, std::size_t size, RecorderMessage* out) noexcept;

/// Writes the host options as the 6-byte payload of message kind 4.
///
/// @param options the options
/// @param[out] out six bytes
void encode_recorder_host_options(const RecorderHostOptions& options, uint8_t* out) noexcept;

/// Reads the host options of a kind 4 message.
///
/// @param payload the payload
/// @param size its length
/// @param[out] out the options
/// @return ok; truncated below six bytes; bad_argument for a null pointer
[[nodiscard]] WireError decode_recorder_host_options(
    const uint8_t* payload, std::size_t size, RecorderHostOptions* out
) noexcept;

/// Writes a 0xfc camera record.
///
/// @param x camera x, or recorder_camera_off
/// @param y camera y, or recorder_camera_off
/// @param[out] out five bytes
void encode_recorder_camera(uint16_t x, uint16_t y, uint8_t* out) noexcept;

} // namespace oa::netgame
