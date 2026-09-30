// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The YAML subset reader: block mappings and sequences nested by space
// indentation, flow mappings and sequences that may span lines, plain,
// single-quoted and double-quoted scalars on one line, '#' comments, and one
// optional '---' before the document. Everything else YAML has is refused by
// name.
//
// Line breaks are a line feed or a carriage return and line feed. Control
// characters other than tab, carriage return and line feed are refused
// everywhere, a lone carriage return too. Quoted scalars end on the line they
// start on, and a plain scalar is one line long.

#include "document_internal.hpp"

#include <cstdint>
#include <string>
#include <unordered_set>

namespace oa::formats::oascript::detail {

namespace {

/// The first byte that is not a C0 control character.
constexpr uint8_t first_printable = 0x20;
/// The hexadecimal digits of the \x, \u and \U escapes.
constexpr size_t byte_escape_digits = 2;
constexpr size_t short_unicode_escape_digits = 4;
constexpr size_t long_unicode_escape_digits = 8;
/// The code points of YAML's \N, \_, \L and \P escapes.
constexpr uint32_t next_line = 0x85;
constexpr uint32_t no_break_space = 0xA0;
constexpr uint32_t paragraph_separator = line_separator + 1;
/// The escape character, written by YAML's \e.
constexpr char escape_character = 0x1B;
/// The bell and vertical tab, written by YAML's \a and \v.
constexpr char bell_character = 0x07;
constexpr char vertical_tab_character = 0x0B;
/// The length of the '---' and '...' document markers.
constexpr size_t marker_length = 3;

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

/// What the next line that holds content starts with.
struct LineInfo {
    bool end{};      ///< the text has no more content
    bool marker{};   ///< the line is a '---' or '...' document marker
    size_t indent{}; ///< the spaces before the content
};

/// One YAML text being read.
class YamlReader {
  public:

    /// Starts reading a text.
    ///
    /// @param text the whole text
    /// @param start the offset after the byte order mark
    /// @param error receives a failure
    YamlReader(std::span<const uint8_t> text, size_t start, ReadError& error) noexcept
        : text_{text}, offset_{start}, line_start_{start}, error_{&error} {}

    /// Reads the whole text.
    ///
    /// @param[out] root the top-level mapping
    /// @return true on success
    bool read(Node& root) {
        if (!check_characters())
            return false;
        LineInfo line{};
        bool started{};
        while (true) {
            if (!line_info(line))
                return false;
            if (line.end)
                return fail(ReadStatus::top_level_not_mapping, here());
            if (!line.marker) {
                if (peek() == '%' && line.indent == 0 && !started)
                    return fail(ReadStatus::unsupported_directive, here());
                break;
            }
            if (peek() == '.' || started)
                return fail(ReadStatus::several_documents, here());
            started = true;
            offset_ += marker_length;
            if (!end_line())
                return false;
        }
        const bool flow_root{peek() == '[' || peek() == '{'};
        if (!read_block_node(root, line.indent, 1, 0))
            return false;
        if (root.kind != NodeKind::mapping)
            return fail(ReadStatus::top_level_not_mapping, root.position);
        if (!line_info(line))
            return false;
        if (line.end)
            return true;
        if (!line.marker)
            return fail(
                flow_root ? ReadStatus::trailing_content : ReadStatus::bad_indentation, here()
            );
        if (peek() == '-')
            return fail(ReadStatus::several_documents, here());
        offset_ += marker_length;
        if (!end_line() || !line_info(line))
            return false;
        if (!line.end)
            return fail(ReadStatus::several_documents, here());
        return true;
    }

  private:

