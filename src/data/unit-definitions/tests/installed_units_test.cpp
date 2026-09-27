// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The installed game's units/*.fbi and gamedata/moveinfo.tdf, read through
// its archives: every definition loads, resolves its runtime metadata against
// the movement classes, and belongs to each category it names.
#include "oa/data/unit_definitions.hpp"
#include "oa/test/game_assets.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace oa::data::unit_definitions;

namespace {

int failures = 0;

/// Reports a failed check and counts it.
///
/// @param what the failure, printed after "FAIL: "
void fail(const std::string& what) {
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
}

/// Views a file's bytes as text.
///
/// @param bytes the file
/// @return the same bytes as characters
std::string_view text_of(const std::vector<uint8_t>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// Lower-cases ASCII letters, as the category registry keys names.
///
/// @param text a category name
/// @return the name in lower case
std::string lower_ascii(std::string text) {
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 'a' - 'A');
    return text;
}

/// Loads every installed unit definition and checks its metadata and categories.
///
/// @param assets the installed game's store
void check_installed_units(const oa::AssetStore& assets) {
    const auto movement_bytes = oa::test::read_game_file(assets, "gamedata/moveinfo.tdf");
    auto classes = load_movement_classes(text_of(movement_bytes));
    if (movement_bytes.empty() || !classes) {
        fail("the install's gamedata/moveinfo.tdf is missing or does not load");
        return;
    }
    UnitCatalog catalog;
    std::size_t buildings = 0;
    for (const auto& path : assets.list_effective("units", ".FBI")) {
        auto definition = load_fbi(text_of(oa::test::read_game_file(assets, path)), path);
        if (!definition) {
            fail(path + ": " + definition.error.message);
            continue;
        }
        auto runtime = resolve_runtime_metadata(definition.value, classes.value);
        if (!runtime) {
            fail(path + ": " + runtime.error.message);
            continue;
        }
        if (definition.value.bm_code == 0)
            ++buildings;
        catalog.entries.push_back(
            {static_cast<uint16_t>(catalog.entries.size() + 1), path, std::move(definition.value)}
        );
    }
    if (catalog.entries.empty()) {
        fail("the install holds no units/*.fbi");
        return;
    }
    auto registry = resolve_unit_categories(catalog);
    if (!registry) {
        fail("categories: " + registry.error.message);
        return;
    }
    for (const auto& unit : catalog.entries)
        for (const auto& category : unit.definition.categories) {
            const auto found = registry.value.categories.find(lower_ascii(category));
            if (found == registry.value.categories.end() || !found->second.contains(unit.type_id))
                fail(unit.logical_path + " is missing from its category " + category);
        }
    std::cout << "resolved " << catalog.entries.size() << " installed FBI files (" << buildings
              << " yard maps, " << registry.value.categories.size() << " categories)\n";
}

} // namespace

int main() {
    check_installed_units(oa::test::require_game_assets("the installed unit definitions"));
    return failures == 0 ? 0 : 1;
}
