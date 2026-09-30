// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The strict JSON reader (RFC 8259): objects, arrays, strings with every
// escape, numbers by the RFC's grammar as exact decimals, and the three
// literals. No comments, no trailing commas, no duplicate keys.

#include "document_internal.hpp"

#include <string>
#include <unordered_set>

namespace oa::formats::oascript::detail {

namespace {

/// The first byte that is not a control character inside a string.
constexpr uint8_t first_printable = 0x20;
/// The hexadecimal digits of a \u escape.
constexpr size_t unicode_escape_digits = 4;
/// The bits a high surrogate contributes to a pair's code point.
constexpr uint32_t surrogate_payload_bits = 10;

/// One JSON text being read.
class JsonReader {
  public:

    /// Starts reading a text.
    ///
    /// @param text the whole text
    /// @param start the offset after the byte order mark
    /// @param error receives a failure
    JsonReader(std::span<const uint8_t> text, size_t start, ReadError& error) noexcept
        : text_{text}, offset_{start}, line_start_{start}, error_{&error} {}

    /// Reads the whole text.
    ///
    /// @param[out] root the top-level object
    /// @return true on success
    bool read(Node& root) {
        skip_space();
        if (peek() != '{')
            return fail(ReadStatus::top_level_not_mapping, here());
        if (!read_value(root, 1))
            return false;
        skip_space();
        if (offset_ != text_.size())
            return fail(ReadStatus::trailing_content, here());
        return true;
    }

  private:

    /// Returns the byte at the read offset plus a distance, or 0 past the end.
    ///
    /// @param distance bytes past the read offset
    /// @return the byte, or 0 past the end
    [[nodiscard]] uint8_t peek(size_t distance = 0) const noexcept {
        return offset_ + distance < text_.size() ? text_[offset_ + distance] : uint8_t{};
    }

    /// Returns the position of the read offset.
    ///
    /// @return its line and byte column
    [[nodiscard]] TextPosition here() const noexcept {
        return TextPosition{line_, static_cast<uint32_t>(offset_ - line_start_ + 1)};
    }

    /// Records a failure.
    ///
    /// @param status what went wrong
    /// @param position where
    /// @return false
    bool fail(ReadStatus status, TextPosition position) noexcept {
        error_->status = status;
        error_->position = position;
        return false;
    }

    /// Records a failure at the read offset, or unexpected_end when the text has ended.
    ///
    /// @return false
    bool fail_here() noexcept {
        return fail(
            offset_ >= text_.size() ? ReadStatus::unexpected_end : ReadStatus::unexpected_character,
            here()
        );
    }

    /// Skips JSON white space: spaces, tabs, carriage returns and line feeds.
    void skip_space() noexcept {
        while (offset_ < text_.size()) {
            const uint8_t c{text_[offset_]};
            if (c == '\n') {
                ++offset_;
                ++line_;
                line_start_ = offset_;
            } else if (c == ' ' || c == '\t' || c == '\r') {
                ++offset_;
            } else {
                return;
            }
        }
    }

    /// Reads one value of any kind.
    ///
    /// @param[out] node the value
    /// @param depth the nesting depth a mapping or sequence here would have
    /// @return true on success
    bool read_value(Node& node, uint32_t depth) {
        node.position = here();
        if (!take_node(budget_))
            return fail(ReadStatus::too_many_nodes, node.position);
        const uint8_t c{peek()};
        if (c == '{' || c == '[') {
            if (depth > max_nesting_depth)
                return fail(ReadStatus::too_deep, node.position);
            return c == '{' ? read_object(node, depth) : read_array(node, depth);
        }
        if (c == '"') {
            node.kind = NodeKind::string;
            return read_string(node.text);
        }
        if (c == '-' || is_digit(c)) {
            node.kind = NodeKind::number;
            return read_number(node.number);
        }
        if (c == 't')
            return read_literal("true", NodeKind::boolean, true, node);
        if (c == 'f')
            return read_literal("false", NodeKind::boolean, false, node);
        if (c == 'n')
            return read_literal("null", NodeKind::null_value, false, node);
        return fail_here();
    }

