// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Whitespace token lines and the named-command table that console input and
// command scripts are dispatched through.

#include <cstddef>
#include <cstdint>

namespace oa::ui::services {

// A token line: up to 20 tokens pointing into the line's own text buffer,
// and their count.
inline constexpr int32_t token_slot_count = 20;
// Token text is copied while fewer than this many bytes of the buffer are used.
inline constexpr std::size_t token_text_limit = 126;

struct TokenLine {
    const char* tokens[token_slot_count];
    char text[token_text_limit + 2];
    int32_t count;
};

/// Empties a token line.
///
/// @param[out] line token line whose count is reset to 0
void token_line_clear(TokenLine* line) noexcept;

/// Splits text into whitespace-separated tokens.
///
/// Tokens are separated by C-locale whitespace; a token starting with '#', or
/// '#' inside a token, ends the line. Tokens past the twentieth are copied but
/// not recorded. When the text buffer fills in the middle of a token, parsing
/// stops there, keeping the truncated token.
///
/// @param[out] line token line that receives the tokens and their text
/// @param begin first character of the text
/// @param end one past the last character, or null for the NUL-terminated length of begin
void token_line_parse(TokenLine* line, const char* begin, const char* end) noexcept;

/// Returns one token of a line.
///
/// @param line parsed token line
/// @param index token index
/// @param fallback value returned when index is outside [0, count)
/// @return the token text, or fallback
[[nodiscard]] const char*
token_line_get(const TokenLine* line, int32_t index, const char* fallback) noexcept;
/// Returns one token parsed as a decimal integer (see parse_int).
///
/// @param line parsed token line
/// @param index token index
/// @param fallback value returned when index is outside [0, count)
/// @return the parsed value, or fallback
[[nodiscard]] int32_t
token_line_get_int(const TokenLine* line, int32_t index, int32_t fallback) noexcept;
/// Returns one token parsed as a decimal floating-point number (see parse_double).
///
/// @param line parsed token line
/// @param index token index
/// @param fallback value returned when index is outside [0, count)
/// @return the parsed value, or fallback widened to double
[[nodiscard]] double
token_line_get_double(const TokenLine* line, int32_t index, float fallback) noexcept;

/// Replaces argument references in a line with the caller's tokens.
///
/// Every token of the form %N becomes token N of arguments when
/// 0 <= N < arguments->count; other %N tokens are left as they are.
///
/// @param[in,out] line token line whose tokens are rewritten
/// @param arguments tokens substituted for %N, or null for none
void token_line_expand_arguments(TokenLine* line, const TokenLine* arguments) noexcept;

/// Parses a decimal integer with leading whitespace and an optional sign.
///
/// @param text NUL-terminated text
/// @return the value; overflow wraps modulo 2^32 and parsing stops at the first non-digit
[[nodiscard]] int32_t parse_int(const char* text) noexcept;
/// Parses a decimal floating-point prefix (sign, digits, fraction, exponent).
///
/// Hexadecimal, inf and nan forms are not accepted.
///
/// @param text NUL-terminated text
/// @return the value, or 0 when no number is present
[[nodiscard]] double parse_double(const char* text) noexcept;

/// Finds the start of a numbered line in a text buffer.
///
/// Both '\\n' and NUL end a line.
///
/// @param text text to scan
/// @param length bytes of text; scanning stops here
/// @param line 0-based line number
/// @return byte offset of the line's first character, or length when the text ends first
[[nodiscard]] std::size_t
text_line_offset(const char* text, std::size_t length, int32_t line) noexcept;

using CommandHandler = void (*)(TokenLine* line);

inline constexpr std::size_t command_name_capacity = 48;
inline constexpr std::size_t command_table_capacity = 256;

struct CommandEntry {
    char name[command_name_capacity];
    CommandHandler handler;
    uint32_t mask; // command classes the handler accepts
};

// Registration record of the static command lists: {name, handler, mask},
// terminated by a null name.
struct CommandRegistration {
    const char* name;
    CommandHandler handler;
    uint32_t mask;
};

// Case-insensitive name -> (handler, mask) map plus an optional fallback.
struct CommandTable {
    CommandEntry entries[command_table_capacity];
    std::size_t count;
    CommandHandler fallback;
    uint32_t fallback_mask;
};

/// Inserts one command, or replaces the entry with the same name ignoring case.
///
/// @param[in,out] table command table
/// @param name command name, shorter than command_name_capacity
/// @param handler function run when the command is dispatched
/// @param mask command classes the handler accepts
/// @return false when the name is too long or the table is full
bool command_table_set(
    CommandTable* table, const char* name, CommandHandler handler, uint32_t mask
) noexcept;
/// Registers a null-name-terminated list of commands in order.
///
/// @param[in,out] table command table
/// @param list registrations ending with an entry whose name is null
/// @return false when any entry was refused; the others are still registered
bool command_table_register(CommandTable* table, const CommandRegistration* list) noexcept;
/// Sets the handler run for lines whose first token names no command.
///
/// @param[in,out] table command table
/// @param handler fallback handler
/// @param mask command classes the fallback accepts
void command_table_set_fallback(
    CommandTable* table, CommandHandler handler, uint32_t mask
) noexcept;
/// Removes the fallback handler.
///
/// @param[in,out] table command table
void command_table_clear_fallback(CommandTable* table) noexcept;

/// Runs the command named by a line's first token.
///
/// Token 0 is looked up ignoring case. When its mask shares a bit with mask
/// the handler runs; otherwise the fallback runs under the same test.
///
/// @param table command table
/// @param[in,out] line parsed line passed to the handler
/// @param mask command classes the caller allows
/// @return the full mask of the handler that ran, or 0 when nothing ran or the line is empty
uint32_t command_dispatch(const CommandTable* table, TokenLine* line, uint32_t mask) noexcept;

/// Tokenises, expands %N arguments in and dispatches each line of a script.
///
/// @param table command table
/// @param text script text; lines end at '\\n'
/// @param length bytes of text
/// @param arguments tokens substituted for %N, or null for none
/// @param mask command classes the caller allows
/// @return the OR of every line's dispatch result
uint32_t command_run_script(
    const CommandTable* table,
    const char* text,
    int32_t length,
    const TokenLine* arguments,
    uint32_t mask
) noexcept;

} // namespace oa::ui::services