    /// Refuses control characters other than tab, carriage return and line
    /// feed, and a carriage return not followed by a line feed.
    ///
    /// @return true when every character is allowed
    bool check_characters() noexcept {
        uint32_t line{1};
        size_t line_start{offset_};
        for (size_t offset{offset_}; offset < text_.size(); ++offset) {
            const uint8_t c{text_[offset]};
            const bool lone_return{
                c == '\r' && (offset + 1 >= text_.size() || text_[offset + 1] != '\n')
            };
            if ((c < first_printable && c != '\t' && c != '\n' && c != '\r') || lone_return)
                return fail(
                    ReadStatus::unexpected_character,
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
    /// @param status what went wrong
    /// @param position where
    /// @return false
    bool fail(ReadStatus status, TextPosition position) noexcept {
        error_->status = status;
        error_->position = position;
        return false;
    }

    /// Records a failure at the read offset: unexpected_end when the text
    /// has ended, otherwise the status given.
    ///
    /// @param status the failure when the text has not ended
    /// @return false
    bool fail_here(ReadStatus status = ReadStatus::unexpected_character) noexcept {
        return fail(offset_ >= text_.size() ? ReadStatus::unexpected_end : status, here());
    }

    /// Counts one more node, failing when the budget is spent.
    ///
    /// @param position where the node starts
    /// @return true while within max_node_count
    bool count_node(TextPosition position) noexcept {
        return take_node(budget_) || fail(ReadStatus::too_many_nodes, position);
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
        if (peek() == 0 && offset_ >= text_.size())
            return true;
        if (peek() != '\n' && peek() != '\r')
            return fail(ReadStatus::unexpected_character, here());
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
                return fail(ReadStatus::tab_indentation, here());
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
            return fail(ReadStatus::unsupported_anchor, here());
        case '*':
            return fail(ReadStatus::unsupported_alias, here());
        case '!':
            return fail(ReadStatus::unsupported_tag, here());
        case '|':
        case '>':
            return fail(
                flow ? ReadStatus::unexpected_character : ReadStatus::unsupported_block_scalar,
                here()
            );
        case '?':
            if (is_blank_or_end(peek(1)) || (flow && is_flow_indicator(peek(1))))
                return fail(ReadStatus::unsupported_complex_key, here());
            return true;
        case '%':
        case '@':
        case '`':
        case ',':
        case ']':
        case '}':
        case '#':
            return fail(ReadStatus::unexpected_character, here());
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
            return fail_here();
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
            return fail(ReadStatus::bad_escape, escape);
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
                return is_break_or_end(peek()) && offset_ >= text_.size()
                           ? fail_here()
                           : fail(ReadStatus::bad_escape, escape);
            code_point = code_point * 16 + static_cast<uint32_t>(digit);
            ++offset_;
        }
        if (code_point > max_code_point ||
            (code_point >= first_high_surrogate && code_point <= last_low_surrogate))
            return fail(ReadStatus::bad_escape, escape);
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
                return fail_here();
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
                return fail(ReadStatus::string_too_long, start);
            }
        }
    }

    /// Reads a scalar that may be a key: quoted, or plain.
    ///
    /// @param flow whether the scalar is inside a flow collection
    /// @param[out] node receives the scalar's kind and value, and its position
    /// @param[out] raw the plain scalar's text; empty for a quoted one
    /// @param[out] quoted whether the scalar was quoted
    /// @return true on success
    bool read_scalar(bool flow, Node& node, std::string_view& raw, bool& quoted) {
        node.position = here();
        quoted = peek() == '"' || peek() == '\'';
        raw = std::string_view{};
        if (quoted) {
            node.kind = NodeKind::string;
            return read_quoted(node.text);
        }
        if (!check_value_start(flow))
            return false;
        raw = read_plain_text(flow);
        if (raw.empty())
            return fail_here();
        return true;
    }

    /// Resolves a plain scalar's text into its node.
    ///
    /// @param raw the text
    /// @param[in,out] node the node
    /// @return true on success
    bool resolve(std::string_view raw, Node& node) {
        const ReadStatus status{resolve_plain_scalar(raw, node)};
        return status == ReadStatus::ok || fail(status, node.position);
    }

    /// Adds a key to a mapping's key set, refusing a duplicate.
    ///
    /// @param[in,out] keys the keys so far
    /// @param key the key
    /// @param position where the key starts
    /// @return true when the key is new
    bool
    add_key(std::unordered_set<std::string>& keys, const std::string& key, TextPosition position) {
        return keys.insert(key).second || fail(ReadStatus::duplicate_key, position);
    }

    /// Checks the nesting depth of a new mapping or sequence and counts its node.
    ///
    /// @param depth its depth
    /// @param position where it starts
    /// @return true within the limits
    bool open_collection(uint32_t depth, TextPosition position) noexcept {
        if (depth > max_nesting_depth)
            return fail(ReadStatus::too_deep, position);
        return count_node(position);
    }

