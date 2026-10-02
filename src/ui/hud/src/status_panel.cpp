// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/status_panel.hpp"

#include <cstdio>
#include <cstring>

namespace oa::ui::hud {
namespace {

constexpr uint32_t ticks_per_second = 30;
constexpr uint32_t ticks_per_minute = 60 * ticks_per_second;
constexpr uint32_t ticks_per_hour = 60 * ticks_per_minute;
constexpr int32_t step_fraction = 3;

const char* label(TranslateText translate, void* context, const char* text) noexcept {
    const char* translated = translate != nullptr ? translate(context, text) : nullptr;
    return translated != nullptr ? translated : text;
}

} // namespace

void reset_status_panel(Game& game) noexcept {
    game.resource_readout.metal_storage = 0.0F;
    game.resource_readout.energy_storage = 0.0F;
    game.resource_readout.metal = 0.0F;
    game.resource_readout.energy = 0.0F;
    game.status_panel_offset = 0;
    game.status_lightbar = 0;
}

bool status_panel_step(Game& game, uint32_t& next_step_ms, uint32_t now_ms, bool held) noexcept {
    if (static_cast<int32_t>(now_ms - next_step_ms) <= 0)
        return false;
    next_step_ms = now_ms + kStatusPanelStepMs;
    const int32_t offset = game.status_panel_offset;
    int32_t step = 0;
    if (held) {
        if (offset <= -kStatusPanelRise)
            return false;
        step = -((offset + kStatusPanelRise) / step_fraction);
        if (step > -1)
            step = -1;
    } else {
        if (offset >= 0)
            return false;
        step = offset / -step_fraction;
        if (step < 1)
            step = 1;
    }
    game.status_panel_offset = offset + step;
    return true;
}

void format_game_time(
    const Game& game, TranslateText translate, void* context, char* out, std::size_t bytes
) noexcept {
    const uint32_t within_hour = game.tick % ticks_per_hour;
    std::snprintf(
        out,
        bytes,
        "%s : %02d:%02d:%02d",
        label(translate, context, "Game Time"),
        static_cast<int>(game.tick / ticks_per_hour),
        static_cast<int>(within_hour / ticks_per_minute),
        static_cast<int>(within_hour % ticks_per_minute / ticks_per_second)
    );
}

void format_status_panel(
    const Game& game, TranslateText translate, void* context, StatusPanelText& out
) noexcept {
    format_game_time(game, translate, context, out.time, sizeof out.time);
    const uint16_t units = game.local_player_index < OA_PLAYER_COUNT
                               ? game.players[game.local_player_index].unit_count
                               : uint16_t{0};
    std::snprintf(
        out.units,
        sizeof out.units,
        "%s : %d  (Max %d)",
        label(translate, context, "Total Units"),
        static_cast<int>(units),
        static_cast<int>(game.units_per_player)
    );
    char speed[64];
    if (game.current_speed == kNormalGameSpeed)
        std::snprintf(speed, sizeof speed, "%s", label(translate, context, "Normal"));
    else
        std::snprintf(
            speed, sizeof speed, "%+d", static_cast<int>(game.current_speed) - kNormalGameSpeed
        );
    std::snprintf(
        out.speed, sizeof out.speed, "%s %s", label(translate, context, "Game Speed"), speed
    );
    if (game.current_speed != game.requested_speed) {
        const auto used = std::strlen(out.speed);
        std::snprintf(
            out.speed + used,
            sizeof out.speed - used,
            " (%+d)",
            static_cast<int>(game.requested_speed) - kNormalGameSpeed
        );
    }
}

} // namespace oa::ui::hud
