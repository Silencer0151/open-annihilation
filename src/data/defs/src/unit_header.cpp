// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/defs/unit_header.hpp"
#include "oa/base/game_math.hpp"

#include "oa/data/defs/locale.hpp"
#include "oa/base/text.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace oa::data::defs {
namespace {

constexpr uint32_t max_weapon_files = 0x1000; // most weapon files read
constexpr uint32_t copyright_capacity = 0x80;
constexpr const char* copyright_year = "0000";
constexpr double version_minor_scale = 10.0;
constexpr const char* weapon_keys[] = {
    "weapon1", "weapon2", "weapon3", "explodeas", "selfdestructas"
};

struct NameList {
    char (*names)[path_capacity];
    uint32_t count{};
    uint32_t capacity{};
    bool failed{};
};

void collect_name(void* user, const char* name) {
    auto* list = static_cast<NameList*>(user);
    if (list->failed)
        return;
    if (list->count == list->capacity) {
        const uint32_t grown = list->capacity == 0 ? 64u : list->capacity * 2u;
        auto* names = grown > max_weapon_files
                          ? nullptr
                          : static_cast<char (*)[path_capacity]>(
                                std::realloc(list->names, sizeof *list->names * grown)
                            );
        if (names == nullptr) {
            list->failed = true;
            return;
        }
        list->names = names;
        list->capacity = grown;
    }
    oa::base::text::copy_padded(list->names[list->count], name, path_capacity - 1);
    list->names[list->count][path_capacity - 1] = '\0';
    ++list->count;
}

uint32_t with_bit(uint32_t word, uint32_t bit, int32_t value) noexcept {
    return (word & ~bit) | ((static_cast<uint32_t>(value) & 1u) != 0 ? bit : 0u);
}

// Low 32 bits of floor(value) truncated toward zero to 64 bits.
int32_t floor_whole(double value) noexcept {
    return static_cast<int32_t>(
        static_cast<uint64_t>(base::game_math::truncate_to_int64(std::floor(value)))
    );
}

} // namespace

void weapon_tdf_set_init(WeaponTdfSet* set) noexcept {
    set->documents = nullptr;
    set->count = 0;
    set->capacity = 0;
}

bool load_weapon_tdf_set(
    const Files* files, const char* variant, bool archive_only, WeaponTdfSet* set
) noexcept {
    weapon_tdf_set_free(set);
    NameList names{nullptr, 0, 0, false};
    files->list(files->context, "Weapons", "tdf", collect_name, &names);
    if (names.failed || names.count == 0) {
        std::free(names.names);
        return !names.failed;
    }
    set->documents = static_cast<formats::tdf::Document*>(
        std::calloc(names.count, sizeof(formats::tdf::Document))
    );
    if (set->documents == nullptr) {
        std::free(names.names);
        return false;
    }
    set->capacity = names.count;
    for (uint32_t index = 0; index < set->capacity; ++index)
        formats::tdf::document_init(&set->documents[index]);
    for (uint32_t file = 0; file < names.count; ++file) {
        formats::tdf::Document* slot = &set->documents[set->count];
        char path[path_capacity];
        build_variant_path(files, path, sizeof path, "Weapons", names.names[file], "TDF", variant);
        if (!load_tdf_file(files, path, slot, nullptr))
            continue;
        if (!slot->from_archive && archive_only)
            continue;
        ++set->count;
    }
    std::free(names.names);
    return true;
}

void weapon_tdf_set_free(WeaponTdfSet* set) noexcept {
    for (uint32_t index = 0; index < set->capacity; ++index)
        formats::tdf::document_free(&set->documents[index]);
    std::free(set->documents);
    weapon_tdf_set_init(set);
}

uint32_t weapon_tdf_hash(const WeaponTdfSet* set, const char* name) noexcept {
    if (name == nullptr || name[0] == '\0')
        return 0;
    for (uint32_t index = 0; index < set->count; ++index) {
        formats::tdf::Document* document = &set->documents[index];
        formats::tdf::reset_cursor(document);
        if (formats::tdf::select_section(document, name))
            return formats::tdf::cursor(document)->body_hash;
    }
    return 0;
}

