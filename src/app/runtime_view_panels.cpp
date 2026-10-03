// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Whose view a running match shows, and the panels a profile's visual rules
// add to it: the resource panel and the clock, wind and tidal line
// (ui.resource-panel), and the other players' camera rectangles on the
// minimap (ui.camera-sharing).

#include "oa/app/runtime.hpp"
#include "oa/sim/match_runtime.hpp"
#include "oa/ui/display_layout.hpp"
#include "oa/ui/hud/kill_board.hpp"
#include "oa/ui/hud/resource_bar.hpp"
#include "oa/ui/hud/resource_panel.hpp"
#include "oa/ui/hud/shared_views.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>

namespace oa::app {
namespace {

namespace hud = oa::ui::hud;

/// The line's place: the battlefield's top left, below the top panel.
constexpr int kClockLineX = oa::ui::display_layout::kSourceLeft + 2;
constexpr int kClockLineY = 34;
/// Rows between the line's three parts.
constexpr int kClockLineStep = 10;

/// A wind generator's output, from the unit type the game's own wind
/// generator names; kDefaultWindGenerator without it.
int32_t wind_generator_output(const oa::World& world) {
    for (uint32_t index = 0; index < world.unit_def_count; ++index) {
        const auto& def = world.unit_defs[index];
        if (std::strncmp(def.unit_name, "ARMWIN", sizeof def.unit_name) == 0) {
            const auto output = static_cast<int32_t>(def.wind_generator);
            return output != 0 ? output : hud::kDefaultWindGenerator;
        }
    }
    return hud::kDefaultWindGenerator;
}

} // namespace

void Runtime::draw_shared_camera_rectangles() {
    if (!match_ || !ui_rules().camera_sharing.enabled)
        return;
    const auto& world = match_->state();
    const auto& game = world.game;
    if (game.radar_width <= 0 || game.radar_height <= 0 || game.map_pixel_width <= 0 ||
        game.map_pixel_height <= 0)
        return;
    hud::MinimapFrame frame{};
    frame.left = game.radar_offset_x;
    frame.top = game.radar_offset_y;
    frame.width = game.radar_width;
    frame.height = game.radar_height;
    frame.map_width = game.map_pixel_width;
    frame.map_height = game.map_pixel_height;
    frame.view_width = visible_map_width() * game.radar_width / game.map_pixel_width;
    frame.view_height = visible_map_height() * game.radar_height / game.map_pixel_height;
    std::array<hud::CameraRectangle, OA_PLAYER_COUNT> rectangles{};
    const auto count =
        hud::camera_rectangles(world, shared_views_, local_player_watches(), frame, rectangles);
    for (uint32_t index = 0; index < count; ++index) {
        const auto& r = rectangles[index];
        const int width = r.right - r.left + 1;
        const int height = r.bottom - r.top + 1;
        fill_source_rect(r.left, r.top, width, 1, r.color);
        fill_source_rect(r.left, r.bottom, width, 1, r.color);
        fill_source_rect(r.left, r.top, 1, height, r.color);
        fill_source_rect(r.right, r.top, 1, height, r.color);
    }
}

std::optional<std::array<int32_t, 2>> Runtime::camera_centre() const {
    if (!match_)
        return std::nullopt;
    return std::array<int32_t, 2>{
        match_camera_x_ + visible_map_width() / 2, match_camera_z_ + visible_map_height() / 2
    };
}

void Runtime::take_reported_cameras(
    const std::array<oa::ui::hud::ReportedCamera, OA_PLAYER_COUNT>& reported
) {
    if (match_)
        hud::take_reported_cameras(match_->state(), reported, shared_views_);
}

void Runtime::follow_shared_cameras() {
    if (!match_)
        return;
    const auto slot = resource_panel_.locked_slot;
    if (slot == 0 || slot >= OA_PLAYER_COUNT || !ui_rules().resource_panel.enabled)
        return;
    if (shared_views_.camera_x[slot] == hud::kUnknownCamera)
        return;
    set_camera_position(
        shared_views_.camera_x[slot] - visible_map_width() / 2,
        shared_views_.camera_y[slot] - visible_map_height() / 2,
        0
    );
}

void Runtime::draw_resource_panel_overlay() {
    if (!match_ || !ui_rules().resource_panel.enabled || resource_panel_.hidden)
        return;
    // Its rows are laid out for the game's font.
    const PanelText panel(*this);
    const auto& world = match_->state();
    const auto rows = hud::resource_panel_rows(world, local_player_watches(), shared_views_);
    if (resource_panel_.x < 0)
        resource_panel_.x = oa::ui::display_layout::kSourceWidth - hud::kResourcePanelWidth;
    hud::ResourcePanelSink sink{};
    sink.user = this;
    sink.fill = [](void* user, int32_t x, int32_t y, int32_t width, int32_t height, uint8_t color) {
        static_cast<Runtime*>(user)->fill_source_rect(x, y, width, height, color);
    };
    sink.text = [](void* user, int32_t x, int32_t y, const char* text, uint8_t color) {
        static_cast<Runtime*>(user)->draw_hud_label(x, y, text, color);
    };
    hud::draw_resource_panel(world, resource_panel_, rows, sink);
}

void Runtime::draw_clock_line() {
    if (!match_ || !ui_rules().resource_panel.enabled)
        return;
    // Its lines step by the game font's height.
    const PanelText panel(*this);
    const auto& world = match_->state();
    const auto& game = world.game;
    const bool watching = local_player_watches();
    const auto text_color = oa::ui::hud::readout_color(game, oa::ui::hud::kReadoutTextColor);
    char line[64];
    hud::format_game_time(line, sizeof line, game.tick);
    draw_hud_label(kClockLineX, kClockLineY, line, text_color);
    const auto& wind = match_->environment_wind();
    hud::format_wind(
        line,
        sizeof line,
        hud::wind_readout(
            wind.strength,
            wind.minimum_strength,
            wind.maximum_strength,
            wind.strength_divisor,
            wind_generator_output(world)
        ),
        watching
    );
    draw_hud_label(kClockLineX, kClockLineY + kClockLineStep, line, text_color);
    hud::format_tidal(line, sizeof line, game.tidal_strength);
    draw_hud_label(kClockLineX, kClockLineY + 2 * kClockLineStep, line, text_color);
}

bool Runtime::resource_panel_pointer(const SDL_Event& event, float x, float y) {
    if (!match_ || screen_ != Screen::match || !ui_rules().resource_panel.enabled ||
        resource_panel_.hidden || resource_panel_.x < 0)
        return false;
    const auto point = hud_source_point(x, y);
    auto& panel = resource_panel_;
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        if (!panel.dragging)
            return false;
        panel.x += point.x - panel.drag_x;
        panel.y += point.y - panel.drag_y;
        panel.drag_x = point.x;
        panel.drag_y = point.y;
        return true;
    }
    if (event.button.button != SDL_BUTTON_LEFT)
        return false;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (!panel.dragging)
            return false;
        panel.dragging = false;
        return true;
    }
    const auto& world = match_->state();
    const bool watching = local_player_watches();
    const auto rows = hud::resource_panel_rows(world, watching, shared_views_);
    const auto hit = hud::resource_panel_hit(panel, rows, point.x, point.y);
    if (!hit.inside)
        return false;
    if (event.button.clicks >= 2 && event.button.clicks % 2 == 0) {
        const auto change = hud::resource_panel_view_switch(panel, rows, hit, watching);
        if (change != hud::ViewSwitch::none) {
            switch_watched_view(change);
            return true;
        }
    }
    panel.dragging = true;
    panel.drag_x = point.x;
    panel.drag_y = point.y;
    return true;
}

