// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/unicode_chat.hpp"

#include "oa/base/text.hpp"
#include "oa/present/game_text.hpp"

namespace oa::netgame {

namespace {

/// The first byte above ASCII.
constexpr uint8_t first_high_byte = 0x80;
/// The byte that stands in for what cannot be read or written.
constexpr char missing_character = '?';
/// How far back from a part's end a space still ends the part.
constexpr std::size_t space_reach = 16;
/// The bytes that end a chat line's speaker prefix.
constexpr std::string_view prefix_end = "> ";

/// Gives the bytes of a line's "<Name> " prefix, 0 when it has none.
std::size_t prefix_bytes(std::string_view line) noexcept {
    if (line.empty() || line.front() != '<')
        return 0;
    const auto end = line.find(prefix_end);
    return end == std::string_view::npos ? 0 : end + prefix_end.size();
}

/// Tells where a part of `rest` ends: at a space near the cut, else at the
/// cut between whole characters.
///
/// @param rest what is left of the line
/// @param room the bytes the part may hold
/// @param earliest the first place a space may end it
/// @param[out] skip the bytes after the end that are left out (the space)
/// @return the bytes the part keeps
std::size_t part_end(
    std::string_view rest, std::size_t room, std::size_t earliest, std::size_t* skip
) noexcept {
    *skip = 0;
    const auto cut = base::text::whole_characters(rest, room);
    if (cut == rest.size())
        return cut;
    const std::size_t lowest = cut > space_reach ? cut - space_reach : 0;
    for (std::size_t at = cut; at > earliest && at >= lowest; --at) {
        if (rest[at] == ' ') {
            *skip = 1;
            return at;
        }
    }
    return cut;
}

} // namespace

void announce_unicode_chat(PlayerInfoRecord& record, bool utf8) noexcept {
    if (!utf8)
        return;
    auto* tail = record.info_tail;
    tail[player_info_chat_signature_offset - player_info_tail_offset] = chat_signature_first;
    tail[player_info_chat_signature_offset + 1 - player_info_tail_offset] = chat_signature_second;
    tail[player_info_chat_flags_offset - player_info_tail_offset] |= chat_flag_utf8;
}

void mark_unicode_chat(uint8_t* block, bool utf8) noexcept {
    if (!utf8) {
        block[player_info_chat_flags_offset] &= static_cast<uint8_t>(~chat_flag_utf8);
        return;
    }
    block[player_info_chat_signature_offset] = chat_signature_first;
    block[player_info_chat_signature_offset + 1] = chat_signature_second;
    block[player_info_chat_flags_offset] |= chat_flag_utf8;
}

bool announces_unicode_chat(const uint8_t* block) noexcept {
    return block != nullptr && block[player_info_chat_signature_offset] == chat_signature_first &&
           block[player_info_chat_signature_offset + 1] == chat_signature_second &&
           (block[player_info_chat_flags_offset] & chat_flag_utf8) != 0;
}

bool chat_command(std::string_view line) noexcept {
    line.remove_prefix(prefix_bytes(line));
    return !line.empty() && (line.front() == '+' || line.front() == '.');
}

std::string chat_utf8(std::string_view game_text) {
    return present::decode_game_text(game_text, true);
}

std::string chat_code_page(std::string_view utf8) {
    return present::encode_game_text(utf8, false);
}

std::string chat_strict_utf8(std::string_view bytes) {
    std::string text;
    text.reserve(bytes.size());
    for (std::size_t at = 0; at < bytes.size();) {
        const auto byte = static_cast<uint8_t>(bytes[at]);
        if (byte < first_high_byte) {
            text.push_back(static_cast<char>(byte));
            ++at;
            continue;
        }
        const auto sequence = present::utf8_sequence(bytes.substr(at));
        if (sequence.bytes == 0) {
            text.push_back(missing_character);
            ++at;
            continue;
        }
        text.append(bytes.substr(at, sequence.bytes));
        at += sequence.bytes;
    }
    return text;
}

std::vector<std::string>
chat_parts(std::string_view line, std::size_t record_bytes, std::size_t most) {
    std::vector<std::string> parts;
    auto prefix = line.substr(0, prefix_bytes(line));
    // A prefix that leaves a part little room is not repeated.
    if (prefix.size() * 2 > record_bytes)
        prefix = {};
    std::string_view rest = line;
    while (parts.size() < most) {
        const bool first = parts.empty();
        const std::string_view head = first ? std::string_view{} : prefix;
        std::size_t skip = 0;
        const auto kept =
            part_end(rest, record_bytes - head.size(), first ? prefix.size() : 0, &skip);
        std::string part(head);
        part.append(rest.substr(0, kept));
        parts.push_back(std::move(part));
        rest.remove_prefix(kept + skip);
        if (rest.empty() || kept + skip == 0)
            break;
    }
    return parts;
}

} // namespace oa::netgame
