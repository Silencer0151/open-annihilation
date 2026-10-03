// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The profile that turns every rule on at its baseline preset.

#include "oa/data/mod_profile.hpp"
#include "oa/data/mod_profile/registry.hpp"

#include <string_view>

namespace oa::data::mod_profile {
namespace {

/// The name of the preset that holds every parameter at its 3.1c value.
constexpr std::string_view baseline_preset = "baseline";

/// The prefix of a limit's registry id, which the limits block leaves out.
constexpr std::string_view limit_prefix = "limits.";

/// Tells whether an entry has a baseline preset.
///
/// @param entry the entry
/// @return true when one of its presets is named baseline
bool has_baseline(const registry::Entry& entry) {
    for (const registry::Preset& preset : registry::presets_of(entry)) {
        if (preset.name == baseline_preset)
            return true;
    }
    return false;
}

} // namespace

std::string baseline_profile_text() {
    const registry::Registry& table = registry::table();
    std::string limits;
    std::string hacks;
    for (const registry::Entry& entry : table.entries) {
        if (!has_baseline(entry))
            continue;
        if (entry.kind == registry::EntryKind::limit && entry.id.starts_with(limit_prefix)) {
            limits += "  ";
            limits += entry.id.substr(limit_prefix.size());
            limits += ": {preset: baseline}\n";
        } else if (entry.kind == registry::EntryKind::hack) {
            hacks += "  ";
            hacks += entry.id;
            hacks += ": {preset: baseline}\n";
        }
    }
    std::string text = "oamod: 1\n"
                       "id: baseline-rules\n"
                       "name: \"Every rule at its baseline\"\n"
                       "version: \"1\"\n"
                       "requires: {base: ta-3.1c, catalogue: ";
    text += std::to_string(table.catalogue);
    text += "}\n";
    if (!limits.empty())
        text += "limits:\n" + limits;
    if (!hacks.empty())
        text += "hacks:\n" + hacks;
    return text;
}

} // namespace oa::data::mod_profile
