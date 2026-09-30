// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The entry points of the document reader: form detection, the checks every
// text passes before either reader sees it, status messages, entry lookup,
// and the UTF-8 helpers both readers share.

#include "document_internal.hpp"

namespace oa::formats::oascript {

namespace detail {

namespace {

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

/// Tells whether a byte is a UTF-8 continuation byte within a range.
///
/// @param c the byte
/// @param low the smallest allowed value
/// @param high the largest allowed value
/// @return true when `c` lies in [low, high]
constexpr bool in_range(uint8_t c, uint8_t low, uint8_t high) noexcept {
    return c >= low && c <= high;
}

} // namespace

size_t byte_order_mark_length(std::span<const uint8_t> text) noexcept {
    if (text.size() < byte_order_mark.size())
        return 0;
    for (size_t index{}; index < byte_order_mark.size(); ++index) {
        if (text[index] != static_cast<uint8_t>(byte_order_mark[index]))
            return 0;
    }
    return byte_order_mark.size();
}

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
            // No overlong forms below U+0800, and no surrogates.
            if (lead == first_lead_of_three)
                second_low = first_second_after_e0;
            else if (lead == surrogate_lead)
                second_high = last_second_after_ed;
        } else if (in_range(lead, first_lead_of_four, last_lead_of_four)) {
            length = 4;
            // No overlong forms below U+10000, nothing past U+10FFFF.
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

int hex_digit_value(uint8_t c) noexcept {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

} // namespace detail

const char* read_status_message(ReadStatus status) noexcept {
    switch (status) {
    case ReadStatus::ok:
        return "no error";
    case ReadStatus::too_large:
        return "the text is longer than the largest script read";
    case ReadStatus::invalid_utf8:
        return "a byte sequence is not UTF-8";
    case ReadStatus::unexpected_end:
        return "the text ends inside a value";
    case ReadStatus::unexpected_character:
        return "a character is not allowed here";
    case ReadStatus::too_deep:
        return "mappings and sequences are nested too deeply";
    case ReadStatus::too_many_nodes:
        return "the document holds too many values";
    case ReadStatus::string_too_long:
        return "a key or string is too long";
    case ReadStatus::bad_escape:
        return "a backslash escape is not defined";
    case ReadStatus::bad_number:
        return "a number is not written as the form allows";
    case ReadStatus::number_out_of_range:
        return "a number has too many digits or decimal places";
    case ReadStatus::duplicate_key:
        return "a key is written twice in one mapping";
    case ReadStatus::trailing_content:
        return "there is text after the document";
    case ReadStatus::top_level_not_mapping:
        return "the document is not a mapping";
    case ReadStatus::tab_indentation:
        return "a tab is used for indentation";
    case ReadStatus::bad_indentation:
        return "the indentation nests nothing or breaks a level";
    case ReadStatus::unsupported_anchor:
        return "a YAML anchor (&) is not supported";
    case ReadStatus::unsupported_alias:
        return "a YAML alias (*) is not supported";
    case ReadStatus::unsupported_tag:
        return "a YAML tag (!) is not supported";
    case ReadStatus::unsupported_block_scalar:
        return "a YAML block scalar (| or >) is not supported";
    case ReadStatus::unsupported_directive:
        return "a YAML directive (%) is not supported";
    case ReadStatus::unsupported_complex_key:
        return "a YAML complex key (?) is not supported";
    case ReadStatus::several_documents:
        return "the text holds more than one YAML document";
    case ReadStatus::unsupported_number_form:
        return "hexadecimal and octal numbers, .inf and .nan are not supported";
    }
    return "an unknown status";
}

DocumentForm detect_form(std::span<const uint8_t> text) noexcept {
    for (size_t offset{detail::byte_order_mark_length(text)}; offset < text.size(); ++offset) {
        const uint8_t c{text[offset]};
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        return c == '{' ? DocumentForm::json : DocumentForm::yaml;
    }
    return DocumentForm::yaml;
}

bool read_document(std::span<const uint8_t> text, Node& root, ReadError& error) {
    root = Node{};
    error = ReadError{};
    if (text.size() > max_input_bytes) {
        error.status = ReadStatus::too_large;
        return false;
    }
    const size_t bad_byte{detail::find_invalid_utf8(text)};
    if (bad_byte != text.size()) {
        // Columns of the first line count from after the byte order mark.
        uint32_t line{1};
        size_t line_start{detail::byte_order_mark_length(text)};
        for (size_t offset{line_start}; offset < bad_byte; ++offset) {
            if (text[offset] == '\n') {
                ++line;
                line_start = offset + 1;
            }
        }
        error.status = ReadStatus::invalid_utf8;
        error.position = TextPosition{line, static_cast<uint32_t>(bad_byte - line_start + 1)};
        return false;
    }
    const size_t start{detail::byte_order_mark_length(text)};
    const bool read{
        detect_form(text) == DocumentForm::json ? detail::read_json(text, start, root, error)
                                                : detail::read_yaml(text, start, root, error)
    };
    if (!read)
        root = Node{};
    return read;
}

const Node* find_entry(const Node& mapping, std::string_view key) noexcept {
    if (mapping.kind != NodeKind::mapping)
        return nullptr;
    for (const Node& entry : mapping.children) {
        if (entry.key == key)
            return &entry;
    }
    return nullptr;
}

} // namespace oa::formats::oascript
