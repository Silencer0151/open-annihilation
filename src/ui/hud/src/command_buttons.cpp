// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/command_buttons.hpp"

#include <cstdint>
#include <cstring>

namespace oa::ui::hud {
namespace {

struct CommandButton {
    const char* word;
    uint8_t order;
    bool special;
};

// In the order the names are tried: UNLOAD before LOAD, which it holds.
constexpr CommandButton kCommandButtons[] = {
    {"MOVE", armed_order::move, false},
    {"STOP", armed_order::default_order, false},
    {"ATTACK", armed_order::attack, false},
    {"BLAST", armed_order::blast, false},
    {"DEFEND", armed_order::defend, false},
    {"REPAIR", armed_order::repair, true},
    {"PATROL", armed_order::patrol, false},
    {"RECLAIM", armed_order::reclaim, true},
    {"CAPTURE", armed_order::capture, true},
    {"UNLOAD", armed_order::unload, true},
    {"LOAD", armed_order::load, false},
};

/// Arms the order the pointer gives (Game.pointer_command).
void set_armed_order(Game& game, uint8_t order) noexcept {
    game.pointer_command = order;
}

/// Clears the drag box bit of Game.pointer_flags, keeping the others.
void clear_drag_box(Game& game) noexcept {
    game.pointer_flags = static_cast<uint8_t>(game.pointer_flags & ~kPointerFlagDragBox);
}

} // namespace

uint8_t armed_order_of(const Game& game) noexcept {
    return game.pointer_command;
}

bool order_panel_command(Game& game, const char* name, int16_t status, const HudEvents& events) {
    if (name == nullptr)
        return false;
    const CommandButton* button = nullptr;
    for (const auto& candidate : kCommandButtons)
        if (std::strstr(name, candidate.word) != nullptr) {
            button = &candidate;
            break;
        }
    if (button == nullptr)
        return false;
    const bool stop = button->order == armed_order::default_order;
    set_armed_order(game, status != 0 && !stop ? button->order : armed_order::default_order);
    clear_drag_box(game);
    if (stop && events.apply_standing_order != nullptr)
        events.apply_standing_order(events.user, kStopOrderTag, 0);
    play_sound(events, button->special ? kSpecialOrdersSound : kImmediateOrdersSound);
    return true;
}

} // namespace oa::ui::hud
