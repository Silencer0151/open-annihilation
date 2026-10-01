// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/spatial_state/spatial.hpp"

#include <functional>
#include <algorithm>
#include <cstdint>
#include <limits>

namespace oa::sim::spatial_state {
namespace {
Bucket* bucket(World& world, std::optional<std::size_t> index) {
    return index ? &world.buckets[*index] : &world.outside_bucket;
}

bool valid(const World& w) {
    const auto plots = static_cast<uint64_t>(w.terrain_width) * w.terrain_height;
    const auto buckets = static_cast<uint64_t>(w.bucket_width) * w.bucket_height;
    return plots == w.plots.size() && buckets == w.buckets.size() && !w.units.empty();
}

int32_t signed_word(uint32_t bits) {
    return static_cast<int32_t>(bits);
}

uint8_t yard_bit(bool open) {
    return open ? 2U : 4U;
}

int16_t add16(int16_t left, int16_t right) {
    return static_cast<int16_t>(static_cast<uint16_t>(left) + static_cast<uint16_t>(right));
}

bool valid_mask(const Unit& unit) {
    if (unit.footprint[0] < 0 || unit.footprint[1] < 0)
        return false;
    return unit.yard_mask.size() == static_cast<std::size_t>(unit.footprint[0]) *
                                        static_cast<std::size_t>(unit.footprint[1]);
}

/// Lists a plot of the world's plots in World::written_occupants, or marks
/// the list as lost when it is full; a plot outside them is not listed.
///
/// @param[in,out] world the world whose plots hold the plot
/// @param plot the plot whose ground or air word is written
void note_written_occupant(World& world, const Plot& plot) noexcept {
    const Plot* first = world.plots.data();
    const Plot* end = first + world.plots.size();
    if (std::less<const Plot*>{}(&plot, first) || !std::less<const Plot*>{}(&plot, end))
        return;
    if (world.written_occupant_count >= world.written_occupants.size()) {
        world.written_occupants_lost = true;
        return;
    }
    world.written_occupants[world.written_occupant_count++] = static_cast<uint32_t>(&plot - first);
}

/// Runs update_occupancy on a unit visit_overlapping_units reached.
///
/// @param[in,out] unit visited unit
/// @param[in,out] world world holding the plots
/// @param context unused walk context
/// @return the update_occupancy result
Error wake_occupancy(Unit& unit, World& world, void* /*context*/) {
    return update_occupancy(unit, world);
}
} // namespace

Error occupy(Plot& plot, bool air_layer, Unit& unit, World& world) {
    auto& slot = air_layer ? plot.air : plot.ground;
    note_written_occupant(world, plot);
    if (slot != no_unit) {
        if (slot >= world.units.size())
            return Error::invalid_unit_id;
        auto& previous = world.units[slot];
        if (!previous.owner_object_present || previous.owner_status != 3) {
            previous.flags |= collision_other;
            unit.flags |= collision_self;
            return Error::none;
        } else {
            previous.flags |= collision_self;
            unit.flags |= collision_other;
        }
    }
    slot = unit.id;
    return Error::none;
}

Error move_bucket(Unit& unit, std::optional<std::size_t> target, World& world) {
    if (target && *target >= world.buckets.size())
        return Error::spatial_bucket_out_of_range;
    if (unit.bucket_linked && unit.bucket == target)
        return Error::none;
    if (!unit.spatial_link_locked) {
        if (unit.bucket_linked && unit.bucket) {
            if (*unit.bucket >= world.buckets.size())
                return Error::spatial_bucket_out_of_range;
        }
        if (unit.bucket_linked) {
            auto* link = &bucket(world, unit.bucket)->head;
            std::size_t steps = 0;
            while (*link != unit.id) {
                if (*link == no_unit || *link >= world.units.size() || ++steps > world.units.size())
                    return Error::broken_bucket_chain;
                link = &world.units[*link].next_in_bucket;
            }
            *link = unit.next_in_bucket;
            unit.next_in_bucket = no_unit;
        }
        auto* destination = bucket(world, target);
        unit.next_in_bucket = destination->head;
        destination->head = unit.id;
    }
    unit.bucket = target;
    unit.bucket_linked = true;
    return Error::none;
}

Error bucket_unlink(Bucket& chain, Unit& unit, World& world) {
    auto* link = &chain.head;
    std::size_t steps = 0;
    while (*link != unit.id) {
        if (*link == no_unit || *link >= world.units.size() || ++steps > world.units.size())
            return Error::broken_bucket_chain;
        link = &world.units[*link].next_in_bucket;
    }
    *link = unit.next_in_bucket;
    unit.next_in_bucket = no_unit;
    return Error::none;
}

void bucket_push_front(Bucket& chain, Unit& unit) noexcept {
    unit.next_in_bucket = chain.head;
    chain.head = unit.id;
}

Error register_unit(Unit& unit, World& world, Host& host) {
    if (!valid(world))
        return Error::malformed_world;
    if (unit.id == no_unit || unit.id >= world.units.size() || &world.units[unit.id] != &unit)
        return Error::invalid_unit_id;
    if (unit.object_present)
        unit.object_tick = world.tick;
    const auto x = static_cast<int32_t>(unit.cell[0]);
    const auto z = static_cast<int32_t>(unit.cell[1]);
    const auto fw = static_cast<int32_t>(unit.footprint[0]);
    const auto fh = static_cast<int32_t>(unit.footprint[1]);
    const bool outside = x < 0 || z < 0 || static_cast<int64_t>(x) + fw >= world.terrain_width ||
                         static_cast<int64_t>(z) + fh >= world.terrain_height;
    std::optional<std::size_t> target;
    if (!outside) {
        const auto bx = signed_word(unit.position[0]) >> 23;
        const auto bz = signed_word(unit.position[2]) >> 23;
        if (bx < 0 || bz < 0 || static_cast<uint32_t>(bx) >= world.bucket_width ||
            static_cast<uint32_t>(bz) >= world.bucket_height)
            return Error::spatial_bucket_out_of_range;
        target = static_cast<std::size_t>(bz) * world.bucket_width + static_cast<std::size_t>(bx);
    }
    if (const auto error = move_bucket(unit, target, world); error != Error::none)
        return error;
    if (outside)
        return Error::none;
    if ((unit.flags & masked_footprint_flag) != 0) {
        if (!valid_mask(unit))
            return Error::invalid_yard_mask;
        std::size_t mask_index = 0;
        for (int32_t row = 0; row < fh; ++row)
            for (int32_t column = 0; column < fw; ++column) {
                auto& plot = world.plots
                                 [static_cast<std::size_t>(z + row) * world.terrain_width +
                                  static_cast<std::size_t>(x + column)];
                const auto mask = unit.yard_mask[mask_index++];
                if ((mask & yard_bit(unit.yard_open)) != 0) {
                    if (const auto error = occupy(plot, false, unit, world); error != Error::none)
                        return error;
                }
                if ((mask & 1U) != 0)
                    plot.flags |= plot_claimed;
            }
        host.refresh_plot_height_range(
            {static_cast<int16_t>(x - 1), static_cast<int16_t>(z - 1)},
            {static_cast<int16_t>(fw + 2), static_cast<int16_t>(fh + 2)}
        );
        host.notify_footprint_changed(unit.cell, unit.footprint);
        return Error::none;
    }
    const auto kind = unit.flags & terrain_occupancy_mask;
    if (kind != 1 && kind != 2)
        return Error::none;
    for (int32_t row = 0; row < fh; ++row)
        for (int32_t column = 0; column < fw; ++column) {
            auto& plot = world.plots
                             [static_cast<std::size_t>(z + row) * world.terrain_width +
                              static_cast<std::size_t>(x + column)];
            if (const auto error = occupy(plot, kind == 2, unit, world); error != Error::none)
                return error;
        }
    return Error::none;
}

bool can_change_yard(const Unit& unit, int32_t value, const World& world) {
    if (!valid(world) || !valid_mask(unit) || unit.cell[0] <= 0 || unit.cell[1] <= 0)
        return false;
    const auto end_x = static_cast<int32_t>(add16(unit.cell[0], unit.footprint[0]));
    const auto end_z = static_cast<int32_t>(add16(unit.cell[1], unit.footprint[1]));
    if (end_x >= static_cast<int32_t>(world.terrain_width) ||
        end_z >= static_cast<int32_t>(world.terrain_height))
        return false;
    std::size_t mask_index = 0;
    for (int32_t z = unit.cell[1]; z < end_z; ++z)
        for (int32_t x = unit.cell[0]; x < end_x; ++x) {
            const auto mask = unit.yard_mask[mask_index++];
            const auto occupant = world
                                      .plots
                                          [static_cast<std::size_t>(z) * world.terrain_width +
                                           static_cast<std::size_t>(x)]
                                      .ground;
            if ((mask & yard_bit(value != 0)) != 0 && occupant != no_unit && occupant != unit.id)
                return false;
        }
    return true;
}

Error update_occupancy(Unit& unit, World& world) {
    if (!valid(world))
        return Error::malformed_world;
    if (unit.id == no_unit || unit.id >= world.units.size() || &world.units[unit.id] != &unit)
        return Error::invalid_unit_id;
    if ((unit.flags & collision_self) == 0)
        return Error::none;
    unit.flags &= ~collision_self;
    const auto x = static_cast<int32_t>(unit.cell[0]);
    const auto z = static_cast<int32_t>(unit.cell[1]);
    const auto fw = static_cast<int32_t>(unit.footprint[0]);
    const auto fh = static_cast<int32_t>(unit.footprint[1]);
    if (x < 0 || z < 0 || x + fw > static_cast<int32_t>(world.terrain_width) ||
        z + fh > static_cast<int32_t>(world.terrain_height))
        return Error::malformed_world;
    if ((unit.flags & masked_footprint_flag) != 0) {
        if (!valid_mask(unit))
            return Error::invalid_yard_mask;
        std::size_t mask_index = 0;
        for (int32_t row = 0; row < fh; ++row)
            for (int32_t column = 0; column < fw; ++column) {
                auto& plot = world.plots
                                 [static_cast<std::size_t>(z + row) * world.terrain_width +
                                  static_cast<std::size_t>(x + column)];
                if ((unit.yard_mask[mask_index++] & yard_bit(unit.yard_open)) == 0) {
                    if (plot.ground == unit.id) {
                        plot.ground = no_unit;
                        note_written_occupant(world, plot);
                    }
                } else if (
                    const auto error = occupy(plot, false, unit, world); error != Error::none
                )
                    return error;
            }
        return Error::none;
    }
    const bool ground = (unit.flags & terrain_occupancy_mask) == 1;
    for (int32_t row = 0; row < fh; ++row)
        for (int32_t column = 0; column < fw; ++column) {
            auto& plot = world.plots
                             [static_cast<std::size_t>(z + row) * world.terrain_width +
                              static_cast<std::size_t>(x + column)];
            if (const auto error = occupy(plot, !ground, unit, world); error != Error::none)
                return error;
        }
    return Error::none;
}

bool change_yard(Unit& unit, int32_t value, World& world, Host& host) {
    if (!can_change_yard(unit, value, world))
        return false;
    unit.flags |= collision_self;
    unit.yard_open = (value & 1) != 0;
    if (update_occupancy(unit, world) != Error::none)
        return false;
    host.notify_footprint_changed(unit.cell, unit.footprint);
    return true;
}

bool can_unload_at(
    const Unit& unit,
    int32_t world_x,
    int32_t world_z,
    bool player_sees_cell,
    bool flies_not_amphibious,
    const World& world
) {
    if (!valid(world))
        return false;
    const auto footprint_x = static_cast<int32_t>(unit.footprint[0]);
    const auto footprint_z = static_cast<int32_t>(unit.footprint[1]);
    const auto cell_x = static_cast<int32_t>(
        static_cast<uint32_t>(world_x + footprint_x * -0x80000 + 0x80000) >> 20
    );
    const auto cell_z = static_cast<int32_t>(
        static_cast<uint32_t>(world_z + footprint_z * -0x80000 + 0x80000) >> 20
    );
    if (cell_x < 0 || cell_z < 0)
        return false;
    const auto width = static_cast<int32_t>(world.terrain_width);
    const auto height = static_cast<int32_t>(world.terrain_height);
    if (cell_x + footprint_x >= width || cell_z + footprint_z >= height)
        return false;
    if (!player_sees_cell)
        return true;
    auto floor = static_cast<int32_t>(world.sea_level) - static_cast<int32_t>(unit.max_water_depth);
    if (flies_not_amphibious && floor < static_cast<int32_t>(world.sea_level))
        floor = static_cast<int32_t>(world.sea_level);
    const auto ceiling =
        static_cast<int32_t>(world.sea_level) - static_cast<int32_t>(unit.min_water_depth);
    for (int32_t row = 0; row < footprint_z; ++row)
        for (int32_t column = 0; column < footprint_x; ++column) {
            const auto& plot = world.plots
                                   [static_cast<std::size_t>(cell_z + row) * world.terrain_width +
                                    static_cast<std::size_t>(cell_x + column)];
            if (plot.blocking_feature || (plot.flags & plot_claimed) != 0)
                return false;
            if (plot.ground != no_unit && plot.ground != unit.id)
                return false;
            if (plot.air != no_unit && plot.air != unit.id)
                return false;
            if (static_cast<int32_t>(plot.low_height) < floor)
                return false;
            if (ceiling < static_cast<int32_t>(plot.high_height))
                return false;
            if (static_cast<int32_t>(unit.max_slope) <
                static_cast<int32_t>(plot.high_height) - static_cast<int32_t>(plot.low_height))
                return false;
        }
    return true;
}

std::optional<bool> can_occupy(
    const Unit& unit, UnitId ignored, std::array<int16_t, 2> cell, int32_t mode, const World& world
) {
    if (!valid(world))
        return false;
    const auto x = static_cast<int32_t>(cell[0]), z = static_cast<int32_t>(cell[1]);
    const auto fw = static_cast<int32_t>(unit.footprint[0]),
               fh = static_cast<int32_t>(unit.footprint[1]);
    if (x < 0 || z < 0 || x + fw >= static_cast<int32_t>(world.terrain_width) ||
        z + fh >= static_cast<int32_t>(world.terrain_height))
        return mode == 2;
    if (unit.bm_code == 0)
        return std::nullopt;
    if (mode != 1)
        return true;
    for (int32_t row = 0; row < fh; ++row)
        for (int32_t column = 0; column < fw; ++column) {
            const auto& plot = world.plots
                                   [static_cast<std::size_t>(z + row) * world.terrain_width +
                                    static_cast<std::size_t>(x + column)];
            if (plot.blocking_feature || (plot.ground != no_unit && plot.ground != ignored))
                return false;
            if (static_cast<int32_t>(plot.low_height) <
                static_cast<int32_t>(world.sea_level) - unit.max_water_depth)
                return false;
            if (static_cast<int32_t>(world.sea_level) - unit.min_water_depth <
                static_cast<int32_t>(plot.high_height))
                return false;
            const auto delta = static_cast<int32_t>(plot.high_height) - plot.low_height;
            if (static_cast<int32_t>(unit.max_slope) < delta) {
                if (world.sea_level <= plot.low_height ||
                    static_cast<int32_t>(unit.max_water_slope) < delta)
                    return false;
            }
        }
    return true;
}

bool rectangles_overlap(
    std::array<int16_t, 2> cell,
    std::array<int16_t, 2> size,
    std::array<int16_t, 2> other_cell,
    std::array<int16_t, 2> other_size
) noexcept {
    return cell[0] < other_size[0] + other_cell[0] && other_cell[0] < size[0] + cell[0] &&
           cell[1] < other_size[1] + other_cell[1] && other_cell[1] < size[1] + cell[1];
}

Error visit_overlapping_units(
    std::array<int16_t, 2> cell,
    std::array<int16_t, 2> footprint,
    World& world,
    Error (*visit)(Unit&, World&, void* context),
    void* context
) {
    constexpr int32_t bucket_shift = 3;
    const auto first_x = (static_cast<int32_t>(cell[0]) >> bucket_shift) - 1;
    const auto last_x = ((static_cast<int32_t>(footprint[0]) + cell[0]) >> bucket_shift) + 1;
    const auto first_z = (static_cast<int32_t>(cell[1]) >> bucket_shift) - 1;
    const auto last_z = ((static_cast<int32_t>(footprint[1]) + cell[1]) >> bucket_shift) + 1;
    const auto overlapping = [&](const Unit& other) {
        return rectangles_overlap(cell, footprint, other.cell, other.footprint);
    };
    for (auto bx = first_x; bx <= last_x; ++bx)
        for (auto bz = first_z; bz <= last_z; ++bz) {
            if (static_cast<uint32_t>(bx) >= world.bucket_width ||
                static_cast<uint32_t>(bz) >= world.bucket_height)
                continue;
            auto id = world
                          .buckets
                              [static_cast<std::size_t>(bz) * world.bucket_width +
                               static_cast<std::size_t>(bx)]
                          .head;
            std::size_t steps = 0;
            while (id != no_unit) {
                if (id >= world.units.size() || ++steps > world.units.size())
                    return Error::broken_bucket_chain;
                auto& parent = world.units[id];
                if (overlapping(parent))
                    if (const auto error = visit(parent, world, context); error != Error::none)
                        return error;
                auto child = parent.first_attachment;
                std::size_t child_steps = 0;
                while (child != no_unit) {
                    if (child >= world.units.size() || ++child_steps > world.units.size())
                        return Error::broken_bucket_chain;
                    auto& attached = world.units[child];
                    if (overlapping(attached))
                        if (const auto error = visit(attached, world, context);
                            error != Error::none)
                            return error;
                    child = attached.next_in_bucket;
                }
                id = parent.next_in_bucket;
            }
        }
    return Error::none;
}

Error refresh_footprint_occupancy(Unit& unit, World& world, Host& host) {
    if (!valid(world))
        return Error::malformed_world;
    if (unit.id == no_unit || unit.id >= world.units.size() || &world.units[unit.id] != &unit)
        return Error::invalid_unit_id;
    unit.flags |= collision_self;
    if (const auto error =
            visit_overlapping_units(unit.cell, unit.footprint, world, wake_occupancy, nullptr);
        error != Error::none)
        return error;
    host.notify_footprint_changed(unit.cell, unit.footprint);
    return Error::none;
}

Error remove_occupancy(Unit& unit, World& world, Host& host) {
    if (!valid(world))
        return Error::malformed_world;
    if (unit.id == no_unit || unit.id >= world.units.size() || &world.units[unit.id] != &unit)
        return Error::invalid_unit_id;
    const auto x = static_cast<int32_t>(unit.cell[0]), z = static_cast<int32_t>(unit.cell[1]);
    const auto fw = static_cast<int32_t>(unit.footprint[0]),
               fh = static_cast<int32_t>(unit.footprint[1]);
    // The plots are left alone only for a unit filed in the off-map bucket.
    if (!(unit.bucket_linked && !unit.bucket)) {
        if (x < 0 || z < 0 || x + fw > static_cast<int32_t>(world.terrain_width) ||
            z + fh > static_cast<int32_t>(world.terrain_height))
            return Error::malformed_world;
        if ((unit.flags & masked_footprint_flag) != 0) {
            if (!valid_mask(unit))
                return Error::invalid_yard_mask;
            std::size_t mask = 0;
            for (int32_t row = 0; row < fh; ++row)
                for (int32_t column = 0; column < fw; ++column) {
                    auto& plot = world.plots
                                     [static_cast<std::size_t>(z + row) * world.terrain_width +
                                      static_cast<std::size_t>(x + column)];
                    if (plot.ground == unit.id) {
                        plot.ground = no_unit;
                        note_written_occupant(world, plot);
                    }
                    if ((unit.yard_mask[mask++] & 1U) != 0)
                        plot.flags &= static_cast<uint8_t>(~plot_claimed);
                }
            host.refresh_plot_height_range(
                {static_cast<int16_t>(x - 1), static_cast<int16_t>(z - 1)},
                {static_cast<int16_t>(fw + 2), static_cast<int16_t>(fh + 2)}
            );
        } else {
            const auto kind = unit.flags & terrain_occupancy_mask;
            if (kind == 1 || kind == 2)
                for (int32_t row = 0; row < fh; ++row)
                    for (int32_t column = 0; column < fw; ++column) {
                        auto& plot = world.plots
                                         [static_cast<std::size_t>(z + row) * world.terrain_width +
                                          static_cast<std::size_t>(x + column)];
                        auto& slot = kind == 1 ? plot.ground : plot.air;
                        if (slot == unit.id) {
                            slot = no_unit;
                            note_written_occupant(world, plot);
                        }
                    }
        }
    }
    unit.flags &= ~collision_self;
    if ((unit.flags & collision_other) != 0) {
        unit.flags &= ~collision_other;
        if (const auto error =
                visit_overlapping_units(unit.cell, unit.footprint, world, wake_occupancy, nullptr);
            error != Error::none)
            return error;
    }
    if (unit.object_present) {
        const auto old_tick = unit.object_tick;
        unit.object_tick = world.tick;
        host.notify_object_footprint_removed(unit, old_tick);
    } else
        host.notify_footprint_changed(unit.cell, unit.footprint);
    return Error::none;
}

Error remove_unit(Unit& unit, World& world, Host& host) {
    if (const auto error = remove_occupancy(unit, world, host); error != Error::none)
        return error;
    if (!unit.spatial_link_locked && unit.bucket_linked) {
        if (unit.bucket && *unit.bucket >= world.buckets.size())
            return Error::spatial_bucket_out_of_range;
        auto* link = &bucket(world, unit.bucket)->head;
        std::size_t steps = 0;
        while (*link != unit.id) {
            if (*link == no_unit || *link >= world.units.size() || ++steps > world.units.size())
                return Error::broken_bucket_chain;
            link = &world.units[*link].next_in_bucket;
        }
        *link = unit.next_in_bucket;
        unit.next_in_bucket = no_unit;
    }
    unit.bucket_linked = false;
    return Error::none;
}

uint8_t footprint_build_height(
    int16_t footprint_x,
    int16_t footprint_z,
    std::span<const uint8_t> yard_mask,
    int8_t waterline,
    int32_t cell_x,
    int32_t cell_z,
    const World& world
) {
    constexpr uint8_t level_cell = 0x08;
    if (cell_x <= 0 || cell_z <= 0 ||
        cell_x + footprint_x >= static_cast<int32_t>(world.terrain_width) ||
        cell_z + footprint_z >= static_cast<int32_t>(world.terrain_height) ||
        world.plots.size() != static_cast<std::size_t>(world.terrain_width) * world.terrain_height)
        return 0;
    uint8_t lowest = 0xff, highest = 0;
    std::size_t mask_index = 0;
    for (int32_t row = 0; row < footprint_z; ++row)
        for (int32_t column = 0; column < footprint_x; ++column, ++mask_index) {
            if (mask_index >= yard_mask.size() || (yard_mask[mask_index] & level_cell) == 0)
                continue;
            const auto& plot = world.plots
                                   [static_cast<std::size_t>(cell_z + row) * world.terrain_width +
                                    static_cast<std::size_t>(cell_x + column)];
            lowest = std::min(lowest, plot.low_height);
            highest = std::max(highest, plot.high_height);
        }
    if (lowest <= highest)
        return lowest;
    return static_cast<uint8_t>(world.sea_level - static_cast<uint8_t>(waterline));
}

namespace {
constexpr uint32_t plots_per_bucket_shift = 3;
constexpr uint32_t world_units_per_plot = 16;
constexpr int32_t fixed_to_bucket_shift = 23; // 16.16 world units to 128-unit buckets

// Buckets along a map side of `plots` plots: its 16.16 extent rounded up.
uint32_t bucket_span(uint32_t plots) {
    const auto extent = (plots * world_units_per_plot) << 16;
    return static_cast<uint32_t>(static_cast<int32_t>(extent + 0x7fffffu) >> fixed_to_bucket_shift);
}

// Walks `count` buckets `stride` apart: each takes the highest value of itself
// and its two neighbours on the line, the last one that of the last pair. The
// values are high_height, or area_high_height when `from_area` is set.
void spread_highest(Bucket* first, uint32_t count, std::size_t stride, bool from_area) {
    const auto value = [from_area](const Bucket& b) {
        return from_area ? b.area_high_height : b.high_height;
    };
    uint8_t pair = 0;
    uint8_t previous = 0;
    Bucket* at = first;
    for (uint32_t i = 1; i < count; ++i) {
        Bucket* next = at + stride;
        pair = std::max(value(*at), value(*next));
        at->area_high_height = std::max(previous, pair);
        previous = pair;
        at = next;
    }
    at->area_high_height = pair;
}
} // namespace

const Bucket& unit_bucket(const World& world, const Unit& unit) noexcept {
    if (unit.bucket_linked)
        return unit.bucket && *unit.bucket < world.buckets.size() ? world.buckets[*unit.bucket]
                                                                  : world.outside_bucket;
    const auto bx = signed_word(unit.position[0]) >> fixed_to_bucket_shift;
    const auto bz = signed_word(unit.position[2]) >> fixed_to_bucket_shift;
    if (bx < 0 || bz < 0 || static_cast<uint32_t>(bx) >= world.bucket_width ||
        static_cast<uint32_t>(bz) >= world.bucket_height)
        return world.outside_bucket;
    return world
        .buckets[static_cast<std::size_t>(bz) * world.bucket_width + static_cast<std::size_t>(bx)];
}

void build_buckets(World& world) {
    world.outside_bucket = Bucket{};
    world.outside_bucket.edges = off_map_bucket_edges;
    const auto width = bucket_span(world.terrain_width);
    const auto height = bucket_span(world.terrain_height);
    world.bucket_width = width;
    world.bucket_height = height;
    world.buckets.assign(static_cast<std::size_t>(width) * height, Bucket{});
    if (width == 0 || height == 0)
        return;
    const auto at = [&](uint32_t x, uint32_t z) -> Bucket& {
        return world.buckets[static_cast<std::size_t>(z) * width + x];
    };
    for (uint32_t x = 0; x < width; ++x) {
        at(x, 0).edges |= bucket_edge_north;
        at(x, height - 1).edges |= bucket_edge_south;
    }
    for (uint32_t z = 0; z < height; ++z) {
        at(0, z).edges |= bucket_edge_west;
        at(width - 1, z).edges |= bucket_edge_east;
    }
    for (auto& b : world.buckets)
        b.high_height = world.sea_level;
    if (world.plots.size() == static_cast<std::size_t>(world.terrain_width) * world.terrain_height)
        for (uint32_t z = 0; z < world.terrain_height; ++z)
            for (uint32_t x = 0; x < world.terrain_width; ++x) {
                auto& b = at(x >> plots_per_bucket_shift, z >> plots_per_bucket_shift);
                b.high_height = std::max(
                    b.high_height,
                    world.plots[static_cast<std::size_t>(z) * world.terrain_width + x].high_height
                );
            }
    for (uint32_t z = 0; z < height; ++z)
        spread_highest(&at(0, z), width, 1, false);
    for (uint32_t x = 0; x < width; ++x)
        spread_highest(&at(x, 0), height, width, true);
}

} // namespace oa::sim::spatial_state
