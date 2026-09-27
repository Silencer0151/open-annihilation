// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/ingame_menu.hpp"

#include "oa/ui/hud/chat_panel.hpp"
#include "oa/ui/hud/game_fields.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace oa::ui::hud {
ChatSendMode chat_send_mode(const Game& game) noexcept {
    return static_cast<ChatSendMode>(game.chat_mode);
}

bool chat_chosen_target(const Game& game, uint8_t player) noexcept {
    return player < OA_PLAYER_COUNT && game.chat_targets[player] != 0;
}

void refresh_chat_targets(const Game& game, const PanelControls& controls) {
    const auto local = game.local_player_index;
    const auto& me = game.players[local < OA_PLAYER_COUNT ? local : 0];
    const auto mode = game.chat_mode;
    char name[52];
    for (uint8_t index = 0; index < OA_PLAYER_COUNT; ++index) {
        std::snprintf(name, sizeof name, "PLAYER%d", index);
        if (const auto control = find_control(controls, name); control != -1 && controls.set_value)
            controls.set_value(controls.user, control, 0);
        const auto status = game.players[index].status;
        if (status == OA_PLAYER_STATUS_FREE || status == OA_PLAYER_STATUS_CLOSED || index == local)
            continue;
        std::snprintf(name, sizeof name, "LIVEPLYR%d", index);
        int32_t reached = 0;
        switch (mode) {
        case static_cast<uint8_t>(ChatSendMode::everyone):
            reached = 1;
            break;
        case static_cast<uint8_t>(ChatSendMode::allies):
            reached = me.alliance[index];
            break;
        case static_cast<uint8_t>(ChatSendMode::enemies):
            reached = me.alliance[index] == 0 ? 1 : 0;
            break;
        case static_cast<uint8_t>(ChatSendMode::chosen):
            reached = game.chat_targets[index];
            break;
        default:
            break;
        }
        if (const auto control = find_control(controls, name); control != -1 && controls.set_state)
            controls.set_state(controls.user, control, reached);
    }
}

IngameMenuAction ingame_menu_click(
    const char* name, uint8_t& gui_flags, uint16_t& frame_flags, const HudEvents& events
) {
    if (name == nullptr) {
        gui_flags = static_cast<uint8_t>(gui_flags & ~kGuiFlagsMenuOpen);
        return IngameMenuAction::closed;
    }
    if (std::strcmp(name, "OPTIONS") == 0) {
        frame_flags |= kFrameIngameOptions;
        play_sound(events, "BigButton");
        return IngameMenuAction::options;
    }
    if (std::strcmp(name, "SHARE") == 0) {
        play_sound(events, "BigButton");
        return IngameMenuAction::share;
    }
    if (std::strcmp(name, "CONTROL") == 0) {
        play_sound(events, "BigButton");
        return IngameMenuAction::control;
    }
    if (std::strcmp(name, "ALLIES") == 0) {
        play_sound(events, "BigButton");
        return IngameMenuAction::allies;
    }
    if (std::strcmp(name, "CANCEL") == 0)
        return IngameMenuAction::cancel;
    return IngameMenuAction::none;
}

namespace {

constexpr int32_t kTabMenuPanelFlags = 0x800;
constexpr int32_t kMissionPanelFlags = 0x20;
constexpr uint8_t kGuiFlagsMenuKeep = 0x1fu;
/// Game.setup_options bit: the game is locked (reported). /* ? */
constexpr uint16_t kSetupOptionLocked = 0x0001u;

bool is_watcher(World& world, const Player& player) noexcept {
    const auto* info = world_player_info(&world, &player);
    return info != nullptr && (info->options & OA_SETUP_OPTION_WATCHER) != 0;
}

void set_named(const PanelControls& controls, const char* name, int32_t value) {
    const auto index = find_control(controls, name);
    if (index != -1 && controls.set_value != nullptr)
        controls.set_value(controls.user, index, value);
}

} // namespace

bool toggle_tab_menu(
    World& world,
    int32_t session_kind,
    bool control_offered,
    const PanelLoader& loader,
    const PanelControls& controls,
    const HudEvents& events
) {
    Game& game = world.game;
    play_sound(events, "SmallButton");
    if ((game.gui_flags & kGuiFlagsMenuOpen) != 0) {
        game.gui_flags = static_cast<uint8_t>(game.gui_flags & kGuiFlagsMenuKeep);
        if (loader.is_loaded != nullptr && loader.is_loaded(loader.user, "TABMENU.GUI") &&
            loader.close_to_root != nullptr)
            loader.close_to_root(loader.user);
        return false;
    }
    game.gui_flags = static_cast<uint8_t>((game.gui_flags & ~kGuiFlagsMenuOpen) | kGuiFlagsTabMenu);
    if (loader.load == nullptr ||
        !loader.load(loader.user, "TABMENU.GUI", nullptr, kTabMenuPanelFlags))
        return false;
    int32_t others = 0;
    for (const auto& player : game.players)
        if ((player.in_use == 0 || player.status != OA_PLAYER_STATUS_LOCAL) &&
            !is_watcher(world, player))
            ++others;
    const Player& local = game.players[game.local_player_index % OA_PLAYER_COUNT];
    int32_t control = 0;
    if (session_kind == kSessionMultiplayer && !is_watcher(world, local)) {
        set_named(controls, "ALLIES", others > 0 ? 1 : 0);
        set_named(controls, "SHARE", others > 0 ? 1 : 0);
        control = (game.setup_options & kSetupOptionLocked) == 0 && control_offered ? 1 : 0;
    } else {
        set_named(controls, "ALLIES", 0);
        set_named(controls, "SHARE", 0);
    }
    set_named(controls, "CONTROL", control);
    return true;
}

bool load_mission_start_panel(const Game& game, const PanelLoader& loader) {
    char name[kMissionPanelNameBytes + 1]{};
    std::memcpy(name, mission_panel_name(game), kMissionPanelNameBytes);
    return loader.load != nullptr && loader.load(loader.user, name, nullptr, kMissionPanelFlags);
}

bool mission_start_panel_click(int32_t control) noexcept {
    return control != -1;
}

} // namespace oa::ui::hud
