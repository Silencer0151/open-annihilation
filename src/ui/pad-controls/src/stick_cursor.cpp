// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The stick cursor and the flick detector (pad_controls.hpp).
#include "oa/ui/pad_controls.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace oa::ui::pad_controls {

namespace {

/// Nanoseconds in a millisecond.
constexpr uint64_t ns_per_ms = 1'000'000;
/// Seconds in a nanosecond.
constexpr double seconds_per_ns = 1.0e-9;

/// Returns the distance between two points.
///
/// @param a one point
/// @param b the other
/// @return the distance
float distance_between(Vec2 a, Vec2 b) noexcept {
    return std::hypot(a.x - b.x, a.y - b.y);
}

/// Returns whether any target lies within a reach of a point.
///
/// @param point the point, canvas pixels
/// @param targets the targets, canvas pixels
/// @param reach the reach, canvas pixels
/// @return whether one does
bool any_within(Vec2 point, std::span<const Vec2> targets, float reach) noexcept {
    return std::any_of(targets.begin(), targets.end(), [&](const Vec2& target) {
        return distance_between(point, target) <= reach;
    });
}

/// Returns the lone target magnetism takes: the nearest within a reach of the pointer, when no
/// other target lies within a rival's reach of it.
///
/// @param pointer the pointer, canvas pixels
/// @param targets the targets, canvas pixels
/// @param reach how far from the pointer a target is taken, canvas pixels
/// @param rival_reach how near the target another one blocks it, canvas pixels
/// @return the target, or none
std::optional<Vec2>
lone_target(Vec2 pointer, std::span<const Vec2> targets, float reach, float rival_reach) noexcept {
    std::optional<std::size_t> nearest;
    float nearest_distance = reach;
    for (std::size_t index = 0; index < targets.size(); ++index) {
        const float distance = distance_between(pointer, targets[index]);
        if (distance <= nearest_distance) {
            nearest = index;
            nearest_distance = distance;
        }
    }
    if (!nearest)
        return std::nullopt;
    for (std::size_t index = 0; index < targets.size(); ++index)
        if (index != *nearest && distance_between(targets[*nearest], targets[index]) <= rival_reach)
            return std::nullopt;
    return targets[*nearest];
}

} // namespace

float stick_deflection(Vec2 stick) noexcept {
    const float raw = std::hypot(stick.x, stick.y);
    if (raw <= stick_inner_dead_zone)
        return 0.0f;
    const float part =
        (raw - stick_inner_dead_zone) / (stick_outer_dead_zone - stick_inner_dead_zone);
    return std::clamp(part, 0.0f, 1.0f);
}

void StickCursor::configure(const PadSettings& settings, float px_per_point) noexcept {
    px_per_point_ = px_per_point > 0.0f ? px_per_point : 1.0f;
    magnetism_ = settings.magnetism;
    if (!magnetism_)
        easing_ = false;
}

PointerStep StickCursor::advance(
    Vec2 stick, Vec2 pointer, std::span<const Vec2> targets, bool blocked, uint64_t now_ns
) noexcept {
    const uint64_t elapsed = started_ && now_ns > last_ns_ ? now_ns - last_ns_ : 0;
    started_ = true;
    last_ns_ = now_ns;
    const uint64_t most_ns = uint64_t{longest_pointer_step_ms} * ns_per_ms;
    const double seconds = static_cast<double>(std::min(elapsed, most_ns)) * seconds_per_ns;
    const float deflection = stick_deflection(stick);
    PointerStep step{};
    if (deflection > 0.0f) {
        easing_ = false;
        moving_ = true;
        const float raw = std::hypot(stick.x, stick.y);
        if (raw >= stick_ramp_deflection) {
            if (!full_since_ns_)
                full_since_ns_ = now_ns;
        } else {
            full_since_ns_.reset();
        }
        float gain = 1.0f;
        const uint64_t ramp_after_ns = uint64_t{stick_ramp_after_ms} * ns_per_ms;
        if (full_since_ns_ && now_ns > *full_since_ns_ + ramp_after_ns) {
            const float ramped = static_cast<float>(now_ns - *full_since_ns_ - ramp_after_ns) /
                                 static_cast<float>(uint64_t{stick_ramp_ms} * ns_per_ms);
            gain = 1.0f + (stick_ramp_gain - 1.0f) * std::min(ramped, 1.0f);
        }
        float speed_points = stick_speed_points * std::pow(deflection, stick_curve) * gain;
        if (!blocked && any_within(pointer, targets, friction_points * px_per_point_))
            speed_points *= friction_factor;
        const auto travel =
            static_cast<float>(static_cast<double>(speed_points * px_per_point_) * seconds);
        step.move = {stick.x / raw * travel, stick.y / raw * travel};
        step.ticks = ticks_.add(std::hypot(step.move.x, step.move.y));
        return step;
    }
    full_since_ns_.reset();
    if (moving_ && magnetism_ && !blocked) {
        // The stick came back to rest: a lone target near the pointer draws it in.
        const auto target = lone_target(
            pointer, targets, magnet_points * px_per_point_, magnet_rival_points * px_per_point_
        );
        if (target) {
            easing_ = true;
            ease_from_ = pointer;
            ease_to_ = *target;
            ease_start_ns_ = now_ns;
        }
    }
    moving_ = false;
    if (easing_ && (blocked || !magnetism_))
        easing_ = false;
    if (easing_) {
        const float part = std::min(
            static_cast<float>(now_ns - ease_start_ns_) /
                static_cast<float>(uint64_t{magnet_ease_ms} * ns_per_ms),
            1.0f
        );
        const Vec2 wanted{
            ease_from_.x + (ease_to_.x - ease_from_.x) * part,
            ease_from_.y + (ease_to_.y - ease_from_.y) * part
        };
        step.move = {wanted.x - pointer.x, wanted.y - pointer.y};
        step.ticks = ticks_.add(std::hypot(step.move.x, step.move.y));
        if (part >= 1.0f)
            easing_ = false;
    }
    return step;
}

void StickCursor::reset() noexcept {
    started_ = false;
    last_ns_ = 0;
    moving_ = false;
    full_since_ns_.reset();
    easing_ = false;
    ticks_.reset();
}

bool StickCursor::easing() const noexcept {
    return easing_;
}

Flick FlickDetector::update(Vec2 stick) noexcept {
    const float across = std::fabs(stick.x);
    const float down = std::fabs(stick.y);
    if (!armed_) {
        armed_ = across < flick_back_deflection && down < flick_back_deflection;
        return Flick::none;
    }
    if (across < flick_out_deflection && down < flick_out_deflection)
        return Flick::none;
    armed_ = false;
    if (across >= down)
        return stick.x < 0.0f ? Flick::left : Flick::right;
    return stick.y < 0.0f ? Flick::up : Flick::down;
}

void FlickDetector::reset() noexcept {
    armed_ = true;
}

} // namespace oa::ui::pad_controls
