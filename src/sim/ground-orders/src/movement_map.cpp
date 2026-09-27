// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/ground_orders/movement_map.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace oa::sim::ground_orders {
namespace {
constexpr uint32_t occupancy_history_ticks = 30;
constexpr uint32_t cells_per_word = 16;

int16_t half(uint32_t value) noexcept {
    return std::bit_cast<int16_t>(static_cast<uint16_t>(value));
}

std::size_t words_for(uint32_t width, uint32_t height) {
    const auto rows = (uint64_t(height) + cells_per_word - 1) / cells_per_word;
    const auto words = uint64_t(width) * rows;
    if (words > std::numeric_limits<std::size_t>::max())
        throw std::length_error("movement map dimensions exceed address space");
    return static_cast<std::size_t>(words);
}
} // namespace

MovementMap::MovementMap(
    uint32_t width,
    uint32_t height,
    int16_t footprint_x,
    int16_t footprint_z,
    MovementMapSampler& sampler
)
    : width_(width), height_(height), footprint_x_(footprint_x), footprint_z_(footprint_z),
      sampler_(&sampler), cells_(words_for(width, height)) {
}

uint8_t MovementMap::cell(uint32_t x, uint32_t z) const noexcept {
    if (x >= width_ || z >= height_)
        return 0;
    const auto word = (z / cells_per_word) * width_ + x;
    return static_cast<uint8_t>((cells_[word] >> ((z & (cells_per_word - 1)) * 2)) & 3u);
}

void MovementMap::store(uint32_t x, uint32_t z, uint8_t value) noexcept {
    const auto word = (z / cells_per_word) * width_ + x, shift = (z & (cells_per_word - 1)) * 2;
    const auto mask = uint32_t(3) << shift;
    cells_[word] = (cells_[word] & ~mask) | (uint32_t(value & 3u) << shift);
}

void MovementMap::refresh(OccupancyRectangle rectangle) {
    const auto ox = int32_t(half(rectangle.origin)), oz = int32_t(half(rectangle.origin >> 16));
    const auto ex = int32_t(half(rectangle.extent)), ez = int32_t(half(rectangle.extent >> 16));
    uint32_t begin_x = static_cast<uint32_t>(ox - footprint_x_),
             begin_z = static_cast<uint32_t>(oz - footprint_z_);
    uint32_t end_x = static_cast<uint32_t>(ox + ex + 1), end_z = static_cast<uint32_t>(oz + ez + 1);
    if (std::bit_cast<int32_t>(begin_x) < 0)
        begin_x = 0;
    if (std::bit_cast<int32_t>(begin_z) < 0)
        begin_z = 0;
    if (width_ < end_x)
        end_x = width_;
    if (height_ < end_z)
        end_z = height_;
    if (std::bit_cast<int32_t>(begin_x) < std::bit_cast<int32_t>(end_x))
        for (auto z = begin_z; std::bit_cast<int32_t>(z) < std::bit_cast<int32_t>(end_z); ++z)
            for (auto x = begin_x; std::bit_cast<int32_t>(x) < std::bit_cast<int32_t>(end_x); ++x)
                store(
                    x,
                    z,
                    sampler_->classify_cell(std::bit_cast<int32_t>(x), std::bit_cast<int32_t>(z))
                );
}

void MovementMap::prepare_search(
    OccupancyRectangle active,
    uint32_t& active_changed_tick,
    std::span<const OccupancyChange> units,
    uint32_t current_tick
) {
    const auto previous = projection_tick_;
    const auto next = std::max(current_tick, occupancy_history_ticks) - occupancy_history_ticks;
    projection_tick_ = next;
    const auto active_tick = active_changed_tick;
    active_changed_tick = current_tick;
    if (active_tick < previous)
        refresh(active);
    if (next != previous)
        for (const auto& unit : units)
            if (unit.live && unit.changed_tick >= previous && unit.changed_tick < next)
                refresh(unit.rectangle);
    active_changed_tick = active_tick;
}

void MovementMap::release_unit(OccupancyRectangle rectangle, uint32_t changed_tick) {
    if (changed_tick < projection_tick_)
        refresh(rectangle);
}

namespace {
// One padded line of the whole-map build: line[2 + i] holds cell i, line[0..1]
// and the footprint-plus-one entries past the end are 0. Output cell i - 1 is
// the minimum of cells i - 1 .. i + footprint - 2, kept as a running value that
// is only rescanned when the cell leaving the window was the minimum.
template <typename Write>
void filter_line(
    const std::vector<uint8_t>& line, uint32_t length, int32_t footprint, Write&& write
) {
    uint8_t running = 0;
    for (uint32_t i = 1; i <= length; ++i) {
        const auto end = footprint - 2 + static_cast<int32_t>(i);
        const auto entering = line[static_cast<std::size_t>(end + 2)];
        auto next = entering;
        if (running < entering) {
            next = running;
            if (line[i] <= running) {
                running = line[i + 1];
                for (auto u = static_cast<int32_t>(i); u <= end; ++u)
                    running = std::min(running, line[static_cast<std::size_t>(u + 2)]);
                next = running;
            }
        }
        running = next;
        const bool edge_not_clear = line[i] < 3 || line[static_cast<std::size_t>(end + 3)] < 3;
        write(i - 1, running == 3 && edge_not_clear ? uint8_t{1} : running);
    }
}
} // namespace

void MovementMap::rebuild() {
    const auto fx = std::max<int32_t>(footprint_x_, 0), fz = std::max<int32_t>(footprint_z_, 0);
    const auto longest = std::max(
        uint64_t{width_} + static_cast<uint64_t>(fx), uint64_t{height_} + static_cast<uint64_t>(fz)
    );
    std::vector<uint8_t> line(static_cast<std::size_t>(longest + 3));
    for (uint32_t z = 0; z < height_; ++z) {
        for (uint32_t x = 0; x < width_; ++x)
            line[2 + x] = sampler_->classify_plot(static_cast<int32_t>(x), static_cast<int32_t>(z));
        line[0] = line[1] = 0;
        for (int32_t i = 0; i <= fx; ++i)
            line[2 + width_ + static_cast<uint32_t>(i)] = 0;
        filter_line(line, width_, fx, [&](uint32_t x, uint8_t value) { store(x, z, value); });
    }
    for (uint32_t x = 0; x < width_; ++x) {
        for (uint32_t z = 0; z < height_; ++z)
            line[2 + z] = cell(x, z);
        line[0] = line[1] = 0;
        for (int32_t i = 0; i <= fz; ++i)
            line[2 + height_ + static_cast<uint32_t>(i)] = 0;
        filter_line(line, height_, fz, [&](uint32_t z, uint8_t value) { store(x, z, value); });
    }
}

OccupancyRectangle occupancy_rectangle(const oa::Unit& unit) noexcept {
    const auto pack = [](int32_t x, int32_t z) {
        return uint32_t{static_cast<uint16_t>(x)} | uint32_t{static_cast<uint16_t>(z)} << 16;
    };
    return {pack(unit.cell_x, unit.cell_z), pack(unit.footprint_x, unit.footprint_z)};
}

} // namespace oa::sim::ground_orders
