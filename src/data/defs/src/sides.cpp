// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/defs/sides.hpp"
#include "oa/data/defs/layout.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace oa::data::defs {
namespace {

constexpr std::size_t font_name_capacity = 0x100;
constexpr uint32_t reload_rect_count = 3;

struct RectKey {
    const char* section{};
    Rect32 Side::* rect{};
};

// The game's read order; it decides which missing section is reported.
constexpr RectKey rect_keys[] = {
    {"LOGO", &Side::rect_logo},
    {"ENERGYBAR", &Side::rect_energy_bar},
    {"ENERGYNUM", &Side::rect_energy_num},
    {"METALBAR", &Side::rect_metal_bar},
    {"METALNUM", &Side::rect_metal_num},
    {"TOTALUNITS", &Side::rect_total_units},
    {"TOTALTIME", &Side::rect_total_time},
    {"ENERGY0", &Side::rect_energy0},
    {"METAL0", &Side::rect_metal0},
    {"ENERGYMAX", &Side::rect_energy_max},
    {"METALMAX", &Side::rect_metal_max},
    {"ENERGYPRODUCED", &Side::rect_energy_produced},
    {"ENERGYCONSUMED", &Side::rect_energy_consumed},
    {"METALPRODUCED", &Side::rect_metal_produced},
    {"METALCONSUMED", &Side::rect_metal_consumed},
    {"LOGO2", &Side::rect_logo2},
    {"UNITNAME", &Side::rect_unit_name},
    {"DAMAGEBAR", &Side::rect_damage_bar},
    {"UNITMETALMAKE", &Side::rect_unit_metal_make},
    {"UNITMETALUSE", &Side::rect_unit_metal_use},
    {"UNITENERGYMAKE", &Side::rect_unit_energy_make},
    {"UNITENERGYUSE", &Side::rect_unit_energy_use},
    {"MISSIONTEXT", &Side::rect_mission_text},
    {"UNITNAME2", &Side::rect_unit_name2},
    {"DAMAGEBAR2", &Side::rect_damage_bar2},
    {"NAME", &Side::rect_name},
    {"DESCRIPTION", &Side::rect_description},
};

void copy_if_present(
    const formats::tdf::Block* block, const char* key, char* out, std::size_t size
) noexcept {
    char buffer[font_name_capacity];
    if (formats::tdf::get_string(block, key, buffer, size, ""))
        std::memcpy(out, buffer, std::strlen(buffer) + 1);
}

} // namespace

uint32_t side_index(const Side* side) noexcept {
    return side->side_index;
}

bool side_load_rect(
    formats::tdf::Document* sidedata,
    Rect32* rect,
    const char* section,
    const char* side_name,
    char* error,
    std::size_t error_capacity
) noexcept {
    const formats::tdf::Block* saved = formats::tdf::cursor(sidedata);
    bool found = formats::tdf::select_section(sidedata, section);
    if (found) {
        const formats::tdf::Block* block = formats::tdf::cursor(sidedata);
        rect->x1 = formats::tdf::get_int(block, "x1", 0);
        rect->y1 = formats::tdf::get_int(block, "y1", 0);
        rect->x2 = formats::tdf::get_int(block, "x2", 0);
        rect->y2 = formats::tdf::get_int(block, "y2", 0);
    } else if (error != nullptr && error_capacity != 0) {
        std::snprintf(
            error,
            error_capacity,
            "Section [%s] is missing from GAMEDATA/SIDEDATA.TDF for the %s side",
            section,
            side_name
        );
    }
    formats::tdf::set_cursor(sidedata, saved);
    return found;
}

bool side_table_load(
    formats::tdf::Document* sidedata, SideTable* table, const SideFontResolver* fonts
) noexcept {
    std::memset(static_cast<void*>(table), 0, sizeof *table);
    uint32_t index = 0;
    for (; index < OA_SIDE_COUNT; ++index) {
        Side* side = &table->sides[index];
        side->side_index = index;
        char section[32];
        std::snprintf(section, sizeof section, "SIDE%u", index);
        formats::tdf::reset_cursor(sidedata);
        if (!formats::tdf::select_section(sidedata, section))
            break;
        const formats::tdf::Block* block = formats::tdf::cursor(sidedata);
        copy_if_present(block, "name", side->name, sizeof side->name);
        copy_if_present(block, "nameprefix", side->name_prefix, sizeof side->name_prefix);
        copy_if_present(block, "commander", side->commander, sizeof side->commander);
        char font[font_name_capacity];
        if (formats::tdf::get_string(block, "font", font, sizeof font, "") && fonts != nullptr &&
            fonts->font != nullptr)
            side->font = fonts->font(fonts->context, font);
        side->energy_color = static_cast<uint32_t>(formats::tdf::get_int(block, "energycolor", 0));
        side->metal_color = static_cast<uint32_t>(formats::tdf::get_int(block, "metalcolor", 0));
        for (const RectKey& key : rect_keys)
            if (!side_load_rect(
                    sidedata,
                    &(side->*key.rect),
                    key.section,
                    side->name,
                    table->error,
                    sizeof table->error
                ))
                return false;
        Rect32* reload[reload_rect_count] = {
            &side->rect_reload1, &side->rect_reload2, &side->rect_reload3
        };
        for (uint32_t slot = 0; slot < reload_rect_count; ++slot) {
            char reload_section[32];
            std::snprintf(reload_section, sizeof reload_section, "RELOAD%u", slot + 1);
            if (!side_load_rect(
                    sidedata,
                    reload[slot],
                    reload_section,
                    side->name,
                    table->error,
                    sizeof table->error
                ))
                return false;
        }
    }
    table->count = index;
    return true;
}

bool load_side_data(
    const Files* files, SideTable* table, const char* variant, const SideFontResolver* fonts
) noexcept {
    char path[path_capacity];
    build_variant_path(
        files,
        path,
        sizeof path,
        directory_name(DataDirectory::gamedata),
        "sidedata",
        "tdf",
        variant
    );
    formats::tdf::Document document;
    formats::tdf::document_init(&document);
    // 3.1c ignores a failed load here and ends up with zero sides.
    const bool loaded = load_tdf_file(files, path, &document, nullptr);
    const bool ok = side_table_load(&document, table, fonts);
    formats::tdf::document_free(&document);
    return loaded && ok;
}

} // namespace oa::data::defs
