// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unicode chat's wire forms: the announcement in the setup block, the
// code-page form, the strict read of what a peer sent, and the parts a long
// line goes out as.

#include "oa/netgame/unicode_chat.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

using namespace oa::netgame;

namespace {

int failures = 0;
const char* current_test = "";

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            ++failures;                                                                            \
            std::fprintf(stderr, "%s: %s:%d: %s\n", current_test, __FILE__, __LINE__, #condition); \
        }                                                                                          \
    } while (0)

void start_case(const char* name) {
    current_test = name;
    std::printf("unicode chat: %s\n", name);
    std::fflush(stdout);
}

// Tells whether a text is well-formed UTF-8 by reading it strictly.
bool well_formed(std::string_view text) {
    return chat_strict_utf8(text) == text;
}

// The block carries 'U', '8' in chat_signature and bit 0 of chat_flags only
// while the setting is on; off, the record keeps its bytes. A block with the bit but
// without the signature, as a zero tail, says nothing.
void the_block_announces_utf8_chat() {
    start_case("the_block_announces_utf8_chat");
    PlayerInfoRecord record{};
    record.info_tail[player_info_recorder_protocol_offset - player_info_tail_offset] = 7;
    const auto before = record;
    announce_unicode_chat(record, false);
    CHECK(std::memcmp(&record, &before, sizeof record) == 0);
    announce_unicode_chat(record, true);
    uint8_t block[player_info_block_bytes]{};
    std::memcpy(block + player_info_tail_offset, record.info_tail, sizeof record.info_tail);
    CHECK(block[0xb4] == 7 && block[0xb5] == 'U' && block[0xb6] == '8' && block[0xb7] == 1);
    CHECK(block[0xb8] == 0);
    CHECK(announces_unicode_chat(block));
    block[0xb6] = 0;
    CHECK(!announces_unicode_chat(block));
    const uint8_t zero_tail[player_info_block_bytes]{};
    CHECK(!announces_unicode_chat(zero_tail));
}

// A machine without Unicode chat gets the code page: é as 0xe9, a hanzi as
// '?'. A peer's malformed and overlong sequences read as '?' and never
// reach the line as bytes.
void forms_and_strict_reading() {
    start_case("forms_and_strict_reading");
    CHECK(chat_code_page("<Zo\xc3\xa9> \xe4\xbd\xa0\xe5\xa5\xbd") == "<Zo\xe9> ??");
    CHECK(chat_utf8("<Zo\xe9> \xe4\xbd\xa0") == "<Zo\xc3\xa9> \xe4\xbd\xa0");
    CHECK(chat_strict_utf8("ok \xe4\xbd\xa0") == "ok \xe4\xbd\xa0");
    // Overlong '/', a surrogate, a cut sequence, a stray continuation, 0xff.
    CHECK(chat_strict_utf8("\xc0\xaf") == "??");
    CHECK(chat_strict_utf8("\xed\xa0\x80") == "???");
    CHECK(chat_strict_utf8("a\xe4\xbd") == "a??");
    CHECK(chat_strict_utf8("\x80z\xff") == "?z?");
    CHECK(chat_strict_utf8("\xf4\x90\x80\x80") == "????");
}

// A command, after the speaker's name or without one, goes as typed; a
// long line goes as parts, each "<Name> " and a part of at most 64 bytes,
// cut between whole characters and at a near space, and no more than four.
void long_lines_go_as_parts() {
    start_case("long_lines_go_as_parts");
    CHECK(chat_command("+SetShareMetal 5") && chat_command("<Alpha> .record x"));
    CHECK(!chat_command("<Alpha> a .x") && !chat_command(""));
    const std::string hanzi = "\xe4\xbd\xa0";
    std::string line = "<Alpha> ";
    for (int i = 0; i < 30; ++i)
        line += hanzi;
    auto parts = chat_parts(line, chat_record_text_bytes, chat_line_parts);
    CHECK(parts.size() == 2);
    std::string joined;
    for (const auto& part : parts) {
        CHECK(part.size() <= chat_record_text_bytes && well_formed(part));
        CHECK(part.rfind("<Alpha> ", 0) == 0);
        joined += part.substr(8);
    }
    CHECK(parts.size() == 2 && parts[0].size() == 8 + 18 * 3 && joined.size() == 30 * 3);
    std::string words = "<Alpha>";
    for (int i = 0; i < 11; ++i)
        words += " word";
    parts = chat_parts(words, chat_record_text_bytes, chat_line_parts);
    CHECK(parts.size() == 1);
    words += words.substr(7) + words.substr(7);
    parts = chat_parts(words, chat_record_text_bytes, chat_line_parts);
    CHECK(parts.size() == 3 && parts[0].size() == 62 && parts[2].size() == 8 + 54);
    CHECK(parts.size() == 3 && parts[1] == "<Alpha> " + words.substr(8, 54));
    const std::string flood(400, 'x');
    parts = chat_parts("<A> " + flood, chat_record_text_bytes, chat_line_parts);
    CHECK(parts.size() == chat_line_parts && parts.back().size() == chat_record_text_bytes);
    CHECK(chat_parts("", chat_record_text_bytes, chat_line_parts).size() == 1);
}

} // namespace

int main() {
    the_block_announces_utf8_chat();
    forms_and_strict_reading();
    long_lines_go_as_parts();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("unicode chat: all tests passed");
    return 0;
}