    /// Reads true, false or null.
    ///
    /// @param word the literal's spelling
    /// @param kind the node kind it makes
    /// @param value the boolean it holds
    /// @param[out] node the value
    /// @return true on success
    bool read_literal(std::string_view word, NodeKind kind, bool value, Node& node) noexcept {
        for (size_t index{}; index < word.size(); ++index) {
            if (offset_ + index >= text_.size()) {
                offset_ = text_.size();
                return fail(ReadStatus::unexpected_end, here());
            }
            if (text_[offset_ + index] != static_cast<uint8_t>(word[index]))
                return fail(ReadStatus::unexpected_character, node.position);
        }
        offset_ += word.size();
        node.kind = kind;
        node.boolean = value;
        return true;
    }

    /// Reads an object, the read offset on its '{'.
    ///
    /// @param[out] node the mapping
    /// @param depth its nesting depth
    /// @return true on success
    bool read_object(Node& node, uint32_t depth) {
        node.kind = NodeKind::mapping;
        ++offset_;
        skip_space();
        if (peek() == '}') {
            ++offset_;
            return true;
        }
        std::unordered_set<std::string> keys{};
        while (true) {
            if (peek() != '"')
                return fail_here();
            Node entry{};
            entry.key_position = here();
            if (!read_string(entry.key))
                return false;
            if (!keys.insert(entry.key).second)
                return fail(ReadStatus::duplicate_key, entry.key_position);
            skip_space();
            if (peek() != ':')
                return fail_here();
            ++offset_;
            skip_space();
            if (!read_value(entry, depth + 1))
                return false;
            node.children.push_back(std::move(entry));
            skip_space();
            if (peek() == ',') {
                ++offset_;
                skip_space();
                continue;
            }
            if (peek() == '}') {
                ++offset_;
                return true;
            }
            return fail_here();
        }
    }

    /// Reads an array, the read offset on its '['.
    ///
    /// @param[out] node the sequence
    /// @param depth its nesting depth
    /// @return true on success
    bool read_array(Node& node, uint32_t depth) {
        node.kind = NodeKind::sequence;
        ++offset_;
        skip_space();
        if (peek() == ']') {
            ++offset_;
            return true;
        }
        while (true) {
            Node item{};
            if (!read_value(item, depth + 1))
                return false;
            node.children.push_back(std::move(item));
            skip_space();
            if (peek() == ',') {
                ++offset_;
                skip_space();
                continue;
            }
            if (peek() == ']') {
                ++offset_;
                return true;
            }
            return fail_here();
        }
    }

    /// Reads the four hexadecimal digits of a \u escape, the read offset on the first.
    ///
    /// @param escape where the escape's backslash is
    /// @param[out] unit the UTF-16 code unit
    /// @return true on success
    bool read_unicode_digits(TextPosition escape, uint32_t& unit) noexcept {
        unit = 0;
        for (size_t index{}; index < unicode_escape_digits; ++index) {
            if (offset_ >= text_.size())
                return fail(ReadStatus::unexpected_end, here());
            const int digit{hex_digit_value(text_[offset_])};
            if (digit < 0)
                return fail(ReadStatus::bad_escape, escape);
            unit = unit * 16 + static_cast<uint32_t>(digit);
            ++offset_;
        }
        return true;
    }

    /// Reads a string, the read offset on its opening quote.
    ///
    /// @param[out] out the decoded text
    /// @return true on success
    bool read_string(std::string& out) {
        const TextPosition start{here()};
        ++offset_;
        while (true) {
            if (offset_ >= text_.size())
                return fail(ReadStatus::unexpected_end, here());
            const uint8_t c{text_[offset_]};
            if (c == '"') {
                ++offset_;
                return true;
            }
            if (c < first_printable)
                return fail(ReadStatus::unexpected_character, here());
            const size_t size_before{out.size()};
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                ++offset_;
            } else if (!read_escape(out)) {
                return false;
            }
            if (out.size() > max_string_bytes) {
                out.resize(size_before);
                return fail(ReadStatus::string_too_long, start);
            }
        }
    }

