// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Pieces the JSON reader, the YAML reader and the writers share: the
// position and node budget of a read, UTF-8 checks and encoding, exact
// decimal assembly and the core-schema resolution of YAML plain scalars.
#pragma once

#include "oa/formats/oascript/document.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace oa::formats::oascript::detail {

/// The UTF-8 byte order mark.
inline constexpr std::string_view byte_order_mark{"\xEF\xBB\xBF"};
/// The largest Unicode code point.
inline constexpr uint32_t max_code_point = 0x10FFFF;
/// The first and last code points of the UTF-16 surrogate halves.
inline constexpr uint32_t first_high_surrogate = 0xD800;
inline constexpr uint32_t last_high_surrogate = 0xDBFF;
inline constexpr uint32_t first_low_surrogate = 0xDC00;
inline constexpr uint32_t last_low_surrogate = 0xDFFF;
/// The line separator (U+2028); the paragraph separator follows it.
inline constexpr uint32_t line_separator = 0x2028;
/// The byte order mark's code point.
inline constexpr uint32_t byte_order_mark_code_point = 0xFEFF;
/// The first code point past the surrogate-pair range's base.
inline constexpr uint32_t surrogate_pair_base = 0x10000;
/// The largest exponent magnitude a number keeps before it is out of range
/// anyway; larger exponents saturate here, so reading one never overflows.
inline constexpr int64_t saturated_exponent = 1000000000;

/// Returns the length of the byte order mark at the start of a text.
///
/// @param text the text
/// @return 3 when `text` starts with a UTF-8 byte order mark, 0 otherwise
[[nodiscard]] size_t byte_order_mark_length(std::span<const uint8_t> text) noexcept;

/// Finds the first byte of a text that does not start a well-formed UTF-8
/// sequence: an overlong form, a surrogate, a code point past U+10FFFF, a
/// stray continuation byte or a truncated sequence.
///
/// @param text the text
/// @return the offset of that byte, or text.size() when the text is UTF-8
[[nodiscard]] size_t find_invalid_utf8(std::span<const uint8_t> text) noexcept;

/// Appends the UTF-8 encoding of a code point.
///
/// @param code_point a code point that is not a surrogate, at most max_code_point
/// @param[in,out] out the string appended to
void append_utf8(uint32_t code_point, std::string& out);

/// The value of one hexadecimal digit.
///
/// @param c the character
/// @return 0 to 15, or -1 when `c` is not a hexadecimal digit
[[nodiscard]] int hex_digit_value(uint8_t c) noexcept;

/// Tells whether a byte is an ASCII decimal digit.
///
/// @param c the byte
/// @return true for '0' to '9'
[[nodiscard]] constexpr bool is_digit(uint8_t c) noexcept {
    return c >= '0' && c <= '9';
}

/// The parts of a number's text, split by its grammar.
struct NumberParts {
    bool negative{};                    ///< a leading minus sign
    std::string_view integer_digits{};  ///< the digits before the point; may be empty
    std::string_view fraction_digits{}; ///< the digits after the point; may be empty
    bool exponent_negative{};           ///< a minus sign after e or E
    std::string_view exponent_digits{}; ///< the exponent's digits; empty without one
};

/// Builds an exact decimal from a number's parts, folding the exponent into
/// the places and keeping the places the text was written with.
///
/// Leading zeros count for nothing. A number with more than
/// max_significant_digits significant digits (zeros an exponent appends
/// included) or more than max_decimal_places places is out of range; a
/// negative place count is folded into the mantissa as trailing zeros.
///
/// @param parts the number's parts; every view holds digits only
/// @param[out] value the number; unchanged on failure
/// @return ok, or number_out_of_range
[[nodiscard]] ReadStatus assemble_decimal(const NumberParts& parts, Decimal& value) noexcept;

/// Resolves a YAML plain scalar as the core schema does, with decimal
/// numbers only.
///
/// null, Null, NULL, ~ and the empty text are null; true, True, TRUE, false,
/// False and FALSE are booleans; text matching
/// [-+]?(digits(.digits?)?|.digits)([eE][-+]?digits)? is a number; the rest is
/// a string. The hexadecimal (0x...) and octal (0o...) integers, .inf and
/// .nan of the core schema are refused.
///
/// @param raw the scalar's text, trimmed
/// @param[out] node receives the kind and value; its key and position are untouched
/// @return ok, unsupported_number_form, number_out_of_range or string_too_long
[[nodiscard]] ReadStatus resolve_plain_scalar(std::string_view raw, Node& node);

/// Tracks the nodes a read has made against max_node_count.
struct NodeBudget {
    size_t used{}; ///< nodes made so far
};

/// Counts one more node.
///
/// @param[in,out] budget the read's budget
/// @return false when the node would pass max_node_count
[[nodiscard]] inline bool take_node(NodeBudget& budget) noexcept {
    if (budget.used >= max_node_count)
        return false;
    ++budget.used;
    return true;
}

/// Reads a strict JSON text (RFC 8259) whose top level is an object.
///
/// @param text the whole text, already checked to be UTF-8 and within max_input_bytes
/// @param start the offset after the byte order mark, if any
/// @param[out] root the document's top-level mapping
/// @param[out] error the status and position of a failure
/// @return true when the whole text was read
[[nodiscard]] bool
read_json(std::span<const uint8_t> text, size_t start, Node& root, ReadError& error);

/// Reads a text in the YAML subset whose top level is a mapping.
///
/// @param text the whole text, already checked to be UTF-8 and within max_input_bytes
/// @param start the offset after the byte order mark, if any
/// @param[out] root the document's top-level mapping
/// @param[out] error the status and position of a failure
/// @return true when the whole text was read
[[nodiscard]] bool
read_yaml(std::span<const uint8_t> text, size_t start, Node& root, ReadError& error);

/// Tells whether a string is written plain in YAML: it reads back as the
/// same string in block and in flow context.
///
/// @param text the string
/// @return true when no quotes are needed
[[nodiscard]] bool yaml_plain_safe(std::string_view text);

/// Appends a string double-quoted with JSON's escapes, which YAML's
/// double-quoted scalars also read.
///
/// @param text the string, valid UTF-8
/// @param[in,out] out the text appended to
void append_quoted(std::string_view text, std::string& out);

} // namespace oa::formats::oascript::detail
