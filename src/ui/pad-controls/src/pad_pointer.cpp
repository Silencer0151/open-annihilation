// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The right trackpad as a pointer, the pointer's ticks and the gyro pointer
// (pad_controls.hpp).
#include "oa/ui/pad_controls.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace oa::ui::pad_controls {

namespace {

/// Nanoseconds in a millisecond.
constexpr uint64_t ns_per_ms = 1'000'000;
/// Seconds in a nanosecond.
constexpr double seconds_per_ns = 1.0e-9;
/// The most ticks one step reports.
constexpr uint32_t most_ticks_per_step = std::numeric_limits<uint8_t>::max();

/// Returns a vector's length.
///
/// @param vector the vector
/// @return its length
float length_of(Vec2 vector) noexcept {
    return std::hypot(vector.x, vector.y);
}

/// Returns a speed setting as a factor, held to its range.
///
/// @param percent the setting, percent
/// @param lowest its lowest value, percent
/// @param highest its highest value, percent
/// @return the factor, 1 at full_speed_percent
float speed_factor(uint32_t percent, uint32_t lowest, uint32_t highest) noexcept {
    return static_cast<float>(std::clamp(percent, lowest, highest)) /
           static_cast<float>(full_speed_percent);
}

/// Returns the time a step covers, in seconds, held between a least and a most.
///
/// @param elapsed_ns the time since the last step, nanoseconds
/// @param least_ns the least it counts, nanoseconds
/// @return seconds
double step_seconds(uint64_t elapsed_ns, uint64_t least_ns) noexcept {
    const uint64_t most_ns = uint64_t{longest_pointer_step_ms} * ns_per_ms;
    return static_cast<double>(std::clamp(elapsed_ns, least_ns, most_ns)) * seconds_per_ns;
}

/// Returns the greatest gain of a Pointer acceleration setting.
///
/// @param acceleration the setting
/// @return the gain at acceleration_top and above
float greatest_gain_of(Acceleration acceleration) noexcept {
    switch (acceleration) {
    case Acceleration::off:
        return 1.0f;
    case Acceleration::low:
        return low_acceleration_gain;
    case Acceleration::high:
        return high_acceleration_gain;
    }
    return 1.0f;
}

/// Returns the acceleration's gain at a thumb speed: ×1 up to acceleration_floor, rising
/// linearly to the greatest gain at acceleration_top.
///
/// @param speed the thumb's speed, pad widths a second
/// @param greatest the greatest gain
/// @return the gain
float acceleration_gain(float speed, float greatest) noexcept {
    if (greatest <= 1.0f || speed <= acceleration_floor)
        return 1.0f;
    if (speed >= acceleration_top)
        return greatest;
    const float part = (speed - acceleration_floor) / (acceleration_top - acceleration_floor);
    return 1.0f + (greatest - 1.0f) * part;
}

} // namespace

uint8_t TickCounter::add(float distance) noexcept {
    travel_ += std::max(distance, 0.0f);
    const float crossings = std::floor(travel_ / tick_travel_px);
    if (crossings >= static_cast<float>(most_ticks_per_step)) {
        travel_ = 0.0f;
        return static_cast<uint8_t>(most_ticks_per_step);
    }
    travel_ -= crossings * tick_travel_px;
    return static_cast<uint8_t>(crossings);
}

void TickCounter::reset() noexcept {
    travel_ = 0.0f;
}

void PadPointer::configure(const PadSettings& settings, Vec2 canvas, Area absolute_area) noexcept {
    gain_px_per_pad_ =
        canvas.x * pad_width_canvas_share *
        speed_factor(settings.pointer_speed, lowest_pointer_speed, highest_pointer_speed);
    greatest_gain_ = greatest_gain_of(settings.acceleration);
    glide_on_ = settings.glide;
    if (!glide_on_)
        gliding_ = false;
    const bool absolute = settings.right_trackpad == RightTrackpad::absolute;
    if (absolute != absolute_) {
        place_.reset();
        gliding_ = false;
    }
    absolute_ = absolute;
    area_ = absolute_area;
}

void PadPointer::touch_down(Vec2 pad, uint64_t now_ns) noexcept {
    touched_ = true;
    touch_ = pad;
    anchor_ = pad;
    landed_ns_ = now_ns;
    last_sample_ns_ = now_ns;
    velocity_px_per_s_ = {};
    gliding_ = false;
    place_.reset();
}

PointerStep PadPointer::touch_move(Vec2 pad, uint64_t now_ns) noexcept {
    if (!touched_)
        return {};
    touch_ = pad;
    PointerStep step{};
    if (now_ns < lock_until_ns_) {
        // The click lock drops the motion: the pointer stays on the click's target.
        anchor_ = pad;
        last_sample_ns_ = now_ns;
        velocity_px_per_s_ = {};
        step.place = place_;
        return step;
    }
    const bool landing = now_ns < landed_ns_ + uint64_t{landing_ms} * ns_per_ms;
    const Vec2 delta{pad.x - anchor_.x, pad.y - anchor_.y};
    const bool within_band = landing && length_of(delta) < landing_dead_band;
    if (absolute_) {
        const Vec2 place = pad_to_area(within_band ? anchor_ : pad, area_);
        if (place_)
            step.ticks = ticks_.add(length_of({place.x - place_->x, place.y - place_->y}));
        place_ = place;
        step.place = place;
        if (!within_band)
            anchor_ = pad;
        return step;
    }
    if (within_band)
        return step;
    const uint64_t elapsed = now_ns > last_sample_ns_ ? now_ns - last_sample_ns_ : 0;
    anchor_ = pad;
    last_sample_ns_ = now_ns;
    return relative_step(delta, elapsed);
}

