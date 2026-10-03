// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Rule keys over an installed mod's own files: every unit and weapon file in
// the directories its profile names is read with the keys its profile binds.
// Each bound key a file holds must read without a problem, and what the
// readers keep must agree with a plain scan of the same text. Prints how many
// files hold each key. Skips without OA_MOD_GAME_DIR and OA_MOD_PROFILES_DIR.

#include "oa/data/defs/asset_files.hpp"
#include "oa/data/defs/rule_keys.hpp"
#include "oa/data/defs/weapons.hpp"
#include "oa/test/check.hpp"
#include "oa/test/mod_install.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace oa::data::defs;
namespace tdf = oa::formats::tdf;
namespace mr = oa::data::match_rules;

/// Returns the veterancy thresholds a record holds: an empty list without any.
///
/// @param data the record
/// @return its thresholds
mr::FixedList<uint16_t, mr::max_veterancy_thresholds>
listed_thresholds(const mr::UnitTypeRules& data) {
    return data.veterancy_thresholds.value_or(
        mr::FixedList<uint16_t, mr::max_veterancy_thresholds>{}
    );
}

/// The names of a directory's files with an extension, in listing order.
std::vector<std::string> listed(const Files& files, const std::string& directory, const char* ext) {
    std::vector<std::string> names;
    files.list(
        files.context,
        directory.c_str(),
        ext,
        [](void* user, const char* name) {
            static_cast<std::vector<std::string>*>(user)->emplace_back(name);
        },
        &names
    );
    return names;
}

/// A bound name, or null for an unbound one.
const char* bound(const std::string& name) {
    return name.empty() ? nullptr : name.c_str();
}

/// Whether a block holds a key whose integer is odd: the plain scan the
/// weapon reader must agree with.
bool odd_key(const tdf::Block* block, const char* name) {
    if (name == nullptr)
        return false;
    const char* text = tdf::find_value(block, name);
    return text != nullptr && (std::atoi(text) & 1) != 0;
}

} // namespace

int main() {
    const oa::test::ModInstall install =
        oa::test::require_mod_install("defs-rule-keys-mod-install");
    const oa::AssetStore assets = oa::test::open_mod_assets(install);
    const Files files = asset_store_files(&assets);
    const auto& bindings = install.profile.data_keys;
    const auto& directories = install.profile.layout.directories;
    const auto& extensions = install.profile.layout.file_extensions;

    const UnitDataKeys unit_keys{
        bound(bindings.veterancy_thresholds),
        bound(bindings.veterancy_accuracy_rate),
        bound(bindings.units_build_facings)
    };
    const UnitPreviewDataKeys preview_keys{
        bound(bindings.ui_preview_pieces),
        bound(bindings.ui_preview_pieces_by_facing),
        bound(bindings.ui_preview_object),
        bound(bindings.ui_preview_face_opponent)
    };
    size_t units = 0, thresholds = 0, rates = 0, facings = 0;
    for (const std::string& name :
         listed(files, directories.units, extensions.unit_definition.c_str())) {
        const std::string path = directories.units + "\\" + name;
        mr::UnitTypeRules data{};
        UnitPreviewKeys preview{};
        RuleKeyIssues issues{};
        if (!load_unit_rule_keys(
                &files, path.c_str(), unit_keys, preview_keys, data, &preview, &issues
            ))
            continue; // a file the game cannot read either
        ++units;
        if (issues.any())
            std::fprintf(stderr, "%s: a bound key could not be read\n", path.c_str());
        OA_CHECK(!issues.any());
        thresholds += data.veterancy_thresholds.has_value() ? 1 : 0;
        rates += data.veterancy_accuracy_rate.has_value() ? 1 : 0;
        facings += data.build_facings != mr::build_facing::south ? 1 : 0;
        for (uint8_t level = 1; level < listed_thresholds(data).count; ++level)
            OA_CHECK(
                listed_thresholds(data).items[level - 1] <= listed_thresholds(data).items[level]
            );
    }
    std::printf(
        "%zu unit files: %zu with thresholds, %zu with an accuracy rate, %zu with facings\n",
        units,
        thresholds,
        rates,
        facings
    );
    OA_CHECK(units > 0);
    if (unit_keys.veterancy_thresholds != nullptr)
        OA_CHECK(thresholds > 0);

    const WeaponDataKeys weapon_keys{
        bound(bindings.weapons_not_to_air),
        bound(bindings.weapons_surface_fire),
        bound(bindings.weapons_not_to_underwater),
        bound(bindings.weapons_no_map_alert)
    };
    const auto table = std::make_unique<WeaponTable>();
    weapon_table_init(table.get());
    const WeaponLoadOptions options{nullptr, nullptr, false, false, &weapon_keys};
    size_t sections = 0, not_to_air = 0, surface_fire = 0, not_to_underwater = 0, no_alert = 0;
    for (const std::string& name : listed(files, directories.weapons, "tdf")) {
        const std::string path = directories.weapons + "\\" + name;
        tdf::OwnedDocument document;
        if (!load_tdf_file(&files, path.c_str(), document.get(), nullptr))
            continue;
        for (uint32_t index = 0; index < tdf::child_count(document.root()); ++index) {
            const tdf::Block* section = tdf::child_at(document.root(), index);
            mr::WeaponTypeRules data{};
            read_weapon_rule_keys(section, weapon_keys, data);
            ++sections;
            OA_CHECK(data.not_to_air == odd_key(section, weapon_keys.not_to_air));
            OA_CHECK(data.surface_fire == odd_key(section, weapon_keys.surface_fire));
            OA_CHECK(data.not_to_underwater == odd_key(section, weapon_keys.not_to_underwater));
            OA_CHECK(data.no_map_alert == odd_key(section, weapon_keys.no_map_alert));
            not_to_air += data.not_to_air ? 1 : 0;
            surface_fire += data.surface_fire ? 1 : 0;
            not_to_underwater += data.not_to_underwater ? 1 : 0;
            no_alert += data.no_map_alert ? 1 : 0;
        }
        // The loader fills the table's records the same way.
        uint8_t* bytes = nullptr;
        uint32_t size = 0;
        bool archived = false;
        if (files.read(files.context, path.c_str(), &bytes, &size, &archived)) {
            (void)load_weapon_text(
                table.get(), reinterpret_cast<const char*>(bytes), size, &options
            );
            files.release(files.context, bytes);
        }
    }
    size_t table_not_to_air = 0;
    for (const mr::WeaponTypeRules& data : table->rule_data)
        table_not_to_air += data.not_to_air ? 1 : 0;
    weapon_table_free(table.get());
    std::printf(
        "%zu weapon sections: %zu not to air, %zu surface fire, %zu not to underwater, "
        "%zu without map alert; %zu weapon ids not to air\n",
        sections,
        not_to_air,
        surface_fire,
        not_to_underwater,
        no_alert,
        table_not_to_air
    );
    OA_CHECK(sections > 0);
    if (weapon_keys.not_to_air != nullptr)
        OA_CHECK(not_to_air > 0 && table_not_to_air > 0);
    return oa::test::check_exit_status();
}
