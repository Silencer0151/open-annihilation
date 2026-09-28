// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unit types of the installed game for match tests: their FBI definitions over
// the installed movement classes, weapons, categories, corpses, models and
// scripts, laid out as a Match takes them, and a map of open sea, a shallow
// shelf and land to put them on.
#pragma once

#include "oa/data/mission_types.hpp"
#include "oa/sim/map_runtime.hpp"
#include "oa/sim/map_runtime/feature_defs.hpp"
#include "oa/sim/match_runtime.hpp"
#include "oa/test/game_assets.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oa::test {

/// Reads the installed game's unit models and scripts for a runtime type.
class InstalledReader final : public sim::unit_spawn::AssetReader {
  public:

    /// Binds the reader to a mounted store.
    ///
    /// @param store the installed game's store; must outlive the reader
    explicit InstalledReader(const AssetStore& store) : store_(store) {}

    /// Reads a file of the installed game.
    ///
    /// @param path path inside the game data
    /// @return the bytes, or nothing when no archive provides the file
    std::optional<std::vector<uint8_t>> read(std::string_view path) override {
        auto bytes = read_game_file(store_, path);
        if (bytes.empty())
            return std::nullopt;
        return bytes;
    }

  private:

    const AssetStore& store_;
};

/// Lists and reads the installed game's feature TDFs.
class InstalledFeatureReader final : public sim::map_runtime::FeatureAssetReader {
  public:

    /// Binds the reader to a mounted store.
    ///
    /// @param store the installed game's store; must outlive the reader
    explicit InstalledFeatureReader(const AssetStore& store) : store_(store) {}

    /// Lists every effective file under a directory, recursively.
    ///
    /// @param directory logical directory
    /// @param extension extension kept
    /// @return the logical paths
    data::unit_definitions::Result<std::vector<std::string>> list_effective_recursive(
        std::string_view directory, std::string_view extension
    ) const override {
        return {store_.list_effective_recursive(directory, extension), {}};
    }

    /// Reads a file's text.
    ///
    /// @param path logical path
    /// @return the text
    data::unit_definitions::Result<std::string> read(std::string_view path) const override {
        const auto bytes = read_game_file(store_, path);
        return {std::string(bytes.begin(), bytes.end()), {}};
    }

  private:

    const AssetStore& store_;
};

/// Named unit types of the installed game, as a Match's offline inputs take them.
///
/// Type 0 is reserved; the named types follow in the order given. Weapons are
/// every installed weapon, and the category masks are resolved over the named
/// types alone, which the categories they name need.
struct InstalledUnits {
    std::vector<sim::unit_spawn::LoadedType> loaded;
    std::vector<sim::unit_spawn::Type> types;
    std::vector<data::unit_definitions::UnitDefinition> definitions;
    std::vector<data::unit_definitions::RuntimeDefinitionMetadata> metadata;
    std::vector<sim::match_runtime::RuntimeTypeFields> fields;
    data::unit_definitions::ResolvedCategoryRegistry categories;
    sim::combat_state::WeaponRegistry weapons;
    std::vector<data::unit_definitions::TdfDocument> feature_documents;
    sim::map_runtime::FeatureDefTable features;

