// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Skirmish match bootstrap from the selected map and players.
#include "oa/app/runtime.hpp"
#include "oa/app/asset_files.hpp"
#include "oa/app/match_console.hpp"
#include "oa/data/campaign/campaign_file.hpp"
#include "oa/data/defs/gamedata_tables.hpp"
#include "oa/data/defs/unit_def_loader.hpp"
#include "oa/data/defs/unit_header.hpp"
#include "oa/data/mission_types.hpp"
#include "oa/sim/weapon_execution/weapon_launch.hpp"
#include "oa/sim/session.hpp"
#include "oa/ui/console/game_fields.hpp"
#include "oa/ui/hud/player_records.hpp"
#include "oa/ui/hud/status_panel.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace oa::app {

namespace {

// A seated player reports the machine's memory; without a measurement the
// match reports the player setup's default, 256 MB.
constexpr int32_t kStandInPhysicalMemory = 256 * 0x100000;
// The player timeout of a match no launch switch changed: seconds a silent
// player is waited for.
constexpr int32_t kPlayerTimeoutSeconds = 30;

class UnitCatalogReader final : public oa::data::unit_definitions::CatalogAssetReader {
  public:

    explicit UnitCatalogReader(const oa::AssetStore& assets) : assets_(assets) {}

    oa::data::unit_definitions::Result<std::vector<std::string>>
    list_effective(std::string_view directory, std::string_view extension) const override {
        try {
            return {assets_.list_effective(directory, extension), {}};
        } catch (const std::exception& error) {
            return {{}, {oa::data::unit_definitions::ErrorCode::io, 0, error.what()}};
        }
    }

    oa::data::unit_definitions::Result<std::string> read(std::string_view path) const override {
        try {
            const auto bytes = assets_.read(path).bytes;
            return {std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()), {}};
        } catch (const std::exception& error) {
            return {{}, {oa::data::unit_definitions::ErrorCode::io, 0, error.what()}};
        }
    }

  private:

    const oa::AssetStore& assets_;
};

class FeatureCatalogReader final : public oa::sim::map_runtime::FeatureAssetReader {
  public:

    explicit FeatureCatalogReader(const oa::AssetStore& assets) : assets_(assets) {}

    oa::data::unit_definitions::Result<std::vector<std::string>> list_effective_recursive(
        std::string_view directory, std::string_view extension
    ) const override {
        try {
            return {assets_.list_effective_recursive(directory, extension), {}};
        } catch (const std::exception& error) {
            return {{}, {oa::data::unit_definitions::ErrorCode::io, 0, error.what()}};
        }
    }

    oa::data::unit_definitions::Result<std::string> read(std::string_view path) const override {
        try {
            const auto bytes = assets_.read(path).bytes;
            return {std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()), {}};
        } catch (const std::exception& error) {
            return {{}, {oa::data::unit_definitions::ErrorCode::io, 0, error.what()}};
        }
    }

  private:

    const oa::AssetStore& assets_;
};

// A feature one-shot sequence (burn, die, reclamate) plays once: the loader
// clears the repeat byte of the loaded GAF entry.
constexpr uint16_t gaf_repeat_byte = 0x00ff;

template <typename T>
oa_ref32 table_ref(const std::vector<T>& table) {
    return static_cast<oa_ref32>(table.size());
}

// Option bits the match loader clears: the radar, double-shot
// and half-shot console toggles last only for the match they were set in.
constexpr uint16_t match_cleared_console_flags = oa::ui::console::console_flag::full_radar |
                                                 oa::ui::console::console_flag::double_shot |
                                                 oa::ui::console::console_flag::half_shot;

// The build version, 3.1, that unit files are checked against; the engine
// keeps it here rather than in Game.version_block.
constexpr int8_t kBuildVersionMajor = 3;
constexpr int8_t kBuildVersionMinor = 1;

// The unit loader loads a corpse= feature the table lacks; the first failure is kept.
struct CorpseFeatures {
    oa::sim::map_runtime::FeatureDefTable& table;
    std::span<const oa::data::unit_definitions::TdfDocument> documents;
    const oa::sim::map_runtime::FeatureDefHost* host{};
    std::string error;

    static int16_t load(void* context, const char* name) {
        auto& self = *static_cast<CorpseFeatures*>(context);
        const auto corpse =
            oa::sim::map_runtime::find_or_load_feature(self.table, self.documents, name, self.host);
        if (corpse.ok())
            return static_cast<int16_t>(corpse.index);
        if (self.error.empty())
            self.error =
                "cannot load corpse feature '" + std::string(name) + "': " + corpse.error->message;
        return -1;
    }
};

// The rays per TABLEn: line1..lineN, each read into a 0x200-byte
// buffer (a missing line reads empty), become one altitude sight pattern.
struct LosTables {
    std::vector<oa::sim::visibility_state::AltitudeSightPattern>& patterns;
    std::string error;

    static bool resize(void* context, int16_t table_count) {
        auto& self = *static_cast<LosTables*>(context);
        try {
            self.patterns.assign(static_cast<std::size_t>(table_count), {});
            return true;
        } catch (const std::exception& failure) {
            self.error = failure.what();
            return false;
        }
    }

    static bool table(
        void* context, int16_t index, const oa::formats::tdf::Block* section, int16_t line_count
    ) {
        auto& self = *static_cast<LosTables*>(context);
        try {
            std::vector<std::string> texts;
            texts.reserve(static_cast<std::size_t>(line_count));
            for (int16_t line = 0; line < line_count; ++line) {
                char key[32];
                char text[0x200];
                std::snprintf(key, sizeof key, "line%d", line + 1);
                oa::formats::tdf::get_string(section, key, text, sizeof text, "");
                texts.emplace_back(text);
            }
            const std::vector<std::string_view> lines(texts.begin(), texts.end());
            self.patterns[static_cast<std::size_t>(index)] =
                oa::sim::visibility_state::build_altitude_pattern(lines);
            return true;
        } catch (const std::exception& failure) {
            self.error =
                "gamedata/los.tdf TABLE" + std::to_string(index + 1) + ": " + failure.what();
            return false;
        }
    }
};

} // namespace

