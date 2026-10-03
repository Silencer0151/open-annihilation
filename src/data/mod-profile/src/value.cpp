// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The value tree: building, comparing, and writing it as canonical JSON, as
// indented JSON and as message text.

#include "oa/data/mod_profile/value.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace oa::data::mod_profile {

namespace {

/// The first byte that is not a C0 control character.
constexpr unsigned char first_printable = 0x20;
/// The first code point past the Basic Multilingual Plane, which UTF-16
/// writes as a surrogate pair.
constexpr uint32_t first_supplementary = 0x10000;
/// The first high and low surrogate.
constexpr uint32_t first_high_surrogate = 0xD800;
constexpr uint32_t first_low_surrogate = 0xDC00;
/// The bits each surrogate carries.
constexpr uint32_t surrogate_bits = 0x3FF;
/// The payload bits of a UTF-8 continuation byte, and of the lead bytes.
constexpr uint32_t continuation_bits = 0x3F;
constexpr uint32_t two_byte_lead_bits = 0x1F;
constexpr uint32_t three_byte_lead_bits = 0x0F;
constexpr uint32_t four_byte_lead_bits = 0x07;
/// The lead-byte thresholds of two, three and four byte sequences.
constexpr unsigned char first_three_byte_lead = 0xE0;
constexpr unsigned char first_four_byte_lead = 0xF0;
constexpr unsigned char first_lead = 0xC0;

/// Decodes UTF-8 into UTF-16 code units, the order RFC 8785 sorts keys in.
///
/// @param text valid UTF-8
/// @return its UTF-16 code units
std::u16string utf16_units(std::string_view text) {
    std::u16string units;
    size_t at = 0;
    while (at < text.size()) {
        const auto lead = static_cast<unsigned char>(text[at]);
        uint32_t code_point = lead;
        size_t length = 1;
        if (lead >= first_four_byte_lead) {
            code_point = lead & four_byte_lead_bits;
            length = 4;
        } else if (lead >= first_three_byte_lead) {
            code_point = lead & three_byte_lead_bits;
            length = 3;
        } else if (lead >= first_lead) {
            code_point = lead & two_byte_lead_bits;
            length = 2;
        }
        for (size_t index = 1; index < length && at + index < text.size(); ++index)
            code_point = (code_point << 6) |
                         (static_cast<unsigned char>(text[at + index]) & continuation_bits);
        at += length;
        if (code_point >= first_supplementary) {
            const uint32_t offset = code_point - first_supplementary;
            units.push_back(static_cast<char16_t>(first_high_surrogate + (offset >> 10)));
            units.push_back(static_cast<char16_t>(first_low_surrogate + (offset & surrogate_bits)));
        } else {
            units.push_back(static_cast<char16_t>(code_point));
        }
    }
    return units;
}

/// Appends a string as JSON writes it, with the escapes RFC 8785 requires.
///
/// @param text the string, valid UTF-8
/// @param[in,out] out the text appended to
void append_json_string(std::string_view text, std::string& out) {
    out += '"';
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < first_printable) {
                char escape[8]{};
                std::snprintf(escape, sizeof escape, "\\u%04x", static_cast<unsigned>(c));
                out += escape;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

/// Returns a map's members in the canonical order.
///
/// @param map the map
/// @return pointers to its members, sorted by their keys' UTF-16 code units
std::vector<const Member*> sorted_members(const Value& map) {
    std::vector<std::pair<std::u16string, const Member*>> keyed;
    keyed.reserve(map.members.size());
    for (const Member& member : map.members)
        keyed.emplace_back(utf16_units(member.key), &member);
    std::sort(keyed.begin(), keyed.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    std::vector<const Member*> members;
    members.reserve(keyed.size());
    for (const auto& entry : keyed)
        members.push_back(entry.second);
    return members;
}

/// Appends a value's canonical JSON.
///
/// @param value the value
/// @param[in,out] out the text appended to
void append_canonical(const Value& value, std::string& out) {
    switch (value.kind) {
    case ValueKind::null_value:
        out += "null";
        return;
    case ValueKind::boolean:
        out += value.boolean ? "true" : "false";
        return;
    case ValueKind::number:
        out += formats::oamod::canonical_number_text(value.number);
        return;
    case ValueKind::string:
        append_json_string(value.text, out);
        return;
    case ValueKind::list: {
        out += '[';
        bool first = true;
        for (const Value& item : value.items) {
            if (!first)
                out += ',';
            first = false;
            append_canonical(item, out);
        }
        out += ']';
        return;
    }
    case ValueKind::map: {
        out += '{';
        bool first = true;
        for (const Member* member : sorted_members(value)) {
            if (!first)
                out += ',';
            first = false;
            append_json_string(member->key, out);
            out += ':';
            append_canonical(member->value, out);
        }
        out += '}';
        return;
    }
    }
}

/// Appends a value as indented JSON.
///
/// @param value the value
/// @param depth its nesting level
/// @param[in,out] out the text appended to
void append_pretty(const Value& value, size_t depth, std::string& out) {
    const std::string inner((depth + 1) * 2, ' ');
    const std::string outer(depth * 2, ' ');
    if (value.kind == ValueKind::list && !value.items.empty()) {
        out += "[\n";
        for (size_t index = 0; index < value.items.size(); ++index) {
            out += inner;
            append_pretty(value.items[index], depth + 1, out);
            out += index + 1 < value.items.size() ? ",\n" : "\n";
        }
        out += outer + "]";
        return;
    }
    if (value.kind == ValueKind::map && !value.members.empty()) {
        out += "{\n";
        const auto members = sorted_members(value);
        for (size_t index = 0; index < members.size(); ++index) {
            out += inner;
            append_json_string(members[index]->key, out);
            out += ": ";
            append_pretty(members[index]->value, depth + 1, out);
            out += index + 1 < members.size() ? ",\n" : "\n";
        }
        out += outer + "}";
        return;
    }
    append_canonical(value, out);
}

} // namespace

Value make_boolean(bool value) {
    Value made{};
    made.kind = ValueKind::boolean;
    made.boolean = value;
    return made;
}

Value make_number(const formats::oamod::Number& number) {
    Value made{};
    made.kind = ValueKind::number;
    made.number = number;
    return made;
}

Value make_integer(int64_t integer) {
    return make_number(formats::oamod::number_from_integer(integer));
}

Value make_string(std::string_view text) {
    Value made{};
    made.kind = ValueKind::string;
    made.text = std::string{text};
    return made;
}

Value make_list() {
    Value made{};
    made.kind = ValueKind::list;
    return made;
}

Value make_map() {
    Value made{};
    made.kind = ValueKind::map;
    return made;
}

const Value* find_member(const Value& map, std::string_view key) noexcept {
    if (map.kind != ValueKind::map)
        return nullptr;
    for (const Member& member : map.members) {
        if (member.key == key)
            return &member.value;
    }
    return nullptr;
}

Value* find_member(Value& map, std::string_view key) noexcept {
    if (map.kind != ValueKind::map)
        return nullptr;
    for (Member& member : map.members) {
        if (member.key == key)
            return &member.value;
    }
    return nullptr;
}

Value& set_member(Value& map, std::string_view key, Value value) {
    if (Value* existing = find_member(map, key)) {
        *existing = std::move(value);
        return *existing;
    }
    map.members.push_back(Member{std::string{key}, std::move(value)});
    return map.members.back().value;
}

bool values_equal(const Value& left, const Value& right) noexcept {
    if (left.kind != right.kind)
        return false;
    switch (left.kind) {
    case ValueKind::null_value:
        return true;
    case ValueKind::boolean:
        return left.boolean == right.boolean;
    case ValueKind::number:
        return formats::oamod::compare_numbers(left.number, right.number) == 0;
    case ValueKind::string:
        return left.text == right.text;
    case ValueKind::list:
        if (left.items.size() != right.items.size())
            return false;
        for (size_t index = 0; index < left.items.size(); ++index) {
            if (!values_equal(left.items[index], right.items[index]))
                return false;
        }
        return true;
    case ValueKind::map:
        if (left.members.size() != right.members.size())
            return false;
        for (const Member& member : left.members) {
            const Value* other = find_member(right, member.key);
            if (other == nullptr || !values_equal(member.value, *other))
                return false;
        }
        return true;
    }
    return false;
}

std::string canonical_json(const Value& value) {
    std::string out;
    append_canonical(value, out);
    return out;
}

std::string pretty_json(const Value& value) {
    std::string out;
    append_pretty(value, 0, out);
    out += '\n';
    return out;
}

std::string value_text(const Value& value) {
    switch (value.kind) {
    case ValueKind::null_value:
        return "null";
    case ValueKind::boolean:
        return value.boolean ? "true" : "false";
    case ValueKind::number:
        return formats::oamod::canonical_number_text(value.number);
    case ValueKind::string: {
        std::string out;
        append_json_string(value.text, out);
        return out;
    }
    case ValueKind::list: {
        std::string out = "[";
        for (size_t index = 0; index < value.items.size(); ++index) {
            if (index > 0)
                out += ", ";
            out += value_text(value.items[index]);
        }
        return out + "]";
    }
    case ValueKind::map: {
        std::string out = "{";
        for (size_t index = 0; index < value.members.size(); ++index) {
            if (index > 0)
                out += ", ";
            out += value.members[index].key + ": " + value_text(value.members[index].value);
        }
        return out + "}";
    }
    }
    return {};
}

Value value_from_node(const formats::oamod::Node& node) {
    using formats::oamod::NodeKind;
    Value value{};
    value.position = node.position;
    switch (node.kind) {
    case NodeKind::null_value:
        break;
    case NodeKind::boolean:
        value.kind = ValueKind::boolean;
        value.boolean = node.boolean;
        break;
    case NodeKind::number:
        value.kind = ValueKind::number;
        value.number = node.number;
        break;
    case NodeKind::string:
        value.kind = ValueKind::string;
        value.text = node.text;
        break;
    case NodeKind::sequence:
        value.kind = ValueKind::list;
        for (const auto& child : node.children)
            value.items.push_back(value_from_node(child));
        break;
    case NodeKind::mapping:
        value.kind = ValueKind::map;
        for (const auto& child : node.children)
            value.members.push_back(Member{child.key.text, value_from_node(child)});
        break;
    }
    return value;
}

} // namespace oa::data::mod_profile
