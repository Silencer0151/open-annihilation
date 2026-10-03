// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The records of a resolved profile, filled from its effective profile
// through the bindings: each registry meaning into the one field that holds
// it, and the mounted script extensions into their tables.

#include "oa/data/mod_profile/bindings.hpp"

#include <string>
#include <tuple>

namespace oa::data::mod_profile {

void fill_records(const Value& effective, ModProfile& profile);

namespace {

using registry::EntryKind;

/// Returns the block of the effective profile an entry kind's values sit in.
///
/// @param kind identity, layout, string or media
/// @return the block's key
std::string_view value_block(EntryKind kind) {
    switch (kind) {
    case EntryKind::identity:
        return "identity";
    case EntryKind::layout:
        return "layout";
    case EntryKind::string:
        return "strings";
    default:
        return "media";
    }
}

/// Finds the value under a dotted key, such as directories.units.
///
/// @param map the map to start from
/// @param key the dotted key
/// @return the value, or null
const Value* find_dotted(const Value* map, std::string_view key) {
    while (map != nullptr) {
        const size_t dot = key.find('.');
        map = find_member(*map, key.substr(0, dot));
        if (dot == std::string_view::npos)
            return map;
        key.remove_prefix(dot + 1);
    }
    return nullptr;
}

/// Stores each meaning of an effective profile in its field.
struct Filler {
    const Value* effective{};

    /// Stores an identity, layout, string or media value.
    template <class Field>
    void value(std::string_view id, Field& field) {
        const auto* entry = registry::find_entry(id);
        const Value* value =
            find_dotted(find_member(*effective, value_block(entry->kind)), entry->key);
        if (value != nullptr)
            std::ignore = assign_field(entry->value, *value, field);
    }

    /// Stores whether a hack is on.
    void hack(std::string_view id, bool& enabled) const {
        enabled = find_member(*find_member(*effective, "hacks"), id) != nullptr;
    }

    /// Stores a limit's or hack's parameter; an absent hack keeps its baselines.
    template <class Field>
    void parameter(std::string_view id, std::string_view name, Field& field) {
        const auto* entry = registry::find_entry(id);
        const bool limit = entry->kind == EntryKind::limit;
        const Value* values = find_member(
            *find_member(*effective, limit ? "limits" : "hacks"),
            limit ? id.substr(std::string_view{"limits."}.size()) : id
        );
        const int index = registry::find_parameter(*entry, name);
        const Value* value = values != nullptr ? find_member(*values, name) : nullptr;
        if (value != nullptr && index >= 0)
            std::ignore = assign_field(
                registry::parameters_of(*entry)[static_cast<size_t>(index)].value, *value, field
            );
    }

    /// Stores the key a mod's files spell a data-key meaning with.
    void data_key(std::string_view id, std::string& field) const {
        for (const Member& file : find_member(*effective, "data-keys")->members) {
            for (const Member& key : file.value.members) {
                if (key.value.text == id)
                    field = key.key;
            }
        }
    }
};

/// Fills one direction's table of mounted extensions.
///
/// @param mounts the effective profile's index map, in increasing index order
/// @param[out] table the table
void fill_mounts(const Value& mounts, match_rules::ScriptExtensionTable& table) {
    table = {};
    for (const Member& mount : mounts.members) {
        if (table.count == match_rules::max_script_mounts)
            return;
        match_rules::ScriptMount& slot = table.mounts[table.count++];
        slot.index = static_cast<uint16_t>(std::stoi(mount.key));
        for (size_t index = 0; index < match_rules::script_extension_ids.size(); ++index) {
            if (match_rules::script_extension_ids[index] == mount.value.text)
                slot.extension = static_cast<match_rules::ScriptExtension>(index);
        }
    }
}

} // namespace

void fill_records(const Value& effective, ModProfile& profile) {
    Filler filler{&effective};
    visit_bindings(filler, profile);
    const Value& extensions = *find_member(effective, "script-extensions");
    fill_mounts(*find_member(extensions, "get"), profile.rules.script_get);
    fill_mounts(*find_member(extensions, "set"), profile.rules.script_set);
    profile.rules.script_fidelity = find_member(extensions, "fidelity")->text == "safe"
                                        ? match_rules::ScriptFidelity::safe
                                        : match_rules::ScriptFidelity::exact;
}

} // namespace oa::data::mod_profile
