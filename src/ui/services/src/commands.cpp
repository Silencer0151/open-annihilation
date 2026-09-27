// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/services/commands.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace oa::ui::services {
namespace {

constexpr char comment_marker = '#';
constexpr char argument_marker = '%';
constexpr std::size_t number_scratch = 128;

bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

char lower_ascii(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equal_nocase(const char* a, const char* b) noexcept {
    for (;; ++a, ++b) {
        if (lower_ascii(*a) != lower_ascii(*b)) {
            return false;
        }
        if (*a == '\0') {
            return true;
        }
    }
}

const CommandEntry* find_command(const CommandTable* table, const char* name) noexcept {
    for (std::size_t i = 0; i < table->count; ++i) {
        if (equal_nocase(table->entries[i].name, name)) {
            return &table->entries[i];
        }
    }
    return nullptr;
}

} // namespace

void token_line_clear(TokenLine* line) noexcept {
    line->count = 0;
}

void token_line_parse(TokenLine* line, const char* begin, const char* end) noexcept {
    if (end == nullptr) {
        end = begin + std::strlen(begin);
    }
    std::size_t used = 0;
    line->count = 0;
    while (begin != end) {
        while (begin != end && is_space(*begin)) {
            ++begin;
        }
        if (begin == end || *begin == comment_marker) {
            return;
        }
        if (line->count < token_slot_count) {
            line->tokens[line->count] = &line->text[used];
            ++line->count;
        }
        bool buffer_full = false;
        while (begin != end && !is_space(*begin) && *begin != comment_marker) {
            if (used >= token_text_limit) {
                buffer_full = true;
                break;
            }
            line->text[used++] = *begin++;
        }
        line->text[used++] = '\0';
        if (buffer_full) {
            return;
        }
    }
}

const char* token_line_get(const TokenLine* line, int32_t index, const char* fallback) noexcept {
    if (index > -1 && index < line->count) {
        return line->tokens[index];
    }
    return fallback;
}

int32_t token_line_get_int(const TokenLine* line, int32_t index, int32_t fallback) noexcept {
    if (index > -1 && index < line->count) {
        return parse_int(line->tokens[index]);
    }
    return fallback;
}

double token_line_get_double(const TokenLine* line, int32_t index, float fallback) noexcept {
    if (index < 0 || line->count <= index) {
        return fallback;
    }
    return parse_double(line->tokens[index]);
}

void token_line_expand_arguments(TokenLine* line, const TokenLine* arguments) noexcept {
    for (int32_t i = 0; i < line->count; ++i) {
        if (line->tokens[i][0] != argument_marker) {
            continue;
        }
        const int32_t index = parse_int(line->tokens[i] + 1);
        if (arguments != nullptr && index > -1 && index < arguments->count) {
            line->tokens[i] = arguments->tokens[index];
        }
    }
}

int32_t parse_int(const char* text) noexcept {
    while (is_space(*text)) {
        ++text;
    }
    const char sign = *text;
    if (sign == '-' || sign == '+') {
        ++text;
    }
    uint32_t total = 0;
    while (is_digit(*text)) {
        total = total * 10u + static_cast<uint32_t>(*text - '0');
        ++text;
    }
    return static_cast<int32_t>(sign == '-' ? 0u - total : total);
}

double parse_double(const char* text) noexcept {
    while (is_space(*text)) {
        ++text;
    }
    // Copy only the plain decimal grammar so host extensions (hex, inf, nan)
    // are not accepted.
    char scratch[number_scratch];
    std::size_t length = 0;
    auto take = [&](char c) {
        if (length + 1 < number_scratch) {
            scratch[length++] = c;
        }
    };
    if (*text == '-' || *text == '+') {
        take(*text++);
    }
    while (is_digit(*text)) {
        take(*text++);
    }
    if (*text == '.') {
        take(*text++);
        while (is_digit(*text)) {
            take(*text++);
        }
    }
    if ((*text == 'e' || *text == 'E') &&
        (is_digit(text[1]) || ((text[1] == '-' || text[1] == '+') && is_digit(text[2])))) {
        take(*text++);
        if (*text == '-' || *text == '+') {
            take(*text++);
        }
        while (is_digit(*text)) {
            take(*text++);
        }
    }
    scratch[length] = '\0';
    return std::strtod(scratch, nullptr);
}

std::size_t text_line_offset(const char* text, std::size_t length, int32_t line) noexcept {
    std::size_t offset = 0;
    int32_t seen = 0;
    while (seen != line && offset < length) {
        if (text[offset] == '\0' || text[offset] == '\n') {
            ++seen;
        }
        ++offset;
    }
    return offset;
}

bool command_table_set(
    CommandTable* table, const char* name, CommandHandler handler, uint32_t mask
) noexcept {
    if (std::strlen(name) >= command_name_capacity) {
        return false;
    }
    CommandEntry* entry = const_cast<CommandEntry*>(find_command(table, name));
    if (entry == nullptr) {
        if (table->count >= command_table_capacity) {
            return false;
        }
        entry = &table->entries[table->count++];
        std::strcpy(entry->name, name);
    }
    entry->handler = handler;
    entry->mask = mask;
    return true;
}

bool command_table_register(CommandTable* table, const CommandRegistration* list) noexcept {
    bool all = true;
    for (; list->name != nullptr; ++list) {
        all = command_table_set(table, list->name, list->handler, list->mask) && all;
    }
    return all;
}

void command_table_set_fallback(
    CommandTable* table, CommandHandler handler, uint32_t mask
) noexcept {
    table->fallback = handler;
    table->fallback_mask = mask;
}

void command_table_clear_fallback(CommandTable* table) noexcept {
    table->fallback = nullptr;
    table->fallback_mask = 0;
}

uint32_t command_dispatch(const CommandTable* table, TokenLine* line, uint32_t mask) noexcept {
    if (line->count <= 0) {
        return 0;
    }
    const CommandEntry* entry = find_command(table, token_line_get(line, 0, ""));
    if (entry != nullptr && (mask & entry->mask) != 0) {
        entry->handler(line);
        return entry->mask;
    }
    if (table->fallback != nullptr && (mask & table->fallback_mask) != 0) {
        table->fallback(line);
        return table->fallback_mask;
    }
    return 0;
}

uint32_t command_run_script(
    const CommandTable* table,
    const char* text,
    int32_t length,
    const TokenLine* arguments,
    uint32_t mask
) noexcept {
    uint32_t result = 0;
    TokenLine line;
    token_line_clear(&line);
    while (length > 0) {
        int32_t line_length = length;
        for (int32_t i = 0; i < length; ++i) {
            if (text[i] == '\n') {
                line_length = i;
                break;
            }
        }
        token_line_parse(&line, text, text + line_length);
        token_line_expand_arguments(&line, arguments);
        result |= command_dispatch(table, &line, mask);
        text += line_length + 1;
        length -= line_length + 1;
    }
    return result;
}

} // namespace oa::ui::services