void Runtime::keep_available_units(
    oa::data::unit_definitions::UnitCatalog& catalog, std::span<const oa::UnitDef> headers
) {
    std::size_t kept = 0;
    for (std::size_t index = 0; index < catalog.entries.size(); ++index) {
        if ((headers[index + 1U].flags & OA_UNIT_DEF_FLAG_AVAILABLE) == 0)
            continue;
        if (kept != index)
            catalog.entries[kept] = std::move(catalog.entries[index]);
        catalog.entries[kept].type_id = static_cast<uint16_t>(kept + 1U);
        ++kept;
    }
    catalog.entries.resize(kept);
}

void Runtime::restrict_marked_catalog(
    oa::data::unit_definitions::UnitCatalog& catalog, const UnitFilter& filter
) {
    const auto count = catalog.entries.size() + 1U;
    std::vector<oa::UnitDef> headers(count);
    for (std::size_t index = 0; index < catalog.entries.size(); ++index) {
        const auto bytes = assets_.read(catalog.entries[index].logical_path).bytes;
        headers[index + 1U].fbi_hash =
            oa::formats::tdf::buffer_hash(bytes.data(), static_cast<int32_t>(bytes.size()));
    }
    filter.mark_units(filter.context, headers.data(), static_cast<uint32_t>(count));
    keep_available_units(catalog, headers);
}

oa::data::unit_definitions::Result<std::vector<oa::data::unit_definitions::TdfDocument>>
Runtime::load_feature_tdf_set() const {
    return oa::sim::map_runtime::load_feature_documents(FeatureCatalogReader(assets_));
}

oa::sim::map_runtime::FeatureDefHost Runtime::feature_def_host() {
    oa::sim::map_runtime::FeatureDefHost host{};
    host.context = this;
    host.load_animation = [](void* context, const char* gaf_name) -> oa_ref32 {
        auto& self = *static_cast<Runtime*>(context);
        try {
            auto parsed = oa::formats::gaf::parse(
                self.assets_.read("anims/" + std::string(gaf_name) + ".gaf").bytes
            );
            if (!parsed.ok())
                return 0;
            self.feature_assets_.archives.push_back(
                std::make_unique<oa::formats::gaf::Archive>(std::move(*parsed.archive))
            );
        } catch (const std::exception&) {
            return 0;
        }
        return table_ref(self.feature_assets_.archives);
    };
    host.find_sequence =
        [](void* context, oa_ref32 animation, const char* name, bool one_shot) -> oa_ref32 {
        auto& self = *static_cast<Runtime*>(context);
        auto& assets = self.feature_assets_;
        if (animation == 0 || animation > assets.archives.size())
            return 0;
        oa::formats::gaf::Sequence* sequence = nullptr;
        for (auto& entry : assets.archives[animation - 1]->sequences)
            if (tdf_names_equal(entry.name, name)) {
                sequence = &entry;
                break;
            }
        if (sequence == nullptr)
            return 0;
        if (one_shot)
            sequence->repeat_flags =
                static_cast<uint16_t>(sequence->repeat_flags & ~gaf_repeat_byte);
        for (std::size_t index = 0; index < assets.sequences.size(); ++index)
            if (assets.sequences[index] == sequence)
                return static_cast<oa_ref32>(index + 1);
        assets.sequences.push_back(sequence);
        assets.rendered.emplace_back();
        return table_ref(assets.sequences);
    };
    host.load_object = [](void* context, const char* object_name) -> oa_ref32 {
        auto& self = *static_cast<Runtime*>(context);
        try {
            const auto bytes =
                self.assets_.read("objects3d/" + std::string(object_name) + ".3DO").bytes;
            const auto* begin = reinterpret_cast<const std::byte*>(bytes.data());
            self.feature_assets_.models.push_back(
                std::make_shared<const oa::formats::objects3d::Model>(
                    oa::formats::objects3d::load_3do({begin, bytes.size()})
                )
            );
        } catch (const std::exception&) {
            return 0;
        }
        return table_ref(self.feature_assets_.models);
    };
    host.find_weapon = [](void* context, const char* weapon_name) -> oa_ref32 {
        const auto* definition = static_cast<Runtime*>(context)->weapon_registry_.find(weapon_name);
        return definition != nullptr ? oa::oa_ref_from_index(definition->registry_index) : 0u;
    };
    host.sequence_frame = [](void* context,
                             oa_ref32 sequence,
                             uint16_t frame,
                             uint16_t* frame_count,
                             uint8_t* repeat,
                             uint16_t* duration) {
        oa::sim::feature_runtime::FeatureSequenceFrame out{};
        (void)static_cast<Runtime*>(context)->feature_sequence_frame(sequence, frame, out);
        *frame_count = out.frame_count;
        *repeat = out.repeat;
        *duration = out.duration;
    };
    return host;
}

bool Runtime::feature_sequence_frame(
    oa_ref32 sequence, uint16_t frame, oa::sim::feature_runtime::FeatureSequenceFrame& out
) const {
    out = {};
    if (sequence == 0 || sequence > feature_assets_.sequences.size())
        return false;
    const auto& frames = feature_assets_.sequences[sequence - 1]->frames;
    out.frame_count = static_cast<uint16_t>(frames.size());
    out.repeat = static_cast<uint8_t>(
        feature_assets_.sequences[sequence - 1]->repeat_flags & gaf_repeat_byte
    );
    if (frame >= frames.size())
        return false;
    const auto& image = frames[frame];
    out.duration = image.duration;
    out.width = static_cast<int16_t>(image.width);
    out.height = static_cast<int16_t>(image.height);
    out.origin_x = image.origin_x;
    out.origin_y = image.origin_y;
    return true;
}

void Runtime::apply_skirmish_players() {
    bootstrap_match({.seat_roster = true});
}

