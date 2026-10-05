// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Triggers read as buttons, hold timers, menu repeat, double clicks and the
// rings' wedge aim (pad_controls.hpp).
#include "oa/ui/pad_controls.hpp"

#include <algorithm>
#include <cmath>

namespace oa::ui::pad_controls {

namespace {

/// A full turn, in degrees.
constexpr float full_turn_degrees = 360.0f;
/// Half a turn, in degrees.
constexpr float half_turn_degrees = 180.0f;
/// Pi, for the wedge angles.
constexpr double pi = 3.14159265358979323846;

/// Returns the angle of an offset clockwise from the top, in degrees.
///
/// @param offset the offset, y down
/// @return 0 up to (not including) 360
float clockwise_degrees(Vec2 offset) noexcept {
    float degrees = static_cast<float>(
        std::atan2(static_cast<double>(offset.x), static_cast<double>(-offset.y)) *
        static_cast<double>(half_turn_degrees) / pi
    );
    if (degrees < 0.0f)
        degrees += full_turn_degrees;
    if (degrees >= full_turn_degrees)
        degrees -= full_turn_degrees;
    return degrees;
}

/// Returns the angle between two directions, in degrees.
///
/// @param a one direction, degrees
/// @param b the other, degrees
/// @return 0..180
float degrees_apart(float a, float b) noexcept {
    float apart = std::fabs(a - b);
    while (apart > full_turn_degrees)
        apart -= full_turn_degrees;
    return apart > half_turn_degrees ? full_turn_degrees - apart : apart;
}

} // namespace

bool TriggerButton::update(float pull) noexcept {
    if (down_ ? pull < trigger_off_pull : pull >= trigger_on_pull)
        down_ = !down_;
    return down_;
}

bool TriggerButton::down() const noexcept {
    return down_;
}

void HoldTimer::press(uint64_t now_ms) noexcept {
    down_ = true;
    holding_ = false;
    used_ = false;
    pressed_ms_ = now_ms;
}

HoldEvent HoldTimer::release(uint64_t now_ms, uint32_t hold_ms) noexcept {
    if (!down_)
        return HoldEvent::none;
    const bool held_long = now_ms >= pressed_ms_ && now_ms - pressed_ms_ >= hold_ms;
    HoldEvent event = HoldEvent::none;
    if (holding_)
        event = HoldEvent::hold_ended;
    else if (!used_)
        event = held_long ? HoldEvent::hold_ended : HoldEvent::tap;
    down_ = false;
    holding_ = false;
    used_ = false;
    return event;
}

HoldEvent HoldTimer::advance(uint64_t now_ms, uint32_t hold_ms) noexcept {
    if (!down_ || holding_ || used_ || now_ms < pressed_ms_ || now_ms - pressed_ms_ < hold_ms)
        return HoldEvent::none;
    holding_ = true;
    return HoldEvent::hold_started;
}

void HoldTimer::use() noexcept {
    if (down_)
        used_ = true;
}

void HoldTimer::cancel() noexcept {
    down_ = false;
    holding_ = false;
    used_ = false;
}

bool HoldTimer::down() const noexcept {
    return down_;
}

bool HoldTimer::holding() const noexcept {
    return holding_;
}

bool HoldTimer::used() const noexcept {
    return used_;
}

float HoldTimer::progress(uint64_t now_ms, uint32_t hold_ms) const noexcept {
    if (!down_ || used_)
        return 0.0f;
    if (holding_ || hold_ms == 0)
        return 1.0f;
    if (now_ms <= pressed_ms_)
        return 0.0f;
    const float part = static_cast<float>(now_ms - pressed_ms_) / static_cast<float>(hold_ms);
    return std::clamp(part, 0.0f, 1.0f);
}

uint32_t MenuRepeat::press(uint64_t now_ms) noexcept {
    down_ = true;
    next_ms_ = now_ms + menu_repeat_delay_ms;
    return 1;
}

uint32_t MenuRepeat::advance(uint64_t now_ms) noexcept {
    if (!down_ || now_ms < next_ms_)
        return 0;
    const uint64_t due = 1 + (now_ms - next_ms_) / menu_repeat_interval_ms;
    next_ms_ += due * menu_repeat_interval_ms;
    return static_cast<uint32_t>(std::min<uint64_t>(due, menu_repeat_most_steps));
}

void MenuRepeat::release() noexcept {
    down_ = false;
}

uint8_t DoubleClick::press(uint64_t now_ms, Vec2 at, float px_per_point) noexcept {
    const float reach = double_click_points * (px_per_point > 0.0f ? px_per_point : 1.0f);
    const bool soon = armed_ && now_ms >= last_ms_ && now_ms - last_ms_ <= double_click_ms;
    const bool near = std::hypot(at.x - last_at_.x, at.y - last_at_.y) <= reach;
    if (soon && near) {
        armed_ = false;
        return 2;
    }
    armed_ = true;
    last_ms_ = now_ms;
    last_at_ = at;
    return 1;
}

void DoubleClick::reset() noexcept {
    armed_ = false;
}

void WedgeAim::configure(uint8_t slots) noexcept {
    slots_ = std::max<uint8_t>(slots, 1);
    slot_.reset();
}

std::optional<uint8_t> WedgeAim::aim(Vec2 offset, float dead_zone) noexcept {
    if (std::hypot(offset.x, offset.y) <= dead_zone) {
        slot_.reset();
        return slot_;
    }
    const float slot_degrees = full_turn_degrees / static_cast<float>(slots_);
    const float degrees = clockwise_degrees(offset);
    if (slot_) {
        // The aimed wedge is kept until the aim is past its edge by the hysteresis.
        const float centre = static_cast<float>(*slot_) * slot_degrees;
        if (degrees_apart(degrees, centre) <= slot_degrees / 2.0f + wedge_hysteresis_degrees)
            return slot_;
    }
    const auto nearest =
        static_cast<uint32_t>(std::floor((degrees + slot_degrees / 2.0f) / slot_degrees));
    slot_ = static_cast<uint8_t>(nearest % slots_);
    return slot_;
}

void WedgeAim::reset() noexcept {
    slot_.reset();
}

std::optional<uint8_t> WedgeAim::slot() const noexcept {
    return slot_;
}

Vec2 wedge_point(
    Vec2 centre, float inner_radius, float outer_radius, uint8_t slot, uint8_t slots
) noexcept {
    if (slots == 0)
        return centre;
    const double radius = (static_cast<double>(inner_radius) + outer_radius) / 2.0;
    const double angle = 2.0 * pi * static_cast<double>(slot % slots) / static_cast<double>(slots);
    return {
        centre.x + static_cast<float>(radius * std::sin(angle)),
        centre.y - static_cast<float>(radius * std::cos(angle))
    };
}

} // namespace oa::ui::pad_controls
