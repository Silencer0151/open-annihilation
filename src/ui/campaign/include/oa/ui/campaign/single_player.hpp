// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// SINGLE.GUI and the NEWGAME.GUI campaign/any-mission setup panel.
#pragma once

#include "oa/data/campaign/campaign_file.hpp"
#include "oa/ui/campaign/frontend_host.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::ui::campaign {

inline constexpr std::size_t kSideNameBytes = 30;
inline constexpr std::size_t kMaxSides = 5;
inline constexpr std::size_t kCampaignListBytes = 16 * 1024;
inline constexpr const char* kCheatCode = "DRDEATH";
inline constexpr std::size_t kCheatCodeLength = 7;
// Game.campaign_unlock_flags bit set once every mission is playable from AnyMsn.
inline constexpr uint16_t kAllMissionsUnlocked = 0x0001;

// State the panels read and write. Fields named after the game-block values
// they mirror; the host copies them back when the panel closes.
struct CampaignSetup {
    int32_t side{};             // Game.campaign_side (0 ARM, 1 CORE)
    int32_t difficulty{};       // Game.difficulty (0 easy .. 2 hard)
    uint16_t unlock_flags{};    // Game.campaign_unlock_flags
    uint8_t player_side[2]{};   // local and opponent PlayerSetupInfo.side
    uint8_t player_color[2]{};  // local and opponent PlayerSetupInfo.color
    bool any_mission{};         // NEWGAME opened for AnyMsn (mission list shown)
    bool fixed_side_campaign{}; // side buttons choose Arm/Core Campaign directly
    char side_names[kMaxSides][kSideNameBytes]{};
    uint32_t side_count{};
    // Campaign list (NUL-separated) and its selection.
    char campaigns[kCampaignListBytes]{};
    int32_t campaign_count{};
    int32_t campaign_selected{};
    // Mission list of the selected campaign.
    char (*missions)[oa::data::campaign::kCampaignNameBytes];
    int32_t mission_count{};
    int32_t mission_selected{};
    oa::data::campaign::CampaignFile* campaign{};
    oa::data::campaign::CampaignEnv env;
    bool list_dirty{};
};

/// Zeroes the setup state, then sets player 1's side and colour to 1.
///
/// @param[out] setup Setup state to initialise.
void campaign_setup_init(CampaignSetup* setup);

/// Frees the mission list of the setup state.
///
/// @param[in,out] setup Setup state; missions becomes null and mission_count 0.
void campaign_setup_free(CampaignSetup* setup);

/// Joins side names into a NUL-separated list, every character after the first shifted to lower case.
///
/// The shift adds 0x20 to each byte, which lower-cases upper-case names only.
///
/// @param names Side names, kSideNameBytes each.
/// @param count Number of names.
/// @param[out] out Destination buffer; names that do not fit are dropped.
/// @param capacity Size of `out` in bytes; 0 writes nothing.
/// @return Bytes written including the final NUL; 0 for no capacity.
std::size_t build_side_name_list(
    const char (*names)[kSideNameBytes], uint32_t count, char* out, std::size_t capacity
);

/// Mirrors the chosen side into both players and the side radio groups.
///
/// @param[in,out] setup Setup state; player_side follows side (0 Arm, else Core).
/// @param host Selects Arm/Side0 or Core/Side1.
void apply_side_selection(CampaignSetup* setup, const FrontendHost* host);

/// Shows a difficulty as the Difficulty button's stage and selects its Easy/Medium/Hard radio label.
///
/// The label is the name the difficulty carries (ai.difficulty-names).
///
/// @param difficulty Difficulty 0..2; other values only redraw.
/// @param host Frontend services.
/// @param names which of 3.1c's names each difficulty carries; 3.1c's when left out
void show_difficulty(
    int32_t difficulty,
    const FrontendHost* host,
    const data::match_rules::AiDifficultyNames& names = {}
);

/// Toggles the all-missions unlock when DRDEATH was just typed on SINGLE.GUI.
///
/// The AnyMsn button, the unlock flag and the stored all-missions state
/// follow the toggle.
///
/// @param[in,out] setup Setup state; kAllMissionsUnlocked toggles in unlock_flags.
/// @param host Frontend services.
/// @param recent_keys The last seven typed characters.
void check_cheat_code(CampaignSetup* setup, const FrontendHost* host, const char* recent_keys);

