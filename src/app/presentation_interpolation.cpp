// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the battlefield shows between two ticks (presentation_interpolation.hpp).
#include "presentation_interpolation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <tuple>
#include <utility>

namespace oa::app {
namespace {

/// Bits below the binary point of a tick fraction.
constexpr uint32_t fraction_bits = 16;

/// The farthest a unit moves in one tick and still shows between its places, 16.16.
constexpr int64_t unit_jump_fixed = int64_t{unit_jump_pixels} << fraction_bits;

/// Returns the step from one 16.16 value to another, wrapping at 32 bits.
///
/// @param from the first value
/// @param to the second value
/// @return to - from, wrapped to 32 signed bits
int32_t wrapping_step(int32_t from, int32_t to) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(to) - static_cast<uint32_t>(from));
}

/// Returns the part of a step a fraction covers, rounded toward negative infinity.
///
/// @param step the whole step
/// @param fraction the part, 0 to whole_tick
/// @return step * fraction / whole_tick
int64_t part_of(int64_t step, uint32_t fraction) noexcept {
    return (step * static_cast<int64_t>(fraction)) >> fraction_bits;
}

/// Tells whether a unit moved further than a jump along one axis.
///
/// @param from the coordinate at the tick before, 16.16
/// @param to the coordinate at the current tick
/// @return true for a step longer than unit_jump_pixels
bool jumps(int32_t from, int32_t to) noexcept {
    const int64_t step = wrapping_step(from, to);
    return step > unit_jump_fixed || step < -unit_jump_fixed;
}

bool same_point(const FixedVec3& a, const FixedVec3& b) noexcept {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

/// Returns a 16.16 step taken a number of times, wrapping at 32 bits.
///
/// @param step the step
/// @param times how many times it is taken
/// @return step * times, wrapped to 32 signed bits
int32_t steps_of(int32_t step, uint32_t times) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(step) * times);
}

/// Tells whether two shot poses are of one projectile: the same weapon,
/// source and tick of creation.
///
/// @param a a pose
/// @param b another pose
/// @return true when all three match
bool same_shot(const ShotPose& a, const ShotPose& b) noexcept {
    return a.def == b.def && a.source == b.source && a.created_tick == b.created_tick;
}

/// Returns how far apart two points are along the axis they differ most on.
///
/// @param a a point
/// @param b another point
/// @return the largest of the three wrapped differences' magnitudes, 16.16
int64_t point_distance(const FixedVec3& a, const FixedVec3& b) noexcept {
    const auto along = [](int32_t from, int32_t to) {
        return std::abs(static_cast<int64_t>(wrapping_step(from, to)));
    };
    return std::max({along(a.x, b.x), along(a.y, b.y), along(a.z, b.z)});
}

/// Finds each projectile of the current pool in the previous pool across a
/// batch of ticks: the nearest unclaimed projectile of the same weapon,
/// source and tick of creation.
///
/// @param[in,out] flight the pool's flight; its previous_index is filled
void match_shots_across_batch(ShotFlight& flight) {
    // The previous pool's projectiles ordered by weapon, source and tick of
    // creation, then index, so that each projectile's candidates lie together.
    const auto key = [](const ShotPose& pose) {
        return std::tuple{pose.def, pose.source, pose.created_tick};
    };
    std::vector<int32_t> order(flight.previous.size());
    for (std::size_t index = 0; index < order.size(); ++index)
        order[index] = static_cast<int32_t>(index);
    std::sort(order.begin(), order.end(), [&](int32_t a, int32_t b) {
        const auto key_a = key(flight.previous[static_cast<std::size_t>(a)]);
        const auto key_b = key(flight.previous[static_cast<std::size_t>(b)]);
        return key_a != key_b ? key_a < key_b : a < b;
    });
    std::vector<bool> claimed(flight.previous.size());
    for (std::size_t index = 0; index < flight.current.size(); ++index) {
        const ShotPose& shot = flight.current[index];
        const auto first = std::lower_bound(
            order.begin(), order.end(), key(shot), [&](int32_t a, const auto& wanted) {
                return key(flight.previous[static_cast<std::size_t>(a)]) < wanted;
            }
        );
        int32_t nearest = -1;
        int64_t nearest_distance = 0;
        for (auto at = first;
             at != order.end() && same_shot(flight.previous[static_cast<std::size_t>(*at)], shot);
             ++at) {
            const auto candidate = static_cast<std::size_t>(*at);
            if (claimed[candidate])
                continue;
            const int64_t distance =
                point_distance(flight.previous[candidate].position, shot.position);
            if (nearest < 0 || distance < nearest_distance) {
                nearest = *at;
                nearest_distance = distance;
            }
        }
        if (nearest >= 0)
            claimed[static_cast<std::size_t>(nearest)] = true;
        flight.previous_index[index] = nearest;
    }
}

