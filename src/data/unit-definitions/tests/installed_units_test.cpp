// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The installed game's units/*.fbi, loaded the one way a match loads them:
// every header, the catalog sorted by unit name, then each FBI into its
// UnitDef record against the installed movement classes, weapons and sound
// categories. Every type's typed fields and runtime metadata come from its
// record, and a few stock units keep the values 3.1c gives them.
#include "oa/data/defs/asset_files.hpp"
#include "oa/data/defs/unit_catalog.hpp"
#include "oa/data/defs/unit_header.hpp"
#include "oa/data/defs/unit_records.hpp"
#include "oa/data/defs/weapons.hpp"
#include "oa/data/unit_definitions.hpp"
#include "oa/test/game_assets.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace oa::data::unit_definitions;
namespace defs = oa::data::defs;

namespace {

int failures = 0;

/// Reports a failed check and counts it.
///
/// @param what the failure, printed after "FAIL: "
void fail(const std::string& what) {
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
}

/// Checks one condition and reports it when it fails.
///
/// @param ok the condition
/// @param what the failure, printed after "FAIL: "
void check(bool ok, const std::string& what) {
    if (!ok)
        fail(what);
}

/// The tables the installed unit files load against.
struct Tables {
    defs::MoveClassTable classes{};
    std::unique_ptr<defs::WeaponTable> weapons = std::make_unique<defs::WeaponTable>();
    defs::WeaponTdfSet weapon_files{};
    defs::SoundCategoryTable sounds{};
    defs::UnitDefTables units{};

    Tables() {
        defs::weapon_tdf_set_init(&weapon_files);
        defs::unit_def_tables_init(&units);
        defs::weapon_table_init(weapons.get());
    }

    ~Tables() {
        defs::weapon_table_free(weapons.get());
        defs::weapon_tdf_set_free(&weapon_files);
        defs::sound_category_table_free(&sounds);
        defs::unit_def_tables_free(&units);
    }