/// Sets up SINGLE.GUI after the panel and its background load.
///
/// The players take their sides from the stored side, AnyMsn shows when
/// every mission is unlocked, the Spanish game gets 's' as the Skirmish key,
/// and the ready cursor is selected.
///
/// @param[in,out] setup Setup state; player_side follows side.
/// @param host Frontend services.
/// @param language Language name; may be null.
void single_player_enter(CampaignSetup* setup, const FrontendHost* host, const char* language);

/// Handles a click on SINGLE.GUI.
///
/// NewCamp, Skirmish and AnyMsn check the disc (Campaign, Multiplayer and
/// Campaign CD), rescan the archives, play their sound and raise their
/// signal; a missing disc shows the insert-disc message. LoadGame and Options
/// open their panels; PrevMenu raises the back signal. Names compare
/// case-insensitively. The selection is cleared after LoadGame, a failed disc
/// check or any other control.
///
/// @param setup Setup state (unused).
/// @param host Frontend services.
/// @param control Clicked control name; null when the panel closes.
void single_player_click(CampaignSetup* setup, const FrontendHost* host, const char* control);

/// Refills the Campaign list with the campaigns for the current side.
///
/// Plays "smlbutton"; a selection past the new list resets to 0.
///
/// @param[in,out] setup Setup state; campaigns, campaign_count and campaign_selected change.
/// @param host Frontend services.
void populate_campaign_list(CampaignSetup* setup, const FrontendHost* host);

/// Loads the selected campaign and refills the Missions list.
///
/// The mission list is allocated on first use; a selection past the new list
/// resets to 0. Nothing is listed without a campaign object.
///
/// @param[in,out] setup Setup state; the campaign file, missions and mission_count change.
/// @param host Frontend services.
void populate_mission_list(CampaignSetup* setup, const FrontendHost* host);

/// Looks up an entry of the Campaign list.
///
/// @param setup Setup state holding the NUL-separated list.
/// @param index Entry index.
/// @return The campaign name, or null out of range.
[[nodiscard]] const char* campaign_list_entry(const CampaignSetup* setup, int32_t index);

// Record types the NEWGAME setup finds its lists and knobs by.
inline constexpr uint8_t kListControl = 2;
inline constexpr uint8_t kScrollControl = 4;
// Any Mission moves the Campaign list and knob down and shortens Missions.
inline constexpr int16_t kAnyMissionCampaignY = 0x134;
inline constexpr int16_t kAnyMissionCampaignHeight = 0x30;
inline constexpr int16_t kAnyMissionMissionsHeight = 0x3e;
// More campaign files than this list the campaigns.
inline constexpr int32_t kSideSelectCampaignLimit = 2;

/// Sets up NEWGAME.GUI, loaded for a new campaign or for any mission.
///
/// In order: the frame is redrawn; the background is chosen by mode and
/// campaign count, where no more than kSideSelectCampaignLimit campaign files
/// make the side buttons pick Arm or Core Campaign directly; any mission moves
/// the Campaign list and knob down and shortens Missions; the side sequences'
/// origins are zeroed; the side and difficulty buttons, the campaign and
/// mission lists, the focus and the ready cursor follow.
///
/// @param[in,out] setup Setup state; any_mission, fixed_side_campaign and the lists change.
/// @param host Frontend services.
/// @param any_mission Whether the panel opened for AnyMsn.
void new_game_enter(CampaignSetup* setup, const FrontendHost* host, bool any_mission);

/// Handles a click on NEWGAME.GUI.
///
/// PrevMenu raises the back signal. Difficulty cycles easy, medium, hard;
/// Side0/Arm and Side1/Core choose a side and refresh the lists. Start (or the
/// Campaign list, or the Missions list for any mission) checks the Campaign
/// CD, loads the campaign (the side's own campaign when fixed), binds its
/// first or selected mission, sets the player colours to 0 and 1,
/// saves the game options and raises the briefing signal. A closing panel
/// drops the list counts.
///
/// @param[in,out] setup Setup state.
/// @param host Frontend services.
/// @param control Clicked control name, compared case-insensitively; null when the panel closes.
void campaign_setup_click(CampaignSetup* setup, const FrontendHost* host, const char* control);

} // namespace oa::ui::campaign