/// Takes a projectile's place from its pose into a copy of the pose.
///
/// @param previous the pose at the tick before
/// @param current the pose at the current tick
/// @param fraction the part of the way, 0 to whole_tick
/// @return `current` with its place and orientation blended
ShotPose blend_shot(const ShotPose& previous, const ShotPose& current, uint32_t fraction) noexcept {
    ShotPose blended = current;
    blended.position = blend_point(previous.position, current.position, fraction);
    blended.origin = blend_point(previous.origin, current.origin, fraction);
    blended.heading = static_cast<uint16_t>(blend_angle(
        static_cast<int16_t>(previous.heading), static_cast<int16_t>(current.heading), fraction
    ));
    blended.pitch = static_cast<uint16_t>(blend_angle(
        static_cast<int16_t>(previous.pitch), static_cast<int16_t>(current.pitch), fraction
    ));
    return blended;
}

/// Tells whether two debris poses are of the same piece.
///
/// @param a a pose
/// @param b another pose
/// @return true when both are live and of one unit's piece and object
bool same_debris(const DebrisPose& a, const DebrisPose& b) noexcept {
    return a.live && b.live && a.unit == b.unit && a.piece == b.piece && a.model == b.model &&
           a.object == b.object;
}

} // namespace

uint32_t batch_ticks(uint32_t seen, uint32_t tick) noexcept {
    const uint32_t ticks = tick - seen;
    return ticks <= most_batch_ticks ? ticks : 0;
}

uint32_t tick_fraction(float alpha) noexcept {
    if (!(alpha >= 0.0F))
        return std::isnan(alpha) ? whole_tick : 0;
    if (alpha >= 1.0F)
        return whole_tick;
    return static_cast<uint32_t>(std::lround(static_cast<double>(alpha) * whole_tick));
}

int32_t blend_fixed(int32_t previous, int32_t current, uint32_t fraction) noexcept {
    if (fraction >= whole_tick)
        return current;
    const int64_t part = part_of(wrapping_step(previous, current), fraction);
    return static_cast<int32_t>(static_cast<uint32_t>(previous) + static_cast<uint32_t>(part));
}

int16_t blend_angle(int16_t previous, int16_t current, uint32_t fraction) noexcept {
    if (fraction >= whole_tick)
        return current;
    const auto turn = static_cast<int16_t>(static_cast<uint16_t>(current - previous));
    const int64_t part = part_of(turn, fraction);
    return static_cast<int16_t>(static_cast<uint16_t>(previous + part));
}

FixedVec3
blend_point(const FixedVec3& previous, const FixedVec3& current, uint32_t fraction) noexcept {
    return {
        blend_fixed(previous.x, current.x, fraction),
        blend_fixed(previous.y, current.y, fraction),
        blend_fixed(previous.z, current.z, fraction)
    };
}

FixedVec3 point_along_step(
    const FixedVec3& point, const FixedVec3& step, uint32_t ticks, uint32_t fraction
) noexcept {
    const FixedVec3 before{
        wrapping_step(steps_of(step.x, ticks), point.x),
        wrapping_step(steps_of(step.y, ticks), point.y),
        wrapping_step(steps_of(step.z, ticks), point.z)
    };
    return blend_point(before, point, fraction);
}

int16_t angle_along_step(int16_t angle, int16_t step, uint32_t ticks, uint32_t fraction) noexcept {
    const auto turned =
        static_cast<uint16_t>(static_cast<uint32_t>(static_cast<uint16_t>(step)) * ticks);
    return blend_angle(
        static_cast<int16_t>(static_cast<uint16_t>(angle - turned)), angle, fraction
    );
}

