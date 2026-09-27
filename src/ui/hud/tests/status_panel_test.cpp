// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"

#include "oa/ui/hud/status_panel.hpp"

#include <cstring>
#include <memory>
#include <vector>

using namespace oa;
using namespace oa::ui::hud;

namespace {

// The strip steps every 15 ms: up a third of the remaining rise (at least
// one row) while held, down a third of the height once released.
void slides_in_thirds() {
    auto game = std::make_unique<Game>();
    uint32_t next_step = 0;
    std::vector<int32_t> offsets;
    uint32_t now = 100;
    while (status_panel_step(*game, next_step, now, true)) {
        offsets.push_back(game->status_panel_offset);
        CHECK(
            !status_panel_step(*game, next_step, now + kStatusPanelStepMs, true)
        ); // timer not yet past
        now += kStatusPanelStepMs + 1;
    }
    const std::vector<int32_t> rising{-10, -17, -21, -24, -26, -27, -28, -29, -30, -31};
    CHECK(offsets == rising);
    // The step that found the strip fully up still restarted the timer.
    CHECK(!status_panel_step(*game, next_step, now, false));
    now += kStatusPanelStepMs + 1;
    offsets.clear();
    while (status_panel_step(*game, next_step, now, false)) {
        offsets.push_back(game->status_panel_offset);
        now += kStatusPanelStepMs + 1;
    }
    const std::vector<int32_t> falling{-21, -14, -10, -7, -5, -4, -3, -2, -1, 0};
    CHECK(offsets == falling);
}

// The mission-start reset zeroes the readout's metal and energy storage, metal
// and energy, Game.status_panel_offset and Game.status_lightbar, and leaves the
// readout's rates and player alone.
void resets_at_mission_start() {
    auto game = std::make_unique<Game>();
    auto& readout = game->resource_readout;
    readout.player = 4;
    readout.energy = 900.0F;
    readout.energy_produced = 25.0F;
    readout.energy_requested = 3.0F;
    readout.metal = 400.0F;
    readout.metal_produced = 1.5F;
    readout.metal_requested = 0.5F;
    readout.energy_storage = 1000.0F;
    readout.metal_storage = 1000.0F;
    game->status_panel_offset = -31;
    game->status_lightbar = 7;
    reset_status_panel(*game);
    CHECK(readout.energy == 0.0F && readout.metal == 0.0F);
    CHECK(readout.energy_storage == 0.0F && readout.metal_storage == 0.0F);
    CHECK(game->status_panel_offset == 0 && game->status_lightbar == 0);
    CHECK(readout.player == 4 && readout.energy_produced == 25.0F);
    CHECK(readout.energy_requested == 3.0F);
    CHECK(readout.metal_produced == 1.5F && readout.metal_requested == 0.5F);
}

const char* upper(void*, const char* text) {
    return std::strcmp(text, "Normal") == 0 ? "NORMAL" : nullptr;
}

// Readouts follow the strip's formats: "%s : %02d:%02d:%02d",
// "%s : %d  (Max %d)", "%s %s" and " (%+d)".
void formats_readouts() {
    auto game = std::make_unique<Game>();
    game->tick = 2 * 108000 + 3 * 1800 + 4 * 30 + 29;
    game->local_player_index = 2;
    game->players[2].unit_count = 17;
    game->units_per_player = 250;
    game->current_speed = kNormalGameSpeed;
    game->requested_speed = kNormalGameSpeed;
    StatusPanelText text;
    format_status_panel(*game, upper, nullptr, text);
    CHECK(std::strcmp(text.time, "Game Time : 02:03:04") == 0);
    CHECK(std::strcmp(text.units, "Total Units : 17  (Max 250)") == 0);
    CHECK(std::strcmp(text.speed, "Game Speed NORMAL") == 0);
    game->current_speed = 7;
    game->requested_speed = 13;
    format_status_panel(*game, nullptr, nullptr, text);
    CHECK(std::strcmp(text.speed, "Game Speed -3 (+3)") == 0);
    game->current_speed = 12;
    game->requested_speed = kNormalGameSpeed;
    format_status_panel(*game, nullptr, nullptr, text);
    CHECK(std::strcmp(text.speed, "Game Speed +2 (+0)") == 0);
}

} // namespace

int main() {
    slides_in_thirds();
    resets_at_mission_start();
    formats_readouts();
    return 0;
}
