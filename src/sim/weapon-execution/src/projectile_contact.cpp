// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/weapon_execution/projectile_contact.hpp"

#include "oa/core/unit_def.h"
#include "oa/core/weapon_def.h"

#include <cstddef>

namespace oa::sim::weapon_execution {

namespace {

constexpr uint16_t feature_continuation_word = 0xfffe;
constexpr int32_t feature_cell_units = 16;

int16_t high_word(int32_t fixed) noexcept {
    return static_cast<int16_t>(static_cast<uint32_t>(fixed) >> 16);
}

/// Returns the squared distance in whole units of a 16.16 delta.
///
/// @param x X delta, 16.16
/// @param y Y delta, 16.16
/// @param z Z delta, 16.16
/// @return the high 32 bits of each signed square, summed with wrap
int32_t squared_distance_high(int32_t x, int32_t y, int32_t z) noexcept {
    const auto square_high = [](int32_t value) {
        return static_cast<uint32_t>(
            static_cast<uint64_t>(static_cast<int64_t>(value) * static_cast<int64_t>(value)) >> 32
        );
    };
    return static_cast<int32_t>(square_high(x) + square_high(y) + square_high(z));
}

// Cell of a 16.16 coordinate for feature re-entry: the high word over 16, truncating.
int16_t feature_cell(int32_t fixed) noexcept {
    return static_cast<int16_t>(high_word(fixed) / feature_cell_units);
}

// The occupant is hit below its model top; in the air band it must also be
// above its lower bound. The shot's own player never hits.
oa_ref32 occupant_hit(
    const World& world, const Projectile& shot, uint16_t occupant, bool air_band
) noexcept {
    if (occupant == 0 || occupant >= world.unit_slot_count || world.units == nullptr)
        return 0;
    const Unit& unit = world.units[occupant];
    if (unit.owner_index == shot.owner_index)
        return 0;
    const UnitDef* def = world_unit_def_of(&world, &unit);
    if (def == nullptr)
        return 0;
    const int32_t top = def->model_height + unit.position.y;
    if (!air_band)
        return shot.position.y < top ? oa_unit_ref_from_slot(occupant) : 0;
    const int32_t bottom = def->bounds_min_y + unit.position.y;
    return bottom <= shot.position.y && shot.position.y <= top ? oa_unit_ref_from_slot(occupant)
                                                               : 0;
}

} // namespace

const FeatureDef* plot_feature_def(const World& world, const MapPlot& plot) noexcept {
    if (world.feature_defs == nullptr)
        return nullptr;
    const uint16_t word = plot.feature;
    if (word < OA_PLOT_FEATURE_RESERVED) {
        if (static_cast<int32_t>(word) < world.game.feature_def_count &&
            word < world.feature_def_count)
            return &world.feature_defs[word];
        return nullptr;
    }
    if (word != feature_continuation_word || world.plots == nullptr)
        return nullptr;
    const size_t rows_back = plot.feature_record & 0xffu;
    const size_t columns_back = plot.feature_record >> 8;
    const size_t back = rows_back * static_cast<size_t>(world.game.map_width) + columns_back;
    const size_t index = static_cast<size_t>(&plot - world.plots);
    if (back > index)
        return nullptr;
    const uint16_t origin_word = world.plots[index - back].feature;
    if (origin_word >= OA_PLOT_FEATURE_RESERVED || origin_word >= world.feature_def_count)
        return nullptr;
    return &world.feature_defs[origin_word];
}

ContactPlot contact_plot(const World& world, const MapPlot& plot) noexcept {
    ContactPlot cell;
    cell.ground_unit = plot.ground_unit;
    cell.air_unit = plot.air_unit;
    cell.high_height = plot.high_height;
    cell.low_height = plot.low_height;
    if (const FeatureDef* feature = plot_feature_def(world, plot); feature != nullptr) {
        cell.has_feature = true;
        cell.feature_height = static_cast<uint8_t>(feature->height);
    }
    return cell;
}

ProjectileContact projectile_plot_contact(
    World& world, Projectile& shot, const ContactPlot* plot, bool water_surface_ignored
) noexcept {
    ProjectileContact contact;
    const WeaponDef* weapon = world_weapon_def(&world, shot.def);
    if (plot == nullptr || weapon == nullptr) {
        contact.kind = ContactKind::off_map;
        return contact;
    }
    if (shot.intercept_target != 0) {
        const Projectile* target = world_projectile(&world, shot.intercept_target);
        if (target != nullptr) {
            const uint32_t radius = static_cast<uint16_t>(weapon->area_of_effect);
            const int32_t reach = squared_distance_high(
                shot.position.x - target->position.x,
                shot.position.y - target->position.y,
                shot.position.z - target->position.z
            );
            if (reach < static_cast<int32_t>(radius * radius))
                contact.intercepted = true;
        }
    }
    shot.plot_height =
        static_cast<int16_t>((static_cast<uint32_t>(plot->high_height) + plot->low_height) / 2u);
    if (const oa_ref32 hit = occupant_hit(world, shot, plot->ground_unit, false); hit != 0) {
        contact.kind = ContactKind::unit;
        contact.unit = hit;
        return contact;
    }
    if (const oa_ref32 hit = occupant_hit(world, shot, plot->air_unit, true); hit != 0) {
        contact.kind = ContactKind::unit;
        contact.unit = hit;
        return contact;
    }
    if ((weapon->flags & OA_WEAPON_FLAG_UNITS_ONLY) != 0)
        return contact;
    const int16_t cell_x = feature_cell(shot.position.x);
    const int16_t cell_z = feature_cell(shot.position.z);
    const int16_t height = high_word(shot.position.y);
    if (plot->has_feature) {
        const int32_t feature_top = static_cast<int32_t>(plot->feature_height) + plot->low_height;
        if (static_cast<int32_t>(height) < feature_top &&
            (shot.feature_cell_x != cell_x || shot.feature_cell_z != cell_z)) {
            shot.feature_cell_x = cell_x;
            shot.feature_cell_z = cell_z;
            contact.kind = ContactKind::feature;
            return contact;
        }
    }
    if (height < static_cast<int16_t>(plot->low_height)) {
        if ((weapon->flags & OA_WEAPON_FLAG_GROUND_BOUNCE) != 0) {
            shot.velocity.y = -(shot.velocity.y >> 2);
            contact.kind = ContactKind::bounce;
            return contact;
        }
        contact.kind = ContactKind::ground;
        return contact;
    }
    if ((weapon->flags & OA_WEAPON_FLAG_WATER_WEAPON) != 0)
        return contact;
    if (static_cast<int16_t>(world.game.sea_level) <= height)
        return contact;
    if (water_surface_ignored)
        return contact;
    contact.kind = ContactKind::water;
    return contact;
}

ProjectileContact
projectile_map_contact(World& world, Projectile& shot, bool water_surface_ignored) noexcept {
    const MapPlot* plot = world_plot(&world, shot.position.x >> 20, shot.position.z >> 20);
    if (plot == nullptr)
        return projectile_plot_contact(world, shot, nullptr, water_surface_ignored);
    const ContactPlot cell = contact_plot(world, *plot);
    return projectile_plot_contact(world, shot, &cell, water_surface_ignored);
}

} // namespace oa::sim::weapon_execution
