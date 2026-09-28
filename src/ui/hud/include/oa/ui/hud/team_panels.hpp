// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// In-game team panels of a multiplayer game: ALLIES.GUI and CONTROL.GUI,
// opened from the tab menu, the YESORNO.GUI question CONTROL.GUI asks before
// it removes a player, and what they and SHARE.GUI tell the other players'
// machines.
#pragma once

#include "oa/ui/hud/boundary.hpp"
#include "oa/ui/hud/status_panel.hpp"

#include "oa/core/world.h"

#include <cstddef>
#include <cstdint>

namespace oa::ui::hud {

/// Player.reject_reason CONTROL.GUI removes a player with.
inline constexpr uint8_t kRemovedByHost = 1;
/// Player.reject_reason CONTROL.GUI removes a watcher with once watching
/// is no longer allowed.
inline constexpr uint8_t kWatchingNotAllowed = 9;
/// Game.frame_flags bit held while ALLIES.GUI is open.
inline constexpr uint16_t kFrameAlliesPanelOpen = 0x0020u;
/// Bytes of TeamPanelResult::announcement, its terminator included.
inline constexpr size_t kAnnouncementBytes = 64;

/// What the team panels tell the other players' machines, and what they ask
/// of the running game. Every entry is optional: a null entry changes this
/// machine only, or answers as its own comment says.
struct TeamPanelHost {
    void* context{};
    /// Tells a player's machine that a local player's alliance with it
    /// changed; the local alliance tables already hold the change.
    ///
    /// @param context the host's context
    /// @param from local player index 0..9 whose alliance changed
    /// @param to the other player's index 0..9
    /// @param allied 1 allied, 0 not
    void (*alliance_changed)(void* context, uint8_t from, uint8_t to, uint8_t allied){};
    /// Tells every player that the local players' setup blocks changed
    /// (allied victory, watching allowed).
    ///
    /// @param context the host's context
    void (*setup_changed)(void* context){};
    /// Removes a player from the game on every machine.
    ///
    /// @param context the host's context
    /// @param player player index 0..9
    /// @param reason the Player.reject_reason it leaves with
    void (*remove_player)(void* context, uint8_t player, uint8_t reason){};
    /// Publishes the game's description again, when this machine hosts it.
    ///
    /// @param context the host's context
    void (*game_changed)(void* context){};
    /// Tells a player's machine about resources SHARE.GUI gave its
    /// player; they have already moved here.
    ///
    /// @param context the host's context
    /// @param from giving player index 0..9
    /// @param to receiving player index 0..9
    /// @param metal true for metal, false for energy
    /// @param amount amount given
    void (*resources_given)(void* context, uint8_t from, uint8_t to, bool metal, float amount){};
    /// Shares a player's explored map with another (SHARE.GUI's MAPINFO);
    /// the receiver already holds it here.
    ///
    /// @param context the host's context
    /// @param from sharing player index 0..9
    /// @param to receiving player index 0..9
    void (*sight_shared)(void* context, uint8_t from, uint8_t to){};
    /// Tells whether the running game is a tournament game, in which
    /// CONTROL.GUI is not offered.
    ///
    /// @param context the host's context
    /// @return true for a tournament game; a null entry means it is not one
    bool (*tournament_game)(void* context){};
};

/// What the runtime does after a click on ALLIES.GUI, CONTROL.GUI or the
/// removal question.
enum class TeamPanelClick : uint8_t {
    none,            // the panel stays open
    closed,          // the panel closes
    confirm_removal, // ask whether to remove TeamPanelResult::player (YESORNO.GUI)
};

/// The outcome of a team-panel click.
struct TeamPanelResult {
    TeamPanelClick click{};
    uint8_t player{OA_PLAYER_COUNT}; ///< player a removal question names, OA_PLAYER_COUNT for none
    char announcement[kAnnouncementBytes]{}; ///< line the local player says in chat; empty for none
};

/// Tells whether this machine hosts the multiplayer game.
///
/// The host is the first player slot with a status whose setup block carries
/// the host role; this machine hosts the game when that player is in use and
/// is a local or computer player. Only the host controls the other players
/// (CONTROL.GUI).
///
/// @param world players and their setup blocks
/// @return false when no slot carries the host role
[[nodiscard]] bool hosts_multiplayer_game(const World& world) noexcept;

/// Tells whether this machine may control the other players (CONTROL.GUI).
///
/// It may when it hosts the game (hosts_multiplayer_game) and the host does
/// not answer that the game is a tournament game; the lock bit is
/// toggle_tab_menu's.
///
/// @param world players and their setup blocks
/// @param host the team panels' host
/// @return true when CONTROL is offered
[[nodiscard]] bool control_offered(const World& world, const TeamPanelHost& host);

/// Counts a team's members.
///
/// Every live player whose Player.team is `team` counts; once the game has
/// started loading (Game.session_flags bit 2), only those still taking part
/// (player_participating).
///
/// @param world players
/// @param team team number; OA_PLAYER_NO_TEAM counts 0
/// @return the number of members
[[nodiscard]] int32_t team_member_count(const World& world, uint8_t team) noexcept;

/// Sets one player's alliance with another, as ALLIES.GUI does.
///
/// A local or computer `from` takes the alliance in its alliance row, and in
/// its allied_by row too when `to` is a computer player, a defeated player
/// on another machine, or `both_sides` is set. A local or computer `to`
/// takes it in its allied_by row, and in its alliance row too when it is a
/// computer player or `both_sides` is set. A `to` on another machine hears
/// of it through host.alliance_changed. Rows are indexed by Player.index.
///
/// @param[in,out] world players and their alliance tables
/// @param from player index 0..9 whose alliance changes; OA_PLAYER_COUNT or more does nothing
/// @param to the other player's index 0..9; OA_PLAYER_COUNT or more does nothing
/// @param allied 1 allied, 0 not
/// @param both_sides set the alliance on both sides whoever the players are
/// @param host tells another machine's player
void set_alliance(
    World& world,
    uint8_t from,
    uint8_t to,
    uint8_t allied,
    bool both_sides,
    const TeamPanelHost& host
);

/// Lays out the player rows of ALLIES.GUI or CONTROL.GUI.
///
/// Every PLAYERn, LOGOn, ALLYn and TEAMICONSn (n the slot) is hidden first.
/// Then each player gets the next row, unless it is a watcher, not live,
/// the local player with `skip_local`, without a colour, or, once the game
/// has started loading, no longer taking part. The row's PLAYERn shows the
/// name and becomes LIVEPLYRs (s the player's slot); its ALLYn shows for a
/// player taking part that is neither local nor computer nor defeated on
/// another machine, while the local player takes part, becomes LIVEALLYs
/// and is greyed for a member of the local player's team; its TEAMICONSn
/// shows, greyed unless the player is local or computer and the game has not
/// started loading; its LOGOn shows the player's colour as its value.
///
/// @param world players and their setup blocks
/// @param controls named controls of the loaded panel
/// @param skip_local leave the local player out (CONTROL.GUI)
void update_player_rows(const World& world, const PanelControls& controls, bool skip_local);

/// Sets the value of each LIVEALLYs: bit 0 the local player allied with
/// player s, bit 1 player s allied with the local player.
///
/// Watchers, players that are not live or no longer take part, the local
/// player and players without a colour are left alone.
///
/// @param world players and the local player's alliance tables
/// @param controls named controls of the loaded panel
void update_ally_indicators(const World& world, const PanelControls& controls);

/// Sets the value of each TEAMICONSn to its player's team icon.
///
/// Live players other than closed slots take part; once the game has
/// started loading only those still taking part and with a colour, numbered
/// in order, and before that numbered by slot. A team of no members shows
/// icon 10, of one member 2 * team + 1, of more 2 * team.
///
/// @param world players
/// @param controls named controls of the loaded panel
void update_team_icons(const World& world, const PanelControls& controls);

/// Lays out ALLIES.GUI for the local player.
///
/// Raises kFrameAlliesPanelOpen, numbers the ALLYx and TEAMICONSx templates
/// ALLY0, ALLY1, ... and TEAMICONS0, ..., lays out the rows
/// (update_player_rows, the local player included), the alliance indicators
/// and the team icons, sets VICTORY to the local player's allied victory
/// (OA_SETUP_STATUS_ALLIED_VICTORY) and greys it for a team of two or more
/// or a watcher.
///
/// @param[in,out] world players and setup blocks; Game.frame_flags changes
/// @param controls named controls of the loaded panel
void open_allies_panel(World& world, const PanelControls& controls);

/// Handles a click on ALLIES.GUI.
///
/// LIVEALLYs of a live player plays "Options", flips the local player's
/// alliance with player s through set_alliance (one side) and returns the
/// line " allied with <name>" or " broke alliance with <name>", its phrase
/// translated, for the local player to say. VICTORY plays "Options". OK
/// plays "Options", stores VICTORY's value as the local player's allied
/// victory and calls host.setup_changed when it changed, and closes the
/// panel. A null name closes it and drops kFrameAlliesPanelOpen.
///
/// @param[in,out] world players, alliance tables and setup blocks
/// @param name clicked control name (exact match); null when the panel closes
/// @param controls named controls of the loaded panel
/// @param events receives the button sound
/// @param host tells the other players' machines
/// @param translate UI text lookup for the phrase; may be null
/// @param translate_context context passed to `translate`
/// @return what the runtime does next, with the line to say
TeamPanelResult allies_panel_click(
    World& world,
    const char* name,
    const PanelControls& controls,
    const HudEvents& events,
    const TeamPanelHost& host,
    TranslateText translate,
    void* translate_context
);

/// Sets CONTROL.GUI's WATCHING value to whether watching is allowed and
/// GAMEOPEN's to whether the game is open, from the local player's setup
/// block.
///
/// @param world the local player's setup block
/// @param controls named controls of the loaded panel
void update_control_panel(const World& world, const PanelControls& controls);

/// Lays out CONTROL.GUI: the rows without the local player
/// (update_player_rows) and the WATCHING and GAMEOPEN values.
///
/// @param world players and setup blocks
/// @param controls named controls of the loaded panel
/// @return false for a watching local player, whose panel stays unloaded
bool open_control_panel(const World& world, const PanelControls& controls);

/// Handles a click on CONTROL.GUI.
///
/// LIVEPLYRs asks whether to remove player s (confirm_removal). WATCHING
/// flips the local player's OA_SETUP_OPTION_WATCHING_ALLOWED, plays
/// "Options", refreshes the values and calls host.setup_changed. OK calls
/// host.game_changed, plays "Options" and, with watching no longer allowed,
/// removes each watcher on another machine through
/// host.remove_player(watcher, kWatchingNotAllowed); it closes the panel,
/// as a null name does.
///
/// @param[in,out] world players and setup blocks
/// @param name clicked control name (exact match); null when the panel closes
/// @param controls named controls of the loaded panel
/// @param events receives the button sound
/// @param host tells the other players' machines
/// @return what the runtime does next
TeamPanelResult control_panel_click(
    World& world,
    const char* name,
    const PanelControls& controls,
    const HudEvents& events,
    const TeamPanelHost& host
);

/// Fills YESORNO.GUI to ask whether to remove a player: CHOICE1 "Yes",
/// CHOICE2 "No" and TITLE "Reject <name>?", its verb translated.
///
/// @param world players
/// @param player player index 0..9 the question names; OA_PLAYER_COUNT or more names nobody
/// @param controls named controls of the loaded question
/// @param translate UI text lookup for the verb; may be null
/// @param translate_context context passed to `translate`
void open_removal_question(
    const World& world,
    uint8_t player,
    const PanelControls& controls,
    TranslateText translate,
    void* translate_context
);

/// Handles a click on the removal question.
///
/// CHOICE1 removes the player (confirm_player_removal) and closes the
/// question; CHOICE2 and a null name close it; any other control does
/// nothing.
///
/// @param name clicked control name (exact match); null when the question closes
/// @param player player index 0..9 the question names
/// @param host removes the player
/// @return what the runtime does next
TeamPanelResult removal_question_click(const char* name, uint8_t player, const TeamPanelHost& host);

/// Removes a player the removal question accepted:
/// host.remove_player(player, kRemovedByHost).
///
/// @param host removes the player; a null entry does nothing
/// @param player player index 0..9
void confirm_player_removal(const TeamPanelHost& host, uint8_t player);

} // namespace oa::ui::hud
