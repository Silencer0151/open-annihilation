// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/game_clock.hpp"

namespace oa::ui::hud {

uint32_t step_clock_fade(ClockFade& fade, bool held, bool closed, uint32_t now_ms) noexcept {
    if (held) {
        fade.showing = ClockShowing::hidden;
        return 0;
    }
    switch (fade.showing) {
    case ClockShowing::shown:
        return kClockOpaque;
    case ClockShowing::hidden:
        if (!closed)
            return 0;
        fade.showing = ClockShowing::fading;
        fade.fade_start_ms = now_ms;
        return 0;
    case ClockShowing::fading:
        break;
    }
    // The clock wraps after 49.7 days; the difference stays right across it.
    const uint32_t elapsed = now_ms - fade.fade_start_ms;
    if (elapsed >= kClockFadeInMs) {
        fade.showing = ClockShowing::shown;
        return kClockOpaque;
    }
    return elapsed * kClockOpaque / kClockFadeInMs;
}

} // namespace oa::ui::hud