void capture_unit_pose(
    const oa::Unit& unit, const oa::sim::model_runtime::Instance& instance, UnitPose& pose
) {
    pose.position = unit.position;
    pose.heading = unit.heading;
    pose.pitch = unit.pitch;
    pose.bank = unit.bank;
    pose.attach_parent = unit.attach_parent;
    const auto pieces = instance.pieces();
    pose.pieces.resize(pieces.size());
    for (std::size_t index = 0; index < pieces.size(); ++index)
        pose.pieces[index] = {pieces[index].translation, pieces[index].rotation};
}

bool poses_equal(const UnitPose& a, const UnitPose& b) noexcept {
    if (!same_point(a.position, b.position) || a.heading != b.heading || a.pitch != b.pitch ||
        a.bank != b.bank || a.attach_parent != b.attach_parent ||
        a.pieces.size() != b.pieces.size())
        return false;
    for (std::size_t index = 0; index < a.pieces.size(); ++index) {
        const auto& first = a.pieces[index];
        const auto& second = b.pieces[index];
        if (first.translation.x != second.translation.x ||
            first.translation.y != second.translation.y ||
            first.translation.z != second.translation.z ||
            first.rotation.xy != second.rotation.xy || first.rotation.xz != second.rotation.xz ||
            first.rotation.yz != second.rotation.yz)
            return false;
    }
    return true;
}

bool pose_continues(const UnitPose& previous, const UnitPose& current) noexcept {
    return previous.attach_parent == current.attach_parent &&
           previous.pieces.size() == current.pieces.size() &&
           !jumps(previous.position.x, current.position.x) &&
           !jumps(previous.position.y, current.position.y) &&
           !jumps(previous.position.z, current.position.z);
}

void blend_unit_pose(
    const UnitPose& previous,
    const UnitPose& current,
    uint32_t fraction,
    oa::Unit& record,
    oa::sim::model_runtime::Instance& instance
) {
    record.position = blend_point(previous.position, current.position, fraction);
    record.heading = static_cast<uint16_t>(blend_angle(
        static_cast<int16_t>(previous.heading), static_cast<int16_t>(current.heading), fraction
    ));
    record.pitch = blend_angle(previous.pitch, current.pitch, fraction);
    record.bank = blend_angle(previous.bank, current.bank, fraction);
    auto pieces = instance.pieces();
    const std::size_t count =
        std::min({pieces.size(), previous.pieces.size(), current.pieces.size()});
    for (std::size_t index = 0; index < count; ++index) {
        const auto& from = previous.pieces[index];
        const auto& to = current.pieces[index];
        auto& piece = pieces[index];
        piece.translation = {
            blend_fixed(from.translation.x, to.translation.x, fraction),
            blend_fixed(from.translation.y, to.translation.y, fraction),
            blend_fixed(from.translation.z, to.translation.z, fraction)
        };
        piece.rotation = {
            blend_angle(from.rotation.xy, to.rotation.xy, fraction),
            blend_angle(from.rotation.xz, to.rotation.xz, fraction),
            blend_angle(from.rotation.yz, to.rotation.yz, fraction)
        };
    }
}

void observe_unit(
    UnitMotion& motion,
    uint32_t tick,
    uint32_t instance_generation,
    const oa::Unit& unit,
    const oa::sim::model_runtime::Instance& instance
) {
    if (!motion.seen || motion.instance_generation != instance_generation) {
        // A unit the slot did not hold before: its first pose, and a draw
        // state of its own.
        motion.instance_generation = instance_generation;
        motion.state = {};
        motion.blended_draw = 0;
        capture_unit_pose(unit, instance, motion.current);
        motion.tick = tick;
        motion.seen = true;
        motion.continued = false;
        motion.moved = false;
        return;
    }
    if (tick == motion.tick)
        return;
    const bool follows = batch_ticks(motion.tick, tick) != 0;
    if (follows)
        std::swap(motion.previous, motion.current);
    capture_unit_pose(unit, instance, motion.current);
    motion.tick = tick;
    motion.continued = follows && pose_continues(motion.previous, motion.current);
    motion.moved = motion.continued && !poses_equal(motion.previous, motion.current);
}

void forget_unit(UnitMotion& motion) noexcept {
    motion = UnitMotion{};
}

ShotPose shot_pose(const oa::Projectile& shot) noexcept {
    return {
        shot.position,
        shot.origin,
        shot.heading,
        shot.pitch,
        shot.def,
        shot.source,
        shot.created_tick
    };
}