    /// Loads the named types.
    ///
    /// Throws std::runtime_error when a file is missing or does not load.
    ///
    /// @param store the installed game's store
    /// @param names unit names, the FBI file names without extension
    InstalledUnits(const AssetStore& store, std::initializer_list<std::string_view> names) {
        const auto text = [&store](std::string_view path) {
            const auto bytes = read_game_file(store, path);
            if (bytes.empty())
                throw std::runtime_error("the installed game has no " + std::string(path));
            return std::string(bytes.begin(), bytes.end());
        };
        for (const auto& path : store.list_effective("weapons", ".tdf")) {
            const auto document = data::unit_definitions::parse_tdf(text(path));
            if (!document)
                throw std::runtime_error(path + ": " + document.error.message);
            (void)sim::combat_state::install_weapon_tdf(weapons, document.value);
        }
        auto documents = sim::map_runtime::load_feature_documents(InstalledFeatureReader(store));
        if (!documents)
            throw std::runtime_error("feature documents: " + documents.error.message);
        feature_documents = std::move(documents.value);
        const auto movement =
            data::unit_definitions::load_movement_classes(text("gamedata/moveinfo.tdf"));
        if (!movement)
            throw std::runtime_error("moveinfo.tdf: " + movement.error.message);
        const auto count = names.size() + 1;
        loaded.resize(count);
        types.resize(count);
        definitions.resize(count);
        metadata.resize(count);
        fields.resize(count);
        InstalledReader reader(store);
        data::unit_definitions::UnitCatalog catalog;
        std::vector<int16_t> corpses(count, -1);
        uint16_t index = 0;
        for (const auto name : names) {
            ++index;
            const auto fbi = "units/" + std::string(name) + ".fbi";
            auto definition = data::unit_definitions::load_fbi(text(fbi), fbi);
            if (!definition)
                throw std::runtime_error(fbi + ": " + definition.error.message);
            auto resolved =
                data::unit_definitions::resolve_runtime_metadata(definition.value, movement.value);
            if (!resolved)
                throw std::runtime_error(fbi + ": " + resolved.error.message);
            sim::unit_spawn::RuntimeBindings bindings;
            bindings.enabled = true;
            bindings.resolved_weapon_present =
                sim::combat_state::bind_unit_weapons(
                    weapons,
                    {definition.value.weapon1, definition.value.weapon2, definition.value.weapon3}
                )
                    .resolved_nondefault_weapon;
            bindings.default_mission =
                data::mission_types::index_for_name(definition.value.default_mission_type);
            bindings.movement_footprint =
                std::array<int16_t, 2>{resolved.value.footprint_x, resolved.value.footprint_z};
            loaded[index] = sim::unit_spawn::load_runtime_type(definition.value, bindings, reader);
            types[index] = loaded[index].type;
            if (!definition.value.corpse.empty()) {
                const auto found = sim::map_runtime::find_or_load_feature(
                    features, feature_documents, definition.value.corpse, nullptr
                );
                if (!found.ok())
                    throw std::runtime_error(fbi + " corpse: " + found.error->message);
                corpses[index] = static_cast<int16_t>(found.index);
            }
            catalog.entries.push_back({index, fbi, definition.value});
            definitions[index] = std::move(definition.value);
            metadata[index] = std::move(resolved.value);
        }
        if (const auto error =
                sim::map_runtime::load_feature_links(features, feature_documents, nullptr))
            throw std::runtime_error("feature links: " + error->message);
        auto registry = data::unit_definitions::resolve_unit_categories(catalog);
        if (!registry)
            throw std::runtime_error("categories: " + registry.error.message);
        categories = std::move(registry.value);
        categories.target_masks.resize(count);
        for (std::size_t type = 1; type < count; ++type) {
            auto& field = fields[type];
            field.definition = &definitions[type];
            field.yard_mask = metadata[type].yard_cells;
            field.runtime_metadata = &metadata[type];
            field.target_masks = &categories.target_masks[type];
            // A movement class's handle is its slot + 1; 0 means none.
            field.movement_class = metadata[type].movement_class_handle
                                       ? static_cast<sim::unit_spawn::AssetHandle>(
                                             *metadata[type].movement_class_handle
                                         ) + 1U
                                       : sim::unit_spawn::AssetHandle{0};
            field.corpse_feature = corpses[type];
        }
    }

    /// Returns a named type's index.
    ///
    /// Throws std::out_of_range for a name that was not loaded.
    ///
    /// @param name unit name, matched ignoring case
    /// @return the type index, 1 or more
    [[nodiscard]] uint16_t type(std::string_view name) const {
        for (std::size_t index = 1; index < definitions.size(); ++index) {
            const auto& unit = definitions[index].unit_name;
            if (unit.size() == name.size() &&
                std::equal(unit.begin(), unit.end(), name.begin(), [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) ==
                           std::tolower(static_cast<unsigned char>(b));
                }))
                return static_cast<uint16_t>(index);
        }
        throw std::out_of_range("no installed type " + std::string(name));
    }
};

/// A map of open sea, a shallow shelf and land, in 16-unit cells: columns
/// under `shelf_column` lie `deep_floor` high, those under `land_column`
/// `shelf_floor`, and the rest `land_height`, with the sea at `sea_level`.
struct Seascape {
    formats::tnt::Map map;
    std::vector<sim::visibility_state::TerrainCell> terrain_values;
    std::vector<sim::spatial_state::Plot> plots;
    std::array<sim::visibility_state::SightMask, 1> masks{};

    /// Builds the map.
    ///
    /// @param width columns
    /// @param height rows
    /// @param sea_level sea level in whole world units
    /// @param shelf_column first column of the shelf
    /// @param land_column first column of land
    /// @param deep_floor height of the open sea's floor
    /// @param shelf_floor height of the shelf
    /// @param land_height height of the land
    Seascape(
        int32_t width,
        int32_t height,
        uint8_t sea_level,
        int32_t shelf_column,
        int32_t land_column,
        uint8_t deep_floor,
        uint8_t shelf_floor,
        uint8_t land_height
    ) {
        const auto floor_at = [&](int32_t column) {
            if (column >= land_column)
                return land_height;
            return column >= shelf_column ? shelf_floor : deep_floor;
        };
        map.attribute_width = static_cast<uint32_t>(width);
        map.attribute_height = static_cast<uint32_t>(height);
        map.sea_level = sea_level;
        const auto cells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        map.attributes.resize(cells);
        plots.resize(cells);
        terrain_values.resize(cells);
        for (int32_t z = 0; z < height; ++z)
            for (int32_t x = 0; x < width; ++x) {
                const auto index = static_cast<std::size_t>(z * width + x);
                map.attributes[index].height = floor_at(x);
                const auto next = floor_at(std::min(x + 1, width - 1));
                plots[index].low_height = std::min(floor_at(x), next);
                plots[index].high_height = std::max(floor_at(x), next);
            }
        // Sight reaches seven 32-unit cells around a unit.
        masks[0].width = masks[0].height = 15;
        masks[0].offset_x = masks[0].offset_z = 7;
        masks[0].pixels.assign(15 * 15, 1);
    }
};

} // namespace oa::test
