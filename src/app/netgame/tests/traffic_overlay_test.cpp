// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The traffic readouts over the battlefield, drawn through a painter that
// records each call: the "BPS" labels and bars where the game places them,
// at one and two painter pixels to the game's pixel, the rates of a skirmish,
// the full bar of the first sample after a reset, the fonts, and the debug
// keys' traffic line.
#include "traffic_overlay.hpp"

#include "oa/core/game_state.h"
#include "oa/netgame/match/net_match.hpp"
#include "oa/ui/console/game_fields.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace outcome_flag = oa::ui::console::outcome_flag;
using oa::app::MatchOverlay;
using oa::app::OverlayFont;

int g_failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);          \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

// The side panel's and the message log's font heights in the game's data.
constexpr uint8_t kSidePanelHeight = 11;
constexpr uint8_t kMessageLogHeight = 14;
// The battlefield at 640x480 on its own layer: it starts at the layer's
// corner and ends at the bottom bar, 32 + 32 pixels short of the screen.
constexpr int kBottom = 480 - 32 - 32;
// UI colour 15, the bars' and the debug line's.
constexpr uint8_t kBarColor = 0xd0;
constexpr uint8_t kWhite = 0xff;

struct Text {
    OverlayFont font{};
    int x{};
    int y{};
    std::string text;
    uint8_t color{};
};

struct Fill {
    int x{};
    int y{};
    int width{};
    int height{};
    uint8_t color{};

    bool operator==(const Fill&) const = default;
};

struct Painter {
    std::vector<Text> texts;
    std::vector<Fill> fills;
};

/// Returns an overlay over a recording painter.
///
/// @param painter the painter the calls are recorded in
/// @param game the match's Game block
/// @param scale painter pixels to one 640x480 pixel
/// @return the overlay, its battlefield at the painter's corner
MatchOverlay recording_overlay(Painter& painter, const oa::Game& game, int scale) {
    MatchOverlay overlay{};
    overlay.painter = &painter;
    overlay.game = &game;
    overlay.left = 0;
    overlay.top = 0;
    overlay.bottom = kBottom * scale;
    overlay.scale = scale;
    overlay.font_height = [](void*, OverlayFont font) -> uint8_t {
        return font == OverlayFont::message_log ? kMessageLogHeight : kSidePanelHeight;
    };
    overlay.draw_text =
        [](void* user, OverlayFont font, int x, int y, const char* text, uint8_t color) {
            static_cast<Painter*>(user)->texts.push_back({font, x, y, text, color});
        };
    overlay.fill_rect = [](void* user, int x, int y, int width, int height, uint8_t color) {
        static_cast<Painter*>(user)->fills.push_back({x, y, width, height, color});
    };
    return overlay;
}

/// Returns a zeroed Game block with "BPS" on and UI colour 15 set.
///
/// @return the block
std::unique_ptr<oa::Game> bps_game() {
    auto game = std::make_unique<oa::Game>();
    game->show_bandwidth = 1;
    game->ui_colors[15] = kBarColor;
    return game;
}

/// Returns whether a painter drew a bar's outline: its four edges, each one
/// pixel of the game's thick, around the 65 by 9 pixels at x, y.
///
/// @param painter the recording painter
/// @param first the index of the outline's first fill
/// @param x the bar's left edge, in painter pixels
/// @param y its top edge
/// @param scale painter pixels to one 640x480 pixel
/// @return whether the four fills from first are the outline
bool outlined(const Painter& painter, std::size_t first, int x, int y, int scale) {
    if (painter.fills.size() < first + 4)
        return false;
    const Fill top{x, y, 65 * scale, scale, kBarColor};
    const Fill right{x + 64 * scale, y, scale, 9 * scale, kBarColor};
    const Fill bottom{x, y + 8 * scale, 65 * scale, scale, kBarColor};
    const Fill left{x, y, scale, 9 * scale, kBarColor};
    return painter.fills[first] == top && painter.fills[first + 1] == right &&
           painter.fills[first + 2] == bottom && painter.fills[first + 3] == left;
}

// At 640x480 the labels sit at 385 and 405 on the screen and the bars at 396
// and 416, the first rate half of 5600 bytes a second, the second 0.
void test_labels_and_bars() {
    auto game = bps_game();
    Painter painter;
    oa::netgame::TrafficStats traffic{};
    traffic.sent_bytes = 5600;
    oa::app::draw_traffic_overlay(recording_overlay(painter, *game, 1), &traffic, 60);
    CHECK(painter.texts.size() == 2);
    CHECK(painter.fills.size() == 9);
    if (painter.texts.size() != 2 || painter.fills.size() != 9)
        return;
    const auto& send = painter.texts[0];
    CHECK(send.text == "Send - 2.8 K/s");
    CHECK(send.font == OverlayFont::side_panel && send.color == kWhite);
    CHECK(send.x == 1 && send.y == 385 - 32);
    CHECK(outlined(painter, 0, 1, 396 - 32, 1));
    CHECK((painter.fills[4] == Fill{1, 396 - 32, 33, 9, kBarColor}));
    const auto& receive = painter.texts[1];
    CHECK(receive.text == "Receive - 0.0 K/s");
    CHECK(receive.font == OverlayFont::side_panel && receive.color == kWhite);
    CHECK(receive.x == 1 && receive.y == 405 - 32);
    CHECK(outlined(painter, 5, 1, 416 - 32, 1));
}

