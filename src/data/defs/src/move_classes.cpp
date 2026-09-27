// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/defs/move_classes.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace oa::data::defs {
namespace {

constexpr int16_t default_max_water_depth = 10000;
constexpr int16_t default_min_water_depth = -10000;
constexpr uint8_t default_slope = 0xff;

} // namespace

void move_class_init_defaults(MoveClass* move_class) noexcept {
    std::memset(move_class, 0, sizeof *move_class);
    move_class->max_slope = default_slope;
    move_class->max_water_slope = default_slope;
    move_class->bad_slope = default_slope;
    move_class->bad_water_slope = default_slope;
    move_class->max_water_depth = default_max_water_depth;
    move_class->min_water_depth = default_min_water_depth;
}

void move_class_load(MoveClass* move_class, const formats::tdf::Block* section) noexcept {
    move_class->footprint_x = static_cast<int16_t>(formats::tdf::get_int(section, "FootPrintX", 0));
    move_class->footprint_z = static_cast<int16_t>(formats::tdf::get_int(section, "FootPrintZ", 0));
    move_class->max_water_depth = static_cast<int16_t>(
        formats::tdf::get_int(section, "maxwaterdepth", move_class->max_water_depth)
    );
    move_class->min_water_depth = static_cast<int16_t>(
        formats::tdf::get_int(section, "minwaterdepth", move_class->min_water_depth)
    );
    const auto max_slope =
        static_cast<uint32_t>(formats::tdf::get_int(section, "maxslope", move_class->max_slope));
    move_class->max_slope = static_cast<uint8_t>(max_slope);
    move_class->bad_slope = static_cast<uint8_t>(
        formats::tdf::get_int(section, "badslope", static_cast<int32_t>((max_slope & 0xffu) >> 1))
    );
    const auto max_water_slope = static_cast<uint32_t>(
        formats::tdf::get_int(section, "maxwaterslope", move_class->max_water_slope)
    );
    move_class->max_water_slope = static_cast<uint8_t>(max_water_slope);
    const auto bad_water_slope = static_cast<uint8_t>(formats::tdf::get_int(
        section, "badwaterslope", static_cast<int32_t>((max_water_slope & 0xffu) >> 1)
    ));
    move_class->bad_water_slope = bad_water_slope;
    const uint8_t water_limit = move_class->max_water_slope;
    if (water_limit < move_class->max_slope)
        move_class->max_slope = water_limit;
    if (move_class->max_slope < move_class->bad_slope)
        move_class->bad_slope = move_class->max_slope;
    if (water_limit < bad_water_slope)
        move_class->bad_water_slope = water_limit;
}

void move_class_table_init(MoveClassTable* table) noexcept {
    for (uint32_t slot = 0; slot < OA_MOVE_CLASS_COUNT; ++slot) {
        move_class_init_defaults(&table->classes[slot]);
        table->names[slot][0] = '\0';
    }
}

void move_class_table_load(MoveClassTable* table, formats::tdf::Document* moveinfo) noexcept {
    for (uint32_t slot = 0; slot < OA_MOVE_CLASS_COUNT; ++slot) {
        char section[move_class_name_capacity];
        std::snprintf(section, sizeof section, "CLASS%u", slot);
        formats::tdf::reset_cursor(moveinfo);
        if (!formats::tdf::select_section(moveinfo, section))
            continue;
        const formats::tdf::Block* block = formats::tdf::cursor(moveinfo);
        formats::tdf::get_string(block, "name", table->names[slot], move_class_name_capacity, "");
        table->classes[slot].name = slot + 1u;
        move_class_load(&table->classes[slot], block);
    }
}

bool load_move_classes(
    const Files* files, MoveClassTable* table, const char* variant, formats::tdf::ParseError* error
) noexcept {
    move_class_table_init(table);
    char path[path_capacity];
    build_variant_path(files, path, sizeof path, "gamedata", "moveinfo", "tdf", variant);
    formats::tdf::Document document;
    formats::tdf::document_init(&document);
    const bool loaded = load_tdf_file(files, path, &document, error);
    if (loaded)
        move_class_table_load(table, &document);
    formats::tdf::document_free(&document);
    return loaded;
}

int move_class_find(const MoveClassTable* table, const char* name) noexcept {
    for (uint32_t slot = 0; slot < OA_MOVE_CLASS_COUNT; ++slot)
        if (table->classes[slot].name != 0 &&
            formats::tdf::compare_nocase(table->names[slot], name) == 0)
            return static_cast<int>(slot);
    return -1;
}

} // namespace oa::data::defs
