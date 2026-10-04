// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// setup.commander-warp in a running match: while the game is held at its
// start, a left press on the battlefield moves the local player's commander
// to the map point under it, and the Done button ends the placing; the
// prompt and the button show over the battlefield until then, and "Waiting
// for others to finish" after, until the game runs.

#include "oa/app/runtime.hpp"
#include "oa/sim/match_runtime.hpp"
#include "oa/ui/console/game_fields.hpp"
#include "oa/ui/display_layout.hpp"
#include "oa/ui/hud/resource_bar.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace oa::app {
namespace {

using oa::sim::match_runtime::CommanderPlacement;
using oa::ui::display_layout::kSourceWidth;

/// The prompt's left edge, from the screen's right edge, in 640x480 pixels.
constexpr int kPromptFromRight = 400;
/// The prompt's row.
constexpr int kPromptY = 100;
/// The Done button's left edge, from the screen's right edge.
constexpr int kDoneFromRight = 300;
constexpr int kDoneY = 130;
constexpr int kDoneWidth = 96;
constexpr int kDoneHeight = 20;
/// Where the label sits inside the button; a held button moves it one pixel.
constexpr int kDoneLabelInsetX = 3;
constexpr int kDoneLabelInsetY = 4;
/// The rows of the button that take a press, from its top.
constexpr int kDoneHitTop = 1;
constexpr int kDoneHitBottom = 19;
/// The button's edge and face.
constexpr uint8_t kDoneEdgeColor = 0xff;
constexpr uint8_t kDoneFaceColor = 0;

/// The fraction of a 16.16 word.
constexpr uint32_t kFractionMask = 0xffffu;

/// Fails the commander placement check unless a condition holds.
///
/// @param ok Condition that must hold.
/// @param what Failure text.
void require_placement(bool ok, std::string_view what) {
    if (!ok)
        throw std::runtime_error("commander placement check: " + std::string(what));
}

/// Tells whether a 640x480 point is on the Done button.
///
/// @param x column
/// @param y row
/// @return true inside its left and right edges and its pressable rows
bool on_done_button(int x, int y) {
    const int left = kSourceWidth - kDoneFromRight;
    return x > left && x < left + kDoneWidth && y >= kDoneY + kDoneHitTop &&
           y <= kDoneY + kDoneHitBottom;
}

/// Returns where a point of the 640x480 battlefield the prompt and the Done
/// button are laid out on lies on the canvas: where the battlefield's own
/// mapping puts it, moved by as much as the overlays' area's corner lies
/// from the battlefield's. Without the touch controls the area is the
/// battlefield, and the point is the battlefield's mapping alone; on a
/// phone, whose HUD shows in placed regions, the battlefield's mapping is
/// taken whatever region shows that part of the 640x480 screen.
///
/// @param layout the match layout
/// @param area the overlays' area (Runtime::overlay_area), canvas pixels
/// @param x source column
/// @param y source row
/// @return the point, canvas pixels
oa::ui::display_layout::Point overlay_point(
    const oa::ui::display_layout::MatchLayout& layout,
    const oa::ui::display_layout::Rect& area,
    int x,
    int y
) {
    namespace layout_space = oa::ui::display_layout;
    const auto point = layout_space::placed_mode(layout)
                           ? layout_space::source_battlefield_to_canvas(layout, x, y)
                           : layout_space::source_to_canvas(layout, x, y);
    return {point.x + area.x - layout.battlefield_x(), point.y + area.y - layout.battlefield_y()};
}

/// Returns the canvas rectangle a 640x480 rectangle of the battlefield
/// covers, both corners placed as overlay_point places them.
///
/// @param layout the match layout
/// @param area the overlays' area, canvas pixels
/// @param x source left edge
/// @param y source top edge
/// @param width source width
/// @param height source height
/// @return the rectangle, canvas pixels
oa::ui::display_layout::Rect overlay_rect(
    const oa::ui::display_layout::MatchLayout& layout,
    const oa::ui::display_layout::Rect& area,
    int x,
    int y,
    int width,
    int height
) {
    const auto origin = overlay_point(layout, area, x, y);
    const auto corner = overlay_point(layout, area, x + width, y + height);
    oa::ui::display_layout::Rect rect{};
    rect.x = origin.x;
    rect.y = origin.y;
    rect.width = corner.x - origin.x;
    rect.height = corner.y - origin.y;
    return rect;
}

/// Tells whether a canvas point is on the Done button where overlay_rect
/// places it: inside its left and right edges and its pressable rows, as
/// on_done_button tells for a 640x480 point.
///
/// @param layout the match layout
/// @param area the overlays' area, canvas pixels
/// @param x canvas column
/// @param y canvas row
/// @return true on the button's pressable part
bool on_placed_done_button(
    const oa::ui::display_layout::MatchLayout& layout,
    const oa::ui::display_layout::Rect& area,
    float x,
    float y
) {
    const auto pressable = overlay_rect(
        layout,
        area,
        kSourceWidth - kDoneFromRight + 1,
        kDoneY + kDoneHitTop,
        kDoneWidth - 1,
        kDoneHitBottom - kDoneHitTop + 1
    );
    return x >= static_cast<float>(pressable.x) && y >= static_cast<float>(pressable.y) &&
           x < static_cast<float>(pressable.x + pressable.width) &&
           y < static_cast<float>(pressable.y + pressable.height);
}

/// Tells whether a canvas point lies in an area.
///
/// @param area canvas rectangle
/// @param x canvas column
/// @param y canvas row
/// @return true inside its left and top edges and short of its right and bottom ones
bool area_holds(const oa::ui::display_layout::Rect& area, float x, float y) {
    return x >= static_cast<float>(area.x) && y >= static_cast<float>(area.y) &&
           x < static_cast<float>(area.x + area.width) &&
           y < static_cast<float>(area.y + area.height);
}

} // namespace

