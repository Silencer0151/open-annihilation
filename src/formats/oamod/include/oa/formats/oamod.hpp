// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The strict YAML subset a mod profile (oamod.yaml) is written in: bytes in,
// a node tree out. Every reader of a profile that keeps to these rules
// agrees on what it means.
//
// - Encoding: UTF-8 without a byte order mark, at most max_input_bytes; one
//   document, whose top level is a mapping; at most max_nesting_depth levels
//   of mappings and sequences.
// - Grammar: block mappings (key: value) and block sequences (- item) nested
//   by space indentation, and flow mappings ({a: 1}) and flow sequences
//   ([1, 2]), which may span lines, freely mixed; plain, single-quoted and
//   double-quoted scalars, each on one line; '#' comments, which start a line
//   or follow white space; one optional '---' before the document and '...'
//   after it. Refused by name, with the line and column: tags (!), anchors
//   (&), aliases (*), merge keys (<<), directives (%), complex keys (?), block
//   scalars (| and >), a second document, a duplicate key, and a tab in
//   indentation.
// - Types: mappings, sequences, strings, numbers, booleans and null. Plain
//   scalars resolve as YAML 1.2's core schema does, with decimal numbers
//   only: null, Null, NULL, ~ and nothing are null; true, True, TRUE, false,
//   False and FALSE are booleans (yes, no, on and off are strings);
//   -?(0|[1-9][0-9]*) is an integer; the same followed by a fraction
//   (.digits) or an exponent (e or E, an optional sign, digits) is a decimal;
//   everything else, 0x10, 1_000, +1, .5 and 2024-12-01 among them, is a
//   string. A quoted scalar is always a string.
// - Numbers are kept exactly as written, never through binary floating
//   point: an integer within +-2^53, or a decimal of at most
//   max_significant_digits significant digits whose leading digit lies
//   within 10^+-max_decimal_exponent. Within those bounds each number is the
//   one IEEE 754 double the same text reads as, so readers that use doubles
//   agree with this one.
// - Keys are strings or integers: a plain key that reads as an integer
//   (71: unit.my-id) is an integer key, and any quoted key is a string key, so
//   71 and '71' are different keys. A plain key that reads as a boolean, null
//   or decimal is refused.
//
// Errors are returned as values naming the rule broken and where; nothing
// throws, and every read is bounded before anything is allocated for it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::formats::oamod {

/// The longest profile read, in bytes.
inline constexpr size_t max_input_bytes = size_t{256} * 1024;
/// The deepest nesting of mappings and sequences; the top-level mapping is depth 1.
inline constexpr uint32_t max_nesting_depth = 16;
/// The most nodes (scalars, mappings and sequences) one document holds.
inline constexpr size_t max_node_count = size_t{1} << 16;
/// The longest key or string scalar, in bytes of UTF-8.
inline constexpr size_t max_string_bytes = 4096;
/// The largest magnitude of an integer, 2^53: every integer up to it is exact as a double.
inline constexpr uint64_t max_integer_magnitude = uint64_t{1} << 53;
/// The most significant digits a decimal holds; any decimal of at most 15
/// significant digits reads back from its nearest double unchanged.
inline constexpr uint32_t max_significant_digits = 15;
/// The largest power of ten a decimal's leading digit may stand for, either way.
inline constexpr int32_t max_decimal_exponent = 300;

/// An exact number: (negative ? -1 : 1) x significand x 10^exponent.
///
/// The significand carries no trailing zeros, so equal numbers are stored
/// alike whatever their spelling (1.50, 1.5 and 15e-1); zero has significand
/// 0, exponent 0 and is never negative.
struct Number {
    bool negative{};        ///< below zero
    uint64_t significand{}; ///< the digits, without trailing zeros
    int32_t exponent{};     ///< the power of ten the significand is scaled by
    bool integer{};         ///< written as an integer: no point and no exponent
};

/// What a number's text gave.
enum class NumberStatus : uint8_t {
    ok,
    not_a_number,      ///< the text does not follow the number grammar
    integer_too_large, ///< an integer beyond +-max_integer_magnitude
    too_many_digits,   ///< a decimal with more than max_significant_digits significant digits
    out_of_range,      ///< a decimal whose leading digit is beyond 10^+-max_decimal_exponent
};

/// Reads a number written in the profile grammar:
/// -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][-+]?[0-9]+)?, an integer when it has
/// neither a fraction nor an exponent.
///
/// @param text the number's text, nothing around it
/// @param[out] number the number; unchanged unless the status is ok
/// @param leading_zeros whether the digits before the point may start with
///        zeros (0012), as values read from settings files may
/// @return ok, or why the text is not a number the profile can hold
[[nodiscard]] NumberStatus
parse_number(std::string_view text, Number& number, bool leading_zeros = false) noexcept;

/// Makes a number from an integer.
///
/// @param value the integer, within +-max_integer_magnitude
/// @return the number, marked as an integer
[[nodiscard]] Number number_from_integer(int64_t value) noexcept;

/// Compares two numbers by the values they stand for.
///
/// @param left the first number
/// @param right the second number
/// @return negative, zero or positive as `left` is below, equal to or above `right`
[[nodiscard]] int compare_numbers(const Number& left, const Number& right) noexcept;

/// Tells whether a number is a whole number, however it was written.
///
/// @param number the number
/// @param[out] value its value, when whole and within +-max_integer_magnitude
/// @return true when `value` was set
[[nodiscard]] bool integer_value(const Number& number, int64_t& value) noexcept;

