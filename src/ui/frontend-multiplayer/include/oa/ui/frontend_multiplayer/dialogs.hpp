// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Panels stacked over the battleroom or the match: reject confirmation and
// exit confirmation (YESORNO.GUI), player timeout (TIMEOUT.GUI), map
// selection (SELMAP.GUI), map view (VIEWMAP.GUI) and the in-game CONTROL.GUI
// / ALLIES.GUI panels.
#pragma once

#include "oa/ui/frontend_multiplayer/lobby.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::frontend_multiplayer {

/// Fills YESORNO.GUI asking whether to reject a player ("Reject <name>?").
///
/// CHOICE1 and CHOICE2 read "Yes" and "No" in the language shown, and take
/// the quick keys their captions give them (panel_set_text): Y and N in
/// English, J and N in German.
///
/// @param[in,out] lobby Lobby state; the slot is remembered for the answer.
/// @param[in,out] panel The dialog's panel.
/// @param slot Player slot to reject.
void confirm_open(Lobby& lobby, Panel& panel, uint8_t slot) noexcept;

/// Handles a click on YESORNO: CHOICE1 rejects the player, CHOICE2 declines.
///
/// @param[in,out] lobby Lobby state.
/// @param[in,out] panel The dialog's panel.
/// @return True when the dialog closes.
bool confirm_handle_event(Lobby& lobby, Panel& panel) noexcept;

// What an answer to the exit confirmation leads to.
enum class ExitConfirmAnswer : uint8_t {
    none,   // nothing was chosen
    leave,  // CHOICE1: leave the game and end the program
    closed, // CHOICE2: the confirmation closes
};

/// Prepares YESORNO as the exit confirmation a request to close the window opens after a
/// launch.
///
/// CHOICE1 reads "Yes" and CHOICE2 "No" in the language shown, with the
/// quick keys their captions give them (panel_set_text): Y and N in English.
/// CHOICE2 takes the focus, so Enter answers No, as Escape does. The title asks "Surrender this battle and exit
/// to the system?", or "Exit the Battle" in a game a lobby program launched.
///
/// @param lobby Lobby state and its game block.
/// @param[in,out] panel The dialog's panel.
void exit_confirm_open(Lobby& lobby, Panel& panel) noexcept;

/// Handles a click on the exit confirmation.
///
/// Every click plays "Exit". CHOICE1 raises the game block's leaving bit
/// (Game.outcome_flags bit 2) and leaves; CHOICE2 closes.
///
/// @param[in,out] lobby Lobby state and its game block.
/// @param[in,out] panel The dialog's panel; its selection is cleared.
/// @return What the answer leads to.
ExitConfirmAnswer exit_confirm_handle_event(Lobby& lobby, Panel& panel) noexcept;

/// Opens TIMEOUT.GUI for a silent player.
///
/// @param[in,out] lobby Lobby state.
/// @param[in,out] panel The dialog's panel.
/// @param player_id Transport id of the silent player.
/// @return False when no slot answers to the id.
bool timeout_open(Lobby& lobby, Panel& panel, uint32_t player_id) noexcept;

/// Refreshes TIMEOUT.GUI: the chat excerpt and the countdown.
///
/// Rejects the player (connection lost) once it has been silent for the
/// timeout plus 0x78 s.
///
/// @param[in,out] lobby Lobby state.
/// @param[in,out] panel The dialog's panel.
/// @return True when the dialog should close: the player was rejected, left or is no longer remote.
bool timeout_tick(Lobby& lobby, Panel& panel) noexcept;

/// Handles a click on TIMEOUT: TALK sends a chat line; REJECT drops the player now.
///
/// @param[in,out] lobby Lobby state.
/// @param[in,out] panel The dialog's panel.
/// @return True when the dialog closes.
bool timeout_handle_event(Lobby& lobby, Panel& panel) noexcept;

/// Largest picture box, in pixels a side, that fit_map_picture fills.
inline constexpr int32_t kMapPictureMaxSide = 2048;

/// Fits a map's minimap into a picture box, as 3.1c fits a map's preview.
///
/// The map's playable part is its size in world pixels less 32 on the right
/// and 128 at the bottom, which the minimap leaves out. Its longer side fills
/// the box and the other is scaled to keep its shape, truncated, and centred,
/// the offset truncated; the same share of the minimap, from its top-left
/// corner, is stretched over that rectangle. The rest of the box is palette
/// index 0, and so are the box's last column and row.
///
/// @param pixels the minimap's palette indices, row by row
/// @param picture_width minimap width in pixels
/// @param picture_height minimap height in rows
/// @param box_width box width in pixels, at most kMapPictureMaxSide
/// @param box_height box height in rows, at most kMapPictureMaxSide
/// @param world_width the map's width in world pixels, 16 a terrain cell
/// @param world_height the map's height in world pixels, 16 a terrain cell
/// @return box_width x box_height palette indices; empty when the box or the
///         minimap is empty or too large, or the map has no playable part
[[nodiscard]] std::vector<uint8_t> fit_map_picture(
    std::span<const uint8_t> pixels,
    int32_t picture_width,
    int32_t picture_height,
    int32_t box_width,
    int32_t box_height,
    int32_t world_width,
    int32_t world_height
);

/// Shows the selected map's name, size, minimap and description.
///
/// SIZE reads "<memory>  Players: <player counts>" from the map's OTA, or the
/// map's size text without a map context. MAPPIC gets the map's minimap
/// fitted to its box (fit_map_picture), or no picture when the map has none.
///
/// @param lobby Lobby state whose map services describe the map.
/// @param[in,out] panel The dialog's panel.
void map_summary_update(Lobby& lobby, Panel& panel) noexcept;