bool Runtime::commander_placement_pointer(const SDL_Event& event, float x, float y) {
    if (!match_ || screen_ != Screen::match || match_paused_ ||
        match_->commander_placement() == CommanderPlacement::none) {
        commander_done_held_ = false;
        commander_place_held_ = false;
        return false;
    }
    const bool placing = match_->commander_placement() == CommanderPlacement::placing;
    // The prompt and the button stand in the overlays' area: the
    // battlefield, or with the touch controls on, the part of it they leave
    // clear, where the button is found on the canvas
    // (draw_commander_placement); without them it is found at the 640x480
    // point under the pointer.
    const auto area = overlay_area();
    const bool touch = touch_controls_active();
    const auto on_done = [&] {
        if (touch)
            return on_placed_done_button(match_layout_, area, x, y);
        const auto point = hud_source_point(x, y);
        return on_done_button(point.x, point.y);
    };
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event.button.button != SDL_BUTTON_LEFT)
            return false;
        // The release of a press that moved the commander is the
        // placement's, not a click on the battlefield.
        if (std::exchange(commander_place_held_, false))
            return true;
        if (!commander_done_held_)
            return false;
        commander_done_held_ = false;
        if (placing && on_done())
            match_->finish_commander_placement();
        return true;
    }
    if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || event.button.button != SDL_BUTTON_LEFT)
        return false;
    if (placing && on_done()) {
        commander_done_held_ = true;
        return true;
    }
    // A press on a touch control or a placed part of the HUD is theirs.
    if (!area_holds(area, x, y) || placed_hud_covers(x, y))
        return false;
    // A press moves the commander there, as often as the player likes.
    const auto at = battlefield_map_point(x, y);
    (void)match_->place_commander(at[0], at[1]);
    commander_place_held_ = true;
    return true;
}

void Runtime::draw_commander_placement() {
    if (!match_ || match_->commander_placement() == CommanderPlacement::none)
        return;
    // The prompt and the Done button are laid out for the game's font.
    const PanelText panel(*this);
    const auto& game = match_->state().game;
    const auto text_color = oa::ui::hud::readout_color(game, oa::ui::hud::kReadoutTextColor);
    // Painted on the battlefield's layer, laid out on the 640x480
    // battlefield and placed in the overlays' area: the battlefield, or with
    // the touch controls on, the part of it they leave clear.
    const auto area = overlay_area();
    const auto label = [&](int x, int y, std::string_view text) {
        const auto canvas = overlay_point(match_layout_, area, x, y);
        const auto at = canvas_paint(canvas.x, canvas.y);
        draw_match_label(at.x, at.y, text, text_color);
    };
    const auto fill = [&](int x, int y, int width, int height, uint8_t color) {
        const auto rect = overlay_rect(match_layout_, area, x, y, width, height);
        const auto at = canvas_paint(rect.x, rect.y);
        fill_hud_rect(at.x, at.y, std::max(1, rect.width), std::max(1, rect.height), color);
    };
    const int prompt_x = kSourceWidth - kPromptFromRight;
    if (match_->commander_placement() == CommanderPlacement::waiting) {
        label(prompt_x, kPromptY, "Waiting for others to finish");
        return;
    }
    label(prompt_x, kPromptY, "Place your commander and click done");
    const int left = kSourceWidth - kDoneFromRight;
    fill(left, kDoneY, kDoneWidth, kDoneHeight, kDoneEdgeColor);
    fill(left + 1, kDoneY + 1, kDoneWidth - 2, kDoneHeight - 2, kDoneFaceColor);
    const int held = commander_done_held_ ? 1 : 0;
    label(left + kDoneLabelInsetX + held, kDoneY + kDoneLabelInsetY + held, "Done");
}

