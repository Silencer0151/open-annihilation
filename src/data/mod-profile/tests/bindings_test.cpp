// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The registry and the records agree: every parameter, hack, value and data
// key of the registry is bound to exactly one record field; every field can
// hold every value its parameter allows; every field starts at its
// parameter's 3.1c baseline, so a default-constructed profile is 3.1c; and
// every baseline, default and preset of the registry passes the resolver's
// own checks, presets are complete and constraints hold.

#include "oa/data/mod_profile/bindings.hpp"
#include "oa/test/check.hpp"

#include <cstdio>
#include <limits>
#include <map>
#include <string>
#include <type_traits>

namespace {

using namespace oa::data::mod_profile;
using registry::ValueSpec;
using registry::ValueType;

/// Tells whether a field of some type can hold every value a spec allows.
template <class Field>
bool fits(const ValueSpec& spec, ValueType type) {
    using namespace binding_types;
    if constexpr (std::is_same_v<Field, bool>) {
        return type == ValueType::boolean;
    } else if constexpr (std::is_same_v<Field, double>) {
        return type == ValueType::decimal;
    } else if constexpr (std::is_integral_v<Field>) {
        if (type != ValueType::integer && type != ValueType::integer_or_none)
            return false;
        const auto lowest =
            oa::formats::oamod::number_from_integer(std::numeric_limits<Field>::min());
        const auto highest = oa::formats::oamod::number_from_integer(
            static_cast<int64_t>(
                std::min<uint64_t>(std::numeric_limits<Field>::max(), uint64_t{1} << 53)
            )
        );
        return spec.has_minimum && spec.has_maximum &&
               oa::formats::oamod::compare_numbers(spec.minimum, lowest) >= 0 &&
               oa::formats::oamod::compare_numbers(spec.maximum, highest) <= 0;
    } else if constexpr (std::is_enum_v<Field>) {
        return type == ValueType::enumeration && enum_size(Field{}) == spec.value_count;
    } else if constexpr (std::is_same_v<Field, std::string>) {
        return type == ValueType::string;
    } else if constexpr (is_fixed_text<Field>::value) {
        // A character takes at most four bytes of UTF-8.
        return type == ValueType::string && spec.max_length >= 0 &&
               static_cast<size_t>(spec.max_length) * 4 <= Field{}.bytes.size();
    } else if constexpr (is_optional<Field>::value) {
        return type == ValueType::integer_or_none && fits<typename Field::value_type>(spec, type);
    } else {
        const ValueType item = type == ValueType::integer_list   ? ValueType::integer
                               : type == ValueType::decimal_list ? ValueType::decimal
                               : type == ValueType::string_list  ? ValueType::string
                                                                 : ValueType::enumeration;
        if constexpr (is_array<Field>::value) {
            return type != ValueType::enumeration_set && spec.has_length &&
                   spec.length_minimum == spec.length_maximum &&
                   spec.length_maximum == Field{}.size() &&
                   fits<typename Field::value_type>(spec, item);
        } else if constexpr (is_fixed_list<Field>::value) {
            return type != ValueType::enumeration_set && spec.has_length &&
                   spec.length_maximum <= Field{}.items.size() &&
                   fits<typename decltype(Field{}.items)::value_type>(spec, item);
        } else if constexpr (is_enum_set<Field>::value) {
            return type == ValueType::enumeration_set && Field{}.members.size() == spec.value_count;
        } else if constexpr (is_vector<Field>::value) {
            return type == ValueType::string_list;
        } else {
            return false;
        }
    }
}

/// Checks each binding and counts how often each meaning is bound.
struct Checker {
    std::map<std::string, int> bound{};

    /// Checks a field against its spec and its baseline.
    template <class Field>
    void check(const std::string& name, const ValueSpec& spec, const Field& field) {
        ++bound[name];
        if (!fits<Field>(spec, spec.type)) {
            std::fprintf(
                stderr, "%s: the field cannot hold every value the registry allows\n", name.c_str()
            );
            OA_CHECK(!"field fits its parameter");
        }
        Field baseline{};
        if (!assign_field(spec, registry::literal_value(spec.baseline), baseline) ||
            !(baseline == field)) {
            std::fprintf(stderr, "%s: the field does not start at its baseline\n", name.c_str());
            OA_CHECK(!"field starts at its baseline");
        }
    }

    template <class Field>
    void value(std::string_view id, const Field& field) {
        const auto* entry = registry::find_entry(id);
        OA_CHECK(entry != nullptr);
        if (entry != nullptr)
            check(std::string{id}, entry->value, field);
    }