PointerStep PadPointer::relative_step(Vec2 delta, uint64_t elapsed_ns) noexcept {
    const double seconds = step_seconds(elapsed_ns, uint64_t{shortest_speed_sample_ms} * ns_per_ms);
    const float speed = static_cast<float>(static_cast<double>(length_of(delta)) / seconds);
    const float gain = gain_px_per_pad_ * acceleration_gain(speed, greatest_gain_);
    PointerStep step{};
    step.move = {delta.x * gain, delta.y * gain};
    velocity_px_per_s_ = {
        static_cast<float>(static_cast<double>(step.move.x) / seconds),
        static_cast<float>(static_cast<double>(step.move.y) / seconds)
    };
    step.ticks = ticks_.add(length_of(step.move));
    return step;
}

void PadPointer::touch_up(uint64_t now_ns) noexcept {
    if (!touched_)
        return;
    touched_ = false;
    const bool recent = now_ns <= last_sample_ns_ + uint64_t{glide_rest_ms} * ns_per_ms;
    if (glide_on_ && !absolute_ && recent && length_of(velocity_px_per_s_) > glide_stop_px_per_s) {
        gliding_ = true;
        glide_px_per_s_ = velocity_px_per_s_;
        glide_ns_ = now_ns;
    }
    velocity_px_per_s_ = {};
}

void PadPointer::press(uint64_t now_ns) noexcept {
    lock_until_ns_ = now_ns + uint64_t{click_lock_ms} * ns_per_ms;
}

void PadPointer::release(uint64_t now_ns) noexcept {
    lock_until_ns_ = now_ns + uint64_t{click_lock_ms} * ns_per_ms;
}

PointerStep PadPointer::advance(uint64_t now_ns) noexcept {
    if (!gliding_ || now_ns <= glide_ns_)
        return {};
    const double seconds = step_seconds(now_ns - glide_ns_, 0);
    glide_ns_ = now_ns;
    // The speed decays as exp(-t / τ); the travel over the step is its integral.
    const double tau = static_cast<double>(glide_time_constant_s);
    const double decay = std::exp(-seconds / tau);
    const double travel = tau * (1.0 - decay);
    PointerStep step{};
    step.move = {
        static_cast<float>(static_cast<double>(glide_px_per_s_.x) * travel),
        static_cast<float>(static_cast<double>(glide_px_per_s_.y) * travel)
    };
    glide_px_per_s_ = {
        static_cast<float>(static_cast<double>(glide_px_per_s_.x) * decay),
        static_cast<float>(static_cast<double>(glide_px_per_s_.y) * decay)
    };
    if (length_of(glide_px_per_s_) < glide_stop_px_per_s)
        gliding_ = false;
    step.ticks = ticks_.add(length_of(step.move));
    return step;
}

void PadPointer::stop() noexcept {
    touched_ = false;
    gliding_ = false;
    velocity_px_per_s_ = {};
    lock_until_ns_ = 0;
    place_.reset();
    ticks_.reset();
}

bool PadPointer::touched() const noexcept {
    return touched_;
}

Vec2 PadPointer::touch() const noexcept {
    return touch_;
}

bool PadPointer::gliding() const noexcept {
    return gliding_;
}

Vec2 pad_to_area(Vec2 pad, Area area) noexcept {
    if (area.width <= 0.0f || area.height <= 0.0f)
        return {area.x, area.y};
    // The largest rectangle of the area's shape centred in the square pad.
    const float aspect = area.width / area.height;
    const float used_width = aspect >= 1.0f ? 1.0f : aspect;
    const float used_height = aspect >= 1.0f ? 1.0f / aspect : 1.0f;
    const float left = (1.0f - used_width) / 2.0f;
    const float top = (1.0f - used_height) / 2.0f;
    const float across = std::clamp((pad.x - left) / used_width, 0.0f, 1.0f);
    const float down = std::clamp((pad.y - top) / used_height, 0.0f, 1.0f);
    return {area.x + across * area.width, area.y + down * area.height};
}

void GyroPointer::configure(const PadSettings& settings, Vec2 canvas) noexcept {
    gain_px_per_radian_ = canvas.x * gyro_canvas_share_per_radian *
                          speed_factor(settings.gyro_speed, lowest_gyro_speed, highest_gyro_speed);
    on_ = settings.gyro != Gyro::off;
}

PointerStep GyroPointer::advance(float pitch, float yaw, bool gate, uint64_t now_ns) noexcept {
    const uint64_t elapsed = started_ && now_ns > last_ns_ ? now_ns - last_ns_ : 0;
    started_ = true;
    last_ns_ = now_ns;
    if (!on_ || !gate || elapsed == 0)
        return {};
    const auto seconds = static_cast<float>(step_seconds(elapsed, 0));
    PointerStep step{};
    // Turning left (positive yaw) moves the pointer left; the far end up (positive pitch) up.
    step.move = {-yaw * gain_px_per_radian_ * seconds, -pitch * gain_px_per_radian_ * seconds};
    step.ticks = ticks_.add(length_of(step.move));
    return step;
}

void GyroPointer::reset() noexcept {
    started_ = false;
    last_ns_ = 0;
    ticks_.reset();
}

} // namespace oa::ui::pad_controls