void Runtime::check_commander_placement() {
    require_placement(match_ && sdl_.renderer != nullptr, "needs a match and the SDL renderer");
    auto& world = match_->state();
    auto& game = world.game;
    uint32_t count = 0;
    Unit* commander = world_player_units(&world, &game.players[game.local_player_index], &count);
    require_placement(
        commander != nullptr && (commander->flags & OA_UNIT_FLAG_LIVE) != 0,
        "the local player has no commander"
    );
    // A networked start holds the game and opens the placing.
    game.sim_run_flags = static_cast<uint16_t>(game.sim_run_flags | oa::ui::console::kSimRunPaused);
    match_->begin_commander_placement();
    require_placement(
        match_->commander_placement() == CommanderPlacement::placing, "the placing did not open"
    );
    bool running = true;
    const auto send = [&](SDL_EventType type, float x, float y) {
        float window_x = 0.0F;
        float window_y = 0.0F;
        require_placement(
            frame_to_window(sdl_.renderer, x, y, &window_x, &window_y), SDL_GetError()
        );
        SDL_Event event{};
        event.type = type;
        event.button.windowID = SDL_GetWindowID(sdl_.window);
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.clicks = 1;
        event.button.x = window_x;
        event.button.y = window_y;
        dispatch_event(event, running);
    };
    const auto click = [&](float x, float y) {
        send(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y);
        send(SDL_EVENT_MOUSE_BUTTON_UP, x, y);
    };
    // A click in the battlefield's middle moves the commander's whole x and
    // z there and keeps their fractions and its height.
    const auto before = commander->position;
    const auto area = overlay_area();
    const float x = static_cast<float>(area.x + area.width / 2);
    const float y = static_cast<float>(area.y + area.height / 3);
    const auto at = battlefield_map_point(x, y);
    click(x, y);
    const auto& moved = commander->position;
    require_placement(
        static_cast<int32_t>(static_cast<uint32_t>(moved.x) >> 16) == at[0] &&
            static_cast<int32_t>(static_cast<uint32_t>(moved.z) >> 16) == at[1],
        "the click did not move the commander to the point under it"
    );
    require_placement(
        ((static_cast<uint32_t>(moved.x) ^ static_cast<uint32_t>(before.x)) & kFractionMask) == 0 &&
            ((static_cast<uint32_t>(moved.z) ^ static_cast<uint32_t>(before.z)) & kFractionMask) ==
                0 &&
            moved.y == before.y,
        "the move changed the commander's fractions or height"
    );
    // The prompt and the Done button draw over the battlefield.
    renderer::Surface composed;
    render_match_surface();
    compose_match_frame(composed);
    // A press and release on Done ends the placing.
    const auto done = overlay_point(
        match_layout_,
        area,
        kSourceWidth - kDoneFromRight + kDoneWidth / 2,
        kDoneY + kDoneHeight / 2
    );
    click(static_cast<float>(done.x), static_cast<float>(done.y));
    require_placement(
        match_->commander_placement() == CommanderPlacement::waiting, "Done did not end the placing"
    );
    render_match_surface();
    compose_match_frame(composed);
    // The game running closes it.
    game.sim_run_flags =
        static_cast<uint16_t>(game.sim_run_flags & ~oa::ui::console::kSimRunPaused);
    ++match_timing_.tick;
    match_->simulation().tick = match_timing_.tick;
    match_->tick();
    require_placement(
        match_->commander_placement() == CommanderPlacement::none,
        "the running game did not close the placing"
    );
    std::cout << "commander placement check: a click moved the commander to map point " << at[0]
              << "," << at[1] << ", Done ended the placing and the running game closed it\n";
}

} // namespace oa::app