bool Runtime::resource_panel_f4_key() {
    if (!match_ || !ui_rules().resource_panel.enabled)
        return false;
    const auto& world = match_->state();
    const auto rows = hud::resource_panel_rows(world, local_player_watches(), shared_views_);
    return hud::resource_panel_f4(
        resource_panel_,
        (world.game.graphics_flags & hud::kGraphicsBoardPinned) != 0,
        hud::resource_panel_height(rows) != 0
    );
}

uint8_t Runtime::match_view_player() const noexcept {
    if (watched_player_ < OA_PLAYER_COUNT)
        return watched_player_;
    // "+View" moves the view to another player, as 3.1c's view follows
    // Game.viewpoint_player: its sight, units, economy and speech.
    if (match_) {
        const auto viewed = match_->state().game.viewpoint_player;
        if (viewed < OA_PLAYER_COUNT)
            return viewed;
    }
    return match_local_player_;
}

void Runtime::switch_watched_view(oa::ui::hud::ViewSwitch change) {
    if (!match_)
        return;
    auto& world = match_->state();
    switch (change) {
    case hud::ViewSwitch::none:
    case hud::ViewSwitch::lock_camera:
    case hud::ViewSwitch::unlock_camera:
        return;
    case hud::ViewSwitch::own_view:
        // The watcher's own view shows the whole map.
        watched_player_ = OA_PLAYER_COUNT;
        watched_sight_ = WatchedSight::whole_map;
        world.game.viewpoint_player = match_local_player_;
        break;
    case hud::ViewSwitch::view_player: {
        // A slot not in the game leaves the view as it was, but the sight
        // shows the whole map.
        const auto slot = resource_panel_.viewed_slot;
        const auto& player = world.game.players[slot < OA_PLAYER_COUNT ? slot : 0];
        const bool active = slot < OA_PLAYER_COUNT && player.in_use != 0 &&
                            player.index != OA_PLAYER_COUNT && player.status >= 1 &&
                            player.status <= 4;
        watched_sight_ = WatchedSight::whole_map;
        if (active) {
            watched_player_ = slot;
            watched_sight_ = WatchedSight::player;
            world.game.viewpoint_player = slot;
        }
        break;
    }
    }
    // A replay viewer without a slot watches with full radar in its own view
    // only.
    match_->set_slotless_view_switched(watched_player_ < OA_PLAYER_COUNT);
    // The selection is dropped and the panels and sight drawn again.
    clear_local_selection();
    apply_match_hud_for_selection();
    reset_sight_presentation(true);
}

} // namespace oa::app