/// Writes a number as RFC 8785 (the JSON canonicalization scheme) writes the
/// double it stands for: the shortest digits, plain between 10^-6 and 10^21,
/// otherwise with an exponent (1e+21, 1e-7); zero as 0.
///
/// @param number the number
/// @return its canonical text
[[nodiscard]] std::string canonical_number_text(const Number& number);

/// Converts a number to the nearest double.
///
/// Exact for every integer and for every decimal of at most 15 significant
/// digits whose power of ten is within 10^22 either way; further from one,
/// the last bit may differ from the nearest.
///
/// @param number the number
/// @return its value as a double
[[nodiscard]] double number_to_double(const Number& number) noexcept;

/// What a node of the tree holds.
enum class NodeKind : uint8_t {
    null_value,
    boolean,
    number,
    string,
    mapping,
    sequence,
};

/// What a mapping key is.
enum class KeyKind : uint8_t {
    string,  ///< a quoted key, or a plain key that is not a number
    integer, ///< a plain key that reads as an integer
};

/// Where something starts in the text: a 1-based line, and a 1-based column
/// counted in bytes from the start of the line.
struct TextPosition {
    uint32_t line{};
    uint32_t column{};
};

/// The key a mapping entry is written under.
struct Key {
    KeyKind kind{KeyKind::string};
    std::string text{};      ///< the key as a string: a string key's value, an integer key's digits
    int64_t integer{};       ///< an integer key's value
    bool quoted{};           ///< written in quotes
    TextPosition position{}; ///< where the key starts
};

/// One node of a document tree: a scalar, a mapping or a sequence.
///
/// A mapping's entries are its children in document order, each carrying
/// its key; a sequence's items are its children, whose keys are empty.
struct Node {
    NodeKind kind{NodeKind::null_value};
    TextPosition position{}; ///< where the value starts
    bool quoted{};           ///< a scalar written in quotes
    bool boolean{};          ///< a boolean's value
    Number number{};         ///< a number's value
    std::string text{};      ///< a string's value; the text of any other plain scalar as written
    Key key{};               ///< the key of a mapping entry
    std::vector<Node> children{}; ///< a mapping's entries or a sequence's items
};

/// The rule a text breaks.
enum class Rule : uint8_t {
    ok,
    too_large,       ///< longer than max_input_bytes
    byte_order_mark, ///< starts with a byte order mark
    invalid_utf8,    ///< a byte sequence that is not UTF-8
    control_character, ///< a control character other than tab, line feed and carriage return before a line feed
    unexpected_end,       ///< the text ends inside a value
    unexpected_character, ///< a character no rule allows there
    too_deep,             ///< nesting deeper than max_nesting_depth
    too_many_nodes,       ///< more than max_node_count nodes
    string_too_long,      ///< a key or string longer than max_string_bytes
    bad_escape,           ///< a backslash escape YAML does not define
    multi_line_scalar,    ///< a quoted scalar that does not end on its line
    integer_too_large,    ///< an integer beyond +-max_integer_magnitude
    too_many_digits,      ///< a decimal with more than max_significant_digits significant digits
    number_out_of_range,  ///< a decimal beyond 10^+-max_decimal_exponent
    duplicate_key,        ///< a key written twice in one mapping
    key_not_string,       ///< a key that reads as a boolean, null or decimal
    merge_key,            ///< a merge key (<<)
    trailing_content,     ///< text after the document
    not_mapping,          ///< the document's top level is not a mapping
    tab_indentation,      ///< a tab in indentation
    bad_indentation,      ///< indentation that nests nothing or breaks a level
    anchor,               ///< an anchor (&)
    alias,                ///< an alias (*)
    tag,                  ///< a tag (!)
    block_scalar,         ///< a block scalar (| or >)
    directive,            ///< a directive (%)
    complex_key,          ///< a complex key (?), or a collection used as a key
    several_documents,    ///< a second document
};

/// What went wrong reading a text, and where.
struct ReadError {
    Rule rule{Rule::ok};
    TextPosition position{};
};

/// Returns a short English description of a rule, such as "anchors (&) are
/// not allowed".
///
/// @param rule the rule
/// @return a static string
[[nodiscard]] const char* rule_message(Rule rule) noexcept;

/// Reads a profile's text into a document tree whose top level is a mapping.
///
/// @param text the profile's bytes
/// @param[out] root the top-level mapping; a null node on failure
/// @param[out] error the rule broken, and where; rule ok on success
/// @return true when the whole text was read
[[nodiscard]] bool read_document(std::span<const uint8_t> text, Node& root, ReadError& error);

/// Reads one value under the same rules, whatever its kind: a scalar, a
/// flow collection or a block collection. Used for values given outside a
/// profile, such as a host's option for one game.
///
/// @param text the value's bytes
/// @param[out] root the value; a null node on failure
/// @param[out] error the rule broken, and where; rule ok on success
/// @return true when the whole text was read
[[nodiscard]] bool read_value(std::span<const uint8_t> text, Node& root, ReadError& error);

/// Returns the entry of a mapping under a string key.
///
/// @param mapping the mapping to search
/// @param key the key, compared byte for byte
/// @return the entry, or null when `mapping` is not a mapping or has no such string key
[[nodiscard]] const Node* find_entry(const Node& mapping, std::string_view key) noexcept;

} // namespace oa::formats::oamod
