// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/tdf.hpp"
#include "oa/base/game_math.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace oa::formats::tdf {
using base::game_math::truncate_to_int64;

struct ArenaChunk {
    ArenaChunk* next;
    uint32_t capacity;
    uint32_t used;
    // Storage follows the header.
};

namespace {

constexpr uint32_t arena_chunk_bytes = 64u * 1024u;
constexpr uint32_t arena_alignment = alignof(std::max_align_t);
constexpr std::size_t parsed_number_limit = 512;
constexpr double fixed_one = 65536.0; // 16.16 scale of get_fixed

[[nodiscard]] uint32_t align_up(uint32_t value) noexcept {
    return (value + arena_alignment - 1u) & ~(arena_alignment - 1u);
}

[[nodiscard]] uint32_t chunk_header_bytes() noexcept {
    return align_up(static_cast<uint32_t>(sizeof(ArenaChunk)));
}

void* arena_alloc(Arena* arena, std::size_t size) noexcept {
    if (size > max_arena_bytes)
        return nullptr;
    const auto need = align_up(static_cast<uint32_t>(size));
    ArenaChunk* chunk = arena->head;
    if (chunk == nullptr || chunk->capacity - chunk->used < need) {
        const auto capacity = need > arena_chunk_bytes ? need : arena_chunk_bytes;
        if (arena->used_bytes + capacity > max_arena_bytes)
            return nullptr;
        auto* raw = static_cast<unsigned char*>(std::malloc(chunk_header_bytes() + capacity));
        if (raw == nullptr)
            return nullptr;
        chunk = reinterpret_cast<ArenaChunk*>(raw);
        chunk->next = arena->head;
        chunk->capacity = capacity;
        chunk->used = 0;
        arena->head = chunk;
        arena->used_bytes += capacity;
    }
    auto* base = reinterpret_cast<unsigned char*>(chunk) + chunk_header_bytes();
    void* result = base + chunk->used;
    chunk->used += need;
    return result;
}

void arena_free(Arena* arena) noexcept {
    ArenaChunk* chunk = arena->head;
    while (chunk != nullptr) {
        ArenaChunk* next = chunk->next;
        std::free(chunk);
        chunk = next;
    }
    arena->head = nullptr;
    arena->used_bytes = 0;
}

char* arena_copy(Arena* arena, const char* begin, std::size_t length) noexcept {
    auto* copy = static_cast<char*>(arena_alloc(arena, length + 1));
    if (copy == nullptr)
        return nullptr;
    std::memcpy(copy, begin, length);
    copy[length] = '\0';
    return copy;
}

[[nodiscard]] bool is_separator(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

[[nodiscard]] bool is_c_space(unsigned char c) noexcept {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

[[nodiscard]] unsigned char fold_lower(unsigned char c) noexcept {
    return static_cast<unsigned char>(c - 'A') < 26u ? static_cast<unsigned char>(c + 0x20) : c;
}

struct Parser {
    Document* document;
    const char* text;
    ParseError* error;
};

bool fail(Parser* parser, ParseStatus status, const char* at, const char* block_name) noexcept {
    if (parser->error != nullptr) {
        parser->error->status = status;
        parser->error->offset = static_cast<uint32_t>(at - parser->text);
        std::strncpy(
            parser->error->block_name,
            block_name != nullptr ? block_name : "",
            sizeof parser->error->block_name - 1
        );
        parser->error->block_name[sizeof parser->error->block_name - 1] = '\0';
    }
    return false;
}

/// Copies a token with the TDF separators trimmed from both ends.
///
/// @param[in,out] arena arena the copy is allocated from
/// @param from first character of the token
/// @param to one past its last character
/// @return the NUL-terminated copy, or null when the arena is exhausted
char* copy_trimmed_token(Arena* arena, const char* from, const char* to) noexcept {
    while (*from != '\0' && is_separator(*from))
        ++from;
    if (from >= to)
        return arena_copy(arena, "", 0);
    const char* end = to;
    while (is_separator(end[-1]))
        --end;
    return arena_copy(arena, from, static_cast<std::size_t>(end - from));
}

template <typename T>
bool grow(Arena* arena, T** items, uint32_t count, uint32_t* capacity) noexcept {
    if (count < *capacity)
        return true;
    const uint32_t next = *capacity == 0 ? 8u : *capacity * 2u;
    auto* grown = static_cast<T*>(arena_alloc(arena, sizeof(T) * next));
    if (grown == nullptr)
        return false;
    if (count != 0)
        std::memcpy(grown, *items, sizeof(T) * count);
    *items = grown;
    *capacity = next;
    return true;
}

/// Finds the first property whose key is not less than a key, ignoring ASCII case.
///
/// @param block block searched
/// @param key key looked for
/// @return the property index, or property_count when every key is less
uint32_t lower_bound_key(const Block* block, const char* key) noexcept {
    uint32_t first = 0;
    uint32_t last = block->property_count;
    while (first != last) {
        const uint32_t middle = first + (last - first) / 2u;
        if (compare_nocase(block->properties[middle].key, key) < 0)
            first = middle + 1;
        else
            last = middle;
    }
    return first;
}

/// Returns the value slot for a key, inserting an empty one when no stored key is byte-identical.
///
/// Keys differing only in case get their own slot, placed ahead of the older
/// one so lookups see the newest.
///
/// @param[in,out] arena arena the property table grows in
/// @param[in,out] block block whose sorted property table is searched
/// @param key key, already copied into the arena
/// @return the value slot, or null when the arena is exhausted
char** find_or_add_property(Arena* arena, Block* block, char* key) noexcept {
    const uint32_t position = lower_bound_key(block, key);
    if (position != block->property_count && std::strcmp(block->properties[position].key, key) == 0)
        return &block->properties[position].value;
    if (!grow(arena, &block->properties, block->property_count, &block->property_capacity))
        return nullptr;
    std::memmove(
        &block->properties[position + 1],
        &block->properties[position],
        sizeof(Property) * (block->property_count - position)
    );
    block->properties[position].key = key;
    block->properties[position].value = nullptr;
    ++block->property_count;
    return &block->properties[position].value;
}

void finish_block(Block* block, const char* body, const char* stop) noexcept {
    // The hashed span stops two bytes short of the terminating '}' or NUL.
    block->body_hash = body <= stop - 1 ? buffer_hash(
                                              reinterpret_cast<const uint8_t*>(body),
                                              static_cast<int32_t>(stop - body - 2)
                                          )
                                        : 0u;
}

/// Parses a block body up to its '}' (or the end of text for the root).
///
/// Sub-records ("[name] { ... }") become children in order; "key=value;"
/// fields go into the sorted property table, a later duplicate key replacing
/// the value. The block's body hash is set when it ends.
///
/// @param[in,out] parser parser state; errors are recorded here
/// @param name block name
/// @param body first character after the opening '{', or the text start for the root
/// @param[out] end receives the character after the closing '}'; null for the
///        root, which also ends at the end of text
/// @param depth nesting depth, limited to max_nesting_depth
/// @return the block, or null after recording an error
/// @quirk A stray '}' also ends the root block; anything after it is ignored.
Block* parse_block(
    Parser* parser, const char* name, const char* body, const char** end, uint32_t depth
) noexcept {
    Arena* arena = &parser->document->arena;
    if (depth > max_nesting_depth) {
        fail(parser, ParseStatus::too_deep, body, name);
        return nullptr;
    }
    auto* block = static_cast<Block*>(arena_alloc(arena, sizeof(Block)));
    char* block_name = block != nullptr ? arena_copy(arena, name, std::strlen(name)) : nullptr;
    if (block_name == nullptr) {
        fail(parser, ParseStatus::out_of_memory, body, name);
        return nullptr;
    }
    std::memset(block, 0, sizeof *block);
    block->name = block_name;

    const char* at = body;
    for (;;) {
        while (*at != '\0' && is_separator(*at))
            ++at;
        if (*at == '\0') {
            if (end != nullptr) {
                fail(parser, ParseStatus::unexpected_end, at, name);
                return nullptr;
            }
            finish_block(block, body, at);
            return block;
        }
        if (*at == '[') {
            const char* close = std::strchr(at, ']');
            if (close == nullptr) {
                fail(parser, ParseStatus::sub_record_close_missing, at, name);
                return nullptr;
            }
            char* child_name = copy_trimmed_token(arena, at + 1, close);
            if (child_name == nullptr) {
                fail(parser, ParseStatus::out_of_memory, at, name);
                return nullptr;
            }
            const char* open = close;
            do {
                ++open;
            } while (*open != '\0' && is_separator(*open));
            at = open;
            if (*open != '{') {
                fail(parser, ParseStatus::sub_record_open_missing, at, name);
                return nullptr;
            }
            Block* child = parse_block(parser, child_name, open + 1, &at, depth + 1);
            if (child == nullptr)
                return nullptr;
            if (!grow(arena, &block->children, block->child_count, &block->child_capacity)) {
                fail(parser, ParseStatus::out_of_memory, at, name);
                return nullptr;
            }
            block->children[block->child_count++] = child;
            continue;
        }
        if (*at == '}') {
            // A stray '}' also ends the root block; anything after it is ignored.
            if (end != nullptr)
                *end = at + 1;
            finish_block(block, body, at);
            return block;
        }
        const char* equals = std::strchr(at, '=');
        if (equals == nullptr) {
            fail(parser, ParseStatus::equals_missing, at, name);
            return nullptr;
        }
        char* key = copy_trimmed_token(arena, at, equals);
        at = equals + 1;
        const char* semicolon = std::strchr(at, ';');
        if (semicolon == nullptr) {
            fail(parser, ParseStatus::semicolon_missing, at, name);
            return nullptr;
        }
        char* value = copy_trimmed_token(arena, at, semicolon);
        at = semicolon + 1;
        char** slot =
            key != nullptr && value != nullptr ? find_or_add_property(arena, block, key) : nullptr;
        if (slot == nullptr) {
            fail(parser, ParseStatus::out_of_memory, at, name);
            return nullptr;
        }
        *slot = value;
    }
}

} // namespace

const char* parse_status_message(ParseStatus status) noexcept {
    switch (status) {
    case ParseStatus::ok:
        return "ok";
    case ParseStatus::sub_record_close_missing:
        return "sub-record name is never closed by ']'";
    case ParseStatus::sub_record_open_missing:
        return "no '{' follows the sub-record name";
    case ParseStatus::equals_missing:
        return "entry lacks '=' between key and value";
    case ParseStatus::semicolon_missing:
        return "entry lacks its closing ';'";
    case ParseStatus::unexpected_end:
        return "end of file inside a sub-record";
    case ParseStatus::too_deep:
        return "sub-records nested too deeply";
    case ParseStatus::too_large:
        return "TDF text too large";
    case ParseStatus::out_of_memory:
        return "TDF arena exhausted";
    }
    return "unknown";
}

void document_init(Document* document) noexcept {
    document->root = nullptr;
    document->cursor = nullptr;
    document->from_archive = false;
    document->arena.head = nullptr;
    document->arena.used_bytes = 0;
}

void document_free(Document* document) noexcept {
    arena_free(&document->arena);
    document->root = nullptr;
    document->cursor = nullptr;
}

bool parse_text(
    Document* document, const char* text, uint32_t length, bool from_archive, ParseError* error
) noexcept {
    document_free(document);
    document->from_archive = from_archive;
    if (error != nullptr) {
        error->status = ParseStatus::ok;
        error->offset = 0;
        error->block_name[0] = '\0';
    }
    Parser parser{document, nullptr, error};
    if (length > max_input_bytes) {
        parser.text = "";
        return fail(&parser, ParseStatus::too_large, parser.text, "");
    }
    auto* copy = static_cast<char*>(arena_alloc(&document->arena, std::size_t{length} + 1u));
    if (copy == nullptr) {
        parser.text = "";
        return fail(&parser, ParseStatus::out_of_memory, parser.text, "");
    }
    if (length != 0)
        std::memcpy(copy, text, length);
    copy[length] = '\0';
    strip_comments(copy);
    parser.text = copy;
    document->root = parse_block(&parser, "", copy, nullptr, 0);
    if (document->root == nullptr) {
        document_free(document);
        return false;
    }
    return true;
}

void strip_comments(char* text) noexcept {
    char* at = text;
    while (*at != '\0') {
        if (at[0] == '/' && at[1] == '/') {
            while (*at != '\0' && *at != '\n')
                *at++ = ' ';
            continue;
        }
        if (at[0] == '/' && at[1] == '*') {
            at[0] = ' ';
            at[1] = ' ';
            at += 2;
            // An unterminated block comment blanks everything up to the end.
            for (; *at != '\0'; ++at) {
                if (at[-1] == '*' && at[0] == '/') {
                    at[-1] = ' ';
                    at[0] = ' ';
                    break;
                }
                at[-1] = ' ';
            }
            if (*at == '\0')
                break;
        }
        ++at;
    }
}

uint32_t buffer_hash(const uint8_t* data, int32_t length) noexcept {
    uint8_t sum = 0;
    uint8_t exclusive = 0;
    uint8_t index_sum = 0;
    uint8_t index_exclusive = 0;
    for (int32_t index = 0; index < length; ++index) {
        const uint8_t byte = data[index];
        const auto low = static_cast<uint8_t>(index);
        sum = static_cast<uint8_t>(sum + byte);
        exclusive = static_cast<uint8_t>(exclusive ^ byte);
        index_sum = static_cast<uint8_t>(index_sum + (low ^ byte));
        index_exclusive = static_cast<uint8_t>(index_exclusive ^ static_cast<uint8_t>(low + byte));
    }
    return uint32_t{sum} | (uint32_t{exclusive} << 8) | (uint32_t{index_sum} << 16) |
           (uint32_t{index_exclusive} << 24);
}

int compare_nocase(const char* left, const char* right) noexcept {
    for (;;) {
        const auto a = static_cast<unsigned char>(*left++);
        const auto b = static_cast<unsigned char>(*right++);
        if (a == b) {
            if (a == 0)
                return 0;
            continue;
        }
        const unsigned char fa = fold_lower(a);
        const unsigned char fb = fold_lower(b);
        if (fa != fb)
            return fa < fb ? -1 : 1;
    }
}

int32_t parse_int(const char* text) noexcept {
    const auto* at = reinterpret_cast<const unsigned char*>(text);
    while (is_c_space(*at))
        ++at;
    const unsigned char sign = *at;
    if (sign == '-' || sign == '+')
        ++at;
    uint32_t total = 0;
    while (*at >= '0' && *at <= '9')
        total = total * 10u + static_cast<uint32_t>(*at++ - '0');
    if (sign == '-')
        total = 0u - total;
    return static_cast<int32_t>(total);
}

double parse_double(const char* text) noexcept {
    // Accepts only the game's number grammar: [sign] digits [. digits] [(e|E|d|D) [sign] digits].
    const auto* at = reinterpret_cast<const unsigned char*>(text);
    while (is_c_space(*at))
        ++at;
    char buffer[parsed_number_limit];
    std::size_t used = 0;
    const auto put = [&](char c) {
        if (used + 1 < sizeof buffer)
            buffer[used++] = c;
    };
    if (*at == '-' || *at == '+')
        put(static_cast<char>(*at++));
    bool digits = false;
    while (*at >= '0' && *at <= '9') {
        put(static_cast<char>(*at++));
        digits = true;
    }
    if (*at == '.') {
        put('.');
        ++at;
        while (*at >= '0' && *at <= '9') {
            put(static_cast<char>(*at++));
            digits = true;
        }
    }
    if (!digits)
        return 0.0;
    if (*at == 'e' || *at == 'E' || *at == 'd' || *at == 'D') {
        const unsigned char* exponent = at + 1;
        if (*exponent == '-' || *exponent == '+')
            ++exponent;
        if (*exponent >= '0' && *exponent <= '9') {
            put('e');
            ++at;
            if (*at == '-' || *at == '+')
                put(static_cast<char>(*at++));
            while (*at >= '0' && *at <= '9')
                put(static_cast<char>(*at++));
        }
    }
    buffer[used] = '\0';
    return std::strtod(buffer, nullptr);
}

const Block* find_child(const Block* block, const char* name) noexcept {
    if (block == nullptr)
        return nullptr;
    for (uint32_t index = 0; index < block->child_count; ++index)
        if (compare_nocase(block->children[index]->name, name) == 0)
            return block->children[index];
    return nullptr;
}

const char* find_value(const Block* block, const char* key) noexcept {
    if (block == nullptr)
        return nullptr;
    const uint32_t position = lower_bound_key(block, key);
    if (position != block->property_count &&
        compare_nocase(key, block->properties[position].key) >= 0)
        return block->properties[position].value;
    return nullptr;
}

int32_t get_int(const Block* block, const char* key, int32_t fallback) noexcept {
    const char* value = find_value(block, key);
    return value != nullptr ? parse_int(value) : fallback;
}

double get_double(const Block* block, const char* key, double fallback) noexcept {
    const char* value = find_value(block, key);
    return value != nullptr ? parse_double(value) : fallback;
}

int32_t get_fixed(const Block* block, const char* key, int32_t fallback) noexcept {
    const char* value = find_value(block, key);
    if (value == nullptr)
        return fallback;
    return static_cast<int32_t>(
        static_cast<uint64_t>(truncate_to_int64(parse_double(value) * fixed_one))
    );
}

bool get_string(
    const Block* block, const char* key, char* out, std::size_t size, const char* fallback
) noexcept {
    if (size == 0)
        return false;
    const char* value = find_value(block, key);
    if (value == nullptr) {
        if (fallback != nullptr) {
            std::strncpy(out, fallback, size - 1);
            out[size - 1] = '\0';
        }
        return false;
    }
    std::strncpy(out, value, size);
    out[size - 1] = '\0';
    return true;
}

uint32_t child_count(const Block* block) noexcept {
    return block != nullptr ? block->child_count : 0u;
}

const Block* child_at(const Block* block, uint32_t index) noexcept {
    if (block == nullptr || index >= block->child_count)
        return nullptr;
    return block->children[index];
}

uint32_t property_count(const Block* block) noexcept {
    return block != nullptr ? block->property_count : 0u;
}

const char* property_key_at(const Block* block, int32_t index) noexcept {
    if (block == nullptr || index < 0 || static_cast<uint32_t>(index) >= block->property_count)
        return nullptr;
    return block->properties[index].key;
}

void reset_cursor(Document* document) noexcept {
    document->cursor = nullptr;
}

const Block* cursor(const Document* document) noexcept {
    return document->cursor;
}

void set_cursor(Document* document, const Block* block) noexcept {
    document->cursor = block;
}

bool select_section(Document* document, const char* name) noexcept {
    const Block* scope = document->cursor != nullptr ? document->cursor : document->root;
    document->cursor = find_child(scope, name);
    return document->cursor != nullptr;
}

bool step_entry(Document* document, uint32_t index) noexcept {
    const Block* scope = document->cursor != nullptr ? document->cursor : document->root;
    document->cursor = child_at(scope, index);
    return document->cursor != nullptr;
}

} // namespace oa::formats::tdf