void Runtime::seat_skirmish_roster(oa::World& world) {
    std::array<oa::ui::hud::SkirmishSlot, entry::skirmish_slot_capacity> roster{};
    for (std::size_t index = 0; index < roster.size(); ++index) {
        const auto& slot = skirmish_settings_.slots[index];
        roster[index] = {
            slot.controller, slot.side, slot.alliance, slot.metal, slot.energy, slot.color
        };
    }
    oa::ui::hud::init_player_slots_from_roster(
        world,
        roster.data(),
        skirmish_settings_.slot_count,
        oa::ui::hud::kSessionSkirmish,
        kStandInPhysicalMemory
    );
}

void Runtime::seat_campaign_players(oa::World& world) {
    oa::ui::hud::init_player_slot(
        world, 0, OA_PLAYER_STATUS_LOCAL, oa::ui::hud::kSessionCampaign, kStandInPhysicalMemory
    );
    oa::ui::hud::init_player_slot(
        world, 1, OA_PLAYER_STATUS_COMPUTER, oa::ui::hud::kSessionCampaign, kStandInPhysicalMemory
    );
    if (auto* info = oa::world_player_info(&world, &world.game.players[1]))
        info->color = 1;
}

void Runtime::bind_match_speech() {
    // Captions come in the game's English wording and are shown in the
    // player's language; the queue keeps its own copy.
    oa::sim::match_runtime::SpeechHooks hooks;
    hooks.context = this;
    hooks.speak =
        [](void* context, oa::sim::unit_spawn::Slot& slot, uint32_t category, const char* caption) {
            auto& self = *static_cast<Runtime*>(context);
            self.offline_services_.command_speech(
                slot, category, self.translate_ui(caption != nullptr ? caption : "")
            );
        };
    match_->set_speech_hooks(hooks);
}

