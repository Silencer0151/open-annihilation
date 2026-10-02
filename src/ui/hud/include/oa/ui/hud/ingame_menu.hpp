// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// In-game menu (Options/Share/Control/Allies) and chat target list.
#pragma once

#include "oa/ui/hud/boundary.hpp"
#include "oa/ui/hud/order_panel.hpp"

#include "oa/core/world.h"
#include "oa/data/campaign/campaign_file.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::ui::hud {

/// Values of Game.chat_mode.
enum class ChatSendMode : uint8_t {
    everyone = OA_CHAT_MODE_EVERYONE,
    allies = OA_CHAT_MODE_ALLIES,
    enemies = OA_CHAT_MODE_ENEMIES,
    chosen = OA_CHAT_MODE_CHOSEN,
};
/// Game.gui_flags bits the in-game menu holds while open.
inline constexpr uint8_t kGuiFlagsMenuOpen = 0xe0u;
/// frame_flags bit raised when the options panel is opened in-game.
inline constexpr uint16_t kFrameIngameOptions = 0x0001u;

/// Reads Game.chat_mode as a ChatSendMode.
///
/// @param game Game block.
/// @return The chat send mode.
[[nodiscard]] ChatSendMode chat_send_mode(const Game& game) noexcept;
/// Tells whether a player is ticked as a chosen chat target.
///
/// @param game Game block holding the chat targets.
/// @param player Player index.
/// @return false past OA_PLAYER_COUNT.
[[nodiscard]] bool chat_chosen_target(const Game& game, uint8_t player) noexcept;

/// Refreshes the chat panel's per-player target boxes.
///
/// Every PLAYER<n> is cleared. For each other player whose slot is neither
/// free nor closed, LIVEPLYR<n> is ticked when the local player's messages
/// reach it: always for everyone, by the alliance flag for allies, by its
/// absence for enemies, and by the chosen-target flag for chosen targets.
///
/// @param game Game block holding the chat mode, targets and players.
/// @param controls Named controls of the chat panel.
void refresh_chat_targets(const Game& game, const PanelControls& controls);

/// What the in-game menu asks the host to open.
enum class IngameMenuAction : uint8_t {
    none,
    closed,  // menu dismissed
    options, // in-game options panel
    share,   // resource share panel
    control, // player control panel
    allies,  // alliance panel
    cancel,  // refresh the menu's gadgets
};

/// Dispatches a click on a control of the in-game menu.
///
/// OPTIONS, SHARE, CONTROL and ALLIES play "BigButton"; OPTIONS also raises
/// kFrameIngameOptions.
///
/// @param name Clicked control name (exact match); null when the menu closes.
/// @param[in,out] gui_flags Game.gui_flags; the menu bits are cleared on close.
/// @param[in,out] frame_flags Game.frame_flags; gains kFrameIngameOptions for OPTIONS.
/// @param events Receives the button sound.
/// @return The panel the host opens next, closed, cancel or none.
IngameMenuAction ingame_menu_click(
    const char* name, uint8_t& gui_flags, uint16_t& frame_flags, const HudEvents& events
);

/// Game.gui_flags bit the tab menu raises while open.
inline constexpr uint8_t kGuiFlagsTabMenu = 0x20u;

/// Opens TABMENU.GUI, or closes it when a menu is already open.
///
/// Plays "SmallButton" either way. Closing keeps only the low five bits of
/// Game.gui_flags and pops the loaded TABMENU.GUI; opening raises
/// kGuiFlagsTabMenu. ALLIES and SHARE show in a multiplayer game, for a
/// local player who is not a watcher, when another slot (an unused one
/// included) is neither an in-use local player nor a watcher; CONTROL also
/// needs `control_offered` and a game that is not locked. The others are
/// hidden.
///
/// @param[in,out] world World whose Game.gui_flags change.
/// @param session_kind CampaignFile.kind of the session.
/// @param control_offered Whether this machine may control the other players:
///        it hosts the game and the game is not a tournament game
///        (control_offered in team_panels.hpp).
/// @param loader Loads and closes the menu panel.
/// @param controls Named controls of the loaded menu; set_active shows and
///        hides ALLIES, SHARE and CONTROL.
/// @param events Receives the sound.
/// @return Whether the menu opened.
bool toggle_tab_menu(
    World& world,
    oa::data::campaign::SessionKind session_kind,
    bool control_offered,
    const PanelLoader& loader,
    const PanelControls& controls,
    const HudEvents& events
);

/// Loads the mission start panel named by the game state.
///
/// @param game Game block holding the panel file name (Game.mission_panel_name, up
///        to 30 characters).
/// @param loader Loads the panel.
/// @return Whether the panel loaded.
bool load_mission_start_panel(const Game& game, const PanelLoader& loader);

/// Tells whether a click on the mission start panel clears its selection.
///
/// @param control Index of the clicked control, -1 for none.
/// @return true for any control.
[[nodiscard]] bool mission_start_panel_click(int32_t control) noexcept;

} // namespace oa::ui::hud
