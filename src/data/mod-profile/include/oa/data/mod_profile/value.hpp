// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The value tree the resolver works on and hands out: the effective profile
// as maps, lists, strings, numbers, booleans and null, with string keys. Its
// canonical form is the JSON canonicalization scheme's (RFC 8785): members
// sorted by their keys' UTF-16 code units, no white space, numbers written as
// the shortest text of their double, strings with only the required escapes.
#pragma once

#include "oa/formats/oamod.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace oa::data::mod_profile {

struct Member;

/// What a value holds.
enum class ValueKind : uint8_t {
    null_value,
    boolean,
    number,
    string,
    list,
    map,
};

/// One value: a scalar, a list or a map whose members keep their order.
struct Value {
    ValueKind kind{ValueKind::null_value};
    bool boolean{};                  ///< a boolean's value
    formats::oamod::Number number{}; ///< a number's value
    std::string text{};              ///< a string's value, UTF-8
    std::vector<Value> items{};      ///< a list's items
    // No initializer here: one would need Member complete before it is.
    std::vector<Member> members; ///< a map's members, keys unique
    formats::oamod::TextPosition
        position{}; ///< where a value read from a profile starts; 0:0 otherwise
};

/// One member of a map.
struct Member {
    std::string key{}; ///< the key, UTF-8
    Value value{};     ///< the value
};

/// Makes a boolean.
///
/// @param value the boolean
/// @return the value
[[nodiscard]] Value make_boolean(bool value);

/// Makes a number.
///
/// @param number the number
/// @return the value
[[nodiscard]] Value make_number(const formats::oamod::Number& number);

/// Makes an integer.
///
/// @param integer the integer, within +-2^53
/// @return the value, a number marked as an integer
[[nodiscard]] Value make_integer(int64_t integer);

/// Makes a string.
///
/// @param text the string, UTF-8
/// @return the value
[[nodiscard]] Value make_string(std::string_view text);

/// Makes an empty list.
///
/// @return the value
[[nodiscard]] Value make_list();

/// Makes an empty map.
///
/// @return the value
[[nodiscard]] Value make_map();

/// Returns a map's member under a key.
///
/// @param map the map
/// @param key the key
/// @return the member's value, or null when `map` is not a map or has no such key
[[nodiscard]] const Value* find_member(const Value& map, std::string_view key) noexcept;

/// Returns a map's member under a key, for changing.
///
/// @param map the map
/// @param key the key
/// @return the member's value, or null when `map` is not a map or has no such key
[[nodiscard]] Value* find_member(Value& map, std::string_view key) noexcept;

/// Sets a map's member, replacing one under the same key or appending a new one.
///
/// @param[in,out] map the map
/// @param key the key
/// @param value the value
/// @return the member's value as stored
Value& set_member(Value& map, std::string_view key, Value value);

/// Compares two values as JSON values: numbers by value (1 and 1.0 are
/// equal), maps by their members whatever their order.
///
/// @param left the first value
/// @param right the second value
/// @return true when they are equal
[[nodiscard]] bool values_equal(const Value& left, const Value& right) noexcept;

/// Writes a value in canonical JSON (RFC 8785).
///
/// @param value the value; strings are valid UTF-8
/// @return the canonical text
[[nodiscard]] std::string canonical_json(const Value& value);

/// Writes a value as indented JSON, members sorted as the canonical form
/// sorts them, for people to read.
///
/// @param value the value
/// @return the text, two spaces a level, ending with a line feed
[[nodiscard]] std::string pretty_json(const Value& value);

/// Writes a value briefly for a message, as YAML's flow style would.
///
/// @param value the value
/// @return the text, strings quoted
[[nodiscard]] std::string value_text(const Value& value);

/// Converts a node of a profile's tree into a value. Integer keys become
/// their digits.
///
/// @param node the node
/// @return the value, with the node's position
[[nodiscard]] Value value_from_node(const formats::oamod::Node& node);

} // namespace oa::data::mod_profile
