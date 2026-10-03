// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The binding of every registry meaning to the one record field that holds
// it (visit_bindings, generated from the registry into bindings.inc), and the
// conversion of a resolved value into a field of any of the records' types.
#pragma once

#include "oa/data/mod_profile.hpp"
#include "oa/data/mod_profile/registry.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace oa::data::mod_profile {

#include "oa/data/mod_profile/bindings.inc"

namespace binding_types {

/// Tells whether a type is a std::array.
template <class Type>
struct is_array : std::false_type {};

template <class Item, size_t Count>
struct is_array<std::array<Item, Count>> : std::true_type {};

/// Tells whether a type is a FixedList.
template <class Type>
struct is_fixed_list : std::false_type {};

template <class Item, size_t Capacity>
struct is_fixed_list<FixedList<Item, Capacity>> : std::true_type {};

/// Tells whether a type is a FixedText.
template <class Type>
struct is_fixed_text : std::false_type {};

template <size_t Capacity>
struct is_fixed_text<FixedText<Capacity>> : std::true_type {};

/// Tells whether a type is an EnumSet.
template <class Type>
struct is_enum_set : std::false_type {};

template <class Enum, size_t Count>
struct is_enum_set<EnumSet<Enum, Count>> : std::true_type {};

/// Tells whether a type is a std::optional.
template <class Type>
struct is_optional : std::false_type {};

template <class Item>
struct is_optional<std::optional<Item>> : std::true_type {};

/// Tells whether a type is a std::vector.
template <class Type>
struct is_vector : std::false_type {};

template <class Item>
struct is_vector<std::vector<Item>> : std::true_type {};

} // namespace binding_types

/// Returns the number of the enumeration value a string names.
///
/// @param spec the enumeration's spec
/// @param text the value's name
/// @return its number, or -1 when the spec has no such value
[[nodiscard]] inline int enum_index(const registry::ValueSpec& spec, std::string_view text) {
    const auto values = registry::enum_values_of(spec);
    for (size_t index = 0; index < values.size(); ++index) {
        if (values[index] == text)
            return static_cast<int>(index);
    }
    return -1;
}

/// Stores a resolved value in a record field.
///
/// The value has been checked against the spec, so it has the spec's type and
/// bounds; a field too small for it is left holding what fits, and the
/// bindings test makes sure every field is large enough.
///
/// @param spec the value's type, for enumeration names
/// @param value the resolved value
/// @param[out] field the field
/// @return false when the value did not fit the field
template <class Field>
bool assign_field(const registry::ValueSpec& spec, const Value& value, Field& field) {
    using namespace binding_types;
    if constexpr (std::is_same_v<Field, bool>) {
        field = value.boolean;
        return value.kind == ValueKind::boolean;
    } else if constexpr (std::is_same_v<Field, double>) {
        field = formats::oamod::number_to_double(value.number);
        return value.kind == ValueKind::number;
    } else if constexpr (std::is_integral_v<Field>) {
        int64_t whole = 0;
        if (value.kind != ValueKind::number || !formats::oamod::integer_value(value.number, whole))
            return false;
        field = static_cast<Field>(whole);
        return static_cast<int64_t>(field) == whole;
    } else if constexpr (std::is_enum_v<Field>) {
        const int index = enum_index(spec, value.text);
        if (index < 0)
            return false;
        field = static_cast<Field>(index);
        return true;
    } else if constexpr (std::is_same_v<Field, std::string>) {
        field = value.text;
        return value.kind == ValueKind::string;
    } else if constexpr (is_fixed_text<Field>::value) {
        return field.assign(value.text);
    } else if constexpr (is_optional<Field>::value) {
        if (value.kind == ValueKind::string && value.text == "none") {
            field.reset();
            return true;
        }
        typename Field::value_type inner{};
        const bool fits = assign_field(spec, value, inner);
        field = inner;
        return fits;
    } else if constexpr (is_array<Field>::value) {
        bool fits = value.items.size() == field.size();
        for (size_t index = 0; index < field.size() && index < value.items.size(); ++index)
            fits = assign_field(spec, value.items[index], field[index]) && fits;
        return fits;
    } else if constexpr (is_fixed_list<Field>::value) {
        field = Field{};
        bool fits = true;
        for (const Value& item : value.items) {
            typename decltype(field.items)::value_type inner{};
            fits = assign_field(spec, item, inner) && fits;
            fits = field.push(inner) && fits;
        }
        return fits;
    } else if constexpr (is_enum_set<Field>::value) {
        field = Field{};
        bool fits = true;
        for (const Value& item : value.items) {
            const int index = enum_index(spec, item.text);
            fits = fits && index >= 0;
            if (index >= 0)
                field.members.at(static_cast<size_t>(index)) = true;
        }
        return fits;
    } else if constexpr (is_vector<Field>::value) {
        field.clear();
        bool fits = true;
        for (const Value& item : value.items) {
            typename Field::value_type inner{};
            fits = assign_field(spec, item, inner) && fits;
            field.push_back(std::move(inner));
        }
        return fits;
    } else {
        static_assert(sizeof(Field) == 0, "a record field of a type the bindings do not handle");
        return false;
    }
}

} // namespace oa::data::mod_profile
