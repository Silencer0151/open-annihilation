// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Network play's traffic readouts over the battlefield (traffic_overlay.hpp).
#include "traffic_overlay.hpp"

#include "oa/core/game_state.h"
#include "oa/netgame/match/net_match.hpp"
#include "oa/present/world_renderer/world_overlays.hpp"
#include "oa/ui/console/game_fields.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace oa::app {

namespace {

namespace wr = oa::present::world_renderer;

// Where the readouts sit, in the game's 640x480 pixels. The battlefield
// starts 128 pixels from the screen's left edge and 32 from its top, and the
// bottom bar under it is 32 high.
constexpr int kBattlefieldLeft = 0x80;
constexpr int kBattlefieldTop = 0x20;
constexpr int kBottomBarHeight = 0x20;
constexpr int kLabelLeft = 0x81;
constexpr int kLabelInset = kLabelLeft - kBattlefieldLeft;
constexpr int kLabelRise = 0x5f - kBottomBarHeight; // the first label's top above the bottom bar
constexpr int kBarWidth = 0xc1 - kLabelLeft; // the bar's right edge less its left, both drawn
constexpr int kBarHeight = 8;                // its bottom edge less its top, both drawn
constexpr int kDebugLineInset = 0xbc - kBattlefieldLeft;
// The debug line is the second line of the debug readout, whose first line
// starts 10 pixels above three font heights from the screen's top: its top
// is four font heights less 10 pixels down.
constexpr int kDebugLineHeights = 4;
constexpr int kDebugLineRise = 10;
// A bar is full at this rate, in bytes a second.
constexpr uint32_t kBarFullRate = 0x15e0;
constexpr uint32_t kPercent = 100;
// Label text: the palette's white.
constexpr uint8_t kLabelColor = 0xff;
constexpr std::size_t kLabelBytes = 32;
constexpr double kBytesToKilobytes = 0.001;

// A raster that draws the bars of wr::overlay_traffic_bar through the
// overlay's fill_rect: its rectangles are inclusive, in 640x480 pixels from
// origin.
struct ScaledRaster {
    const MatchOverlay* overlay{};
    int origin_x{};
    int origin_y{};
};

/// Fills an inclusive rectangle of 640x480 pixels from the raster's origin.
///
/// @param raster the overlay and origin
/// @param rect the rectangle, corners included
/// @param color palette colour
void fill_scaled(const ScaledRaster& raster, const Rect32& rect, uint8_t color) {
    const auto& overlay = *raster.overlay;
    const int scale = std::max(overlay.scale, 1);
    overlay.fill_rect(
        overlay.painter,
        raster.origin_x + rect.x1 * scale,
        raster.origin_y + rect.y1 * scale,
        (rect.x2 - rect.x1 + 1) * scale,
        (rect.y2 - rect.y1 + 1) * scale,
        color
    );
}

/// Returns an OverlayRaster whose outlines and fills go to the overlay.
///
/// @param raster the overlay and origin; must outlive the returned raster's use
/// @return the raster
wr::OverlayRaster overlay_raster(ScaledRaster& raster) {
    wr::OverlayRaster bars{};
    bars.user = &raster;
    bars.rect_outline = [](void* user, oa::Surface*, const Rect32& rect, uint8_t color) {
        const auto& scaled = *static_cast<const ScaledRaster*>(user);
        fill_scaled(scaled, {rect.x1, rect.y1, rect.x2, rect.y1}, color);
        fill_scaled(scaled, {rect.x2, rect.y1, rect.x2, rect.y2}, color);
        fill_scaled(scaled, {rect.x1, rect.y2, rect.x2, rect.y2}, color);
        fill_scaled(scaled, {rect.x1, rect.y1, rect.x1, rect.y2}, color);
    };
    bars.fill_rect = [](void* user, oa::Surface*, const Rect32& rect, uint8_t color) {
        fill_scaled(*static_cast<const ScaledRaster*>(user), rect, color);
    };
    return bars;
}

} // namespace

void draw_traffic_overlay(
    const MatchOverlay& overlay, oa::netgame::TrafficStats* traffic, uint32_t now_time
) {
    if (overlay.game == nullptr || overlay.font_height == nullptr || overlay.draw_text == nullptr ||
        overlay.fill_rect == nullptr)
        return;
    namespace outcome_flag = oa::ui::console::outcome_flag;
    const oa::Game& game = *overlay.game;
    const int scale = std::max(overlay.scale, 1);
    const uint8_t bar_color = game.ui_colors[wr::ui_color_traffic_bar];
    if (game.show_bandwidth != 0) {
        uint32_t rates[2]{};
        if (traffic != nullptr)
            oa::netgame::traffic_stats_rates(traffic, now_time, &rates[0], &rates[1]);
        constexpr uint16_t kDebugReadout = outcome_flag::debug_keys | outcome_flag::debug_toggle_i;
        const auto font = (game.outcome_flags & kDebugReadout) == kDebugReadout
                              ? OverlayFont::message_log
                              : OverlayFont::side_panel;
        const int font_height = overlay.font_height(overlay.painter, font);
        ScaledRaster raster{&overlay, overlay.left, overlay.bottom};
        const auto bars = overlay_raster(raster);
        const char* formats[2] = {"Send - %1.1f K/s", "Receive - %1.1f K/s"};
        int y = -kLabelRise;
        for (int i = 0; i < 2; ++i) {
            char label[kLabelBytes];
            std::snprintf(
                label, sizeof label, formats[i], static_cast<double>(rates[i]) * kBytesToKilobytes
            );
            overlay.draw_text(
                overlay.painter,
                font,
                overlay.left + kLabelInset * scale,
                overlay.bottom + y * scale,
                label,
                kLabelColor
            );
            y += font_height;
            // The rate times 100 keeps its low 32 bits before the division.
            const uint32_t percent = rates[i] * kPercent / kBarFullRate;
            wr::overlay_traffic_bar(
                game,
                bars,
                nullptr,
                {kLabelInset, y, kLabelInset + kBarWidth, y + kBarHeight},
                static_cast<int32_t>(percent)
            );
            y += kBarHeight + 1;
        }
    }
    if ((game.outcome_flags & outcome_flag::debug_keys) != 0 &&
        (game.session_flags & oa::netgame::match::kNetFlagLive) != 0 && traffic != nullptr) {
        char line[oa::netgame::traffic_debug_line_bytes];
        oa::netgame::traffic_stats_debug_line(traffic, now_time, line, sizeof line);
        if (auto* newline = std::strchr(line, '\n'))
            *newline = '\0';
        const int font_height = overlay.font_height(overlay.painter, OverlayFont::message_log);
        const int y = kDebugLineHeights * font_height - kDebugLineRise - kBattlefieldTop;
        overlay.draw_text(
            overlay.painter,
            OverlayFont::message_log,
            overlay.left + kDebugLineInset * scale,
            overlay.top + y * scale,
            line,
            bar_color
        );
    }
}

} // namespace oa::app
