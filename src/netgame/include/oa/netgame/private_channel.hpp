// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The private chat channel: binary messages carried in ordinary 0x05 chat
// records whose text starts with a NUL byte, which 3.1c shows as nothing.
// Each message is one whole 65-byte chat record:
//
//   +0 0x05  +1 0x00  +2 sub-id  +3 u16 0x0041  +5 op  +6 payload, zero padded
//
// A reply of two parts travels as two such records back to back; the frame
// splitter hands them out as two records, so no reassembly is needed.

#include "oa/netgame/wire.hpp"
#include "oa/netgame/wire_rules.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::netgame {

inline constexpr uint8_t private_sub_integrity = 0x2b; ///< integrity check messages
inline constexpr uint8_t private_sub_vote = 0x2c;      ///< votes to reject a player
inline constexpr uint16_t private_length_word = 0x41;  ///< the u16 at +3 of every message
inline constexpr std::size_t private_record_bytes = 0x41;
inline constexpr std::size_t private_header_bytes = 6; ///< type, NUL, sub-id, length word, op
inline constexpr std::size_t private_payload_bytes = private_record_bytes - private_header_bytes;

// Integrity check ops.
inline constexpr uint8_t integrity_op_challenge = 0x01;    ///< 32 random bytes, unicast
inline constexpr uint8_t integrity_op_accepted = 0x02;     ///< u32, u32; read and ignored
inline constexpr uint8_t integrity_op_first_report = 0x03; ///< report requests 3..7
inline constexpr uint8_t integrity_op_last_report = 0x07;
inline constexpr uint8_t integrity_op_module_reply = 0x20; ///< 32-byte answer over the program
inline constexpr uint8_t integrity_op_data_reply = 0x21;   ///< 32-byte answer over the game data
inline constexpr std::size_t integrity_nonce_bytes = 32;
inline constexpr std::size_t integrity_answer_bytes = 32;

// Vote ops and flags.
inline constexpr uint8_t vote_op_propose = 1; ///< a proposer asking for the vote counts as yes
inline constexpr uint8_t vote_op_yes = 2;
inline constexpr uint8_t vote_op_no = 3;             ///< also withdraws an earlier yes
inline constexpr uint8_t vote_flag_manual = 1;       ///< someone asked to reject the player
inline constexpr uint8_t vote_flag_timeout = 6;      ///< the player stopped answering
inline constexpr std::size_t vote_target_offset = 0; ///< payload offset of the target id (u32)
inline constexpr std::size_t vote_flag_offset = 4;   ///< payload offset of the flag

/// One private message, as the record carries it.
struct PrivateMessage {
    uint8_t sub_id{};
    uint8_t op{};
    uint8_t payload[private_payload_bytes]{};
};

/// Tells whether a chat record belongs to the private channel: its text starts with a NUL byte.
///
/// @param record the record, type byte first
/// @param size its length in bytes
/// @return true for a 0x05 record of at least two bytes whose second byte is 0
[[nodiscard]] constexpr bool is_private_chat(const uint8_t* record, std::size_t size) noexcept {
    return record != nullptr && size >= 2 && record[0] == static_cast<uint8_t>(RecordType::chat) &&
           record[1] == 0;
}

/// Reads a private message out of a chat record.
///
/// @param record the record, type byte first
/// @param size its length in bytes
/// @param[out] out the message; left unchanged on failure
/// @return ok; bad_argument for a null pointer or a record that is not private chat; truncated below
///         65 bytes; length_mismatch when the u16 at +3 is not 0x41
[[nodiscard]] WireError
decode_private_message(const uint8_t* record, std::size_t size, PrivateMessage* out) noexcept;

/// Writes a private message as a 65-byte chat record.
///
/// @param message the message
/// @param[out] out storage for the record
/// @param capacity bytes available at out
/// @param[out] written the record length, 65
/// @return ok; bad_argument for a null pointer; buffer_too_small below 65 bytes
[[nodiscard]] WireError encode_private_message(
    const PrivateMessage& message, uint8_t* out, std::size_t capacity, std::size_t* written
) noexcept;

/// Tells whether the rules hand a private message to its handler.
///
/// The sub-id dispatch takes integrity checks and votes; the integrity-only
/// form takes integrity messages whose op is 1..7, 0x20 or 0x21.
///
/// @param channel the rules' private channel
/// @param message the message
/// @return true when a handler reads it; false drops it unseen
[[nodiscard]] bool
private_message_accepted(PrivateChannel channel, const PrivateMessage& message) noexcept;

} // namespace oa::netgame