// At two painter pixels to the game's pixel everything is twice as far from
// the battlefield's edges and twice as large.
void test_scaled() {
    auto game = bps_game();
    Painter painter;
    oa::netgame::TrafficStats traffic{};
    traffic.received_bytes = 5600 * 2;
    oa::app::draw_traffic_overlay(recording_overlay(painter, *game, 2), &traffic, 60);
    CHECK(painter.texts.size() == 2 && painter.fills.size() == 9);
    if (painter.texts.size() != 2 || painter.fills.size() != 9)
        return;
    CHECK(painter.texts[0].text == "Send - 0.0 K/s");
    CHECK(painter.texts[0].x == 2 && painter.texts[0].y == 2 * (385 - 32));
    CHECK(outlined(painter, 0, 2, 2 * (396 - 32), 2));
    CHECK(painter.texts[1].text == "Receive - 5.6 K/s");
    CHECK(painter.texts[1].x == 2 && painter.texts[1].y == 2 * (405 - 32));
    CHECK(outlined(painter, 4, 2, 2 * (416 - 32), 2));
    CHECK((painter.fills[8] == Fill{2, 2 * (416 - 32), 2 * 65, 2 * 9, kBarColor}));
}

// Without a connection both rates read 0.0 K/s over empty bars, as in a
// skirmish; with "BPS" off nothing is drawn.
void test_skirmish_and_off() {
    auto game = bps_game();
    Painter painter;
    oa::app::draw_traffic_overlay(recording_overlay(painter, *game, 1), nullptr, 0);
    CHECK(painter.texts.size() == 2 && painter.fills.size() == 8);
    if (painter.texts.size() == 2) {
        CHECK(painter.texts[0].text == "Send - 0.0 K/s");
        CHECK(painter.texts[1].text == "Receive - 0.0 K/s");
    }
    game->show_bandwidth = 0;
    Painter off;
    oa::netgame::TrafficStats traffic{};
    traffic.sent_bytes = 5600;
    oa::app::draw_traffic_overlay(recording_overlay(off, *game, 1), &traffic, 60);
    CHECK(off.texts.empty() && off.fills.empty());
}

// The first sample after a reset takes the counters from their earlier
// sample, which wraps: the rate is huge and its bar full.
void test_rate_after_reset() {
    auto game = bps_game();
    Painter painter;
    oa::netgame::TrafficStats traffic{};
    traffic.rates.sampled_sent_bytes = 5600;
    oa::app::draw_traffic_overlay(recording_overlay(painter, *game, 1), &traffic, 60);
    CHECK(painter.fills.size() == 9);
    if (painter.fills.size() == 9)
        CHECK((painter.fills[4] == Fill{1, 396 - 32, 65, 9, kBarColor}));
}

// With the debug keys and the 'i' toggle both on, the labels are in the
// message log's font and step by its height.
void test_debug_readout_font() {
    auto game = bps_game();
    game->outcome_flags = outcome_flag::debug_keys | outcome_flag::debug_toggle_i;
    Painter painter;
    oa::app::draw_traffic_overlay(recording_overlay(painter, *game, 1), nullptr, 0);
    CHECK(painter.texts.size() == 2);
    if (painter.texts.size() != 2)
        return;
    CHECK(painter.texts[0].font == OverlayFont::message_log && painter.texts[0].y == 385 - 32);
    CHECK(painter.texts[1].font == OverlayFont::message_log);
    CHECK(painter.texts[1].y == 385 - 32 + 14 + 9);
    CHECK(outlined(painter, 0, 1, 385 - 32 + 14, 1));
}

// The debug keys show the traffic line in a live game only: in the message
// log's font and UI colour 15, at 188, 46 on the screen.
void test_debug_line() {
    auto game = std::make_unique<oa::Game>();
    game->ui_colors[15] = kBarColor;
    game->outcome_flags = outcome_flag::debug_keys;
    oa::netgame::TrafficStats traffic{};
    Painter offline;
    oa::app::draw_traffic_overlay(recording_overlay(offline, *game, 1), &traffic, 60);
    CHECK(offline.texts.empty() && offline.fills.empty());
    game->session_flags = oa::netgame::match::kNetFlagLive;
    Painter live;
    traffic.sent_bytes = 600;
    traffic.sent_datagrams = 2;
    oa::app::draw_traffic_overlay(recording_overlay(live, *game, 2), &traffic, 60);
    CHECK(live.texts.size() == 1 && live.fills.empty());
    if (live.texts.size() != 1)
        return;
    const auto& line = live.texts[0];
    CHECK(line.font == OverlayFont::message_log && line.color == kBarColor);
    CHECK(line.x == 2 * (188 - 128) && line.y == 2 * (46 - 32));
    CHECK(line.text == "pS=   0 pR=   0 (S=1/ 300, R=0/   0) C=  0%");
    Painter no_connection;
    oa::app::draw_traffic_overlay(recording_overlay(no_connection, *game, 1), nullptr, 0);
    CHECK(no_connection.texts.empty());
}

} // namespace

int main() {
    test_labels_and_bars();
    test_scaled();
    test_skirmish_and_off();
    test_rate_after_reset();
    test_debug_readout_font();
    test_debug_line();
    if (g_failures != 0) {
        std::fprintf(stderr, "traffic overlay: %d checks failed\n", g_failures);
        return 1;
    }
    std::printf("traffic overlay: labels, bars, scale, skirmish, reset, fonts and debug line\n");
    return 0;
}
