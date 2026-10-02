// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"

#include "oa/ui/hud/game_clock.hpp"
#include "oa/ui/hud/status_panel.hpp"

#include <cstring>
#include <memory>

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

} // namespace

int main() {
    picks_the_font_the_log_leaves();
    sits_above_the_bottom_bar();
    formats_the_game_time();
    return 0;
}
