// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Unicode multiplayer chat: chat lines in UTF-8 between machines that say
// they read it, in the same 65-byte 0x05 record 3.1c sends.
//
// A machine with Unicode chat on says so in the setup block it sends
// (record 0x20), in bytes 3.1c carries unchanged and never reads: 'U' and
// '8' in chat_signature, and bit 0 of chat_flags, which says it sends and
// reads UTF-8 chat.
//
// Each machine then gets a line in one form: UTF-8 when its block says so,
// else the game's 8-bit code page with '?' for each character the code page
// lacks. A line longer than one record goes as up to chat_line_parts
// records, each "<Name> " and a part, cut between whole characters.

#include "oa/netgame/records.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace oa::netgame {

/// Block offset of the two signature bytes, 'U' and '8', that make the
/// flags byte after them the sender's chat flags.
inline constexpr std::size_t player_info_chat_signature_offset = 0xb5;
/// Block offset of the sender's chat flags (chat_flag_utf8).
inline constexpr std::size_t player_info_chat_flags_offset = 0xb7;
/// The signature's bytes.
inline constexpr uint8_t chat_signature_first = 'U';
inline constexpr uint8_t chat_signature_second = '8';
/// Chat flag: the sender sends and reads chat lines as UTF-8.
inline constexpr uint8_t chat_flag_utf8 = 0x01;
/// The most records one chat line goes out as while Unicode chat is on.
inline constexpr std::size_t chat_line_parts = 4;
/// The bytes of a line one chat record carries.
inline constexpr std::size_t chat_record_text_bytes = sizeof(ChatRecord::text);

/// Writes the Unicode chat announcement into a setup block about to be sent.
///
/// @param[in,out] record the block, as record 0x20 carries it
/// @param utf8 the machine sends and reads UTF-8 chat; without it the
///        block's bytes are left as they are
void announce_unicode_chat(PlayerInfoRecord& record, bool utf8) noexcept;

/// Sets or clears the UTF-8 chat flag of a setup block kept with a
/// recording: on writes the signature and the flag, off clears the flag.
///
/// @param[in,out] block the 0xb9-byte block
/// @param utf8 the player's lines are kept in UTF-8
void mark_unicode_chat(uint8_t* block, bool utf8) noexcept;

/// Tells whether a setup block says its sender sends and reads UTF-8 chat.
///
/// @param block the 0xb9-byte block, as a player's PlayerSetupInfo holds it
/// @return true when the signature and chat_flag_utf8 are set
[[nodiscard]] bool announces_unicode_chat(const uint8_t* block) noexcept;

/// Tells whether a chat line is a command: '+' or '.' first, after the
/// "<Name> " prefix when the line has one. The game and the recorders read
/// commands, so one goes out in one record as typed.
///
/// @param line the line
/// @return true for a command
[[nodiscard]] bool chat_command(std::string_view line) noexcept;

/// Reads game text as UTF-8: each well-formed UTF-8 sequence as itself,
/// every other byte as its character in the game's code page.
///
/// @param game_text the line as this machine holds it
/// @return well-formed UTF-8
[[nodiscard]] std::string chat_utf8(std::string_view game_text);

/// Writes UTF-8 text in the game's code page: each character as its byte,
/// '?' for one the code page lacks.
///
/// @param utf8 UTF-8 text
/// @return the code-page form, at most as long as the text
[[nodiscard]] std::string chat_code_page(std::string_view utf8);

/// Reads a received line strictly as UTF-8: each byte that starts no
/// well-formed sequence (overlong, surrogate, past U+10FFFF, cut short or a
/// stray continuation) becomes '?'.
///
/// @param bytes the line as received
/// @return well-formed UTF-8, at most as long as the line
[[nodiscard]] std::string chat_strict_utf8(std::string_view bytes);

/// Cuts a UTF-8 chat line into the records it goes out as.
///
/// The first part is the line's start; each further part starts with the
/// line's "<Name> " (or "<Name->Target> ") prefix. Each part holds at most
/// `record_bytes` bytes, ends between whole characters, and ends at a
/// space when one lies within the part's last 16 bytes; that space is not
/// sent. What does not fit in `most` parts is left out.
///
/// @param line well-formed UTF-8
/// @param record_bytes the bytes one record carries
/// @param most the most parts
/// @return the parts, one for an empty line
[[nodiscard]] std::vector<std::string>
chat_parts(std::string_view line, std::size_t record_bytes, std::size_t most);

} // namespace oa::netgame