    Tables(const Tables&) = delete;
    Tables& operator=(const Tables&) = delete;
};

/// Finds a loaded unit's typed fields by name.
///
/// @param records the catalog, slot 0 reserved
/// @param definitions the typed fields, by type id
/// @param name unit name
/// @return the fields, or null
const UnitDefinition* find(
    const defs::UnitDefTables& records,
    const std::vector<UnitDefinition>& definitions,
    const char* name
) {
    const uint16_t type = defs::unit_defs_type_id(records.records, records.count, name);
    return type != 0 && type < definitions.size() ? &definitions[type] : nullptr;
}

/// Loads every installed unit and checks its fields, metadata and categories.
///
/// @param assets the installed game's store
void check_installed_units(const oa::AssetStore& assets) {
    const auto files = defs::asset_store_files(&assets);
    Tables tables;
    if (!defs::load_move_classes(&files, &tables.classes, nullptr, nullptr) ||
        defs::load_weapon_defs(&files, tables.weapons.get(), nullptr) == 0 ||
        !defs::load_weapon_tdf_set(&files, nullptr, false, &tables.weapon_files) ||
        !defs::load_sound_categories(&files, &tables.sounds, nullptr)) {
        fail("the install's movement classes, weapons or sound categories do not load");
        return;
    }
    std::vector<std::string> names;
    files.list(
        files.context,
        "units",
        "FBI",
        [](void* user, const char* name) {
            static_cast<std::vector<std::string>*>(user)->emplace_back(name);
        },
        &names
    );
    if (names.empty() ||
        !defs::unit_def_tables_allocate(&tables.units, static_cast<uint32_t>(names.size() + 1))) {
        fail("the install holds no units/*.fbi");
        return;
    }
    const defs::UnitHeaderSources header_sources{"", &tables.weapon_files, 3, 1, false, false};
    for (std::size_t index = 0; index < names.size(); ++index) {
        char path[defs::path_capacity];
        defs::build_variant_path(
            &files, path, sizeof path, "units", names[index].c_str(), "FBI", nullptr
        );
        bool refused = false;
        if (!defs::load_unit_header(
                &files, path, tables.units.records[index + 1], header_sources, &refused
            ))
            fail(std::string(path) + ": the header does not load");
    }
    const auto loaded = static_cast<std::size_t>(tables.units.count);
    tables.units.count = defs::unit_defs_finalize_catalog(tables.units.records, tables.units.count);
    check(tables.units.count == loaded, "a stock unit is not available to 3.1");
    const defs::UnitDefSources sources{
        "",
        &tables.classes,
        tables.weapons->defs,
        &tables.sounds,
        &tables.units.categories,
        &tables.units.blocks,
        nullptr
    };
    std::vector<UnitDefinition> definitions(tables.units.count);
    std::size_t buildings = 0;
    for (uint32_t type = 1; type < tables.units.count; ++type) {
        auto& record = tables.units.records[type];
        const std::string unit(record.unit_name);
        char path[defs::path_capacity];
        defs::build_variant_path(
            &files, path, sizeof path, "units", record.unit_name, "FBI", nullptr
        );
        if (!defs::load_unit_def(&files, path, record, sources)) {
            fail(unit + ": the FBI does not load");
            continue;
        }
        definitions[type] = unit_definition_from(
            record,
            {&tables.classes, tables.weapons->defs, &tables.sounds, &tables.units.categories}
        );
        const auto metadata = resolve_runtime_metadata(record, tables.classes, tables.units.blocks);
        if (!metadata) {
            fail(unit + ": " + metadata.error.message);
            continue;
        }
        if (record.bm_code == 0)
            ++buildings;
        check(definitions[type].unit_name == unit, unit + " loses its name");
        check(
            metadata.value.footprint_x == definitions[type].footprint_x &&
                metadata.value.footprint_z == definitions[type].footprint_z,
            unit + ": the metadata and the fields disagree on the footprint"
        );
    }
    for (uint32_t type = 1; type < tables.units.count; ++type)
        for (const auto& category : definitions[type].categories) {
            const auto* mask =
                defs::category_registry_find(&tables.units.categories, category.c_str());
            check(
                mask != nullptr && defs::category_mask_contains(mask, static_cast<uint16_t>(type)),
                definitions[type].unit_name + " is missing from its category " + category
            );
        }

    // The commander: its weapons by section name, its movement class's
    // footprint and its categories.
    if (const auto* commander = find(tables.units, definitions, "ARMCOM")) {
        check(
            commander->weapon1 == "ARMCOMLASER" && commander->weapon3 == "ARM_DISINTEGRATOR",
            "ARMCOM does not carry its laser and D-gun"
        );
        check(
            oa::formats::tdf::compare_nocase(commander->movement_class.c_str(), "TANKDS2") == 0,
            "ARMCOM is not of TANKDS2"
        );
        check(
            commander->footprint_x == 2 && commander->footprint_z == 2,
            "ARMCOM does not take its movement class's footprint"
        );
        check(
            std::any_of(
                commander->categories.begin(),
                commander->categories.end(),
                [](const std::string& name) {
                    return oa::formats::tdf::compare_nocase(name.c_str(), "commander") == 0;
                }
            ),
            "ARMCOM is not a commander"
        );
    } else {
        fail("no ARMCOM");
    }
    // ARMSCORP.FBI's ItalianDescription=;Scorpione runs into the next line's
    // key, so its canguard is never read, as in 3.1c.
    if (const auto* scorpion = find(tables.units, definitions, "ARMSCORP"))
        check(!scorpion->can_guard && scorpion->can_patrol, "ARMSCORP reads its canguard");
    // A sound category SOUND.TDF lacks is read as a number: category 0.
    if (const auto* freaker = find(tables.units, definitions, "CORFAST"))
        check(freaker->sound_category == "ARM_KBOT", "CORFAST does not speak as category 0");
    std::cout << "resolved " << tables.units.count - 1 << " installed FBI files (" << buildings
              << " yard maps, " << tables.units.categories.count << " categories)\n";
}

} // namespace

int main() {
    check_installed_units(oa::test::require_game_assets("the installed unit definitions"));
    return failures == 0 ? 0 : 1;
}
