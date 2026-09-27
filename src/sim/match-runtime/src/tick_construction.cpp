// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "tick_internal.hpp"

#include <cstdint>

namespace oa::sim::match_runtime {

namespace {
// Yard map cell bits the building-site test reads (the FBI yardmap letters
// combine them).
constexpr uint8_t yard_claims_plot = 0x01; // placed, marks plot_claimed
constexpr uint8_t yard_refuses_units = 0x06;
constexpr uint8_t yard_level = 0x08;     // bounds slope and water depth
constexpr uint8_t yard_submerged = 0x10; // plot stays below the building
constexpr uint8_t yard_refuses_features = 0x20;
constexpr uint8_t yard_refuses_indestructible = 0x40;
constexpr uint8_t yard_geothermal = 0x80;
// Stands in, as an 'o' cell, where a yard map is shorter than the footprint.
constexpr uint8_t yard_default_cell = 0x2f;
} // namespace

std::optional<uint8_t> Match::building_site(
    uint16_t type, int32_t cell_x, int32_t cell_z, uint16_t skip_unit, uint8_t placing_player
) const {
    if (type == 0 || type >= world_.types.size())
        return std::nullopt;
    const auto fx = static_cast<int32_t>(world_.types[type].footprint_x);
    const auto fz = static_cast<int32_t>(world_.types[type].footprint_z);
    const auto width = static_cast<int32_t>(spatial_.terrain_width);
    const auto height = static_cast<int32_t>(spatial_.terrain_height);
    // Packed cell_x < 1, cell_z < 1, and a one-cell far border.
    if (cell_x < 1 || cell_z < 1 || width <= cell_x + fx || height <= cell_z + fz)
        return std::nullopt;
    bool in_sight = true;
    if (placing_player != no_placing_player) {
        const auto corner = static_cast<std::size_t>(cell_z) * input_.map.attribute_width +
                            static_cast<std::size_t>(cell_x);
        const uint32_t corner_height =
            corner < input_.map.attributes.size() ? input_.map.attributes[corner].height : 0u;
        const std::array<uint32_t, 3> centre{
            static_cast<uint32_t>((cell_x * 2 + fx) * 0x80000),
            corner_height << 16,
            static_cast<uint32_t>((cell_z * 2 + fz) * 0x80000)
        };
        if (!point_mapped(centre))
            return std::nullopt;
        in_sight = point_visible(placing_player, centre);
    }
    std::span<const uint8_t> yard;
    uint8_t max_slope = 255;
    int16_t max_water_depth = 10000, min_water_depth = -10000;
    int8_t waterline = 0;
    if (type < input_.fields.size()) {
        yard = input_.fields[type].yard_mask;
        if (const auto* meta = input_.fields[type].runtime_metadata) {
            max_slope = meta->max_slope;
            max_water_depth = meta->max_water_depth;
            min_water_depth = meta->min_water_depth;
        }
        if (const auto* definition = input_.fields[type].definition)
            waterline = definition->waterline;
    }
    uint8_t min_low = 0xff, max_high = 0, submerged_high = 0;
    bool needs_geothermal = false, found_geothermal = false;
    for (int32_t row = 0; row < fz; ++row) {
        for (int32_t column = 0; column < fx; ++column) {
            const auto& plot =
                spatial_.plots
                    [static_cast<std::size_t>(cell_z + row) * spatial_.terrain_width +
                     static_cast<std::size_t>(cell_x + column)];
            const auto cell = static_cast<std::size_t>(row) * static_cast<std::size_t>(fx) +
                              static_cast<std::size_t>(column);
            const uint8_t mask = cell < yard.size() ? yard[cell] : yard_default_cell;
            if ((mask & yard_level) != 0) {
                min_low = std::min(min_low, plot.low_height);
                max_high = std::max(max_high, plot.high_height);
            }
            if ((mask & yard_submerged) != 0)
                submerged_high = std::max(submerged_high, plot.high_height);
            if ((mask & yard_claims_plot) != 0 &&
                (plot.flags & sim::spatial_state::plot_claimed) != 0 && in_sight)
                return std::nullopt;
            if ((mask & yard_refuses_units) != 0 && plot.ground != sim::spatial_state::no_unit &&
                plot.ground != skip_unit && in_sight)
                return std::nullopt;
            if ((mask & yard_refuses_features) != 0 && plot.blocking_feature)
                return std::nullopt;
            if ((mask & yard_refuses_indestructible) != 0 && plot.indestructible_feature)
                return std::nullopt;
            if ((mask & yard_geothermal) != 0) {
                needs_geothermal = true;
                if (plot.geo_feature)
                    found_geothermal = true;
            }
        }
    }
    if (needs_geothermal && !found_geothermal)
        return std::nullopt;
    const auto sea = static_cast<int32_t>(spatial_.sea_level);
    uint8_t base = min_low;
    if (min_low <= max_high) {
        if (static_cast<int32_t>(max_slope) < max_high - min_low)
            return std::nullopt;
    } else {
        max_high = 0;
        base = static_cast<uint8_t>(sea - waterline);
    }
    if (base < submerged_high)
        return std::nullopt;
    if (static_cast<int32_t>(min_low) < sea - static_cast<int32_t>(max_water_depth))
        return std::nullopt;
    max_high = std::max(max_high, submerged_high);
    if (sea - static_cast<int32_t>(min_water_depth) < static_cast<int32_t>(max_high))
        return std::nullopt;
    return base;
}

uint8_t Match::footprint_height(uint16_t type, int32_t cell_x, int32_t cell_z) const {
    if (type == 0 || type >= world_.types.size())
        return 0;
    std::span<const uint8_t> yard;
    int8_t waterline = 0;
    if (type < input_.fields.size()) {
        yard = input_.fields[type].yard_mask;
        if (const auto* definition = input_.fields[type].definition)
            waterline = definition->waterline;
    }
    return sim::spatial_state::footprint_build_height(
        world_.types[type].footprint_x,
        world_.types[type].footprint_z,
        yard,
        waterline,
        cell_x,
        cell_z,
        spatial_
    );
}

uint16_t Match::feature_word_under(
    const sim::ground_orders::Point& point, int16_t* origin_x, int16_t* origin_z
) const {
    const auto& plots = spatial_.plots;
    const auto plot_index = [&](int32_t cell_x, int32_t cell_z) -> std::optional<std::size_t> {
        if (cell_x < 0 || cell_z < 0 || static_cast<uint32_t>(cell_x) >= spatial_.terrain_width ||
            static_cast<uint32_t>(cell_z) >= spatial_.terrain_height)
            return std::nullopt;
        const auto index = static_cast<std::size_t>(cell_z) * spatial_.terrain_width +
                           static_cast<std::size_t>(cell_x);
        if (index >= plots.size())
            return std::nullopt;
        return index;
    };
    auto cell_x = static_cast<int16_t>(point[0] >> 20);
    auto cell_z = static_cast<int16_t>(point[2] >> 20);
    auto index = plot_index(cell_x, cell_z);
    if (!index)
        return sim::spatial_state::no_feature;
    if (plots[*index].feature_word == sim::spatial_state::feature_continuation) {
        cell_x = static_cast<int16_t>(cell_x - plots[*index].feature_back_x);
        cell_z = static_cast<int16_t>(cell_z - plots[*index].feature_back_z);
        index = plot_index(cell_x, cell_z);
        if (!index)
            return sim::spatial_state::no_feature;
    }
    const auto word = plots[*index].feature_word;
    if (word >= sim::spatial_state::first_reserved_feature)
        return sim::spatial_state::no_feature;
    if (origin_x != nullptr)
        *origin_x = cell_x;
    if (origin_z != nullptr)
        *origin_z = cell_z;
    return word;
}

bool Match::site_clear_for(
    uint16_t type, int32_t cell_x, int32_t cell_z, uint16_t skip_unit, uint8_t occupancy_kind
) const {
    const auto oob = occupancy_kind == 2;
    if (type == 0 || type >= world_.types.size())
        return oob;
    const auto fx = static_cast<int32_t>(world_.types[type].footprint_x);
    const auto fz = static_cast<int32_t>(world_.types[type].footprint_z);
    const auto width = static_cast<int32_t>(spatial_.terrain_width);
    const auto height = static_cast<int32_t>(spatial_.terrain_height);
    // Packed cell_x/cell_z >= 0 and footprint inside the far border.
    if (cell_x < 0 || cell_z < 0 || width <= cell_x + fx || height <= cell_z + fz)
        return oob;
    if (world_.types[type].bm_code == 0)
        return building_site_clear(type, cell_x, cell_z, 0);
    if (occupancy_kind != 1)
        return true;
    uint8_t max_slope = 255, max_water_slope = 255;
    int16_t max_water_depth = 10000, min_water_depth = -10000;
    if (type < input_.fields.size()) {
        if (const auto* meta = input_.fields[type].runtime_metadata) {
            max_slope = meta->max_slope;
            max_water_slope = meta->max_water_slope;
            max_water_depth = meta->max_water_depth;
            min_water_depth = meta->min_water_depth;
        }
    }
    const auto sea = static_cast<int32_t>(spatial_.sea_level);
    for (int32_t row = 0; row < fz; ++row) {
        for (int32_t column = 0; column < fx; ++column) {
            const auto& plot =
                spatial_.plots
                    [static_cast<std::size_t>(cell_z + row) * spatial_.terrain_width +
                     static_cast<std::size_t>(cell_x + column)];
            if (plot.blocking_feature)
                return false;
            if (plot.ground != sim::spatial_state::no_unit && plot.ground != skip_unit)
                return false;
            const auto low = static_cast<int32_t>(plot.low_height);
            const auto high = static_cast<int32_t>(plot.high_height);
            if (low < sea - static_cast<int32_t>(max_water_depth))
                return false;
            if (sea - static_cast<int32_t>(min_water_depth) < high)
                return false;
            const auto slope = high - low;
            if (static_cast<int32_t>(max_slope) < slope) {
                if (sea <= low)
                    return false;
                if (static_cast<int32_t>(max_water_slope) < slope)
                    return false;
            }
        }
    }
    return true;
}

} // namespace oa::sim::match_runtime
