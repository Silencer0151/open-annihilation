// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The profile reader: the checks every text passes first (size, byte order
// mark, UTF-8, control characters), then block mappings and sequences nested
// by space indentation, flow mappings and sequences that may span lines,
// plain, single-quoted and double-quoted scalars on one line, '#' comments
// and the document markers. Everything else YAML has is refused by name.
//
// Line breaks are a line feed or a carriage return and line feed.

#include "oa/formats/oamod.hpp"

#include <cstdint>
#include <string>
#include <unordered_set>

namespace oa::formats::oamod {

namespace {

/// The UTF-8 byte order mark.
constexpr std::string_view byte_order_mark{"\xEF\xBB\xBF"};
/// The first byte that is not a C0 control character.
constexpr uint8_t first_printable = 0x20;
/// The delete character, the one control character above the C0 range.
constexpr uint8_t delete_character = 0x7F;
/// The largest Unicode code point.
constexpr uint32_t max_code_point = 0x10FFFF;
/// The first and last code points of the UTF-16 surrogate halves.
constexpr uint32_t first_surrogate = 0xD800;
constexpr uint32_t last_surrogate = 0xDFFF;
/// The first byte of a two-byte, three-byte and four-byte UTF-8 sequence.
constexpr uint8_t first_lead_of_two = 0xC2;
constexpr uint8_t first_lead_of_three = 0xE0;
constexpr uint8_t first_lead_of_four = 0xF0;
constexpr uint8_t last_lead_of_four = 0xF4;
/// The lead byte of the three-byte sequences that hold the surrogates.
constexpr uint8_t surrogate_lead = 0xED;
/// The marker bits of a two-byte sequence's lead byte.
constexpr uint8_t two_byte_marker = 0xC0;
/// The second-byte bounds that keep three- and four-byte sequences
/// shortest-form, free of surrogates and within U+10FFFF.
constexpr uint8_t first_second_after_e0 = 0xA0;
constexpr uint8_t last_second_after_ed = 0x9F;
constexpr uint8_t first_second_after_f0 = 0x90;
constexpr uint8_t last_second_after_f4 = 0x8F;
/// The range of continuation bytes.
constexpr uint8_t first_continuation = 0x80;
constexpr uint8_t last_continuation = 0xBF;
/// The payload bits of a continuation byte.
constexpr uint32_t continuation_bits = 0x3F;
/// The largest code point of one, two and three bytes.
constexpr uint32_t max_one_byte = 0x7F;
constexpr uint32_t max_two_bytes = 0x7FF;
constexpr uint32_t max_three_bytes = 0xFFFF;
/// The hexadecimal digits of the \x, \u and \U escapes.
constexpr size_t byte_escape_digits = 2;
constexpr size_t short_unicode_escape_digits = 4;
constexpr size_t long_unicode_escape_digits = 8;
/// The code points of YAML's \N, \_, \L and \P escapes.
constexpr uint32_t next_line = 0x85;
constexpr uint32_t no_break_space = 0xA0;
constexpr uint32_t line_separator = 0x2028;
constexpr uint32_t paragraph_separator = 0x2029;
/// The escape character, written by YAML's \e.
constexpr char escape_character = 0x1B;
/// The bell and vertical tab, written by YAML's \a and \v.
constexpr char bell_character = 0x07;
constexpr char vertical_tab_character = 0x0B;
/// The length of the '---' and '...' document markers.
constexpr size_t marker_length = 3;

/// Tells whether a byte is a UTF-8 continuation byte within a range.
///
/// @param c the byte
/// @param low the smallest allowed value
/// @param high the largest allowed value
/// @return true when `c` lies in [low, high]
constexpr bool in_range(uint8_t c, uint8_t low, uint8_t high) noexcept {
    return c >= low && c <= high;
}

/// Finds the first byte of a text that does not start a well-formed UTF-8
/// sequence: an overlong form, a surrogate, a code point past U+10FFFF, a
/// stray continuation byte or a truncated sequence.
///
/// @param text the text
/// @return the offset of that byte, or text.size() when the text is UTF-8
size_t find_invalid_utf8(std::span<const uint8_t> text) noexcept {
    size_t offset{};
    while (offset < text.size()) {
        const uint8_t lead{text[offset]};
        if (lead <= max_one_byte) {
            ++offset;
            continue;
        }
        size_t length{};
        uint8_t second_low{first_continuation};
        uint8_t second_high{last_continuation};
        if (in_range(lead, first_lead_of_two, first_lead_of_three - 1)) {
            length = 2;
        } else if (in_range(lead, first_lead_of_three, first_lead_of_four - 1)) {
            length = 3;
            if (lead == first_lead_of_three)
                second_low = first_second_after_e0;
            else if (lead == surrogate_lead)
                second_high = last_second_after_ed;
        } else if (in_range(lead, first_lead_of_four, last_lead_of_four)) {
            length = 4;
            if (lead == first_lead_of_four)
                second_low = first_second_after_f0;
            else if (lead == last_lead_of_four)
                second_high = last_second_after_f4;
        } else {
            return offset;
        }
        if (text.size() - offset < length)
            return offset;
        if (!in_range(text[offset + 1], second_low, second_high))
            return offset;
        for (size_t index{2}; index < length; ++index) {
            if (!in_range(text[offset + index], first_continuation, last_continuation))
                return offset;
        }
        offset += length;
    }
    return text.size();
}

/// Appends the UTF-8 encoding of a code point.
///
/// @param code_point a code point that is not a surrogate, at most max_code_point
/// @param[in,out] out the string appended to
void append_utf8(uint32_t code_point, std::string& out) {
    if (code_point <= max_one_byte) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point <= max_two_bytes) {
        out.push_back(static_cast<char>(two_byte_marker | (code_point >> 6)));
        out.push_back(static_cast<char>(first_continuation | (code_point & continuation_bits)));
    } else if (code_point <= max_three_bytes) {
        out.push_back(static_cast<char>(first_lead_of_three | (code_point >> 12)));
        out.push_back(
            static_cast<char>(first_continuation | ((code_point >> 6) & continuation_bits))
        );
        out.push_back(static_cast<char>(first_continuation | (code_point & continuation_bits)));
    } else {
        out.push_back(static_cast<char>(first_lead_of_four | (code_point >> 18)));
        out.push_back(
            static_cast<char>(first_continuation | ((code_point >> 12) & continuation_bits))
        );
        out.push_back(
            static_cast<char>(first_continuation | ((code_point >> 6) & continuation_bits))
        );
        out.push_back(static_cast<char>(first_continuation | (code_point & continuation_bits)));
    }
}

/// The value of one hexadecimal digit.
///
/// @param c the character
/// @return 0 to 15, or -1 when `c` is not a hexadecimal digit
int hex_digit_value(uint8_t c) noexcept {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/// Tells whether a byte ends a line: a line feed, a carriage return, or the
/// 0 peek gives past the end.
///
/// @param c the byte
/// @return true at a line break or the end
constexpr bool is_break_or_end(uint8_t c) noexcept {
    return c == '\n' || c == '\r' || c == 0;
}

/// Tells whether a byte is white space, a line break or the end.
///
/// @param c the byte
/// @return true for a space, a tab, a line break or the end
constexpr bool is_blank_or_end(uint8_t c) noexcept {
    return c == ' ' || c == '\t' || is_break_or_end(c);
}

/// Tells whether a byte is one of the flow indicators , [ ] { }.
///
/// @param c the byte
/// @return true for a flow indicator
constexpr bool is_flow_indicator(uint8_t c) noexcept {
    return c == ',' || c == '[' || c == ']' || c == '{' || c == '}';
}

/// Returns the rule a number's status breaks.
///
/// @param status the status of a text that looked like a number
/// @return the rule; ok for ok and not_a_number
constexpr Rule number_rule(NumberStatus status) noexcept {
    switch (status) {
    case NumberStatus::integer_too_large:
        return Rule::integer_too_large;
    case NumberStatus::too_many_digits:
        return Rule::too_many_digits;
    case NumberStatus::out_of_range:
        return Rule::number_out_of_range;
    case NumberStatus::ok:
    case NumberStatus::not_a_number:
        break;
    }
    return Rule::ok;
}

/// What the next line that holds content starts with.
struct LineInfo {
    bool end{};      ///< the text has no more content
    bool marker{};   ///< the line is a '---' or '...' document marker
    size_t indent{}; ///< the spaces before the content
};

/// One text being read.
class Reader {
  public:

    /// Starts reading a text.
    ///
    /// @param text the whole text, already checked to be UTF-8 and within max_input_bytes
    /// @param error receives a failure
    Reader(std::span<const uint8_t> text, ReadError& error) noexcept
        : text_{text}, error_{&error} {}

    /// Reads the whole text.
    ///
    /// @param[out] root the top-level node
    /// @param mapping_only whether the top level must be a mapping
    /// @return true on success
    bool read(Node& root, bool mapping_only) {
        if (!check_characters())
            return false;
        LineInfo line{};
        bool started{};
        while (true) {
            if (!line_info(line))
                return false;
            if (line.end) {
                if (mapping_only)
                    return fail(Rule::not_mapping, here());
                root = Node{};
                root.position = here();
                return true;
            }
            if (!line.marker) {
                if (peek() == '%' && line.indent == 0 && !started)
                    return fail(Rule::directive, here());
                break;
            }
            if (peek() == '.' || started)
                return fail(Rule::several_documents, here());
            started = true;
            offset_ += marker_length;
            if (!end_line())
                return false;
        }
        const bool flow_root{peek() == '[' || peek() == '{'};
        if (!read_block_node(root, line.indent, 1, 0))
            return false;
        if (mapping_only && root.kind != NodeKind::mapping)
            return fail(Rule::not_mapping, root.position);
        if (!line_info(line))
            return false;
        if (line.end)
            return true;
        if (!line.marker)
            return fail(flow_root ? Rule::trailing_content : Rule::bad_indentation, here());
        if (peek() == '-')
            return fail(Rule::several_documents, here());
        offset_ += marker_length;
        if (!end_line() || !line_info(line))
            return false;
        if (!line.end)
            return fail(Rule::several_documents, here());
        return true;
    }

  private:

    /// Refuses control characters other than tab, carriage return and line
    /// feed, and a carriage return not followed by a line feed.
    ///
    /// @return true when every character is allowed
    bool check_characters() noexcept {
        uint32_t line{1};
        size_t line_start{};
        for (size_t offset{}; offset < text_.size(); ++offset) {
            const uint8_t c{text_[offset]};
            const bool lone_return{
                c == '\r' && (offset + 1 >= text_.size() || text_[offset + 1] != '\n')
            };
            if ((c < first_printable && c != '\t' && c != '\n' && c != '\r') ||
                c == delete_character || lone_return)
                return fail(
                    Rule::control_character,
                    TextPosition{line, static_cast<uint32_t>(offset - line_start + 1)}
                );
            if (c == '\n') {
                ++line;
                line_start = offset + 1;
            }
        }
        return true;
    }

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

    /// Returns the column of the read offset, counted from 0.
    ///
    /// @return the bytes between the line's start and the read offset
    [[nodiscard]] size_t column() const noexcept { return offset_ - line_start_; }

    /// Records a failure.
    ///
    /// @param rule the rule broken
    /// @param position where
    /// @return false
    bool fail(Rule rule, TextPosition position) noexcept {
        error_->rule = rule;
        error_->position = position;
        return false;
    }

    /// Records a failure at the read offset: unexpected_end when the text
    /// has ended, otherwise the rule given.
    ///
    /// @param rule the failure when the text has not ended
    /// @return false
    bool fail_here(Rule rule = Rule::unexpected_character) noexcept {
        return fail(offset_ >= text_.size() ? Rule::unexpected_end : rule, here());
    }

    /// Counts one more node, failing when the budget is spent.
    ///
    /// @param position where the node starts
    /// @return true while within max_node_count
    bool count_node(TextPosition position) noexcept {
        if (nodes_ >= max_node_count)
            return fail(Rule::too_many_nodes, position);
        ++nodes_;
        return true;
    }

    /// Moves past a line break at the read offset.
    void consume_break() noexcept {
        if (peek() == '\r')
            ++offset_;
        ++offset_;
        ++line_;
        line_start_ = offset_;
    }

    /// Skips spaces and tabs within a line.
    void skip_inline_space() noexcept {
        while (peek() == ' ' || peek() == '\t')
            ++offset_;
    }

    /// Tells whether a '#' at the read offset starts a comment: it is first
    /// on its line or follows white space.
    ///
    /// @return true at a comment
    [[nodiscard]] bool at_comment() const noexcept {
        if (peek() != '#')
            return false;
        if (offset_ == line_start_)
            return true;
        const uint8_t before{text_[offset_ - 1]};
        return before == ' ' || before == '\t';
    }

    /// Skips to the line break that ends the current line.
    void skip_to_break() noexcept {
        while (!is_break_or_end(peek()))
            ++offset_;
    }

    /// Finishes a line after its content: white space, an optional comment,
    /// then the line break or the end of the text.
    ///
    /// @return true when nothing else is on the line
    bool end_line() noexcept {
        skip_inline_space();
        if (at_comment())
            skip_to_break();
        if (offset_ >= text_.size())
            return true;
        if (peek() != '\n' && peek() != '\r')
            return fail(Rule::unexpected_character, here());
        consume_break();
        return true;
    }

    /// Finds the next line that holds content, skipping blank and comment
    /// lines, and leaves the read offset on its first content byte.
    ///
    /// Asking again without moving gives the same answer.
    ///
    /// @param[out] info the line's indentation and whether it is a marker or the end
    /// @return false when the line's indentation holds a tab
    bool line_info(LineInfo& info) noexcept {
        if (offset_ == cached_offset_) {
            info = cached_line_;
            return true;
        }
        while (true) {
            size_t first_tab{text_.size()};
            size_t content{offset_};
            while (content < text_.size() && (text_[content] == ' ' || text_[content] == '\t')) {
                if (text_[content] == '\t' && first_tab == text_.size())
                    first_tab = content;
                ++content;
            }
            const uint8_t c{content < text_.size() ? text_[content] : uint8_t{}};
            if (content >= text_.size()) {
                offset_ = content;
                info = LineInfo{true, false, 0};
                break;
            }
            if (c == '\n' || c == '\r') {
                offset_ = content;
                consume_break();
                continue;
            }
            if (c == '#') {
                offset_ = content;
                skip_to_break();
                continue;
            }
            if (first_tab != text_.size()) {
                offset_ = first_tab;
                return fail(Rule::tab_indentation, here());
            }
            offset_ = content;
            info = LineInfo{false, false, column()};
            info.marker = info.indent == 0 && is_marker();
            break;
        }
        cached_offset_ = offset_;
        cached_line_ = info;
        return true;
    }

    /// Tells whether the read offset starts a '---' or '...' marker followed
    /// by white space, a line break or the end.
    ///
    /// @return true at a document marker
    [[nodiscard]] bool is_marker() const noexcept {
        const uint8_t c{peek()};
        if (c != '-' && c != '.')
            return false;
        return peek(1) == c && peek(2) == c && is_blank_or_end(peek(marker_length));
    }

    /// Tells whether the read offset starts a block sequence entry: '-'
    /// followed by white space, a line break or the end.
    ///
    /// @return true at "- "
    [[nodiscard]] bool at_sequence_entry() const noexcept {
        return peek() == '-' && is_blank_or_end(peek(1));
    }

    /// Refuses the indicators that start a construct outside the subset, or
    /// that cannot start a value.
    ///
    /// @param flow whether the value is inside a flow collection
    /// @return true when the read offset may start a value
    bool check_value_start(bool flow) noexcept {
        const uint8_t c{peek()};
        switch (c) {
        case '&':
            return fail(Rule::anchor, here());
        case '*':
            return fail(Rule::alias, here());
        case '!':
            return fail(Rule::tag, here());
        case '|':
        case '>':
            return fail(flow ? Rule::unexpected_character : Rule::block_scalar, here());
        case '?':
            if (is_blank_or_end(peek(1)) || (flow && is_flow_indicator(peek(1))))
                return fail(Rule::complex_key, here());
            return true;
        case '%':
        case '@':
        case '`':
        case ',':
        case ']':
        case '}':
        case '#':
            return fail(Rule::unexpected_character, here());
        case '-':
        case ':':
            if (is_blank_or_end(peek(1)) || (flow && is_flow_indicator(peek(1))))
                return fail_here();
            return true;
        default:
            return true;
        }
    }

    /// Reads a plain scalar's text on one line, the read offset on its first
    /// byte, and leaves the offset where it stops.
    ///
    /// @param flow whether the scalar is inside a flow collection
    /// @return the text, trailing white space removed
    std::string_view read_plain_text(bool flow) noexcept {
        const size_t first{offset_};
        while (true) {
            const uint8_t c{peek()};
            if (is_break_or_end(c))
                break;
            if (c == ':' && (is_blank_or_end(peek(1)) || (flow && is_flow_indicator(peek(1)))))
                break;
            if (c == '#' && at_comment())
                break;
            if (flow && is_flow_indicator(c))
                break;
            ++offset_;
        }
        size_t last{offset_};
        while (last > first && (text_[last - 1] == ' ' || text_[last - 1] == '\t'))
            --last;
        return std::string_view{reinterpret_cast<const char*>(text_.data()) + first, last - first};
    }

    /// Reads one backslash escape of a double-quoted scalar, the read offset
    /// on the backslash.
    ///
    /// @param[in,out] out the string the escaped character is appended to
    /// @return true on success
    bool read_escape(std::string& out) {
        const TextPosition escape{here()};
        ++offset_;
        const uint8_t kind{peek()};
        if (is_break_or_end(kind))
            return fail_here(Rule::multi_line_scalar);
        ++offset_;
        switch (kind) {
        case '0':
            out.push_back('\0');
            return true;
        case 'a':
            out.push_back(bell_character);
            return true;
        case 'b':
            out.push_back('\b');
            return true;
        case 't':
        case '\t':
            out.push_back('\t');
            return true;
        case 'n':
            out.push_back('\n');
            return true;
        case 'v':
            out.push_back(vertical_tab_character);
            return true;
        case 'f':
            out.push_back('\f');
            return true;
        case 'r':
            out.push_back('\r');
            return true;
        case 'e':
            out.push_back(escape_character);
            return true;
        case ' ':
        case '"':
        case '/':
        case '\\':
            out.push_back(static_cast<char>(kind));
            return true;
        case 'N':
            append_utf8(next_line, out);
            return true;
        case '_':
            append_utf8(no_break_space, out);
            return true;
        case 'L':
            append_utf8(line_separator, out);
            return true;
        case 'P':
            append_utf8(paragraph_separator, out);
            return true;
        case 'x':
        case 'u':
        case 'U':
            break;
        default:
            return fail(Rule::bad_escape, escape);
        }
        const size_t digits{
            kind == 'x'   ? byte_escape_digits
            : kind == 'u' ? short_unicode_escape_digits
                          : long_unicode_escape_digits
        };
        uint32_t code_point{};
        for (size_t index{}; index < digits; ++index) {
            const int digit{hex_digit_value(peek())};
            if (digit < 0)
                return offset_ >= text_.size() ? fail_here() : fail(Rule::bad_escape, escape);
            code_point = code_point * 16 + static_cast<uint32_t>(digit);
            ++offset_;
        }
        if (code_point > max_code_point ||
            (code_point >= first_surrogate && code_point <= last_surrogate))
            return fail(Rule::bad_escape, escape);
        append_utf8(code_point, out);
        return true;
    }

    /// Reads a single- or double-quoted scalar on one line, the read offset
    /// on its opening quote.
    ///
    /// @param[out] out the decoded text
    /// @return true on success
    bool read_quoted(std::string& out) {
        const TextPosition start{here()};
        const uint8_t quote{peek()};
        ++offset_;
        while (true) {
            const uint8_t c{peek()};
            if (is_break_or_end(c))
                return fail_here(Rule::multi_line_scalar);
            const size_t size_before{out.size()};
            if (c == quote) {
                ++offset_;
                if (quote == '\'' && peek() == '\'') {
                    out.push_back('\'');
                    ++offset_;
                } else {
                    return true;
                }
            } else if (c == '\\' && quote == '"') {
                if (!read_escape(out))
                    return false;
            } else {
                out.push_back(static_cast<char>(c));
                ++offset_;
            }
            if (out.size() > max_string_bytes) {
                out.resize(size_before);
                return fail(Rule::string_too_long, start);
            }
        }
    }

    /// Reads a scalar that may be a key: quoted, or plain.
    ///
    /// @param flow whether the scalar is inside a flow collection
    /// @param[out] node receives a quoted scalar's text and the position
    /// @param[out] raw the plain scalar's text; empty for a quoted one
    /// @return true on success
    bool read_scalar(bool flow, Node& node, std::string_view& raw) {
        node.position = here();
        node.quoted = peek() == '"' || peek() == '\'';
        raw = std::string_view{};
        if (node.quoted) {
            node.kind = NodeKind::string;
            return read_quoted(node.text);
        }
        if (!check_value_start(flow))
            return false;
        raw = read_plain_text(flow);
        if (raw.empty())
            return fail_here();
        if (raw.size() > max_string_bytes)
            return fail(Rule::string_too_long, node.position);
        return true;
    }

    /// Resolves a plain scalar's text into its node as the core schema does,
    /// with decimal numbers only.
    ///
    /// @param raw the text, trimmed
    /// @param[in,out] node the node, holding its position
    /// @return true on success
    bool resolve(std::string_view raw, Node& node) {
        node.text = std::string{raw};
        if (raw.empty() || raw == "~" || raw == "null" || raw == "Null" || raw == "NULL") {
            node.kind = NodeKind::null_value;
            return true;
        }
        if (raw == "true" || raw == "True" || raw == "TRUE" || raw == "false" || raw == "False" ||
            raw == "FALSE") {
            node.kind = NodeKind::boolean;
            node.boolean = raw[0] == 't' || raw[0] == 'T';
            return true;
        }
        Number number{};
        const NumberStatus status{parse_number(raw, number)};
        if (status == NumberStatus::ok) {
            node.kind = NodeKind::number;
            node.number = number;
            return true;
        }
        if (status != NumberStatus::not_a_number)
            return fail(number_rule(status), node.position);
        node.kind = NodeKind::string;
        return true;
    }

    /// Makes a scalar just read the key of an entry.
    ///
    /// @param raw the plain scalar's text; empty for a quoted one
    /// @param[in,out] scalar the scalar; a quoted one's text is moved out
    /// @param[out] entry receives the key
    /// @return false when the key is not a string or an integer, or a merge key
    bool take_key(std::string_view raw, Node& scalar, Node& entry) {
        Key& key{entry.key};
        key.position = scalar.position;
        key.quoted = scalar.quoted;
        if (scalar.quoted) {
            key.kind = KeyKind::string;
            key.text = std::move(scalar.text);
            return true;
        }
        if (raw == "<<")
            return fail(Rule::merge_key, key.position);
        Node resolved{};
        resolved.position = scalar.position;
        if (!resolve(raw, resolved))
            return false;
        if (resolved.kind == NodeKind::string) {
            key.kind = KeyKind::string;
            key.text = std::move(resolved.text);
            return true;
        }
        int64_t value{};
        if (resolved.kind == NodeKind::number && resolved.number.integer &&
            integer_value(resolved.number, value)) {
            key.kind = KeyKind::integer;
            key.integer = value;
            key.text = std::to_string(value);
            return true;
        }
        return fail(Rule::key_not_string, key.position);
    }

    /// Adds a key to a mapping's key set, refusing a duplicate. A string key
    /// and an integer key never collide, as 71 and '71' are different keys.
    ///
    /// @param[in,out] keys the keys so far
    /// @param key the key
    /// @return true when the key is new
    bool add_key(std::unordered_set<std::string>& keys, const Key& key) {
        std::string tagged{key.kind == KeyKind::integer ? "i" : "s"};
        tagged += key.text;
        return keys.insert(std::move(tagged)).second || fail(Rule::duplicate_key, key.position);
    }

    /// Checks the nesting depth of a new mapping or sequence and counts its node.
    ///
    /// @param depth its depth
    /// @param position where it starts
    /// @return true within the limits
    bool open_collection(uint32_t depth, TextPosition position) noexcept {
        if (depth > max_nesting_depth)
            return fail(Rule::too_deep, position);
        return count_node(position);
    }

    /// Reads a block node whose first byte is at the read offset.
    ///
    /// @param[out] node the node
    /// @param indent the column of the node's first byte, counted from 0
    /// @param depth the nesting depth a mapping or sequence here would have
    /// @param flow_floor the smallest column a flow collection's later lines may start at
    /// @return true on success
    bool read_block_node(Node& node, size_t indent, uint32_t depth, size_t flow_floor) {
        node.position = here();
        if (at_sequence_entry())
            return read_block_sequence(node, indent, depth);
        if (peek() == '[' || peek() == '{') {
            if (!read_flow_collection(node, depth, flow_floor))
                return false;
            skip_inline_space();
            if (peek() == ':' && is_blank_or_end(peek(1)))
                return fail(Rule::complex_key, node.position);
            return end_line();
        }
        std::string_view raw{};
        if (!read_scalar(false, node, raw))
            return false;
        skip_inline_space();
        if (peek() == ':' && is_blank_or_end(peek(1))) {
            Node first{};
            if (!take_key(raw, node, first))
                return false;
            // The node becomes the mapping; an entry keeps its own key.
            Key key{std::move(node.key)};
            const TextPosition position{node.position};
            node = Node{};
            node.key = std::move(key);
            node.position = position;
            return read_block_mapping(node, indent, depth, std::move(first));
        }
        if (!count_node(node.position))
            return false;
        if (!node.quoted && !resolve(raw, node))
            return false;
        return end_line();
    }

    /// Reads the key of a block mapping entry and its ':', the read offset
    /// on the key's first byte.
    ///
    /// @param[out] entry receives the key
    /// @return true on success
    bool read_block_key(Node& entry) {
        if (at_sequence_entry())
            return fail(Rule::bad_indentation, here());
        if (peek() == '[' || peek() == '{')
            return fail(Rule::complex_key, here());
        Node scalar{};
        std::string_view raw{};
        if (!read_scalar(false, scalar, raw))
            return false;
        skip_inline_space();
        if (peek() != ':' || !is_blank_or_end(peek(1)))
            return fail_here();
        return take_key(raw, scalar, entry);
    }

    /// Reads a block mapping whose first key has been read, the read offset
    /// on that key's ':'.
    ///
    /// @param[out] node the mapping
    /// @param indent the column of its keys
    /// @param depth its nesting depth
    /// @param first the first entry, holding its key
    /// @return true on success
    bool read_block_mapping(Node& node, size_t indent, uint32_t depth, Node first) {
        node.kind = NodeKind::mapping;
        if (!open_collection(depth, node.position))
            return false;
        std::unordered_set<std::string> keys{};
        Node entry{std::move(first)};
        while (true) {
            if (!add_key(keys, entry.key))
                return false;
            ++offset_; // the ':'
            if (!read_mapping_value(entry, indent, depth))
                return false;
            node.children.push_back(std::move(entry));
            LineInfo line{};
            if (!line_info(line))
                return false;
            if (line.end || line.marker || line.indent < indent)
                return true;
            if (line.indent > indent)
                return fail(Rule::bad_indentation, here());
            entry = Node{};
            if (!read_block_key(entry))
                return false;
        }
    }

    /// Reads the value of a block mapping entry, the read offset after its ':'.
    ///
    /// @param[in,out] entry the entry, holding its key
    /// @param indent the column of the mapping's keys
    /// @param depth the mapping's nesting depth
    /// @return true on success
    bool read_mapping_value(Node& entry, size_t indent, uint32_t depth) {
        skip_inline_space();
        if (is_break_or_end(peek()) || at_comment()) {
            entry.position = here();
            if (!end_line())
                return false;
            LineInfo line{};
            if (!line_info(line))
                return false;
            if (!line.end && !line.marker) {
                if (line.indent > indent)
                    return read_block_node(entry, line.indent, depth + 1, indent + 1);
                if (line.indent == indent && at_sequence_entry()) {
                    entry.position = here();
                    return read_block_sequence(entry, indent, depth + 1);
                }
            }
            entry.kind = NodeKind::null_value;
            return count_node(entry.position);
        }
        return read_inline_value(entry, depth + 1, indent + 1);
    }

    /// Reads a value that starts on the line of its key or sequence dash and
    /// ends its line: a flow collection or a scalar.
    ///
    /// @param[out] node the value
    /// @param depth the nesting depth a collection here would have
    /// @param flow_floor the smallest column a flow collection's later lines may start at
    /// @return true on success
    bool read_inline_value(Node& node, uint32_t depth, size_t flow_floor) {
        node.position = here();
        if (at_sequence_entry())
            return fail(Rule::unexpected_character, here());
        if (peek() == '[' || peek() == '{') {
            if (!read_flow_collection(node, depth, flow_floor))
                return false;
            skip_inline_space();
            if (peek() == ':' && is_blank_or_end(peek(1)))
                return fail(Rule::complex_key, node.position);
            return end_line();
        }
        std::string_view raw{};
        if (!read_scalar(false, node, raw))
            return false;
        skip_inline_space();
        if (peek() == ':' && is_blank_or_end(peek(1)))
            return fail(Rule::unexpected_character, here());
        if (!count_node(node.position))
            return false;
        if (!node.quoted && !resolve(raw, node))
            return false;
        return end_line();
    }

    /// Reads a block sequence, the read offset on its first '-'.
    ///
    /// @param[out] node the sequence
    /// @param indent the column of its dashes
    /// @param depth its nesting depth
    /// @return true on success
    bool read_block_sequence(Node& node, size_t indent, uint32_t depth) {
        node.kind = NodeKind::sequence;
        node.position = here();
        if (!open_collection(depth, node.position))
            return false;
        while (true) {
            Node item{};
            ++offset_; // the '-'
            skip_inline_space();
            item.position = here();
            if (is_break_or_end(peek()) || at_comment()) {
                if (!end_line())
                    return false;
                LineInfo line{};
                if (!line_info(line))
                    return false;
                if (!line.end && !line.marker && line.indent > indent) {
                    if (!read_block_node(item, line.indent, depth + 1, indent + 1))
                        return false;
                } else {
                    item.kind = NodeKind::null_value;
                    if (!count_node(item.position))
                        return false;
                }
            } else if (!read_block_node(item, column(), depth + 1, indent + 1)) {
                return false;
            }
            node.children.push_back(std::move(item));
            LineInfo line{};
            if (!line_info(line))
                return false;
            if (line.end || line.marker || line.indent < indent)
                return true;
            if (line.indent > indent)
                return fail(Rule::bad_indentation, here());
            if (!at_sequence_entry())
                return true;
        }
    }

    /// Skips white space, line breaks and comments inside a flow collection.
    ///
    /// @param flow_floor the smallest column a later line's content may start at
    /// @return false when a later line is indented with a tab or too little
    bool skip_flow_space(size_t flow_floor) noexcept {
        while (true) {
            skip_inline_space();
            if (at_comment())
                skip_to_break();
            if (peek() != '\n' && peek() != '\r')
                return true;
            consume_break();
            while (peek() == ' ')
                ++offset_;
            if (peek() == '\t')
                return fail(Rule::tab_indentation, here());
            const uint8_t c{peek()};
            if (!is_break_or_end(c) && c != '#' && c != ']' && c != '}' && column() < flow_floor)
                return fail(Rule::bad_indentation, here());
        }
    }

    /// Reads a node inside a flow collection.
    ///
    /// @param[out] node the node
    /// @param depth the nesting depth a collection here would have
    /// @param flow_floor the smallest column a later line may start at
    /// @return true on success
    bool read_flow_node(Node& node, uint32_t depth, size_t flow_floor) {
        node.position = here();
        if (peek() == '[' || peek() == '{')
            return read_flow_collection(node, depth, flow_floor);
        std::string_view raw{};
        if (!read_scalar(true, node, raw))
            return false;
        if (!count_node(node.position))
            return false;
        return node.quoted || resolve(raw, node);
    }

    /// Reads a flow mapping or sequence, the read offset on its '{' or '['.
    ///
    /// @param[out] node the collection
    /// @param depth its nesting depth
    /// @param flow_floor the smallest column a later line may start at
    /// @return true on success
    bool read_flow_collection(Node& node, uint32_t depth, size_t flow_floor) {
        node.position = here();
        const bool mapping{peek() == '{'};
        const uint8_t closing{mapping ? uint8_t{'}'} : uint8_t{']'}};
        node.kind = mapping ? NodeKind::mapping : NodeKind::sequence;
        if (!open_collection(depth, node.position))
            return false;
        ++offset_;
        std::unordered_set<std::string> keys{};
        while (true) {
            if (!skip_flow_space(flow_floor))
                return false;
            if (peek() == closing) {
                ++offset_;
                return true;
            }
            Node item{};
            if (mapping) {
                if (!read_flow_entry(item, depth, flow_floor) || !add_key(keys, item.key))
                    return false;
            } else {
                if (!read_flow_node(item, depth + 1, flow_floor))
                    return false;
                if (!skip_flow_space(flow_floor))
                    return false;
                if (peek() == ':')
                    return fail(Rule::complex_key, item.position);
            }
            node.children.push_back(std::move(item));
            if (!skip_flow_space(flow_floor))
                return false;
            if (peek() == ',') {
                ++offset_;
                continue;
            }
            if (peek() != closing)
                return fail_here();
        }
    }

    /// Reads one entry of a flow mapping: a key, then ':' and a value, which
    /// may be left out for null.
    ///
    /// @param[out] entry the entry
    /// @param depth the mapping's nesting depth
    /// @param flow_floor the smallest column a later line may start at
    /// @return true on success
    bool read_flow_entry(Node& entry, uint32_t depth, size_t flow_floor) {
        if (peek() == '[' || peek() == '{')
            return fail(Rule::complex_key, here());
        Node scalar{};
        std::string_view raw{};
        if (!read_scalar(true, scalar, raw))
            return false;
        skip_inline_space();
        const bool has_value{
            peek() == ':' &&
            (scalar.quoted || is_blank_or_end(peek(1)) || is_flow_indicator(peek(1)))
        };
        if (!take_key(raw, scalar, entry))
            return false;
        if (has_value) {
            ++offset_;
            if (!skip_flow_space(flow_floor))
                return false;
        }
        entry.position = here();
        if (!has_value || peek() == ',' || peek() == '}') {
            if (!has_value && peek() != ',' && peek() != '}' && !is_break_or_end(peek()) &&
                !at_comment())
                return fail_here();
            entry.kind = NodeKind::null_value;
            return count_node(entry.position);
        }
        return read_flow_node(entry, depth + 1, flow_floor);
    }

    std::span<const uint8_t> text_{};
    size_t offset_{};
    uint32_t line_{1};
    size_t line_start_{};
    size_t cached_offset_{SIZE_MAX};
    LineInfo cached_line_{};
    size_t nodes_{};
    ReadError* error_{};
};

/// Reads a text after the checks every text passes first.
///
/// @param text the bytes
/// @param[out] root the top-level node
/// @param[out] error the rule broken, and where
/// @param mapping_only whether the top level must be a mapping
/// @return true when the whole text was read
bool read_text(std::span<const uint8_t> text, Node& root, ReadError& error, bool mapping_only) {
    root = Node{};
    error = ReadError{};
    if (text.size() > max_input_bytes) {
        error.rule = Rule::too_large;
        return false;
    }
    if (text.size() >= byte_order_mark.size() &&
        std::string_view{reinterpret_cast<const char*>(text.data()), byte_order_mark.size()} ==
            byte_order_mark) {
        error.rule = Rule::byte_order_mark;
        error.position = TextPosition{1, 1};
        return false;
    }
    const size_t bad_byte{find_invalid_utf8(text)};
    if (bad_byte != text.size()) {
        uint32_t line{1};
        size_t line_start{};
        for (size_t offset{}; offset < bad_byte; ++offset) {
            if (text[offset] == '\n') {
                ++line;
                line_start = offset + 1;
            }
        }
        error.rule = Rule::invalid_utf8;
        error.position = TextPosition{line, static_cast<uint32_t>(bad_byte - line_start + 1)};
        return false;
    }
    Reader reader{text, error};
    const bool read{reader.read(root, mapping_only)};
    if (!read)
        root = Node{};
    return read;
}

} // namespace

const char* rule_message(Rule rule) noexcept {
    switch (rule) {
    case Rule::ok:
        return "no error";
    case Rule::too_large:
        return "the text is longer than 256 KiB";
    case Rule::byte_order_mark:
        return "the text starts with a byte order mark";
    case Rule::invalid_utf8:
        return "a byte sequence is not UTF-8";
    case Rule::control_character:
        return "a control character is not allowed";
    case Rule::unexpected_end:
        return "the text ends inside a value";
    case Rule::unexpected_character:
        return "a character is not allowed here";
    case Rule::too_deep:
        return "mappings and sequences are nested more than 16 levels deep";
    case Rule::too_many_nodes:
        return "the document holds too many values";
    case Rule::string_too_long:
        return "a key or string is too long";
    case Rule::bad_escape:
        return "a backslash escape is not defined";
    case Rule::multi_line_scalar:
        return "a quoted string must end on the line it starts on";
    case Rule::integer_too_large:
        return "an integer is beyond 2^53";
    case Rule::too_many_digits:
        return "a decimal number has more than 15 significant digits";
    case Rule::number_out_of_range:
        return "a decimal number is beyond 10^300";
    case Rule::duplicate_key:
        return "a key is written twice in one mapping";
    case Rule::key_not_string:
        return "mapping keys must be strings or integers";
    case Rule::merge_key:
        return "merge keys (<<) are not allowed";
    case Rule::trailing_content:
        return "there is text after the document";
    case Rule::not_mapping:
        return "the document is not a mapping";
    case Rule::tab_indentation:
        return "a tab is used for indentation";
    case Rule::bad_indentation:
        return "the indentation nests nothing or breaks a level";
    case Rule::anchor:
        return "anchors (&) are not allowed";
    case Rule::alias:
        return "aliases (*) are not allowed";
    case Rule::tag:
        return "tags (!) are not allowed";
    case Rule::block_scalar:
        return "block scalars (| and >) are not allowed";
    case Rule::directive:
        return "directives (%) are not allowed";
    case Rule::complex_key:
        return "complex keys (?) and collections as keys are not allowed";
    case Rule::several_documents:
        return "the text must hold exactly one document";
    }
    return "an unknown rule";
}

bool read_document(std::span<const uint8_t> text, Node& root, ReadError& error) {
    return read_text(text, root, error, true);
}

bool read_value(std::span<const uint8_t> text, Node& root, ReadError& error) {
    return read_text(text, root, error, false);
}

const Node* find_entry(const Node& mapping, std::string_view key) noexcept {
    if (mapping.kind != NodeKind::mapping)
        return nullptr;
    for (const Node& entry : mapping.children) {
        if (entry.key.kind == KeyKind::string && entry.key.text == key)
            return &entry;
    }
    return nullptr;
}

} // namespace oa::formats::oamod