void Runtime::bootstrap_match(const MatchBootstrap& bootstrap) {
    // A new Start attempt owns a new world.  Do not let a failed bootstrap
    // expose commanders, timing state, or a renderable match from an older
    // map selection, nor the last game's end screen.
    release_endgame();
    teardown_match();
    altitude_sight_blocked_ = false;
    match_tick_blocked_ = false;
    match_timing_ = {};
    load_progress_.fill(0);
    begin_loading_screen();
    set_load_progress(0, 10);
    if (bootstrap.place_commanders && selected_start_markers_.empty() && !campaign_mission_)
        throw std::runtime_error("selected map has no start-position schema");
    session_schema_ = session_schema();
    const auto movement_data = assets_.read("gamedata/moveinfo.tdf");
    const std::string_view movement_text(
        reinterpret_cast<const char*>(movement_data.bytes.data()), movement_data.bytes.size()
    );
    const auto movement = oa::data::unit_definitions::load_movement_classes(movement_text);
    if (!movement)
        throw std::runtime_error("cannot load moveinfo.tdf: " + movement.error.message);

    weapon_registry_ = oa::sim::combat_state::WeaponRegistry{};
    std::size_t installed_weapons = 0;
    const auto weapon_documents = assets_.list_effective("weapons", ".tdf");
    for (const auto& path : weapon_documents) {
        const auto bytes = read(path);
        if (!bytes)
            continue;
        const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        const auto document = oa::data::unit_definitions::parse_tdf(text);
        if (!document)
            throw std::runtime_error(
                "cannot parse weapon definitions '" + std::string(path) +
                "': " + document.error.message
            );
        installed_weapons +=
            oa::sim::combat_state::install_weapon_tdf(weapon_registry_, document.value);
    }
    if (installed_weapons == 0)
        throw std::runtime_error("no base weapon definitions were installed");
    set_load_progress(0, 100);

    // Mission state set-up: Terrain before Units.
    set_load_progress(1, 15);
    if (!selected_tnt_)
        throw std::runtime_error("selected map terrain is not loaded");
    // The feature TDF set is parsed once; the map's
    // feature table, the unit corpses and the link pass all search it.
    const auto feature_documents = load_feature_tdf_set();
    if (!feature_documents)
        throw std::runtime_error(
            "cannot load feature definitions: " + feature_documents.error.message
        );
    const auto feature_terrain =
        oa::sim::map_runtime::resolve_feature_terrain(*selected_tnt_, feature_documents.value);
    if (!feature_terrain.ok())
        throw std::runtime_error("cannot resolve map features: " + feature_terrain.error->message);
    // The map's features fill the table in TNT order, so
    // the plot feature words index it directly. The table references the
    // GAF sequences, 3DO models and burn weapons through feature_assets_.
    feature_assets_ = {};
    const auto feature_host = feature_def_host();
    if (const auto error = oa::sim::map_runtime::init_feature_table(
            feature_table_, *selected_tnt_, feature_documents.value, &feature_host
        ))
        throw std::runtime_error("cannot load map feature table: " + error->message);
    load_mission_features(feature_documents.value, feature_host);
    set_load_progress(1, 45);
    const auto mask_bytes = assets_.read(oa::sim::map_runtime::sight_mask_archive).bytes;
    const auto mask_archive = oa::formats::gaf::parse(mask_bytes);
    if (!mask_archive.ok())
        throw std::runtime_error("cannot parse visibility masks: " + mask_archive.error->message);
    // The terrain load seeds every plot's metal from the session object's SurfaceMetal.
    configured_map_metal_ = schema_integer("SurfaceMetal", 0);
    auto prepared = oa::sim::map_runtime::prepare(
        *selected_tnt_, configured_map_metal_, *feature_terrain.value, *mask_archive.archive
    );
    if (!prepared.ok())
        throw std::runtime_error("cannot prepare map runtime: " + prepared.error->message);
    prepared_map_ = std::move(*prepared.value);
    set_load_progress(1, 100);

    loaded_commander_types_.clear();
    unit_definitions_.clear();
    runtime_definition_metadata_.clear();
    offline_type_fields_.clear();
    spawn_types_.clear();
    spawn_type_names_.clear();
    spawn_types_.push_back({});
    spawn_type_names_.emplace_back();
    loaded_commander_types_.push_back({});
    const UnitCatalogReader catalog_reader(assets_);
    auto catalog = oa::data::unit_definitions::load_unit_catalog(catalog_reader);
    if (!catalog)
        throw std::runtime_error("cannot load unit catalog: " + catalog.error.message);
    if (campaign_mission_)
        restrict_campaign_catalog(catalog.value);
    if (bootstrap.unit_filter.mark_units != nullptr)
        restrict_marked_catalog(catalog.value, bootstrap.unit_filter);
    const auto categories = oa::data::unit_definitions::resolve_unit_categories(catalog.value);
    if (!categories)
        throw std::runtime_error("cannot resolve unit categories: " + categories.error.message);
    category_registry_ = std::move(categories.value);
    // The unit definitions over the catalog order: the movement classes, then
    // each unit's FBI against Game.weapon_defs and the sound categories.
    const oa::data::defs::Files files = asset_files(assets_);
    if (!oa::data::defs::load_move_classes(&files, &unit_table_.move_classes, nullptr, nullptr))
        throw std::runtime_error("Can't load MOVEINFO.TDF");
    const auto weapon_defs = std::make_unique<oa::WeaponDef[][OA_WEAPON_DEF_COUNT]>(1);
    oa::sim::weapon_execution::store_weapon_defs(weapon_registry_, weapon_defs[0]);
    oa::data::defs::WeaponTdfSet weapon_files{};
    if (!oa::data::defs::load_weapon_tdf_set(&files, nullptr, false, &weapon_files))
        throw std::runtime_error("cannot list the weapon files");
    const std::
        unique_ptr<oa::data::defs::WeaponTdfSet, void (*)(oa::data::defs::WeaponTdfSet*) noexcept>
            weapon_files_owner(&weapon_files, oa::data::defs::weapon_tdf_set_free);
    const oa::data::defs::UnitHeaderSources header_sources{
        "", &weapon_files, kBuildVersionMajor, kBuildVersionMinor, false, false
    };
    CorpseFeatures corpses{feature_table_, feature_documents.value, &feature_host, {}};
    const oa::data::defs::UnitDefLoadHost corpse_host{&corpses, CorpseFeatures::load};
    auto& unit_table = unit_table_.tables;
    if (!oa::data::defs::unit_def_tables_allocate(
            &unit_table, static_cast<uint32_t>(catalog.value.entries.size() + 1U)
        ))
        throw std::runtime_error("unit catalog exceeds the unit table");
    const oa::data::defs::UnitDefSources unit_sources{
        "",
        &unit_table_.move_classes,
        weapon_defs[0],
        &unit_table_.sound_categories,
        &unit_table.categories,
        &unit_table.blocks,
        &corpse_host
    };
    loaded_commander_types_.reserve(catalog.value.entries.size() + 1U);
    unit_definitions_.reserve(catalog.value.entries.size());
    runtime_definition_metadata_.reserve(catalog.value.entries.size());
    spawn_types_.reserve(catalog.value.entries.size() + 1);
    spawn_type_names_.reserve(catalog.value.entries.size() + 1);
    set_load_progress(2, 5);
    std::size_t loaded_units = 0;
    for (const auto& catalog_entry : catalog.value.entries) {
        const auto& definition = catalog_entry.definition;
        auto metadata =
            oa::data::unit_definitions::resolve_runtime_metadata(definition, movement.value);
        if (!metadata)
            throw std::runtime_error(
                "cannot resolve runtime metadata for '" + definition.unit_name +
                "': " + metadata.error.message
            );
        oa::sim::unit_spawn::RuntimeBindings bindings;
        bindings.enabled = true;
        const auto weapon_binding = oa::sim::combat_state::bind_unit_weapons(
            weapon_registry_, {definition.weapon1, definition.weapon2, definition.weapon3}
        );
        bindings.resolved_weapon_present = weapon_binding.resolved_nondefault_weapon;
        bindings.default_mission =
            oa::data::mission_types::index_for_name(definition.default_mission_type);
        bindings.movement_footprint =
            std::array<int16_t, 2>{metadata.value.footprint_x, metadata.value.footprint_z};
        // The unit header from the enumerated file, then the FBI by unit name.
        const auto type_id = static_cast<uint16_t>(loaded_units + 1U);
        oa::UnitDef& record = unit_table.records[type_id];
        record.type_id = type_id;
        const auto& logical_path = catalog_entry.logical_path;
        const auto file_name = logical_path.substr(logical_path.find_last_of("/\\") + 1U);
        char fbi_path[oa::data::defs::path_capacity];
        oa::data::defs::build_variant_path(
            &files, fbi_path, sizeof fbi_path, "units", file_name.c_str(), "FBI", nullptr
        );
        bool refused = false;
        if (!oa::data::defs::load_unit_header(&files, fbi_path, record, header_sources, &refused))
            throw std::runtime_error("cannot load unit header " + std::string(fbi_path));
        oa::data::defs::build_variant_path(
            &files, fbi_path, sizeof fbi_path, "units", record.unit_name, "FBI", nullptr
        );
        if (!oa::data::defs::load_unit_def(&files, fbi_path, record, unit_sources))
            throw std::runtime_error("cannot load unit definition " + std::string(fbi_path));
        if (!corpses.error.empty())
            throw std::runtime_error(corpses.error);
        loaded_commander_types_.push_back(
            oa::sim::unit_spawn::load_runtime_type(definition, bindings, *this)
        );
        // After the FBI: the model's height, the GUI page count and
        // the page-zero bit; the COB stays with the runtime type.
        const auto& loaded_type = loaded_commander_types_.back();
        record.bounds_min_y = 0;
        record.model_height = oa::formats::objects3d::maximum_height_fixed(*loaded_type.model);
        record.size_y = record.model_height - record.bounds_min_y;
        record.flags = (record.flags & ~OA_UNIT_DEF_FLAG_BUILD_MENU_DEFAULT) |
                       (loaded_type.type.simulation.flags & OA_UNIT_DEF_FLAG_BUILD_MENU_DEFAULT);
        record.gui_page_count = loaded_type.type.gui_page_count;
        unit_definitions_.push_back(definition);
        runtime_definition_metadata_.push_back(std::move(metadata.value));
        spawn_types_.push_back(loaded_commander_types_.back().type);
        spawn_type_names_.push_back(loaded_commander_types_.back().unit_name);
        ++loaded_units;
        if (catalog.value.entries.size() != 0 && loaded_units % 20 == 0)
            set_load_progress(
                2, static_cast<uint8_t>(loaded_units * 100 / catalog.value.entries.size())
            );
    }
    // The headers loaded above are the unit headers; the kept types take their
    // agreed limits (UnitDef.player_limit) from the verdicts.
    if (bootstrap.unit_filter.mark_units != nullptr)
        bootstrap.unit_filter.mark_units(
            bootstrap.unit_filter.context, unit_table.records, unit_table.count
        );
    // The CANBUILD pass, then the download menus.
    if (!oa::data::defs::load_build_lists(&files, nullptr, &unit_table))
        throw std::runtime_error("Can't load GAMEDATA.TDF");
    if (!oa::data::defs::load_download_menu(&files, nullptr, &unit_table))
        throw std::runtime_error("cannot load the download menus");
    // The header table is marked stale once the full load is done; the
    // frontend reads its unit headers again when it next runs.
    frontend_game().unit_defs_stale = 1;
    set_load_progress(2, 100);
    set_load_progress(4, 20);
    offline_type_fields_.resize(loaded_commander_types_.size());
    for (std::size_t index = 1; index < loaded_commander_types_.size(); ++index) {
        const auto& metadata = runtime_definition_metadata_[index - 1U];
        auto& fields = offline_type_fields_[index];
        fields.definition = &unit_definitions_[index - 1U];
        fields.yard_mask = metadata.yard_cells;
        fields.runtime_metadata = &metadata;
        fields.target_masks = &category_registry_.target_masks[index];
        // UnitDef.move_class refers to the movement class, so class 0 (KBOTSS2)
        // is nonzero too; the handle is the class index + 1 and 0 means none.
        if (metadata.movement_class_handle)
            fields.movement_class =
                static_cast<oa::sim::unit_spawn::AssetHandle>(*metadata.movement_class_handle) + 1U;
        else
            fields.movement_class = oa::sim::unit_spawn::AssetHandle{0};
        fields.corpse_feature = unit_table.records[index].corpse;
    }
    // Featuredead/reclamate/burnt links resolve once the units have
    // added their corpses, appending any remnant the table still lacks.
    if (const auto error = oa::sim::map_runtime::load_feature_links(
            feature_table_, feature_documents.value, &feature_host
        ))
        throw std::runtime_error("cannot link feature definitions: " + error->message);
    set_load_progress(3, 20);
    match_features_.clear();
    match_gaf_features_.clear();
    match_gaf_anims_.clear();
    match_gaf_anim_index_.clear();
    gaf_feature_anim_tick_ = 0;
    wrecks_drawn_ = 0;
    {
        auto catalog = oa::sim::map_runtime::load_feature_catalog(feature_documents.value);
        if (!catalog.ok())
            throw std::runtime_error("cannot load feature catalog: " + catalog.error->message);
        feature_catalog_ = std::move(*catalog.value);
    }
    std::unordered_map<std::string, oa::formats::gaf::Archive> feature_gafs;
    for (const auto& placed : prepared_map_->placed_features) {
        const auto world_x = (placed.cell_x * 16 + static_cast<int32_t>(placed.footprint_x) * 8)
                             << 16;
        const auto world_z = (placed.cell_z * 16 + static_cast<int32_t>(placed.footprint_z) * 8)
                             << 16;
        const oa::formats::objects3d::FixedVector3 position{
            world_x, static_cast<int32_t>(placed.height) << 16, world_z
        };
        uint16_t feature_word = 0xffff;
        if (placed.cell_x >= 0 && placed.cell_z >= 0) {
            const auto plot =
                static_cast<std::size_t>(placed.cell_z) * selected_tnt_->attribute_width +
                static_cast<std::size_t>(placed.cell_x);
            if (plot < prepared_map_->collision_plots.size())
                feature_word = prepared_map_->collision_plots[plot].feature_word;
        }
        if (!placed.object.empty()) {
            try {
                const auto path = "objects3d/" + placed.object + ".3DO";
                const auto bytes = assets_.read(path).bytes;
                const auto* begin = reinterpret_cast<const std::byte*>(bytes.data());
                auto model = std::make_shared<const oa::formats::objects3d::Model>(
                    oa::formats::objects3d::load_3do({begin, bytes.size()})
                );
                MatchFeatureDraw draw;
                draw.instance = oa::sim::model_runtime::make_instance(std::move(model));
                draw.position = position;
                draw.cell_x = placed.cell_x;
                draw.cell_z = placed.cell_z;
                draw.feature_index = feature_word;
                match_features_.push_back(std::move(draw));
            } catch (const std::exception& error) {
                std::cerr << "feature model '" << placed.object << "' unavailable: " << error.what()
                          << '\n';
            }
            continue;
        }
        if (placed.filename.empty() || placed.seqname.empty())
            continue;
        try {
            auto& archive = feature_gafs[placed.filename];
            if (archive.sequences.empty()) {
                const auto path = "anims/" + placed.filename + ".gaf";
                const auto parsed = oa::formats::gaf::parse(assets_.read(path).bytes);
                if (!parsed.ok()) {
                    std::cerr << "feature GAF '" << path
                              << "' parse failed: " << parsed.error->message << '\n';
                    continue;
                }
                archive = std::move(*parsed.archive);
            }
            const auto* sequence = gaf_sequence(archive, placed.seqname);
            if (sequence == nullptr || sequence->frames.empty())
                continue;
            const auto anim = intern_gaf_feature_anim(
                placed.filename, placed.seqname, *sequence, placed.animating
            );
            if (anim == static_cast<std::size_t>(-1))
                continue;
            match_gaf_features_.push_back(
                {anim,
                 position,
                 placed.cell_x,
                 placed.cell_z,
                 feature_word,
                 intern_feature_shadow_anim(feature_word, placed.filename, placed.animating)}
            );
        } catch (const std::exception& error) {
            std::cerr << "feature sprite '" << placed.filename << "/" << placed.seqname
                      << "' unavailable: " << error.what() << '\n';
        }
    }
    if (match_)
        gaf_feature_anim_tick_ = match_->simulation().tick;
    std::vector<uint8_t> terrain_heights;
    terrain_heights.reserve(selected_tnt_->attributes.size());
    for (const auto& attribute : selected_tnt_->attributes)
        terrain_heights.push_back(attribute.height);
    altitude_cells_ = oa::sim::visibility_state::build_altitude_cells(
        terrain_heights,
        static_cast<int32_t>(selected_tnt_->attribute_width),
        static_cast<int32_t>(selected_tnt_->attribute_height),
        static_cast<uint8_t>(selected_tnt_->sea_level)
    );

    // The terrain load reads the line-of-sight ray tables.
    LosTables los_tables{altitude_patterns_, {}};
    if (!oa::data::defs::load_gamedata_tables(
            &files, nullptr, {&los_tables, LosTables::resize, LosTables::table}
        ))
        throw std::runtime_error(
            los_tables.error.empty() ? std::string("cannot load gamedata/los.tdf")
                                     : los_tables.error
        );
    collision_plots_.assign(prepared_map_->collision_plots.size(), {});
    for (std::size_t index = 0; index < collision_plots_.size(); ++index) {
        const auto& source = prepared_map_->collision_plots[index];
        collision_plots_[index].high_height = source.high_height;
        collision_plots_[index].low_height = source.low_height;
        collision_plots_[index].blocking_feature = source.blocking_feature;
        collision_plots_[index].metal_feature = source.metal_feature;
        collision_plots_[index].geo_feature = source.geo_feature;
        collision_plots_[index].indestructible_feature = source.indestructible_feature;
        collision_plots_[index].metal = source.metal;
        collision_plots_[index].feature_word = source.feature_word;
        collision_plots_[index].feature_back_x = source.feature_back_x;
        collision_plots_[index].feature_back_z = source.feature_back_z;
        collision_plots_[index].feature_height = source.feature_height;
        collision_plots_[index].feature_footprint_x = source.feature_footprint_x;
        collision_plots_[index].feature_footprint_z = source.feature_footprint_z;
    }
    const auto counter =
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto random_seed = options_.seed.value_or(
        options_.fixed_clock
            ? kFixedRandomSeed
            : static_cast<uint32_t>(counter) + static_cast<uint32_t>(counter >> 32U)
    );
    // A campaign plays under the rules block its mission loader wrote, a
    // skirmish under its own settings; the session flags apply either.
    int32_t session_record[4] = {
        static_cast<int32_t>(preferences_.skirmish.commander_death),
        static_cast<int32_t>(preferences_.skirmish.mapping),
        static_cast<int32_t>(preferences_.skirmish.line_of_sight),
        static_cast<int32_t>(preferences_.skirmish.los_type)
    };
    if (campaign_mission_)
        campaign_session_rules(session_record);
    const auto visibility_flags = static_cast<uint16_t>(
        (session_record[1] & 1) | ((session_record[2] & 1) << 1) | ((session_record[3] & 1) << 2)
    );
    uint8_t local_player = 0;
    for (std::size_t index = 0; index < skirmish_settings_.slots.size(); ++index)
        if (skirmish_settings_.slots[index].controller == entry::controller::human)
            local_player = static_cast<uint8_t>(index);
    match_local_player_ = local_player;
    offline_services_.set_viewpoint(local_player);
    oa::sim::match_runtime::OfflineInputs inputs{
        *selected_tnt_,
        loaded_commander_types_,
        spawn_types_,
        offline_type_fields_,
        weapon_registry_,
        prepared_map_->terrain_values,
        prepared_map_->sight_masks,
        prepared_map_->sight_width,
        prepared_map_->sight_height,
        bootstrap.units_per_player,
        visibility_flags,
        local_player,
        30,
        random_seed,
        this,
        [this] { return clock_milliseconds(); },
        collision_plots_,
        oa::sim::visibility_state::AltitudeSightData{
            prepared_map_->sight_width,
            prepared_map_->sight_height,
            altitude_cells_,
            altitude_patterns_
        },
        integer("MinWindSpeed", 0),
        integer("MaxWindSpeed", 0),
        static_cast<float>(integer("TidalStrength", 0)),
        feature_table_.defs
    };
    inputs.unit_defs = {unit_table.records, unit_table.count};
    inputs.mission_features = mission_features_;
    inputs.resuming_saved_game = resuming_saved_game();
    inputs.effect_sequence = [this](std::string_view archive, std::string_view entry) {
        return gaf_sequence(explosion_gaf_archive(archive), entry);
    };
    inputs.feature_sequence_frame =
        [this](
            oa_ref32 sequence, uint16_t frame, oa::sim::feature_runtime::FeatureSequenceFrame& out
        ) { return feature_sequence_frame(sequence, frame, out); };
    inputs.loaded_primitives = [this](
                                   const oa::formats::objects3d::Model& model,
                                   uint32_t object,
                                   std::vector<oa::sim::effect_particles::PiecePrimitive>& out
                               ) { return loaded_match_primitives(model, object, out); };
    // The session start loads FX.GAF before the mission places
    // the map's features, so a geothermal vent smokes from the first tick.
    if (match_fx_.sequences.empty())
        append_gaf_file(match_fx_, "anims/FX.GAF");
    try {
        match_ = std::make_unique<oa::sim::match_runtime::Match>(inputs, offline_services_);
        bind_match_speech();
        match_->set_difficulty(static_cast<int32_t>(preferences_.difficulty));
        // The mission starts with the top bar's shown stores and the space-bar
        // strip cleared; the strip's LIGHTBAR picture is bound again.
        oa::ui::hud::reset_status_panel(match_->state().game);
        status_lightbar_.reset();
        status_lightbar_loaded_ = false;
        // A campaign's block stays in Game.session_record where its loader
        // wrote it, and so does the mission's nomovie flag.
        if (campaign_mission_) {
            std::copy(
                std::begin(session_record),
                std::end(session_record),
                std::begin(match_->state().game.session_record)
            );
            match_->state().game.no_movie = campaign_object().no_movie;
        }
        oa::sim::session::apply_session_flags(&match_->state().game, session_record);
        // The option word the settings load filled persists into the match,
        // less the per-match toggles the match loader clears; its tree-death bit lets weapons
        // damage features.
        oa::ui::console::set_console_flags(
            match_->state().game,
            static_cast<uint16_t>(preferences_.display_flags & ~match_cleared_console_flags)
        );
        seed_match_options(match_->state().game);
    } catch (const std::exception& error) {
        if (std::string_view(error.what()) != "altitude sight data is required")
            throw;
        altitude_sight_blocked_ = true;
        status_ = std::string("Offline match setup blocked: ") + error.what();
        std::cerr << "unsupported operation: " << status_ << '\n';
        return;
    }
    {
        // UnitDef.unit_name is the FBI UnitName that savegame unit
        // records and the console's spawn patterns name a type by; the spawn
        // command walks Game.unit_def_count types.
        oa::World& world = match_->state();
        for (std::size_t type = 1; type < world.unit_def_count && type < spawn_type_names_.size();
             ++type)
            std::snprintf(
                world.unit_defs[type].unit_name,
                sizeof world.unit_defs[type].unit_name,
                "%s",
                spawn_type_names_[type].c_str()
            );
        world.game.unit_def_count = static_cast<int32_t>(world.unit_def_count);
        // A skirmish starts at the configured unit limit (Game.max_units_setting); a
        // save's Summary writes it back as "maxunits". A campaign plays at
        // its mission's limit and leaves the setting alone.
        world.game.max_units_setting =
            campaign_mission_ ? kSkirmishUnitsPerPlayer : bootstrap.units_per_player;
        bind_session_options();
        // Game.player_count, the players Start counted (two in a campaign): the
        // kills board has a row for each.
        world.game.player_count = state_.player_count;
        // The command-line fields of the Game block;
        // the extension writes the ones its launch switches set.
        world.game.player_timeout_seconds = kPlayerTimeoutSeconds;
        world.game.send_error_percent = 0;
        if (extension_.match_game != nullptr)
            extension_.match_game(extension_.context, world.game);
        world.game.setup_options |= options_.launch.game_options;
        bind_player_records(world);
        if (bootstrap.seat_roster)
            seat_skirmish_roster(world);
        else if (campaign_mission_)
            seat_campaign_players(world);
    }
    place_mission_feature_draws();
    match_->point_sound = {
        this,
        [](void* context) { return static_cast<Runtime*>(context)->sound_spatial_ != 0; },
        [](
            void* context, const char* name, const oa::sim::match_runtime::Match::PointSound& sound
        ) { static_cast<Runtime*>(context)->play_point_sound(name, sound); }
    };
    // The panel dropped to its root page is reloaded for the selection by
    // the next panel update; the runtime rebuilds it at once.
    match_->order_panel = {this, [](void* context) {
                               static_cast<Runtime*>(context)->apply_match_hud_for_selection();
                           }};
    // Sound-table names the match plays unplaced, like the frontend's, and
    // the files they name for the ones it plays at a point.
    match_->named_sound = {
        this,
        [](void* context, const char* name) {
            static_cast<Runtime*>(context)->play_ui_sound(name, 0);
        },
        [](void* context, const char* name) -> const char* {
            const auto& registry = static_cast<Runtime*>(context)->audio_registry_;
            const auto* sound = registry.get(registry.find(name));
            return sound != nullptr ? sound->file.c_str() : nullptr;
        }
    };
    bind_respawn_view();
    // The observer pulse pauses while shift is held (asked of the keyboard);
    // the selection it drops takes the runtime's primary unit
    // with it.
    match_->observer = {
        this,
        [](void* context) {
            return static_cast<Runtime*>(context)->control_key_down(
                oa::ui::gui_input::ControlKey::shift
            );
        },
        [](void* context) {
            auto& self = *static_cast<Runtime*>(context);
            self.selected_match_unit_ = 0;
            self.apply_match_hud_for_selection();
        },
    };
    bind_message_log();
    reset_meteors();
    match_->meteor = {this, [](void* context) { static_cast<Runtime*>(context)->step_meteors(); }};
    match_->profile.context = this;
    match_->profile.mark = [](void* context, int32_t category) {
        static_cast<Runtime*>(context)->mark_profile(category);
    };
    // The console binds to the new match here, even over a world at the old
    // one's address, so its host (with the extension's part) is filled before
    // the match posts its first line; the binding restores the carried
    // console values.
    if (console_)
        console_->bound_world = nullptr;
    (void)match_console();
    effect_boundary_.clock = &match_->simulation().tick;
    set_load_progress(3, 100);
    set_load_progress(4, 70);
    offline_effects_.bind(*match_);
    offline_services_.bind_effects(offline_effects_);
    offline_services_.bind_announcements(
        unit_sound_catalog_,
        *match_,
        spawn_types_,
        unit_definitions_,
        {preferences_.unit_chat,
         preferences_.unit_chat_text,
         !options_.mute,
         (preferences_.sound_flags & init::preference_flags::speech_fx) != 0,
         true,
         novelty_voice_ != 0}
    );
    for (std::size_t player = 0; player < skirmish_settings_.slots.size(); ++player) {
        const auto controller = skirmish_settings_.slots[player].controller;
        if (controller != entry::controller::disabled && controller != entry::controller::human &&
            controller != entry::controller::computer)
            throw std::runtime_error("skirmish player has invalid controller state");
        if (bootstrap.seat_roster)
            continue;
        auto& simulation_player = match_->simulation().players[player];
        simulation_player.present = controller != entry::controller::disabled;
        simulation_player.status = static_cast<uint8_t>(controller);
    }
    std::array<uint8_t, 10> local_allies{};
    const auto local_alliance = skirmish_settings_.slots[local_player].alliance;
    for (std::size_t index = 0; index < local_allies.size(); ++index) {
        const auto& candidate = skirmish_settings_.slots[index];
        local_allies[index] = static_cast<uint8_t>(
            index == local_player ||
            (candidate.controller != entry::controller::disabled &&
             candidate.alliance == local_alliance && candidate.alliance != 5)
        );
    }
    match_->configure_outcomes(
        local_player,
        local_allies,
        bootstrap.defeat_allowed,
        campaign_mission_,
        bootstrap.multiplayer
    );
    for (std::size_t player = 0; player < skirmish_settings_.slots.size(); ++player) {
        const auto& owner = skirmish_settings_.slots[player];
        if (owner.controller == entry::controller::disabled)
            continue;
        std::array<uint8_t, 10> allies{};
        for (std::size_t candidate_index = 0; candidate_index < allies.size(); ++candidate_index) {
            const auto& candidate = skirmish_settings_.slots[candidate_index];
            allies[candidate_index] = static_cast<uint8_t>(
                candidate_index == player ||
                (candidate.controller != entry::controller::disabled &&
                 candidate.alliance == owner.alliance && owner.alliance != 5)
            );
        }
        match_->configure_player_alliances(static_cast<uint8_t>(player), allies);
        std::copy(allies.begin(), allies.end(), match_->state().game.players[player].alliance);
    }
    match_timing_ = {};
    match_timing_.requested_rate = preferences_.game_speed;
    match_timing_.actual_rate = preferences_.current_game_speed;
    // The match start resets a multiplayer session to the normal
    // speed in the preference words themselves.
    oa::base::game_loop::reset_mode_timing(match_timing_, bootstrap.multiplayer);
    preferences_.game_speed = match_timing_.requested_rate;
    preferences_.current_game_speed = match_timing_.actual_rate;
    // The speed words are Game.requested_speed and Game.current_speed; a
    // speed change compares against them before it posts its line.
    match_->state().game.requested_speed = match_timing_.requested_rate;
    match_->state().game.current_speed = match_timing_.actual_rate;
    match_timing_.previous_clock =
        oa::base::game_loop::scaled_clock(clock_milliseconds(), match_clock_scale());
    match_tick_blocked_ = false;
    configure_computer_players();
    std::size_t started_players = 0;
    if (campaign_mission_) {
        // Mission start rebuilds the sight grids before the mission's units stand.
        reset_match_sight(true);
        // A mission resumed from a savegame restores it instead.
        if (!resume_saved_mission())
            spawn_campaign_units();
        started_players = 0;
        for (const auto& slot : match_->world().slots)
            if (slot.unit != nullptr && slot.unit->type_index)
                ++started_players;
    } else if (bootstrap.place_commanders) {
        for (std::size_t player = 0; player < skirmish_settings_.slots.size(); ++player) {
            const auto& slot = skirmish_settings_.slots[player];
            if (slot.controller == 0)
                continue;
            oa::sim::unit_spawn::PlayerSetup setup{
                static_cast<uint8_t>(slot.side),
                static_cast<uint8_t>(slot.color),
                slot.metal,
                slot.energy
            };
            oa::sim::unit_spawn::StartResult started;
            try {
                started = match_->start_player(
                    static_cast<uint8_t>(player),
                    setup,
                    selected_start_markers_,
                    static_cast<int32_t>(player),
                    kBattlefieldWidth,
                    kBattlefieldHeight,
                    *this
                );
            } catch (const std::exception& error) {
                const std::string_view message(error.what());
                if (message != "the alternate altitude sight algorithm is not implemented" &&
                    message != "altitude sight data is required")
                    throw;
                altitude_sight_blocked_ = true;
                std::cerr << "unsupported operation: " << error.what() << '\n';
                break;
            }
            if (!started.position_found || started.unit == nullptr)
                throw std::runtime_error("offline commander start failed");
            ++started_players;
        }
        // Mission start seeds the stores once every commander is placed.
        std::array<oa::ui::hud::SkirmishSlot, OA_PLAYER_COUNT> seeds{};
        for (std::size_t player = 0;
             player < seeds.size() && player < skirmish_settings_.slots.size();
             ++player) {
            seeds[player].metal = skirmish_settings_.slots[player].metal;
            seeds[player].energy = skirmish_settings_.slots[player].energy;
        }
        oa::ui::hud::set_starting_resources(
            match_->state(), oa::ui::hud::kSessionSkirmish, nullptr, seeds.data()
        );
        // Mission start rebuilds the sight grids once every commander stands.
        reset_match_sight(true);
    }
    // A campaign's use-only file may leave either commander out.
    const auto armcom = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "ARMCOM");
    const auto corcom = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "CORCOM");
    if (!campaign_mission_ && (armcom == 0 || corcom == 0))
        throw std::runtime_error("unit catalog lacks required commanders");
    set_load_progress(4, 100);
    set_load_progress(5, 100);
    if (!options_.trace_digest.empty() &&
        !match_->record_trace(options_.trace_digest.string(), options_.trace_units.string()))
        throw std::runtime_error(
            "cannot create the trace stream " + options_.trace_digest.string() +
            (options_.trace_units.empty() ? "" : " or " + options_.trace_units.string())
        );
    status_ = "Offline match world prepared for " + selected_map_name_runtime_ + " with " +
              std::to_string(catalog.value.entries.size()) + " unit runtimes, " +
              std::to_string(feature_table_.defs.size()) + " feature definitions and " +
              std::to_string(started_players) +
              (campaign_mission_ ? " placed units" : " commanders") +
              (altitude_sight_blocked_ ? "; altitude sight blocks match entry." : ".");
    std::cerr << status_ << '\n';
}

} // namespace oa::app
