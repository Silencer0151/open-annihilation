// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/services/timers.hpp"

#include "oa/base/game_loop.hpp"
#include "oa/platform/system.hpp"

#include <cstdint>

namespace oa::ui::services {
namespace {

uint32_t host_tick(void*) {
    return platform::tick_ms();
}

void host_sleep(void*, uint32_t milliseconds) {
    platform::sleep_ms(milliseconds);
}

constexpr int32_t frame_rate_window_ms = 1000;
constexpr int32_t frame_rate_backlog_limit_ms = 2000;

} // namespace

Clock host_clock() noexcept {
    return {nullptr, host_tick, host_sleep};
}

uint32_t clock_now(const EngineClock* clock) noexcept {
    return base::game_loop::scaled_clock(clock->clock.tick_ms(clock->clock.context), clock->rate);
}

void clock_set_rate(EngineClock* clock, TimerTable* timers, uint32_t rate) noexcept {
    clock->rate = rate;
    timers_reset(timers, clock);
}

uint32_t clock_rate(const EngineClock* clock) noexcept {
    return clock->rate;
}

void timers_tick(TimerTable* timers, const EngineClock* clock) noexcept {
    const int32_t elapsed = static_cast<int32_t>(clock_now(clock) - timers->last_tick);
    timers->last_tick = clock_now(clock);
    for (TimerSlot& slot : timers->slots) {
        if (slot.interval < 0) {
            continue;
        }
        slot.remaining = static_cast<int32_t>(
            static_cast<uint32_t>(slot.remaining) - static_cast<uint32_t>(elapsed)
        );
        if (slot.remaining < 1) {
            slot.callback(slot.argument);
            slot.remaining = slot.interval;
        }
    }
}

int32_t timers_add(
    TimerTable* timers,
    const EngineClock* clock,
    int32_t interval,
    void* argument,
    TimerCallback callback
) noexcept {
    timers_tick(timers, clock);
    for (int32_t index = 0; index < timer_slot_count; ++index) {
        TimerSlot& slot = timers->slots[index];
        if (slot.interval < 0) {
            ++timers->added;
            slot.callback = callback;
            slot.argument = argument;
            slot.interval = interval;
            slot.remaining = interval;
            return index;
        }
    }
    return -1;
}

bool timers_remove(TimerTable* timers, int32_t index) noexcept {
    if (index < timers->added && index >= 0) {
        timers->slots[index].interval = timer_slot_free;
        return true;
    }
    return false;
}

void timers_reset(TimerTable* timers, const EngineClock* clock) noexcept {
    timers->added = 0;
    for (TimerSlot& slot : timers->slots) {
        slot.interval = timer_slot_free;
    }
    timers->last_tick = clock_now(clock);
}

void frame_rate_update(FrameRate* rate, uint32_t now_ms) noexcept {
    const uint32_t previous = rate->last_tick_ms;
    rate->last_tick_ms = now_ms;
    rate->accumulated_ms =
        static_cast<int32_t>(static_cast<uint32_t>(rate->accumulated_ms) + (now_ms - previous));
    rate->frames += 1;
    const int32_t counted = rate->frames;
    if (rate->accumulated_ms > frame_rate_backlog_limit_ms) {
        rate->accumulated_ms = frame_rate_window_ms;
    }
    if (rate->accumulated_ms > frame_rate_window_ms) {
        rate->frames = 0;
        rate->accumulated_ms -= frame_rate_window_ms;
        rate->rate = counted;
    }
}

int32_t frame_rate_sample(FrameRate* rate, const Clock* clock) noexcept {
    frame_rate_update(rate, clock->tick_ms(clock->context));
    return rate->rate;
}

} // namespace oa::ui::services
