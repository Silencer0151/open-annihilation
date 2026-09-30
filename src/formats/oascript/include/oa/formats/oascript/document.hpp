// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The document tree of a director script (.oascript) and the two texts it is
// read from and written as: strict JSON (RFC 8259) and a subset of YAML 1.2.
//
// The first byte that is not white space, after an optional UTF-8 byte order
// mark, chooses the form: '{' reads JSON, anything else the YAML subset. Both
// give the same tree. Numbers are exact decimals, read and written digit by
// digit: no binary floating point touches them, so a script reads and
// writes alike on every platform.
//
// The YAML subset holds block mappings (key: value) and block sequences
// (- item) nested by indentation with spaces; flow mappings ({ a: 1 }) and
// flow sequences ([1, 2]), which may span lines; plain, single-quoted and
// double-quoted scalars; '#' comments; and one optional '---' before the
// document. Plain scalars resolve as YAML 1.2's core schema resolves them:
// null, ~ and nothing are null; true and false (also True, TRUE, False and
// FALSE) are booleans; decimal numbers are numbers; the rest are strings.
// Anything outside the subset is refused by name with its line and column:
// a tab in indentation, an anchor (&), an alias (*), a tag (!), a block
// scalar (| or >), a directive (%), a complex key (?), a second document,
// a duplicate key, a hexadecimal or octal number, .inf and .nan.
//
// Every read is bounded by the limits below before anything is allocated
// for it; errors are returned as values and nothing throws.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::formats::oascript {

/// The longest text read, in bytes.
inline constexpr size_t max_input_bytes = size_t{16} << 20;
/// The deepest nesting of mappings and sequences read; the top-level mapping is depth 1.
inline constexpr uint32_t max_nesting_depth = 32;
/// The most nodes (scalars, mappings and sequences) one document holds.
inline constexpr size_t max_node_count = size_t{1} << 20;
/// The longest key or string scalar, in bytes of UTF-8.
inline constexpr size_t max_string_bytes = 4096;
/// The most digits after the decimal point a number keeps.
inline constexpr uint32_t max_decimal_places = 18;
/// The most significant digits a number holds; its mantissa fits in 63 bits.
inline constexpr uint32_t max_significant_digits = 18;
/// The widest line a mapping or sequence written in flow style takes, in bytes.
inline constexpr size_t max_flow_width = 100;
/// Spaces per level of indentation the YAML writer uses.
inline constexpr size_t yaml_indent = 2;

/// An exact decimal number, mantissa / 10^places.
///
/// A number is read with the places it was written with (1.50 keeps two),
/// and an exponent is folded in (2.5e2 is mantissa 250, places 0).
struct Decimal {
    int64_t mantissa{};
    uint32_t places{}; ///< digits after the decimal point, at most max_decimal_places
};

/// What a node of the tree holds.
enum class NodeKind : uint8_t {
    null_value,
    boolean,
    number,
    string,
    mapping,
    sequence,
};

/// Where a node starts in the text: a 1-based line, and a 1-based column
/// counted in bytes from the start of the line.
struct TextPosition {
    uint32_t line{};
    uint32_t column{};
};

/// One node of a document tree: a scalar, a mapping or a sequence.
///
/// A mapping's entries are its children in document order, each carrying
/// the key it is written under; a sequence's items are its children with an
/// empty key. Keys are unique within a mapping.
struct Node {
    NodeKind kind{NodeKind::null_value};
    TextPosition position{};      ///< where the value starts
    std::string key{};            ///< the key of a mapping entry; empty elsewhere
    TextPosition key_position{};  ///< where that key starts
    bool boolean{};               ///< a boolean's value
    Decimal number{};             ///< a number's value
    std::string text{};           ///< a string's value, valid UTF-8
    std::vector<Node> children{}; ///< a mapping's entries or a sequence's items
};

/// The text form of a document.
enum class DocumentForm : uint8_t {
    json, ///< strict JSON, RFC 8259
    yaml, ///< the YAML 1.2 subset this header describes
};