    /// Reads one backslash escape, the read offset on the backslash.
    ///
    /// @param[in,out] out the string the escaped character is appended to
    /// @return true on success
    bool read_escape(std::string& out) {
        const TextPosition escape{here()};
        ++offset_;
        if (offset_ >= text_.size())
            return fail(ReadStatus::unexpected_end, here());
        const uint8_t kind{text_[offset_]};
        ++offset_;
        switch (kind) {
        case '"':
        case '\\':
        case '/':
            out.push_back(static_cast<char>(kind));
            return true;
        case 'b':
            out.push_back('\b');
            return true;
        case 'f':
            out.push_back('\f');
            return true;
        case 'n':
            out.push_back('\n');
            return true;
        case 'r':
            out.push_back('\r');
            return true;
        case 't':
            out.push_back('\t');
            return true;
        case 'u':
            break;
        default:
            return fail(ReadStatus::bad_escape, escape);
        }
        uint32_t unit{};
        if (!read_unicode_digits(escape, unit))
            return false;
        if (unit >= first_low_surrogate && unit <= last_low_surrogate)
            return fail(ReadStatus::bad_escape, escape);
        if (unit >= first_high_surrogate && unit <= last_high_surrogate) {
            // A high surrogate must be followed by a \u escape of a low one.
            if (offset_ >= text_.size())
                return fail(ReadStatus::unexpected_end, here());
            if (peek() != '\\')
                return fail(ReadStatus::bad_escape, escape);
            if (offset_ + 1 >= text_.size()) {
                offset_ = text_.size();
                return fail(ReadStatus::unexpected_end, here());
            }
            if (peek(1) != 'u')
                return fail(ReadStatus::bad_escape, escape);
            offset_ += 2;
            uint32_t low{};
            if (!read_unicode_digits(escape, low))
                return false;
            if (low < first_low_surrogate || low > last_low_surrogate)
                return fail(ReadStatus::bad_escape, escape);
            unit = surrogate_pair_base + ((unit - first_high_surrogate) << surrogate_payload_bits) +
                   (low - first_low_surrogate);
        }
        append_utf8(unit, out);
        return true;
    }

    /// Reads a run of digits from the read offset.
    ///
    /// @return the digits, possibly none
    std::string_view read_digits() noexcept {
        const size_t first{offset_};
        while (offset_ < text_.size() && is_digit(text_[offset_]))
            ++offset_;
        return std::string_view{
            reinterpret_cast<const char*>(text_.data()) + first, offset_ - first
        };
    }

    /// Reads a number, -?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?, the read offset on its first byte.
    ///
    /// @param[out] value the number
    /// @return true on success
    bool read_number(Decimal& value) noexcept {
        const TextPosition start{here()};
        NumberParts parts{};
        if (peek() == '-') {
            parts.negative = true;
            ++offset_;
        }
        const auto fail_number = [this, &start]() {
            return fail(
                offset_ >= text_.size() ? ReadStatus::unexpected_end : ReadStatus::bad_number,
                offset_ >= text_.size() ? here() : start
            );
        };
        parts.integer_digits = read_digits();
        if (parts.integer_digits.empty())
            return fail_number();
        if (parts.integer_digits.size() > 1 && parts.integer_digits[0] == '0')
            return fail(ReadStatus::bad_number, start);
        if (peek() == '.') {
            ++offset_;
            parts.fraction_digits = read_digits();
            if (parts.fraction_digits.empty())
                return fail_number();
        }
        if (peek() == 'e' || peek() == 'E') {
            ++offset_;
            if (peek() == '+' || peek() == '-') {
                parts.exponent_negative = peek() == '-';
                ++offset_;
            }
            parts.exponent_digits = read_digits();
            if (parts.exponent_digits.empty())
                return fail_number();
        }
        const ReadStatus status{assemble_decimal(parts, value)};
        if (status != ReadStatus::ok)
            return fail(status, start);
        return true;
    }

    std::span<const uint8_t> text_{};
    size_t offset_{};
    uint32_t line_{1};
    size_t line_start_{};
    NodeBudget budget_{};
    ReadError* error_{};
};

} // namespace

bool read_json(std::span<const uint8_t> text, size_t start, Node& root, ReadError& error) {
    JsonReader reader{text, start, error};
    return reader.read(root);
}

} // namespace oa::formats::oascript::detail