/// Fills VIEWMAP.GUI with the selected map's summary.
///
/// @param lobby Lobby state whose map services describe the map.
/// @param[in,out] panel The dialog's panel.
void viewmap_open(Lobby& lobby, Panel& panel) noexcept;

/// Brings an open VIEWMAP.GUI to the host's map when it is not the selected one.
///
/// The host's map is selected and its summary shown. A map this machine does
/// not have shows the host's map name with no description and no picture.
///
/// @param[in,out] lobby Lobby state whose map services select the map.
/// @param[in,out] panel The dialog's panel.
/// @param host_map the name of the map the host chose
/// @return true when the view changed
bool viewmap_follow_host(Lobby& lobby, Panel& panel, std::string_view host_map) noexcept;

/// Handles a click on VIEWMAP: OK closes the view.
///
/// @param lobby Lobby state used for sounds.
/// @param[in,out] panel The dialog's panel.
/// @return True when the view closes.
bool viewmap_handle_event(Lobby& lobby, Panel& panel) noexcept;

struct MapSelect {
    std::string previous; // map selected when the panel opened
    std::vector<std::string> names;
};

/// Fills MAPNAMES with the multiplayer maps in name order and previews the selected one.
///
/// The selected map's row is selected and scrolled into view, and the
/// scroll bar shows when the maps do not all fit.
///
/// @param[in,out] lobby Lobby state.
/// @param[in,out] select Map selection state.
/// @param[in,out] panel The dialog's panel.
/// @return False when there are no multiplayer maps; a message says so.
bool mapselect_open(Lobby& lobby, MapSelect& select, Panel& panel) noexcept;

/// Selects the highlighted MAPNAMES row and refreshes the summary.
///
/// It runs whenever the selection changes. The map is selected on this
/// machine only: nothing is sent until the map is chosen. A map that cannot
/// be selected hides MAPPIC, so no picture shows, and keeps the summary as
/// it was; the next map that can be selected shows MAPPIC again.
///
/// @param[in,out] lobby Lobby state.
/// @param[in,out] select Map selection state.
/// @param[in,out] panel The dialog's panel.
void mapselect_preview(Lobby& lobby, MapSelect& select, Panel& panel) noexcept;

/// Handles a click on SELMAP.
///
/// A row or LOAD selects the map, announces it (player info and session) and
/// clears the other players' ready marks; PREVMENU restores the previous map.
///
/// @param[in,out] lobby Lobby state.
/// @param[in,out] select Map selection state.
/// @param[in,out] panel The dialog's panel.
/// @return True when the panel closes: LOAD or PREVMENU.
bool mapselect_handle_event(Lobby& lobby, MapSelect& select, Panel& panel) noexcept;

/// Sets the WATCHING and GAMEOPEN stages of CONTROL.GUI from the local lobby block.
///
/// @param lobby Lobby state.
/// @param[in,out] panel The dialog's panel.
void control_update(Lobby& lobby, Panel& panel) noexcept;

/// Handles a click on CONTROL.GUI.
///
/// LIVEPLYRn asks to reject that player; WATCHING toggles watching; OK
/// publishes the session and, when watching is off, rejects remote watchers.
///
/// @param[in,out] lobby Lobby state.
/// @param[in,out] panel The dialog's panel.
/// @param[out] confirm Slot to confirm a rejection for, or kNoSlot.
/// @return True when the panel closes.
bool control_handle_event(Lobby& lobby, Panel& panel, uint8_t* confirm) noexcept;

/// Prepares CONTROL.GUI unless the local player is watching.
///
/// @param[in,out] lobby Lobby state.
/// @param[in,out] panel The dialog's panel.
/// @return False for a watching local player.
bool control_open(Lobby& lobby, Panel& panel) noexcept;

/// Sets the LIVEALLYn stages from the local alliance tables: bit 0 allied, bit 1 allied back.
///
/// @param lobby Lobby state.
/// @param[in,out] panel The dialog's panel.
void allies_update_indicators(Lobby& lobby, Panel& panel) noexcept;

/// Lays out the player rows of CONTROL.GUI and ALLIES.GUI: name, logo, ally toggle and team icon.
///
/// Watchers, inactive slots, slots without a colour and, once a watched game
/// started, non-participants get no row.
///
/// @param lobby Lobby state.
/// @param[in,out] panel The dialog's panel.
/// @param skip_local Leave out the local player.
void allies_update_rows(Lobby& lobby, Panel& panel, bool skip_local) noexcept;

/// Handles a click on ALLIES.GUI: LIVEALLYn toggles an alliance and announces it; OK stores allied victory.
///
/// @param[in,out] lobby Lobby state.
/// @param[in,out] panel The dialog's panel.
/// @return True when the panel closes, or when nothing is selected.
bool allies_handle_event(Lobby& lobby, Panel& panel) noexcept;

/// Prepares ALLIES.GUI: numbers the row templates, lays out the rows and sets the VICTORY toggle.
///
/// VICTORY is grayed for a watcher or when the local team has two or more members.
///
/// @param[in,out] lobby Lobby state.
/// @param[in,out] panel The dialog's panel.
void allies_open(Lobby& lobby, Panel& panel) noexcept;

} // namespace oa::ui::frontend_multiplayer
