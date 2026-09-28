// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// When the match clock steps: every combination of a shared match, an open
// menu and a finished match, and the pause bit of Game.sim_run_flags taken
// into the clock's run flags, whose clock then offers no step.
#include "match_clock.hpp"

#include "oa/base/game_loop.hpp"

#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

void test_single_machine_match_holds() {
    using oa::app::match_clock_runs;
    CHECK(match_clock_runs(false, false, false));
    CHECK(!match_clock_runs(false, true, false));
    CHECK(!match_clock_runs(false, false, true));
    CHECK(!match_clock_runs(false, true, true));
}

void test_shared_match_runs_on() {
    using oa::app::match_clock_runs;
    CHECK(match_clock_runs(true, false, false));
    CHECK(match_clock_runs(true, true, false));
    CHECK(match_clock_runs(true, false, true));
    CHECK(match_clock_runs(true, true, true));
}

void test_pause_bit_is_taken() {
    using oa::app::clock_flags_with_pause;
    CHECK(clock_flags_with_pause(0x0000, 0x0001) == 0x0001);
    CHECK(clock_flags_with_pause(0x0001, 0x0000) == 0x0000);
    // The clock's own bits stay; the match's other bits do not come across.
    CHECK(clock_flags_with_pause(0x0006, 0x00f1) == 0x0007);
    CHECK(clock_flags_with_pause(0x0007, 0x00f0) == 0x0006);
}

void test_paused_clock_offers_no_step() {
    oa::base::game_loop::Timing timing{};
    timing.requested_rate = 10;
    timing.actual_rate = 10;
    oa::base::game_loop::update_timing(timing, 1000);
    // Paused: a second of clock units offers nothing, and the time moves on.
    timing.flags = oa::app::clock_flags_with_pause(timing.flags, 0x0001);
    oa::base::game_loop::update_timing(timing, 1030);
    CHECK(timing.pending_steps == 0);
    CHECK(timing.previous_clock == 1030);
    // Resumed: the next frame steps for its own time only.
    timing.flags = oa::app::clock_flags_with_pause(timing.flags, 0x0000);
    oa::base::game_loop::update_timing(timing, 1031);
    CHECK(timing.pending_steps == 1);
}

} // namespace

int main() {
    test_single_machine_match_holds();
    test_shared_match_runs_on();
    test_pause_bit_is_taken();
    test_paused_clock_offers_no_step();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
