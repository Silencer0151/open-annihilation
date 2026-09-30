// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// ENDMSN.GUI: the mission-end panel with the campaign mission list, the
// difficulty button and Start, Load Game, Save Game and Main Menu, and the
// end-game frontend state that opens it.
#pragma once

#include "oa/data/campaign/campaign_file.hpp"
#include "oa/ui/frontend_state/dispatcher.hpp"
#include "oa/ui/campaign/endgame.hpp"
#include "oa/ui/frontend/ingame_menu.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oa::ui::frontend {

// The packed mission list is allocated at 0x80 bytes per mission; rows are
// NUL-separated, each a marker, a space and the mission name.
inline constexpr std::size_t kMissionEntryBytes = 0x80;
inline constexpr std::size_t kMissionEntries = 0x19; // one per mission-result byte
inline constexpr int32_t kEndMissionMessageWidth = 200;
// Palette screens of the panel: a campaign that continues, or any other end.
inline constexpr const char* kContinuePalette = "outcome1";
inline constexpr const char* kFinishedPalette = "outcome0";
// Label the Main Menu button takes when the return label is too long to show.
inline constexpr const char* kReturnLabelFallback = "OK";

// Byte placed before a mission name in the list, then a space.
namespace mission_marker {
inline constexpr uint8_t lost = 0xff;
inline constexpr uint8_t won = 0xfe;
inline constexpr uint8_t unplayed = 0xfd;
} // namespace mission_marker

// Services the panel calls; null members are absent services.
struct EndMissionHost {
    void* context = nullptr;
    void (*play_sound)(void* context, const char* name) = nullptr;
    bool (*disc_present)(void* context) = nullptr; // null: no disc check
    void (*refresh_archives)(void* context) = nullptr;
    void (*show_message)(void* context, const char* text, int32_t width) = nullptr;
    const char* (*translate)(void* context, const char* text) = nullptr;
    void (*leave_game)(void* context) = nullptr; // a watcher leaves the session
    void (*set_music_kind)(void* context, int32_t kind) = nullptr;
    // Frees the captured last frame and the other outcome-screen buffers.
    void (*release_outcome_frames)(void* context) = nullptr;
};

enum class EndMissionAction : uint8_t {
    none,
    closed,
    open_load_game,
    open_save_game,
    start_mission, // the selected mission's briefing
    main_menu,
};

struct EndMissionContext {
    EndMissionHost host{};
    ui::frontend_state::State* state = nullptr;       // frontend state bytes and flags
    ui::frontend_state::Host* frontend = nullptr;     // modes, cursor and dispatcher steps
    data::campaign::CampaignFile* campaign = nullptr; // Game.game_options
    const data::campaign::CampaignEnv* env = nullptr;
    // The finished game: its mission index, victory, mission results, score
    // table, local player and flags.
    World* world = nullptr;
    int32_t difficulty = 0;          // Game.difficulty
    int32_t skirmish_difficulty = 0; // Preferences.skirmish_difficulty
    // The label the game's return names in place of the main menu,
    // zero-terminated; empty for none.
    std::array<char, kReturnLabelBytes> return_label{};
    // The game data holds LOADGAME.GUI, the dialog LoadGame and SaveGame open.
    bool saved_games_offered = true;
    // Written by end_mission_enter.
    bool continuing = false;
    bool victory_title = false; // title picture: victory, otherwise defeat
    const char* palette = nullptr;
    std::array<char, kControlNameBytes> enter_control{}; // the root's Enter default
    std::array<char, kControlNameBytes> focus{};
    std::array<char, kMissionEntries * kMissionEntryBytes> missions{};
    int32_t mission_count = 0;
};

/// Sets up ENDMSN.GUI for the finished game.
///
/// A campaign that cannot continue gets the outcome0 palette and focus on
/// MainMenu. A continuing campaign binds the finished mission again, gets the
/// outcome1 palette with Enter pressing Start, and shows the mission list with
/// L/W/U markers, selected on the next mission after a victory, and the
/// difficulty caption. The title shows victory unless the local player is a
/// watcher. LoadGame and SaveGame are greyed when the game data has no dialog
/// for them. When Game.gui_flags bit 4 (a multiplayer game?) is set or the game
/// has a return label, Main Menu takes the return label when it has 1 to
/// kMaxReturnLabelLength characters and kReturnLabelFallback otherwise.
///
/// @param[in,out] panel The loaded ENDMSN.GUI.
/// @param[in,out] context Campaign, finished game and host; continuing,
///                        victory_title, palette, enter_control, focus and the
///                        mission list are written.
void end_mission_enter(Panel& panel, EndMissionContext& context) noexcept;

/// Handles a click on ENDMSN.GUI.
///
/// A closing panel releases the outcome buffers and the mission list, leaves
/// the game when Game.gui_flags bit 4 is set and stops the music. LoadGame and SaveGame open their
/// dialogs. Start or a Missions row checks the disc, plays the button sound,
/// raises the start-mission signal and binds the selected mission; once bound
/// the frontend is reset to a single-player briefing (loading and live-game
/// flags and outcome_flags bits 4 and 2 cleared) in application mode 2.
/// A missing disc shows the disc message and clears the selection, but the
/// start still goes ahead. MainMenu returns to the main menu state in
/// application mode 1. Difficulty cycles easy, medium, hard and mirrors the
/// choice into the skirmish settings.
///
/// @param[in,out] panel The loaded ENDMSN.GUI; the selection is read and cleared.
/// @param[in,out] context Campaign, frontend state and host.
/// @return What the caller does next.
EndMissionAction end_mission_on_click(Panel& panel, EndMissionContext& context) noexcept;

/// Runs the end-game frontend state that opens ENDMSN.GUI.
///
/// Sets up the panel (end_mission_enter), lays out the score rows and shows
/// the button set, then enters application mode 7 with the stat bars
/// counting again.
///
/// @param[in,out] panel The loaded ENDMSN.GUI.
/// @param[in,out] context Campaign, finished game, frontend state and host.
/// @param[out] scores Score rows laid out from the finished game.
void end_mission_open(
    Panel& panel, EndMissionContext& context, campaign::ScoreLayout& scores
) noexcept;

} // namespace oa::ui::frontend