    /// Makes a scalar just read the key of an entry.
    ///
    /// @param quoted whether the scalar was quoted
    /// @param raw the plain scalar's text
    /// @param[in,out] scalar the scalar; a quoted one's text is moved out
    /// @param[out] entry receives the key and its position
    /// @return false when the key is longer than max_string_bytes
    bool take_key(bool quoted, std::string_view raw, Node& scalar, Node& entry) {
        entry.key_position = scalar.position;
        if (!quoted && raw.size() > max_string_bytes)
            return fail(ReadStatus::string_too_long, entry.key_position);
        entry.key = quoted ? std::move(scalar.text) : std::string{raw};
        return true;
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
                return fail(ReadStatus::unsupported_complex_key, node.position);
            return end_line();
        }
        std::string_view raw{};
        bool quoted{};
        if (!read_scalar(false, node, raw, quoted))
            return false;
        skip_inline_space();
        if (peek() == ':' && is_blank_or_end(peek(1))) {
            Node first_key{};
            if (!take_key(quoted, raw, node, first_key))
                return false;
            node.kind = NodeKind::null_value;
            node.text.clear();
            return read_block_mapping(node, indent, depth, std::move(first_key));
        }
        if (!count_node(node.position))
            return false;
        if (!quoted && !resolve(raw, node))
            return false;
        return end_line();
    }

    /// Reads the key of a block mapping entry and its ':', the read offset
    /// on the key's first byte.
    ///
    /// @param[out] entry receives the key and its position
    /// @return true on success
    bool read_block_key(Node& entry) {
        if (at_sequence_entry())
            return fail(ReadStatus::bad_indentation, here());
        if (peek() == '[' || peek() == '{')
            return fail(ReadStatus::unsupported_complex_key, here());
        Node scalar{};
        std::string_view raw{};
        bool quoted{};
        if (!read_scalar(false, scalar, raw, quoted))
            return false;
        if (!take_key(quoted, raw, scalar, entry))
            return false;
        skip_inline_space();
        if (peek() != ':' || !is_blank_or_end(peek(1)))
            return fail_here();
        return true;
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
            if (!add_key(keys, entry.key, entry.key_position))
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
                return fail(ReadStatus::bad_indentation, here());
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
            return fail(ReadStatus::unexpected_character, here());
        if (peek() == '[' || peek() == '{') {
            if (!read_flow_collection(node, depth, flow_floor))
                return false;
            skip_inline_space();
            if (peek() == ':' && is_blank_or_end(peek(1)))
                return fail(ReadStatus::unsupported_complex_key, node.position);
            return end_line();
        }
        std::string_view raw{};
        bool quoted{};
        if (!read_scalar(false, node, raw, quoted))
            return false;
        skip_inline_space();
        if (peek() == ':' && is_blank_or_end(peek(1)))
            return fail(ReadStatus::unexpected_character, here());
        if (!count_node(node.position))
            return false;
        if (!quoted && !resolve(raw, node))
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
                return fail(ReadStatus::bad_indentation, here());
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
                return fail(ReadStatus::tab_indentation, here());
            const uint8_t c{peek()};
            if (!is_break_or_end(c) && c != '#' && c != ']' && c != '}' && column() < flow_floor)
                return fail(ReadStatus::bad_indentation, here());
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
        bool quoted{};
        if (!read_scalar(true, node, raw, quoted))
            return false;
        if (!count_node(node.position))
            return false;
        return quoted || resolve(raw, node);
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
                if (!read_flow_entry(item, depth, flow_floor) ||
                    !add_key(keys, item.key, item.key_position))
                    return false;
            } else {
                if (!read_flow_node(item, depth + 1, flow_floor))
                    return false;
                if (!skip_flow_space(flow_floor))
                    return false;
                if (peek() == ':')
                    return fail(ReadStatus::unexpected_character, here());
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
            return fail(ReadStatus::unsupported_complex_key, here());
        Node scalar{};
        std::string_view raw{};
        bool quoted{};
        if (!read_scalar(true, scalar, raw, quoted))
            return false;
        if (!take_key(quoted, raw, scalar, entry))
            return false;
        skip_inline_space();
        const bool has_value{
            peek() == ':' && (quoted || is_blank_or_end(peek(1)) || is_flow_indicator(peek(1)))
        };
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
    NodeBudget budget_{};
    ReadError* error_{};
};

} // namespace

bool read_yaml(std::span<const uint8_t> text, size_t start, Node& root, ReadError& error) {
    YamlReader reader{text, start, error};
    return reader.read(root);
}

} // namespace oa::formats::oascript::detail