void observe_shots(ShotFlight& flight, uint32_t tick, std::span<const oa::Projectile> shots) {
    if (flight.seen && tick == flight.tick)
        return;
    const uint32_t ticks = flight.seen ? batch_ticks(flight.tick, tick) : 0;
    if (ticks != 0) {
        std::swap(flight.previous, flight.current);
        flight.previous_tick = flight.tick;
    }
    flight.current.resize(shots.size());
    for (std::size_t index = 0; index < shots.size(); ++index)
        flight.current[index] = shot_pose(shots[index]);
    flight.tick = tick;
    flight.seen = true;
    flight.continued = ticks != 0;
    flight.previous_index.assign(shots.size(), -1);
    if (ticks == 1) {
        // One tick closed the pool's gaps once: Projectile.compact_index.
        for (std::size_t index = 0; index < shots.size(); ++index) {
            const auto before =
                static_cast<std::size_t>(static_cast<uint16_t>(shots[index].compact_index));
            if (shots[index].compact_index >= 0 && before < flight.previous.size() &&
                same_shot(flight.previous[before], flight.current[index]))
                flight.previous_index[index] = static_cast<int32_t>(before);
        }
    } else if (ticks > 1) {
        match_shots_across_batch(flight);
    }
}

ShotPose presented_shot(
    const ShotFlight& flight,
    uint32_t tick,
    std::size_t index,
    const oa::Projectile& shot,
    uint32_t fraction
) noexcept {
    const ShotPose current = shot_pose(shot);
    if (fraction >= whole_tick || !flight.seen || flight.tick != tick)
        return current;
    if (flight.continued && index < flight.previous_index.size() &&
        flight.previous_index[index] >= 0) {
        const ShotPose& previous =
            flight.previous[static_cast<std::size_t>(flight.previous_index[index])];
        if (same_shot(previous, current))
            return blend_shot(previous, current, fraction);
    }
    // Fired since the tick before: it left its muzzle, Projectile.origin.
    const uint32_t before = flight.continued ? flight.previous_tick : tick - 1;
    if (static_cast<int32_t>(shot.created_tick - before) > 0 &&
        static_cast<int32_t>(tick - shot.created_tick) >= 0) {
        ShotPose muzzle = current;
        muzzle.position = current.origin;
        return blend_shot(muzzle, current, fraction);
    }
    return current;
}

void observe_debris(
    DebrisFall& fall, uint32_t tick, std::span<const oa::sim::effect_particles::DebrisPiece> debris
) {
    if (fall.seen && tick == fall.tick)
        return;
    const bool next = fall.seen && batch_ticks(fall.tick, tick) != 0;
    if (next)
        fall.previous = fall.current;
    const std::size_t count = std::min(debris.size(), fall.current.size());
    for (std::size_t slot = 0; slot < fall.current.size(); ++slot) {
        if (slot >= count) {
            fall.current[slot] = {};
            continue;
        }
        const auto& piece = debris[slot];
        fall.current[slot] = {
            piece.live,
            piece.unit,
            piece.piece,
            piece.model,
            piece.object,
            piece.position,
            {piece.spin[0], piece.spin[1], piece.spin[2]}
        };
    }
    fall.tick = tick;
    fall.seen = true;
    fall.continued = next;
}

oa::sim::effect_particles::DebrisPiece presented_debris(
    const DebrisFall& fall,
    uint32_t tick,
    std::size_t slot,
    const oa::sim::effect_particles::DebrisPiece& piece,
    uint32_t fraction
) noexcept {
    oa::sim::effect_particles::DebrisPiece shown = piece;
    if (fraction >= whole_tick || !fall.seen || !fall.continued || fall.tick != tick ||
        slot >= fall.previous.size())
        return shown;
    const DebrisPose& previous = fall.previous[slot];
    if (!same_debris(previous, fall.current[slot]))
        return shown;
    shown.position = blend_point(previous.position, piece.position, fraction);
    for (std::size_t axis = 0; axis < previous.spin.size(); ++axis)
        shown.spin[axis] = blend_angle(previous.spin[axis], piece.spin[axis], fraction);
    return shown;
}

void start_debug_grid_draw(DebugGridRandom& kept, uint32_t tick) noexcept {
    kept.first = !kept.drawn || kept.tick != tick;
    if (kept.first) {
        kept.tick = tick;
        kept.drawn = true;
        kept.values.clear();
    }
    kept.next = 0;
}

} // namespace oa::app
