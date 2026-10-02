// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_movement/terrain.hpp"
#include "oa/base/game_math.hpp"
#include "oa/core/unit.h"
#include "oa/core/unit_def.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace oa::sim::unit_movement {
namespace {
constexpr unsigned fraction_bits = 16, attribute_shift = 4;
constexpr int attribute_spacing = 1 << attribute_shift;
constexpr uint32_t attribute_fraction_mask = attribute_spacing - 1;
// A hovercraft that is live and not about to die bobs on the water.
constexpr uint32_t hover_type = OA_UNIT_DEF_FLAG_CAN_HOVER;
constexpr uint32_t bob_decay_ticks = 60, bob_clock_mask = 31;
constexpr unsigned bob_vertex_phase = 8, bob_angle_shift = 11;

Fixed bits(uint32_t a) noexcept {
    return std::bit_cast<Fixed>(a);
}

int16_t short_bits(uint32_t a) noexcept {
    return std::bit_cast<int16_t>(static_cast<uint16_t>(a));
}

Fixed low(int64_t a) noexcept {
    return bits(static_cast<uint32_t>(a));
}

Fixed add(Fixed a, Fixed b) noexcept {
    return bits(uint32_t(a) + uint32_t(b));
}

Fixed subtract(Fixed a, Fixed b) noexcept {
    return bits(uint32_t(a) - uint32_t(b));
}

Fixed absolute(Fixed v) noexcept {
    return v < 0 ? subtract(0, v) : v;
}

Fixed nearest_integer(double value) noexcept {
    // Rounds to the nearest integer, ties to even.
    const double floor_value = std::floor(value);
    const double fraction = value - floor_value;
    double rounded = floor_value;
    if (fraction > 0.5 || (fraction == 0.5 && std::fmod(floor_value, 2.0) != 0))
        rounded += 1;
    if (!std::isfinite(rounded) || rounded < std::numeric_limits<Fixed>::min() ||
        rounded > std::numeric_limits<Fixed>::max())
        return std::numeric_limits<Fixed>::min();
    return static_cast<Fixed>(rounded);
}

std::array<Fixed, 2> rotate(std::array<Fixed, 2> v, uint16_t heading) noexcept {
    if (heading == 0)
        return v;
    const auto rotated = base::game_math::rotate_pair(v[0], v[1], short_bits(heading));
    return {nearest_integer(rotated.first), nearest_integer(rotated.second)};
}

int16_t slope_angle(Fixed height, Fixed distance) noexcept {
    return short_bits(base::game_math::direction(height, distance));
}
} // namespace

bool terrain_grid_valid(const oa::formats::tnt::Map& map) noexcept {
    return map.attribute_width >= 2 && map.attribute_height >= 2 &&
           map.attribute_width <= oa::formats::tnt::limit::attribute_dimension &&
           map.attribute_height <= oa::formats::tnt::limit::attribute_dimension &&
           uint64_t(map.attribute_width) * map.attribute_height == map.attributes.size() &&
           map.sea_level <= 255;
}

Terrain::Terrain(const oa::formats::tnt::Map& map) noexcept
    : map_(terrain_grid_valid(map) ? &map : nullptr) {
}

uint8_t Terrain::sea_level() const noexcept {
    return map_ ? static_cast<uint8_t>(map_->sea_level) : 0;
}

int32_t Terrain::height(Fixed x, Fixed z) const noexcept {
    if (!map_)
        return -1;
    const int world_x = short_bits(uint32_t(x) >> fraction_bits),
              world_z = short_bits(uint32_t(z) >> fraction_bits);
    const int cell_x = world_x >> attribute_shift, cell_z = world_z >> attribute_shift;
    if (cell_x < 0 || cell_z < 0 || uint32_t(cell_x) + 1 >= map_->attribute_width ||
        uint32_t(cell_z) + 1 >= map_->attribute_height)
        return -1;
    const auto fx = int(uint32_t(world_x) & attribute_fraction_mask),
               fz = int(uint32_t(world_z) & attribute_fraction_mask);
    const auto at = std::size_t(cell_z) * map_->attribute_width + std::size_t(cell_x);
    const int top_left = map_->attributes[at].height, top_right = map_->attributes[at + 1].height;
    const int bottom_left = map_->attributes[at + map_->attribute_width].height,
              bottom_right = map_->attributes[at + map_->attribute_width + 1].height;
    const int top = top_left + (top_right - top_left) * fx / attribute_spacing;
    const int bottom = bottom_left + (bottom_right - bottom_left) * fx / attribute_spacing;
    return top + (bottom - top) * fz / attribute_spacing;
}

