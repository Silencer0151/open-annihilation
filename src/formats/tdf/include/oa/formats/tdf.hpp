// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// TDF/FBI/GUI text documents, parsed the way the game parses them.
//
// A document is a tree of blocks. Each block has a name, an ordered list of
// child blocks and a property list kept sorted by case-insensitive key. All
// storage lives in one bounded arena owned by the document.
#pragma once

#include <cstddef>
#include <cstdint>

namespace oa::formats::tdf {

// Upper bounds applied before any allocation.
inline constexpr uint32_t max_input_bytes = 16u * 1024u * 1024u;
inline constexpr uint32_t max_arena_bytes = 64u * 1024u * 1024u;
inline constexpr uint32_t max_nesting_depth = 64;

struct Property {
    char* key;
    char* value;
};

struct Block {
    char* name;
    Block** children;
    uint32_t child_count;
    uint32_t child_capacity;
    Property* properties; // sorted by case-insensitive key
    uint32_t property_count;
    uint32_t property_capacity;
    // Buffer hash of the block body; see parse_text for the covered span.
    uint32_t body_hash;
};

struct ArenaChunk;

struct Arena {
    ArenaChunk* head;
    uint32_t used_bytes;
};

// A parsed TDF document: the root plus a cursor used by the section-stepping
// helpers. A null cursor means "at the root".
struct Document {
    Block* root;
    const Block* cursor;
    bool from_archive; // file was read from an HPI rather than a loose file
    Arena arena;
};

enum class ParseStatus : uint8_t {
    ok,
    sub_record_close_missing, // "sub-record name is never closed by ']'"
    sub_record_open_missing,  // "no '{' follows the sub-record name"
    equals_missing,           // "entry lacks '=' between key and value"
    semicolon_missing,        // "entry lacks its closing ';'"
    unexpected_end,           // "end of file inside a sub-record"
    too_deep,
    too_large,
    out_of_memory,
};

struct ParseError {
    ParseStatus status;
    uint32_t offset;     // byte offset into the comment-stripped text
    char block_name[64]; // block being parsed when the error was raised
};

/// Returns a short English description of a parse status.
///
/// @param status parse status
/// @return a static string
[[nodiscard]] const char* parse_status_message(ParseStatus status) noexcept;

/// Makes a document empty with no arena storage.
///
/// @param[out] document document to initialize
void document_init(Document* document) noexcept;
/// Frees a document's arena and clears its root and cursor.
///
/// @param[in,out] document document to empty; it can be parsed into again
void document_free(Document* document) noexcept;

/// Replaces a document's contents with a parse of a text.
///
/// The text is copied and NUL-terminated, its comments are stripped, and the
/// root block is parsed. On failure the document is left empty.
///
/// @param[in,out] document document that receives the tree
/// @param text TDF text; need not be NUL-terminated
/// @param length bytes of text, at most max_input_bytes
/// @param from_archive recorded in Document::from_archive
/// @param[out] error status, offset and block name of a failure; may be null
/// @return true when the text parsed
[[nodiscard]] bool parse_text(
    Document* document, const char* text, uint32_t length, bool from_archive, ParseError* error
) noexcept;

/// Blanks // and /* */ comments with spaces in place.
///
/// The newline ending a line comment survives; an unterminated block comment
/// blanks everything to the end.
///
/// @param[in,out] text NUL-terminated text
void strip_comments(char* text) noexcept;

/// Hashes a byte range the way block bodies are hashed.
///
/// @param data first byte
/// @param length bytes hashed; non-positive hashes nothing
/// @return byte lanes: sum, xor, sum of (index ^ byte) and xor of (index + byte),
///         lowest lane first, with index taken modulo 256
[[nodiscard]] uint32_t buffer_hash(const uint8_t* data, int32_t length) noexcept;

/// Compares two strings ignoring ASCII case, folding to lower case.
///
/// @param left first NUL-terminated string
/// @param right second NUL-terminated string
/// @return negative, zero or positive as left sorts before, with or after right
[[nodiscard]] int compare_nocase(const char* left, const char* right) noexcept;

/// Parses a TDF integer the way the game does.
///
/// @param text NUL-terminated value
/// @return the value; leading space and one sign are accepted and overflow wraps modulo 2^32
[[nodiscard]] int32_t parse_int(const char* text) noexcept;
/// Parses a TDF floating-point value the way the game does.
///
/// Only [sign] digits [. digits] [(e|E|d|D) [sign] digits] is accepted.
///
/// @param text NUL-terminated value
/// @return the value, or 0 when no digits are present
[[nodiscard]] double parse_double(const char* text) noexcept;
/// Truncates a double toward zero to a 64-bit integer, as 3.1c converts TDF numbers.
///
/// @param value value to convert
/// @return the truncated value; out-of-range and NaN give INT64_MIN
[[nodiscard]] int64_t truncate_to_int64(double value) noexcept;

/// Finds the first child block whose name matches, ignoring ASCII case.
///
/// @param block parent block, or null
/// @param name child name
/// @return the child, or null
[[nodiscard]] const Block* find_child(const Block* block, const char* name) noexcept;
/// Looks up a field value, ignoring ASCII case.
///
/// @param block block searched, or null
/// @param key field name
/// @return the value of the newest case variant of key, or null when missing
[[nodiscard]] const char* find_value(const Block* block, const char* key) noexcept;
/// Reads an integer field (see parse_int).
///
/// @param block block searched, or null
/// @param key field name
/// @param fallback value returned when the field is missing
/// @return the value
[[nodiscard]] int32_t get_int(const Block* block, const char* key, int32_t fallback) noexcept;
/// Reads a floating-point field (see parse_double).
///
/// @param block block searched, or null
/// @param key field name
/// @param fallback value returned when the field is missing
/// @return the value
[[nodiscard]] double get_double(const Block* block, const char* key, double fallback) noexcept;
/// Reads a field as 16.16 fixed point.
///
/// @param block block searched, or null
/// @param key field name
/// @param fallback value returned as given, unscaled, when the field is missing
/// @return the low 32 bits of the value times 65536, truncated toward zero to 64 bits
[[nodiscard]] int32_t get_fixed(const Block* block, const char* key, int32_t fallback) noexcept;
/// Copies a string field into a buffer.
///
/// @param block block searched, or null
/// @param key field name
/// @param[out] out buffer; padded with NULs like strncpy and always terminated
/// @param size bytes of out; 0 copies nothing and returns false
/// @param fallback copied, bounded by size, when the field is missing, or null for none
/// @return true when the field was found
bool get_string(
    const Block* block, const char* key, char* out, std::size_t size, const char* fallback
) noexcept;

/// Returns a block's number of child blocks.
///
/// @param block block, or null
/// @return the count; 0 for null
[[nodiscard]] uint32_t child_count(const Block* block) noexcept;
/// Returns one child block by position.
///
/// @param block block, or null
/// @param index child position, in file order
/// @return the child, or null past the end
[[nodiscard]] const Block* child_at(const Block* block, uint32_t index) noexcept;
/// Returns a block's number of fields.
///
/// @param block block, or null
/// @return the count; 0 for null
[[nodiscard]] uint32_t property_count(const Block* block) noexcept;
/// Returns the key at a position in case-insensitive key order.
///
/// @param block block, or null
/// @param index property position
/// @return the key, or null when index is negative or past the end
[[nodiscard]] const char* property_key_at(const Block* block, int32_t index) noexcept;

/// Moves the cursor back to the root.
///
/// @param[in,out] document document whose cursor is cleared
void reset_cursor(Document* document) noexcept;
/// Returns the section the cursor is on.
///
/// @param document document
/// @return the block, or null at the root
[[nodiscard]] const Block* cursor(const Document* document) noexcept;
/// Moves the cursor to a block.
///
/// @param[in,out] document document whose cursor is set
/// @param block block of this document, or null for the root
void set_cursor(Document* document, const Block* block) noexcept;
/// Moves the cursor to a named child of the cursor (or of the root).
///
/// @param[in,out] document document whose cursor moves
/// @param name child name, matched ignoring ASCII case
/// @return true when found; a miss resets the cursor to the root
bool select_section(Document* document, const char* name) noexcept;
/// Moves the cursor to the index-th child of the cursor (or of the root).
///
/// @param[in,out] document document whose cursor moves
/// @param index child position, in file order
/// @return true when the child exists; a miss resets the cursor to the root
bool step_entry(Document* document, uint32_t index) noexcept;

} // namespace oa::formats::tdf