    void hack(std::string_view id, const bool& enabled) {
        ++bound[std::string{id} + " enabled"];
        OA_CHECK(!enabled);
    }

    template <class Field>
    void parameter(std::string_view id, std::string_view name, const Field& field) {
        const auto* entry = registry::find_entry(id);
        const int index = entry != nullptr ? registry::find_parameter(*entry, name) : -1;
        OA_CHECK(index >= 0);
        if (index >= 0)
            check(
                std::string{id} + "." + std::string{name},
                registry::parameters_of(*entry)[static_cast<size_t>(index)].value,
                field
            );
    }

    void data_key(std::string_view id, const std::string& field) {
        ++bound[std::string{id}];
        OA_CHECK(field.empty());
    }
};

void test_bindings() {
    const ModProfile profile{};
    Checker checker{};
    visit_bindings(checker, profile);
    size_t expected = 0;
    for (const auto& entry : registry::table().entries) {
        std::vector<std::string> names;
        switch (entry.kind) {
        case registry::EntryKind::identity:
        case registry::EntryKind::layout:
        case registry::EntryKind::string:
        case registry::EntryKind::media:
        case registry::EntryKind::data_key:
            names.emplace_back(entry.id);
            break;
        case registry::EntryKind::hack:
            names.push_back(std::string{entry.id} + " enabled");
            [[fallthrough]];
        case registry::EntryKind::limit:
            for (const auto& parameter : registry::parameters_of(entry))
                names.push_back(std::string{entry.id} + "." + std::string{parameter.name});
            break;
        case registry::EntryKind::script_extension:
            break;
        }
        for (const std::string& name : names) {
            ++expected;
            const auto found = checker.bound.find(name);
            const int count = found == checker.bound.end() ? 0 : found->second;
            if (count != 1)
                std::fprintf(stderr, "%s is bound %d times\n", name.c_str(), count);
            OA_CHECK(count == 1);
        }
    }
    // Nothing is bound that the registry does not name.
    OA_CHECK(checker.bound.size() == expected);
}

void test_script_extensions() {
    // Each script extension of the registry has its value in ScriptExtension.
    size_t count = 0;
    for (const auto& entry : registry::table().entries) {
        if (entry.kind != registry::EntryKind::script_extension)
            continue;
        ++count;
        OA_CHECK(oa::data::match_rules::script_extension_ids[count] == entry.id);
    }
    OA_CHECK(count + 1 == oa::data::match_rules::script_extension_ids.size());
    OA_CHECK(count <= oa::data::match_rules::max_script_mounts);
}

/// Checks a literal against a spec.
///
/// @param spec the spec
/// @param literal the literal
/// @param where what the literal is, for a failure
void check_literal(const ValueSpec& spec, uint16_t literal, const std::string& where) {
    Value normal{};
    if (auto problem =
            registry::check_value(spec, registry::literal_value(literal), where, normal)) {
        std::fprintf(stderr, "%s: %s\n", problem->path.c_str(), problem->message.c_str());
        OA_CHECK(!"registry value passes its own check");
    }
}

void test_registry_consistency() {
    const auto& table = registry::table();
    for (const auto& entry : table.entries) {
        const std::string id{entry.id};
        if (entry.value.baseline != registry::no_literal)
            check_literal(entry.value, entry.value.baseline, id + " baseline");
        const auto parameters = registry::parameters_of(entry);
        for (const auto& parameter : parameters) {
            const std::string where = id + "." + std::string{parameter.name};
            check_literal(parameter.value, parameter.value.baseline, where + " baseline");
            check_literal(parameter.value, parameter.default_value, where + " default");
            OA_CHECK(
                parameter.setting_source == registry::SettingSource::none ||
                parameter.adjustable != registry::Adjustable::fixed
            );
        }
        for (const auto& preset : registry::presets_of(entry)) {
            OA_CHECK(preset.value_count == parameters.size());
            const auto values = table.preset_values.subspan(preset.first_value, preset.value_count);
            for (const auto& value : values) {
                check_literal(
                    parameters[value.parameter].value,
                    value.literal,
                    id + " preset " + std::string{preset.name}
                );
                // The baseline preset holds exactly the baselines.
                if (preset.name == "baseline")
                    OA_CHECK(values_equal(
                        registry::literal_value(value.literal),
                        registry::literal_value(parameters[value.parameter].value.baseline)
                    ));
            }
        }
        if (entry.kind == registry::EntryKind::hack)
            OA_CHECK(!entry.summary.empty());
        else
            OA_CHECK(!entry.implemented);
    }
}

} // namespace

int main() {
    test_bindings();
    test_script_extensions();
    test_registry_consistency();
    return oa::test::check_exit_status();
}