uint32_t surface_height(const Terrain& terrain, Fixed x, Fixed z) noexcept {
    const auto sea = uint32_t(terrain.sea_level());
    if (static_cast<int32_t>(sea) < terrain.height(x, z))
        return static_cast<uint32_t>(terrain.height(x, z));
    return sea;
}

std::optional<GroundQuad> ground_quad(const oa::formats::objects3d::Model& model) {
    if (model.objects.empty())
        return std::nullopt;
    const auto& root = model.objects.front();
    if (root.selection_primitive < 0)
        return std::nullopt;
    if (std::size_t(root.selection_primitive) >= root.primitives.size())
        return std::nullopt;
    const auto& indices = root.primitives[std::size_t(root.selection_primitive)].vertex_indices;
    if (indices.size() < 4)
        return std::nullopt;
    GroundQuad quad{};
    for (std::size_t i = 0; i < quad.size(); ++i) {
        if (indices[i] >= root.vertices.size())
            return std::nullopt;
        const auto& v = root.vertices[indices[i]];
        quad[i] = {subtract(0, v.x), subtract(0, v.z)};
    }
    return quad;
}

uint32_t scaled_bob_tick(uint32_t uptime_milliseconds, uint32_t clock_scale) noexcept {
    constexpr uint32_t milliseconds_per_second = 1000;
    return (uptime_milliseconds * clock_scale) / milliseconds_per_second;
}

bool fit_ground(
    const Terrain& terrain,
    const std::optional<GroundQuad>& quad,
    GroundPose& pose,
    const Movement& movement,
    const GroundClock& clock
) {
    if (!quad)
        return false;
    std::array<Fixed, 4> heights{};
    for (std::size_t i = 0; i < quad->size(); ++i) {
        const auto vertex = rotate((*quad)[i], pose.heading);
        Fixed sample =
            terrain.height(add(pose.position[0], vertex[0]), subtract(pose.position[2], vertex[1]));
        if (sample < 0)
            return false;
        if ((pose.type_flags & hover_type) != 0 && unit_is_live_target(pose.flags)) {
            sample = std::max(sample, Fixed(terrain.sea_level()));
            const auto phase = static_cast<uint16_t>(
                ((clock.bob_ticks[i] & bob_clock_mask) + uint32_t(i) * bob_vertex_phase)
                << bob_angle_shift
            );
            const auto angle = static_cast<uint16_t>(phase + pose.bob_phase);
            const auto half_maximum = pose.maximum_speed / 2;
            if (half_maximum == 0)
                return false;
            const auto ratio =
                low(int64_t(std::min(movement.speed, half_maximum)) * 65536 / half_maximum);
            const auto amplitude = subtract(2, low((int64_t(ratio) * 2) >> fraction_bits));
            const auto elapsed =
                std::min(clock.simulation_tick - movement.last_motion_tick, bob_decay_ticks);
            const auto decay = (elapsed * uint32_t(amplitude)) / bob_decay_ticks;
            sample = add(sample, cosine_scaled(angle, bits(uint32_t(amplitude) - decay)));
        }
        heights[i] = sample;
    }
    const auto front = add(heights[0], heights[1]) / 2, back = add(heights[2], heights[3]) / 2;
    const auto length =
        short_bits(uint32_t(absolute(subtract((*quad)[0][1], (*quad)[3][1]))) >> fraction_bits);
    const auto width =
        short_bits(uint32_t(absolute(subtract((*quad)[0][0], (*quad)[1][0]))) >> fraction_bits);
    pose.position[1] = bits(
        (uint32_t(pose.position[1]) & 0xffffu) | (uint32_t(add(front, back) / 2) << fraction_bits)
    );
    pose.pitch = slope_angle(subtract(back, front), length);
    pose.roll = slope_angle(subtract(heights[0], heights[1]), width);
    return true;
}
} // namespace oa::sim::unit_movement