bool load_unit_header(
    const Files* files,
    const char* path,
    UnitDef& unit,
    const UnitHeaderSources& sources,
    bool* refused
) noexcept {
    uint8_t* data = nullptr;
    uint32_t size = 0;
    bool from_archive = false;
    if (!files->read(files->context, path, &data, &size, &from_archive))
        return false;
    formats::tdf::Document document;
    formats::tdf::document_init(&document);
    const bool parsed = size != 0 && size <= formats::tdf::max_input_bytes &&
                        formats::tdf::parse_text(
                            &document, reinterpret_cast<const char*>(data), size, false, nullptr
                        );
    const uint32_t file_hash = formats::tdf::buffer_hash(data, static_cast<int32_t>(size));
    files->release(files->context, data);
    if (!parsed || !formats::tdf::select_section(&document, "UNITINFO")) {
        formats::tdf::document_free(&document);
        return false;
    }
    const formats::tdf::Block* block = formats::tdf::cursor(&document);
    unit.fbi_hash = file_hash;
    get_localized_string(block, sources.language, "name", unit.name, sizeof unit.name, nullptr);
    formats::tdf::get_string(block, "unitname", unit.unit_name, sizeof unit.unit_name, "");
    formats::tdf::get_string(block, "side", unit.side, sizeof unit.side, "");
    formats::tdf::get_string(block, "ai_weight", unit.ai_weight, sizeof unit.ai_weight, "");
    formats::tdf::get_string(block, "ai_limit", unit.ai_limit, sizeof unit.ai_limit, "");
    if (!formats::tdf::get_string(
            block, "objectname", unit.object_name, sizeof unit.object_name, ""
        ))
        std::memcpy(unit.object_name, unit.unit_name, sizeof unit.object_name);
    unit.build_cost_energy = static_cast<float>(formats::tdf::get_int(block, "buildcostenergy", 0));
    unit.build_cost_metal = static_cast<float>(formats::tdf::get_int(block, "buildcostmetal", 0));
    unit.abilities = with_bit(
        unit.abilities,
        OA_UNIT_DEF_ABILITY_NO_RESTRICT,
        formats::tdf::get_int(block, "norestrict", 0)
    );
    unit.abilities = with_bit(
        unit.abilities, OA_UNIT_DEF_ABILITY_WACKY, formats::tdf::get_int(block, "wacky", 0)
    );
    for (const char* key : weapon_keys)
        unit.weapon_checksum ^=
            weapon_tdf_hash(sources.weapons, formats::tdf::find_value(block, key));

    const double version = formats::tdf::get_double(block, "Version", 0.0);
    const int32_t major = floor_whole(version);
    const int32_t minor = floor_whole((version - static_cast<double>(major)) * version_minor_scale);
    const bool current = major < sources.build_major ||
                         (major == sources.build_major && minor <= sources.build_minor);
    unit.flags = with_bit(unit.flags, OA_UNIT_DEF_FLAG_AVAILABLE, current ? 1 : 0);
    if ((!from_archive && sources.archive_only) || sources.disc_mismatch) {
        unit.flags &= ~OA_UNIT_DEF_FLAG_AVAILABLE;
        *refused = true;
    }
    char copyright[copyright_capacity];
    formats::tdf::get_string(
        block, "Copyright", copyright, sizeof copyright, unit_copyright_default
    );
    const std::size_t year_at =
        static_cast<std::size_t>(std::strstr(unit_copyright, copyright_year) - unit_copyright);
    std::memcpy(copyright + year_at, copyright_year, std::strlen(copyright_year));
    if (std::strcmp(copyright, unit_copyright) != 0) {
        unit.flags &= ~OA_UNIT_DEF_FLAG_AVAILABLE;
        *refused = true;
    }
    unit.player_limit = -1;
    formats::tdf::document_free(&document);
    return true;
}

void reload_unit_headers(Game* game, const UnitHeaderLoader& loader) noexcept {
    if (game->unit_def_count != 0 && game->unit_defs_stale == 0)
        return;
    if (loader.load != nullptr)
        loader.load(loader.context, game);
    game->unit_defs_stale = 0;
}

} // namespace oa::data::defs
