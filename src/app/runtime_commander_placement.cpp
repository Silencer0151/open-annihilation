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

} // namespace

bool Runtime::commander_placement_pointer(const SDL_Event& event, float x, float y) {
    if (!match_ || screen_ != Screen::match || match_paused_ ||
        match_->commander_placement() == CommanderPlacement::none) {
        commander_done_held_ = false;
        commander_place_held_ = false;
        return false;
    }
    const bool placing = match_->commander_placement() == CommanderPlacement::placing;
    const auto point = hud_source_point(x, y);
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
        if (placing && on_done_button(point.x, point.y))
            match_->finish_commander_placement();
        return true;
    }
    if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || event.button.button != SDL_BUTTON_LEFT)
        return false;
    if (placing && on_done_button(point.x, point.y)) {
        commander_done_held_ = true;
        return true;
    }
    if (!battlefield_contains(x, y))
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
    const int prompt_x = kSourceWidth - kPromptFromRight;
    if (match_->commander_placement() == CommanderPlacement::waiting) {
        draw_hud_label(prompt_x, kPromptY, "Waiting for others to finish", text_color);
        return;
    }
    draw_hud_label(prompt_x, kPromptY, "Place your commander and click done", text_color);
    const int left = kSourceWidth - kDoneFromRight;
    fill_source_rect(left, kDoneY, kDoneWidth, kDoneHeight, kDoneEdgeColor);
    fill_source_rect(left + 1, kDoneY + 1, kDoneWidth - 2, kDoneHeight - 2, kDoneFaceColor);
    const int held = commander_done_held_ ? 1 : 0;
    draw_hud_label(
        left + kDoneLabelInsetX + held, kDoneY + kDoneLabelInsetY + held, "Done", text_color
    );
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
            SDL_RenderCoordinatesToWindow(sdl_.renderer, x, y, &window_x, &window_y), SDL_GetError()
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
    const float x = static_cast<float>(match_layout_.left + match_layout_.battlefield_width() / 2);
    const float y = static_cast<float>(match_layout_.top + match_layout_.battlefield_height() / 3);
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
    const auto done = oa::ui::display_layout::source_to_canvas(
        match_layout_, kSourceWidth - kDoneFromRight + kDoneWidth / 2, kDoneY + kDoneHeight / 2
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
