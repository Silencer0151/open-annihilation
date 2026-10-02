// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Engine clock, interval timers and the frame-rate counter.
//
// Timers are polled, not preemptive: timers_tick runs every due callback on
// the calling thread. Intervals are in engine ticks, the millisecond clock
// scaled by the display context's rate (see clock_now).

#include <cstdint>

namespace oa::ui::services {

// Clock boundary: host millisecond tick count (wrapping at 32 bits) and sleep.
struct Clock {
    void* context{};
    uint32_t (*tick_ms)(void* context){};
    void (*sleep_ms)(void* context, uint32_t milliseconds){};
};

/// Returns the clock boundary backed by the platform tick count and sleep.
///
/// @return the boundary; its context is null
[[nodiscard]] Clock host_clock() noexcept;

struct EngineClock {
    Clock clock;
    uint32_t rate{}; // engine ticks per 1000 ms, kept in the display context
};

/// Returns the current engine tick.
///
/// @param clock engine clock
/// @return (milliseconds * rate) / 1000 with a wrapping 32-bit product
[[nodiscard]] uint32_t clock_now(const EngineClock* clock) noexcept;

inline constexpr int timer_slot_count = 10;
// An interval below zero marks a free slot.
inline constexpr int32_t timer_slot_free = -1;

using TimerCallback = void (*)(void* argument);

// One slot of the ten-slot timer table.
struct TimerSlot {
    TimerCallback callback;
    void* argument{};    // passed to callback
    int32_t interval{};  // engine ticks; reload value, negative when the slot is free
    int32_t remaining{}; // engine ticks until callback runs
};

struct TimerTable {
    TimerSlot slots[timer_slot_count];
    // Successful adds since the last reset. It never decreases on removal, so
    // it only bounds which indices timers_remove accepts.
    int32_t added{};
    uint32_t last_tick{}; // engine tick of the previous poll
};

/// Stores a new clock rate and restarts all timers (see timers_reset).
///
/// @param[in,out] clock engine clock
/// @param[in,out] timers timer table that is reset
/// @param rate engine ticks per 1000 ms
void clock_set_rate(EngineClock* clock, TimerTable* timers, uint32_t rate) noexcept;
/// Returns the clock rate.
///
/// @param clock engine clock
/// @return engine ticks per 1000 ms
[[nodiscard]] uint32_t clock_rate(const EngineClock* clock) noexcept;

/// Polls the timers, running each one that has come due.
///
/// Charges the ticks elapsed since the previous poll to every live slot and
/// runs (then reloads) each one that reaches zero or below, in slot order.
///
/// @param[in,out] timers timer table
/// @param clock engine clock
/// @quirk The clock is read twice, once for the elapsed time and once for the
///        new baseline, so ticks between the two reads are never charged.
void timers_tick(TimerTable* timers, const EngineClock* clock) noexcept;
/// Polls the timers, then claims the lowest free slot.
///
/// @param[in,out] timers timer table
/// @param clock engine clock
/// @param interval engine ticks between calls; also the first delay
/// @param argument value passed to callback
/// @param callback function run when the timer comes due
/// @return the slot index, or -1 when every slot is taken
int32_t timers_add(
    TimerTable* timers,
    const EngineClock* clock,
    int32_t interval,
    void* argument,
    TimerCallback callback
) noexcept;
/// Frees a timer slot.
///
/// @param[in,out] timers timer table
/// @param index slot index; accepted when 0 <= index < added
/// @return whether the slot was freed
bool timers_remove(TimerTable* timers, int32_t index) noexcept;
/// Frees every slot, zeroes the add count and takes a new baseline.
///
/// @param[out] timers timer table
/// @param clock engine clock read for the baseline
void timers_reset(TimerTable* timers, const EngineClock* clock) noexcept;

// Frames-per-second counter kept in the display context.
struct FrameRate {
    int32_t accumulated_ms{}; // milliseconds counted toward the current second
    uint32_t last_tick_ms{};  // host millisecond tick count of the previous frame
    int32_t frames{};         // frames counted toward the current second
    int32_t rate{};           // frames counted over the last full second
};

/// Counts one frame.
///
/// A backlog above two seconds is clamped to one; once more than a second has
/// accumulated the frame count becomes the published rate and a second is
/// subtracted.
///
/// @param[in,out] rate frame-rate counter
/// @param now_ms host millisecond tick count
void frame_rate_update(FrameRate* rate, uint32_t now_ms) noexcept;
/// Counts one frame at the current host time and returns the published rate.
///
/// @param[in,out] rate frame-rate counter
/// @param clock host clock read for the time
/// @return frames counted over the last full second
int32_t frame_rate_sample(FrameRate* rate, const Clock* clock) noexcept;

} // namespace oa::ui::services
