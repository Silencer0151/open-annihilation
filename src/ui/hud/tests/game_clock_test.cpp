// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"

#include "oa/ui/hud/game_clock.hpp"
#include "oa/ui/hud/status_panel.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <tuple>

using namespace oa;
using namespace oa::ui::hud;

namespace {

// The clock writes in COMIX while the message log shows lines, empty or not,
// and in the side's font once the log is turned off.
void picks_the_font_the_log_leaves() {
    auto game = std::make_unique<Game>();
    set_message_lines(*game, 4);
    CHECK(clock_font(*game) == ClockFont::message_log);
    set_message_lines(*game, 1);
    CHECK(clock_font(*game) == ClockFont::message_log);
    set_message_lines(*game, 0);
    CHECK(clock_font(*game) == ClockFont::side_panel);
}

// On the 640x480 screen the pen sits at column 130 and, for COMIX's 14-row
// header height, row 432: two rows above the bottom bar's top at 448 once
// the font's height is added. CONSOLE's 11 rows put it at 435.
void sits_above_the_bottom_bar() {
    CHECK(kClockLeft == 130);
    CHECK(clock_pen_row(480, 14) == 432);
    CHECK(clock_pen_row(480, 11) == 435);
    CHECK(clock_pen_row(480, 14) + 14 + 2 == 480 - 32);
    CHECK(kClockColorSlot == 15);
}

const char* french(void*, const char* text) {
    return std::strcmp(text, "Game Time") == 0 ? "Temps de jeu" : nullptr;
}

// The clock's line is the strip's: hours, minutes and seconds of the 30 Hz
// tick, two digits each, the label translated.
void formats_the_game_time() {
    auto game = std::make_unique<Game>();
    game->tick = 1 * 108000 + 59 * 1800 + 7 * 30 + 12;
    char line[64];
    format_game_time(*game, nullptr, nullptr, line, sizeof line);
    CHECK(std::strcmp(line, "Game Time : 01:59:07") == 0);
    format_game_time(*game, french, nullptr, line, sizeof line);
    CHECK(std::strcmp(line, "Temps de jeu : 01:59:07") == 0);
    char cut[10];
    format_game_time(*game, nullptr, nullptr, cut, sizeof cut);
    CHECK(std::strcmp(cut, "Game Time") == 0);
}

// Space held hides the clock from its first frame, before the strip has
// moved; let go, the clock stays hidden while the strip or the board closes,
// starts to fade in on the first frame after they have closed and shows
// whole kClockFadeInMs later. Space held again mid-fade hides it at once.
void gives_way_to_the_status_strip() {
    ClockFade fade{};
    CHECK(step_clock_fade(fade, false, true, 1000) == kClockOpaque);
    CHECK(step_clock_fade(fade, true, true, 1001) == 0);
    CHECK(fade.showing == ClockShowing::hidden);
    CHECK(step_clock_fade(fade, true, false, 1100) == 0);

    CHECK(step_clock_fade(fade, false, false, 1200) == 0);
    CHECK(step_clock_fade(fade, false, false, 9000) == 0);
    CHECK(fade.showing == ClockShowing::hidden);

    CHECK(step_clock_fade(fade, false, true, 9015) == 0);
    CHECK(fade.showing == ClockShowing::fading && fade.fade_start_ms == 9015);
    CHECK(step_clock_fade(fade, false, true, 9015 + kClockFadeInMs / 2) == kClockOpaque / 2);

    CHECK(step_clock_fade(fade, false, true, 9015 + kClockFadeInMs) == kClockOpaque);
    CHECK(fade.showing == ClockShowing::shown);

    CHECK(step_clock_fade(fade, true, true, 10000) == 0);
    CHECK(step_clock_fade(fade, false, true, 10100) == 0);
    CHECK(step_clock_fade(fade, false, true, 10150) == 50 * kClockOpaque / kClockFadeInMs);
    CHECK(step_clock_fade(fade, true, true, 10100 + kClockFadeInMs / 2) == 0);
    CHECK(fade.showing == ClockShowing::hidden);
}

// The fade runs on time alone: stepped every millisecond or once, it stands
// at the same opacity at the same moment, across the clock's wrap too.
void fades_by_time_not_frames() {
    constexpr uint32_t start = UINT32_MAX - 50;
    ClockFade every{ClockShowing::hidden, 0};
    ClockFade once{ClockShowing::hidden, 0};
    std::ignore = step_clock_fade(every, false, true, start);
    std::ignore = step_clock_fade(once, false, true, start);
    uint32_t stepped = 0;
    for (uint32_t ms = 1; ms <= 100; ++ms)
        stepped = step_clock_fade(every, false, true, start + ms);
    CHECK(stepped == step_clock_fade(once, false, true, start + 100));
    CHECK(stepped == 100 * kClockOpaque / kClockFadeInMs);
}

} // namespace

int main() {
    picks_the_font_the_log_leaves();
    sits_above_the_bottom_bar();
    formats_the_game_time();
    gives_way_to_the_status_strip();
    fades_by_time_not_frames();
    return 0;
}