/// Why a text could not be read.
enum class ReadStatus : uint8_t {
    ok,
    too_large,                ///< the text is longer than max_input_bytes
    invalid_utf8,             ///< a byte sequence is not UTF-8
    unexpected_end,           ///< the text ends inside a value
    unexpected_character,     ///< a character no rule of the form allows there
    too_deep,                 ///< nesting deeper than max_nesting_depth
    too_many_nodes,           ///< more than max_node_count nodes
    string_too_long,          ///< a key or string longer than max_string_bytes
    bad_escape,               ///< a backslash escape the form does not define
    bad_number,               ///< a number that does not follow the form's grammar
    number_out_of_range,      ///< a number with too many digits or decimal places
    duplicate_key,            ///< a key written twice in one mapping
    trailing_content,         ///< text after the document
    top_level_not_mapping,    ///< the document is not a mapping
    tab_indentation,          ///< a tab in YAML indentation
    bad_indentation,          ///< YAML indentation that nests nothing or breaks a level
    unsupported_anchor,       ///< a YAML anchor (&)
    unsupported_alias,        ///< a YAML alias (*)
    unsupported_tag,          ///< a YAML tag (!)
    unsupported_block_scalar, ///< a YAML block scalar (| or >)
    unsupported_directive,    ///< a YAML directive (%)
    unsupported_complex_key,  ///< a YAML complex key (?)
    several_documents,        ///< a second YAML document
    unsupported_number_form,  ///< a hexadecimal or octal number, .inf or .nan
};

/// What went wrong reading a text, and where.
struct ReadError {
    ReadStatus status{ReadStatus::ok};
    TextPosition position{};
};

/// Returns a short English description of a read status, such as "a YAML
/// anchor (&) is not supported".
///
/// @param status the status
/// @return a static string
[[nodiscard]] const char* read_status_message(ReadStatus status) noexcept;

/// Tells which form a text is read as.
///
/// @param text the script's bytes
/// @return json when the first byte that is not white space, after an
///         optional UTF-8 byte order mark, is '{'; yaml otherwise
[[nodiscard]] DocumentForm detect_form(std::span<const uint8_t> text) noexcept;

/// Reads a script's text into a document tree.
///
/// The form comes from detect_form. The top level must be a mapping. On
/// failure `root` is left a null node and `error` says what and where.
///
/// @param text the script's bytes, at most max_input_bytes
/// @param[out] root the document's top-level mapping
/// @param[out] error the status, and the position of a failure
/// @return true when the whole text was read
[[nodiscard]] bool read_document(std::span<const uint8_t> text, Node& root, ReadError& error);

/// Writes a document tree as text.
///
/// The same tree always gives the same bytes. Mappings keep their entries'
/// order. A mapping or sequence whose descendants are all scalars is written
/// in flow style when its line fits in max_flow_width bytes, otherwise in
/// block style (YAML) or one entry per line (JSON), indented by yaml_indent
/// spaces a level. Strings are written plain in YAML when they read back as
/// the same string, otherwise double-quoted with JSON's escapes. Numbers are
/// written from their decimals digit by digit. The text ends with one
/// newline.
///
/// @param root a tree read_document could return: a mapping at the top,
///        unique keys and UTF-8 strings
/// @param form the form to write
/// @return the text
[[nodiscard]] std::string write_document(const Node& root, DocumentForm form);

/// Returns the entry of a mapping that has a key.
///
/// @param mapping the mapping to search
/// @param key the key, compared byte for byte
/// @return the entry, or null when `mapping` is not a mapping or has no such key
[[nodiscard]] const Node* find_entry(const Node& mapping, std::string_view key) noexcept;

/// Writes a decimal number: its digits, with a point before the last
/// `places` of them and a leading minus sign when negative.
///
/// @param value the number
/// @return the text, such as "-12.50" for mantissa -1250 and 2 places
[[nodiscard]] std::string decimal_text(Decimal value);

/// Compares two decimals by the numbers they stand for.
///
/// @param left the first number
/// @param right the second number
/// @return negative, zero or positive as `left` is less than, equal to or
///         greater than `right`; 1.50 and 1.5 compare equal
[[nodiscard]] int compare_decimals(Decimal left, Decimal right) noexcept;

} // namespace oa::formats::oascript
