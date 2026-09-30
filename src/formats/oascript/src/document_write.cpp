// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The JSON and YAML writers. The same tree always gives the same bytes:
// entries keep their order, numbers are written from their decimals digit by
// digit, lines end with a line feed and the text with one.
//
// Layout: the top-level mapping is always written one entry per line. Below
// it, a mapping or sequence whose descendants are all scalars is written in
// flow style when its whole line fits in max_flow_width bytes: indentation,
// the key and ": " or the "- " before it, the collection and, in JSON, the
// comma after it. An empty mapping or sequence is always written in flow
// style ({} or []). Everything else is written in block style (YAML) or one
// entry per line (JSON), yaml_indent spaces a level. In YAML a sequence item
// that is written in block style starts on the dash's line ("- key: value").
//
// An empty top-level mapping is written "{}" in both forms: YAML reads it as
// JSON and gives the same tree.

#include "document_internal.hpp"

#include <string>

namespace oa::formats::oascript {

namespace {

/// The first byte that is not a C0 control character.
constexpr uint8_t first_printable = 0x20;
/// The first byte that is not ASCII: a byte of a multi-byte character.
constexpr uint8_t first_non_ascii = 0x80;
/// The bits of one hexadecimal digit.
constexpr uint8_t hex_digit_bits = 4;
constexpr uint8_t hex_digit_mask = 0xF;
/// Words YAML 1.1 readers commonly take for booleans; written quoted so
/// that they read as strings there too. The single letters y and n are left
/// plain, since camera positions use y as a key.
constexpr std::string_view yaml_1_1_booleans[]{
    "yes", "Yes", "YES", "no", "No", "NO", "on", "On", "ON", "off", "Off", "OFF"
};

/// The UTF-8 encodings of the line and paragraph separators.
constexpr std::string_view line_separator_utf8{"\xE2\x80\xA8"};
constexpr std::string_view paragraph_separator_utf8{"\xE2\x80\xA9"};
/// The lead byte of the two-byte encodings of U+0080 to U+00BF.
constexpr uint8_t lead_of_c1_controls = 0xC2;
/// The C1 control range, U+0080 to U+009F, which the second byte of their
/// encoding spells out.
constexpr uint8_t first_c1_control = 0x80;
constexpr uint8_t last_c1_control = 0x9F;

/// Finds a character at an offset that is written escaped: a C1 control
/// character, the line and paragraph separators (U+2028, U+2029) or a byte
/// order mark (U+FEFF). YAML 1.1 readers take some of them for line
/// breaks, and YAML allows none of them unquoted.
///
/// @param text UTF-8 text
/// @param offset where to look
/// @param[out] code_point the character found
/// @return the length of its encoding, or 0 when none starts at `offset`
size_t escaped_character_at(std::string_view text, size_t offset, uint32_t& code_point) noexcept {
    const std::string_view rest{text.substr(offset)};
    if (rest.size() >= 2 && static_cast<uint8_t>(rest[0]) == lead_of_c1_controls &&
        static_cast<uint8_t>(rest[1]) >= first_c1_control &&
        static_cast<uint8_t>(rest[1]) <= last_c1_control) {
        code_point = static_cast<uint8_t>(rest[1]);
        return 2;
    }
    if (rest.starts_with(line_separator_utf8)) {
        code_point = detail::line_separator;
        return line_separator_utf8.size();
    }
    if (rest.starts_with(paragraph_separator_utf8)) {
        code_point = detail::line_separator + 1;
        return paragraph_separator_utf8.size();
    }
    if (rest.starts_with(detail::byte_order_mark)) {
        code_point = detail::byte_order_mark_code_point;
        return detail::byte_order_mark.size();
    }
    return 0;
}

/// Tells whether a node is a scalar.
///
/// @param node the node
/// @return true for null, boolean, number and string nodes
bool is_scalar(const Node& node) noexcept {
    return node.kind != NodeKind::mapping && node.kind != NodeKind::sequence;
}

/// Tells whether every child of a collection is a scalar.
///
/// @param node a mapping or sequence
/// @return true when no child is a mapping or sequence
bool has_only_scalars(const Node& node) noexcept {
    for (const Node& child : node.children) {
        if (!is_scalar(child))
            return false;
    }
    return true;
}

/// Appends a key: plain when YAML reads it back unchanged, quoted otherwise;
/// always quoted in JSON.
///
/// @param key the key
/// @param form the form written
/// @param[in,out] out the text
void append_key(const std::string& key, DocumentForm form, std::string& out) {
    if (form == DocumentForm::yaml && detail::yaml_plain_safe(key))
        out.append(key);
    else
        detail::append_quoted(key, out);
}

/// Appends a scalar's text.
///
/// @param node a scalar
/// @param form the form written
/// @param[in,out] out the text
void append_scalar(const Node& node, DocumentForm form, std::string& out) {
    switch (node.kind) {
    case NodeKind::null_value:
        out.append("null");
        return;
    case NodeKind::boolean:
        out.append(node.boolean ? "true" : "false");
        return;
    case NodeKind::number:
        out.append(decimal_text(node.number));
        return;
    case NodeKind::string:
        append_key(node.text, form, out);
        return;
    case NodeKind::mapping:
    case NodeKind::sequence:
        return;
    }
}

/// Returns a collection of scalars written in flow style.
///
/// @param node a mapping or sequence whose children are all scalars
/// @param form the form written
/// @return the text, such as "{ x: 1, y: 2 }" in YAML or {"x": 1, "y": 2} in JSON
std::string flow_text(const Node& node, DocumentForm form) {
    const bool mapping{node.kind == NodeKind::mapping};
    if (node.children.empty())
        return mapping ? "{}" : "[]";
    std::string text{};
    text.append(mapping ? (form == DocumentForm::yaml ? "{ " : "{") : "[");
    for (size_t index{}; index < node.children.size(); ++index) {
        const Node& child{node.children[index]};
        if (index != 0)
            text.append(", ");
        if (mapping) {
            append_key(child.key, form, text);
            text.append(": ");
        }
        append_scalar(child, form, text);
    }
    text.append(mapping ? (form == DocumentForm::yaml ? " }" : "}") : "]");
    return text;
}

/// Decides whether a collection is written in flow style.
///
/// @param node a mapping or sequence
/// @param form the form written
/// @param line_before the bytes on its line before it
/// @param line_after the bytes on its line after it
/// @param[out] text the flow text, when flow style is chosen
/// @return true for flow style
bool choose_flow(
    const Node& node, DocumentForm form, size_t line_before, size_t line_after, std::string& text
) {
    if (node.children.empty()) {
        text = flow_text(node, form);
        return true;
    }
    if (!has_only_scalars(node))
        return false;
    text = flow_text(node, form);
    return line_before + text.size() + line_after <= max_flow_width;
}

/// Writes a non-empty mapping or sequence in YAML block style.
///
/// @param node the collection
/// @param indent the column of its keys or dashes
/// @param first_prefix what the first line starts with in place of `indent` spaces
/// @param[in,out] out the text
void write_yaml_block(
    const Node& node, size_t indent, const std::string& first_prefix, std::string& out
) {
    const bool mapping{node.kind == NodeKind::mapping};
    const std::string spaces(indent, ' ');
    for (size_t index{}; index < node.children.size(); ++index) {
        const Node& child{node.children[index]};
        std::string head{index == 0 ? first_prefix : spaces};
        if (mapping) {
            append_key(child.key, DocumentForm::yaml, head);
            head.push_back(':');
        } else {
            head.push_back('-');
        }
        if (is_scalar(child)) {
            out.append(head).push_back(' ');
            append_scalar(child, DocumentForm::yaml, out);
            out.push_back('\n');
            continue;
        }
        std::string flow{};
        if (choose_flow(child, DocumentForm::yaml, head.size() + 1, 0, flow)) {
            out.append(head).append(" ").append(flow).push_back('\n');
            continue;
        }
        if (mapping) {
            out.append(head).push_back('\n');
            write_yaml_block(
                child, indent + yaml_indent, std::string(indent + yaml_indent, ' '), out
            );
        } else {
            head.push_back(' ');
            write_yaml_block(child, indent + yaml_indent, head, out);
        }
    }
}

/// Writes a non-empty mapping or sequence one entry per line in JSON, from
/// its opening bracket to its closing one.
///
/// @param node the collection
/// @param indent the column of its closing bracket
/// @param[in,out] out the text
void write_json_block(const Node& node, size_t indent, std::string& out) {
    const bool mapping{node.kind == NodeKind::mapping};
    out.append(mapping ? "{\n" : "[\n");
    const std::string spaces(indent + yaml_indent, ' ');
    for (size_t index{}; index < node.children.size(); ++index) {
        const Node& child{node.children[index]};
        const bool last{index + 1 == node.children.size()};
        std::string head{spaces};
        if (mapping) {
            detail::append_quoted(child.key, head);
            head.append(": ");
        }
        out.append(head);
        std::string flow{};
        if (is_scalar(child))
            append_scalar(child, DocumentForm::json, out);
        else if (choose_flow(child, DocumentForm::json, head.size(), last ? 0 : 1, flow))
            out.append(flow);
        else
            write_json_block(child, indent + yaml_indent, out);
        out.append(last ? "\n" : ",\n");
    }
    out.append(indent, ' ');
    out.append(mapping ? "}" : "]");
}

} // namespace

namespace detail {

bool yaml_plain_safe(std::string_view text) {
    if (text.empty())
        return false;
    // Only letters, digits, bytes of multi-byte characters and a few
    // punctuation marks that no YAML rule gives a meaning to, and no
    // indicator first.
    for (const char c : text) {
        const uint8_t byte{static_cast<uint8_t>(c)};
        const bool letter{(byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')};
        const bool safe_mark{
            c == '_' || c == '.' || c == '/' || c == '-' || c == '+' || c == '(' || c == ')' ||
            c == '=' || c == ';' || c == '~' || c == '$' || c == '^' || c == ' '
        };
        if (!letter && !is_digit(byte) && byte < first_non_ascii && !safe_mark)
            return false;
    }
    // A leading digit, sign or point is quoted even where YAML 1.2 reads a
    // string (1_000, 0b1, +x), since YAML 1.1 readers may read a number.
    const char first{text.front()};
    if (first == '-' || first == '+' || first == '.' || first == ' ' ||
        is_digit(static_cast<uint8_t>(first)) || text.back() == ' ')
        return false;
    for (const std::string_view word : yaml_1_1_booleans) {
        if (text == word)
            return false;
    }
    for (size_t offset{}; offset < text.size(); ++offset) {
        uint32_t code_point{};
        if (escaped_character_at(text, offset, code_point) != 0)
            return false;
    }
    Node resolved{};
    return resolve_plain_scalar(text, resolved) == ReadStatus::ok &&
           resolved.kind == NodeKind::string;
}

void append_quoted(std::string_view text, std::string& out) {
    constexpr std::string_view hex_digits{"0123456789abcdef"};
    const auto append_unicode_escape = [&out, &hex_digits](uint32_t code_point) {
        out.append("\\u");
        for (int shift{3 * hex_digit_bits}; shift >= 0; shift -= hex_digit_bits)
            out.push_back(hex_digits[(code_point >> shift) & hex_digit_mask]);
    };
    out.push_back('"');
    for (size_t offset{}; offset < text.size(); ++offset) {
        const char c{text[offset]};
        const uint8_t byte{static_cast<uint8_t>(c)};
        uint32_t code_point{};
        const size_t escaped_length{escaped_character_at(text, offset, code_point)};
        if (escaped_length != 0) {
            append_unicode_escape(code_point);
            offset += escaped_length - 1;
            continue;
        }
        switch (c) {
        case '"':
            out.append("\\\"");
            break;
        case '\\':
            out.append("\\\\");
            break;
        case '\b':
            out.append("\\b");
            break;
        case '\f':
            out.append("\\f");
            break;
        case '\n':
            out.append("\\n");
            break;
        case '\r':
            out.append("\\r");
            break;
        case '\t':
            out.append("\\t");
            break;
        default:
            if (byte < first_printable)
                append_unicode_escape(byte);
            else
                out.push_back(c);
            break;
        }
    }
    out.push_back('"');
}

} // namespace detail

std::string write_document(const Node& root, DocumentForm form) {
    std::string out{};
    if (root.children.empty()) {
        out.append("{}\n");
        return out;
    }
    if (form == DocumentForm::yaml) {
        write_yaml_block(root, 0, std::string{}, out);
        return out;
    }
    write_json_block(root, 0, out);
    out.push_back('\n');
    return out;
}

} // namespace oa::formats::oascript
