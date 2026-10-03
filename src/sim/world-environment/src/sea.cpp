// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/world_environment/wind.hpp"

#include <cstring>

namespace oa::sim::world_environment {
namespace {

// Signed high word of a 16.16 value (the model top, UnitDef.model_height, or
// the unit's altitude, Unit.position.y).
int32_t high_word(oa_fixed value) noexcept {
    return static_cast<int16_t>(static_cast<uint32_t>(value) >> 16);
}

void set_sea_occupy(Unit& unit, int32_t code) noexcept {
    std::memcpy(unit.last_occupy_code, &code, sizeof(code));
}

} // namespace

int32_t sea_occupy(const Unit& unit) noexcept {
    int32_t code;
    std::memcpy(&code, unit.last_occupy_code, sizeof(code));
    return code;
}

void update_sea_occupy(
    Unit& unit, const UnitDef& def, uint8_t sea_level, const SeaOccupyHost& host
) {
    const auto layer = unit.flags & OA_UNIT_FLAG_OCCUPANCY_MASK;
    const auto height = high_word(unit.position.y);
    const auto sea = static_cast<int32_t>(sea_level);
    const auto current = sea_occupy(unit);
    auto next = current;
    if (layer == sea_occupy_ground_layer || layer == sea_occupy_air_layer) {
        if (height > sea) {
            next = sea_occupy_above;
        } else {
            const auto waterline = static_cast<int32_t>(static_cast<uint8_t>(def.water_line));
            if (host.reordered && waterline + height <= sea)
                next = sea_occupy_waterline;
            if (height - sea > sea_surface_depth_limit)
                next = sea_occupy_surface;
            if (!host.reordered && waterline + height == sea)
                next = sea_occupy_waterline;
            if (high_word(def.model_height) + height < sea)
                next = sea_occupy_submerged;
        }
    } else {
        next = sea_occupy_none;
    }
    if (next == current)
        return;
    if (host.set_sfx_occupy != nullptr)
        host.set_sfx_occupy(host.context, unit, next);
    set_sea_occupy(unit, next);
}

} // namespace oa::sim::world_environment
