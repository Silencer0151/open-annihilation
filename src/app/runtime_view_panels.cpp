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

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

namespace oa::app {
namespace {

namespace hud = oa::ui::hud;

/// Palette index of the top bar line's labels, the wind's range and the
/// time: a light grey.
constexpr uint8_t kClockLineGrey = 0x55;

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
    // Held on the map, so that a view past its edges is shared at the edge.
    const auto camera = on_map_camera();
    return std::array<int32_t, 2>{
        camera[0] + visible_map_width() / 2, camera[1] + visible_map_height() / 2
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

void Runtime::draw_clock_line(PaintLayer layer) {
    namespace layout = oa::ui::display_layout;
    if (!match_ || !ui_rules().resource_panel.enabled)
        return;
    const auto* font = match_label_font();
    const auto& world = match_->state();
    const auto& game = world.game;
    const auto& wind = match_->environment_wind();
    const auto readout = hud::wind_readout(
        wind.strength,
        wind.minimum_strength,
        wind.maximum_strength,
        wind.strength_divisor,
        wind_generator_output(world)
    );
    const bool watching = local_player_watches();
    char time_line[64];
    char wind_line[64];
    char tidal_line[64];
    hud::format_game_time(time_line, sizeof time_line, game.tick);
    hud::format_wind(wind_line, sizeof wind_line, readout, watching);
    hud::format_tidal(tidal_line, sizeof tidal_line, game.tidal_strength);
    const auto width = [&](std::string_view text) {
        return font != nullptr ? match_text_width(*font, text, 1) : 0;
    };
    // Where the line goes is decided at its widest figures, so that it
    // stays put while they change: the wind at its most, and the time at
    // 00:00:00.
    char widest_time[64];
    char widest_wind[64];
    hud::format_game_time(widest_time, sizeof widest_time, 0);
    hud::format_wind(
        widest_wind,
        sizeof widest_wind,
        hud::WindReadout{readout.maximum, readout.minimum, readout.maximum},
        watching
    );
    const auto wind_parts = hud::split_clock_line(wind_line);
    const auto tidal_parts = hud::split_clock_line(tidal_line);
    const auto time_parts = hud::split_clock_line(time_line);
    const auto widest_time_parts = hud::split_clock_line(widest_time);
    // A signed amount's figures sit as near its sign as they sit to each
    // other: they move left by the columns the sign leaves blank on its
    // right past those a figure does.
    const auto sign_pull = [&](std::string_view amount) {
        if (font == nullptr || amount.size() < 2 || amount.front() != '+')
            return 0;
        return std::max(
            0,
            match_text_margins(*font, amount[0]).right - match_text_margins(*font, amount[1]).right
        );
    };
    const auto amount_width = [&](std::string_view amount) {
        return width(amount) - sign_pull(amount);
    };
    const auto figures = [&](const hud::ClockLineParts& parts) {
        return amount_width(parts.amount) + width(parts.rest);
    };
    // "Game Time" alone, without the " : " that ends its label.
    constexpr std::string_view kLabelEnd = " : ";
    auto time_label = time_parts.label;
    if (time_label.ends_with(kLabelEnd))
        time_label.remove_suffix(kLabelEnd.size());
    // The touch controls' layout shows no top bar past the interface's
    // columns.
    const auto place = layout::placed_mode(match_layout_) || font == nullptr
                           ? hud::ClockLinePlace{}
                           : hud::place_clock_line(
                                 top_bar_pieces_,
                                 layout::kSourceLeft + match_layout_.bar_columns(),
                                 hud::ClockLineWidths{
                                     std::max(width(wind_parts.label), width(tidal_parts.label)),
                                     std::max(
                                         {figures(wind_parts),
                                          figures(hud::split_clock_line(widest_wind)),
                                          figures(tidal_parts)}
                                     ),
                                     std::max(width(time_line), width(widest_time)),
                                     width(time_label),
                                     std::max(width(time_parts.rest), width(widest_time_parts.rest))
                                 },
                                 match_layout_.width
                             );
    if (place.spot != hud::ClockLineSpot::battlefield) {
        if (layer != PaintLayer::hud)
            return;
        const auto green = hud::readout_color(game, hud::kReadoutProducedColor);
        // The label ends at the figures, which follow in their colours.
        const auto line = [&](int y, const hud::ClockLineParts& parts) {
            const int x = place.figures_x;
            draw_hud_label(x - width(parts.label), y, parts.label, kClockLineGrey);
            if (const int pull = sign_pull(parts.amount); pull > 0) {
                // The figures start where the whole amount puts them, less
                // the pull.
                const auto digits = parts.amount.substr(1);
                draw_hud_label(x, y, parts.amount.substr(0, 1), green);
                draw_hud_label(x + width(parts.amount) - width(digits) - pull, y, digits, green);
            } else {
                draw_hud_label(x, y, parts.amount, green);
            }
            draw_hud_label(x + amount_width(parts.amount), y, parts.rest, kClockLineGrey);
        };
        line(hud::kClockLineWindY, wind_parts);
        line(hud::kClockLineTidalY, tidal_parts);
        if (place.spot == hud::ClockLineSpot::sections) {
            draw_hud_label(place.time_x, hud::kClockLineTimeY, time_line, kClockLineGrey);
        } else {
            draw_hud_label(place.time_x, hud::kClockLineWindY, time_label, kClockLineGrey);
            draw_hud_label(place.time_x, hud::kClockLineTidalY, time_parts.rest, kClockLineGrey);
        }
        return;
    }
    if (layer != PaintLayer::battlefield)
        return;
    // Its lines step by the game font's height.
    const PanelText panel(*this);
    const auto text_color = hud::readout_color(game, hud::kReadoutTextColor);
    // At the top left of the overlays' area (the battlefield, or with the
    // touch controls on, the part of it they leave clear), as far from its
    // corner as from the battlefield's; painted on the battlefield's layer.
    const auto area = overlay_area();
    const auto label = [&](int y, const char* text) {
        const auto point =
            layout::placed_mode(match_layout_)
                ? layout::source_battlefield_to_canvas(match_layout_, hud::kClockLineLeft, y)
                : layout::source_to_canvas(match_layout_, hud::kClockLineLeft, y);
        const auto at = canvas_paint(
            point.x + area.x - match_layout_.battlefield_x(),
            point.y + area.y - match_layout_.battlefield_y()
        );
        draw_match_label(at.x, at.y, text, text_color);
    };
    label(hud::kClockLineTop, time_line);
    label(hud::kClockLineTop + hud::kClockLineStep, wind_line);
    label(hud::kClockLineTop + 2 * hud::kClockLineStep, tidal_line);
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
