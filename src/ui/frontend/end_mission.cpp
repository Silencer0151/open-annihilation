// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// ENDMSN.GUI setup and click handler, and the end-game state that opens it.
#include "oa/ui/frontend/end_mission.hpp"

#include "oa/ui/frontend_state/app_modes.hpp"
#include "oa/ui/campaign/single_player.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>

namespace oa::ui::frontend {

namespace {

constexpr std::string_view kBigButtonSound = "BigButton";
constexpr std::string_view kDifficultySound = "SKirmish";
constexpr std::string_view kMissionsList = "Missions";
constexpr std::string_view kKnob = "KNOB";
constexpr int16_t kKnobTrackMargin = 3;
constexpr int32_t kCursorPanelReady = 0x13;
constexpr int32_t kCursorPanelLeaving = 0x14;
constexpr int32_t kMusicKindStopped = 4;
constexpr int32_t kDifficultyCount = 3;
constexpr uint8_t kSignalStartMission = 10;
constexpr int32_t kEndGameMode = 7;
constexpr uint16_t kOutcomeFlagsCleared = quit_flag::out_of_game | quit_flag::leave_application;
constexpr uint8_t kGuiFlagOnline = 0x10; // ? Game.gui_flags bit 4: an online session

namespace app_mode {
constexpr int32_t leave_to_frontend = 1;
constexpr int32_t frontend = 2;
} // namespace app_mode

void play(const EndMissionContext& context, std::string_view name) {
    if (context.host.play_sound != nullptr)
        context.host.play_sound(context.host.context, name.data());
}

const char* translate(const EndMissionContext& context, const char* text) {
    if (context.host.translate == nullptr)
        return text;
    const char* replacement = context.host.translate(context.host.context, text);
    return replacement != nullptr ? replacement : text;
}

void set_name(std::array<char, kControlNameBytes>& out, std::string_view name) {
    out.fill('\0');
    std::memcpy(out.data(), name.data(), std::min(name.size(), out.size() - 1));
}

// The campaign package's button helpers act on this panel's records.
campaign::FrontendHost panel_host(Panel& panel) {
    campaign::FrontendHost host{};
    host.context = &panel;
    host.set_control_value = [](void* context, const char* name, int32_t value) {
        panel_set_active(*static_cast<Panel*>(context), name, static_cast<uint8_t>(value));
    };
    host.select_group = [](void* context, const char* name) {
        (void)panel_set_group_value(*static_cast<Panel*>(context), name, 1);
    };
    host.mark_dirty = [](void* context) { static_cast<Panel*>(context)->dirty = true; };
    host.set_stage = [](void* context, const char* name, uint8_t stage) {
        (void)panel_set_stage(*static_cast<Panel*>(context), name, stage);
    };
    return host;
}

/// Packs mission names into a scroll list, NUL-separated.
///
/// Each name follows the marker of its recorded result (L, W or U) and a
/// space; a name without a result has no marker. Packing stops at
/// kMissionEntries rows or when the next row does not fit.
///
/// @param names Mission names, kCampaignNameBytes each.
/// @param results Recorded result letter of each mission.
/// @param count Number of missions.
/// @param[out] out Packed list; cleared first.
/// @return Number of rows packed.
int32_t pack_scroll_list_items(
    const char (*names)[data::campaign::kCampaignNameBytes],
    const char* results,
    int32_t count,
    std::array<char, kMissionEntries * kMissionEntryBytes>& out
) {
    out.fill('\0');
    int32_t packed = 0;
    std::size_t used = 0;
    for (int32_t index = 0; index < count && index < static_cast<int32_t>(kMissionEntries);
         ++index) {
        const char result = results[index];
        char marker = 0;
        if (result == 'L')
            marker = static_cast<char>(mission_marker::lost);
        else if (result == 'W')
            marker = static_cast<char>(mission_marker::won);
        else if (result == 'U')
            marker = static_cast<char>(mission_marker::unplayed);
        const std::size_t length = ::strnlen(names[index], data::campaign::kCampaignNameBytes - 1);
        const std::size_t needed = (marker != 0 ? 2U : 0U) + length + 1U;
        if (used + needed > out.size())
            break;
        if (marker != 0) {
            out[used++] = marker;
            out[used++] = ' ';
        }
        std::memcpy(out.data() + used, names[index], length);
        used += length + 1U;
        ++packed;
    }
    return packed;
}

void pack_missions(EndMissionContext& context) {
    auto names = std::make_unique<char[][data::campaign::kCampaignNameBytes]>(kMissionEntries);
    const int32_t count =
        data::campaign::campaign_load_mission_list(context.campaign, names.get(), kMissionEntries);
    context.mission_count = pack_scroll_list_items(
        names.get(), context.world->game.mission_results, count, context.missions
    );
}

bool campaign_continues(const EndMissionContext& context) {
    return context.world != nullptr &&
           campaign::campaign_can_continue(context.campaign, context.world->game);
}

// The local player's info record marks it as a watcher.
bool local_watcher(const World& world) {
    const Game& game = world.game;
    if (game.local_player_index >= OA_PLAYER_COUNT)
        return false;
    const Player& local = game.players[game.local_player_index];
    const PlayerSetupInfo* info = world_player_info(&world, &local);
    return local.in_use != 0 && info != nullptr && (info->options & OA_SETUP_OPTION_WATCHER) != 0;
}

bool online_session(const EndMissionContext& context) {
    return context.world != nullptr && (context.world->game.gui_flags & kGuiFlagOnline) != 0;
}

} // namespace

void end_mission_enter(Panel& panel, EndMissionContext& context) noexcept {
    context.continuing = campaign_continues(context);
    context.enter_control.fill('\0');
    context.focus.fill('\0');
    if (!context.continuing) {
        context.palette = kFinishedPalette;
        set_name(context.focus, "MainMenu");
    } else {
        if (context.env != nullptr)
            (void)data::campaign::campaign_bind_mission(
                context.campaign, context.env, context.world->game.mission_index
            );
        context.palette = kContinuePalette;
        set_name(context.enter_control, "Start");
    }
    if (campaign_continues(context)) {
        pack_missions(context);
        if (auto* knob = panel_control(panel, kKnob))
            knob->slider.range =
                static_cast<int16_t>(knob->height - knob->slider.knob_size - kKnobTrackMargin);
        const Game& game = context.world->game;
        if (auto* list = panel_control(panel, kMissionsList)) {
            list->list_selection = static_cast<int16_t>(game.mission_index);
            list->list_selection =
                static_cast<int16_t>(game.mission_index + (game.victory != 0 ? 1 : 0));
        }
        const auto host = panel_host(panel);
        campaign::show_difficulty(context.difficulty, &host);
    }
    if (!context.saved_games_offered) {
        panel_set_grayed(panel, "LoadGame", true);
        panel_set_grayed(panel, "SaveGame", true);
    }
    context.victory_title = context.world != nullptr && context.world->game.victory != 0 &&
                            !local_watcher(*context.world);
    if (online_session(context) || context.service_launch) {
        const std::size_t length =
            ::strnlen(context.service_label.data(), context.service_label.size());
        const bool fits = length != 0 && length < kMaxServiceLabelLength + 1;
        panel_set_text(
            panel,
            "MainMenu",
            fits ? std::string_view(context.service_label.data(), length)
                 : std::string_view(kServiceReturnLabel)
        );
    }
    panel.dirty = true;
    if (context.frontend != nullptr && context.state != nullptr)
        context.frontend->set_cursor(*context.state, kCursorPanelReady);
}

EndMissionAction end_mission_on_click(Panel& panel, EndMissionContext& context) noexcept {
    if (panel.selected == kNoSelection) {
        if (context.host.release_outcome_frames != nullptr)
            context.host.release_outcome_frames(context.host.context);
        context.missions.fill('\0');
        context.mission_count = 0;
        if (online_session(context) && context.host.leave_game != nullptr)
            context.host.leave_game(context.host.context);
        if (context.host.set_music_kind != nullptr)
            context.host.set_music_kind(context.host.context, kMusicKindStopped);
        return EndMissionAction::closed;
    }
    if (panel_selected_is(panel, "LoadGame")) {
        play(context, kBigButtonSound);
        panel_clear_selection(panel);
        return EndMissionAction::open_load_game;
    }
    if (panel_selected_is(panel, "SaveGame")) {
        play(context, kBigButtonSound);
        panel_clear_selection(panel);
        return EndMissionAction::open_save_game;
    }
    auto* state = context.state;
    auto* frontend = context.frontend;
    if (panel_selected_is(panel, "Start") || panel_selected_is(panel, kMissionsList)) {
        const bool present =
            context.host.disc_present == nullptr || context.host.disc_present(context.host.context);
        if (!present) {
            if (context.host.show_message != nullptr)
                context.host.show_message(
                    context.host.context,
                    translate(context, campaign::kCampaignDiscMessage),
                    kEndMissionMessageWidth
                );
            panel_clear_selection(panel);
        }
        if (context.host.refresh_archives != nullptr)
            context.host.refresh_archives(context.host.context);
        play(context, kBigButtonSound);
        if (state != nullptr && frontend != nullptr) {
            state->pending_signal = kSignalStartMission;
            frontend->set_cursor_visible(*state, 1);
            frontend->set_cursor(*state, kCursorPanelLeaving);
        }
        const auto* list = panel_control(panel, kMissionsList);
        const int32_t mission = list != nullptr ? list->list_selection : 0;
        const bool bound =
            context.campaign != nullptr && context.env != nullptr &&
            data::campaign::campaign_bind_mission(context.campaign, context.env, mission);
        if (bound) {
            if (state != nullptr && frontend != nullptr) {
                ui::frontend_state::reset_to_main_menu(*state, *frontend);
                state->session_flags &= static_cast<uint16_t>(~ui::frontend_state::flags::loading);
                state->session_flags |= ui::frontend_state::flags::single_player;
                state->session_flags &=
                    static_cast<uint16_t>(~ui::frontend_state::flags::live_game);
                state->outcome_flags &= static_cast<uint16_t>(~kOutcomeFlagsCleared);
                ui::frontend_state::set_frontend_state(
                    *state, *frontend, ui::frontend_state::state_id::briefing_to_end_mission
                );
                frontend->set_app_mode(*state, app_mode::frontend);
            }
            return EndMissionAction::start_mission;
        }
        panel_clear_selection(panel);
        return EndMissionAction::none;
    }
    if (panel_selected_is(panel, "MainMenu")) {
        play(context, kBigButtonSound);
        if (state != nullptr && frontend != nullptr) {
            ui::frontend_state::set_frontend_state(
                *state, *frontend, ui::frontend_state::state_id::main_menu
            );
            frontend->set_app_mode(*state, app_mode::leave_to_frontend);
            frontend->set_cursor_visible(*state, 1);
            if (!context.service_launch)
                frontend->set_cursor(*state, kCursorPanelLeaving);
        }
        return EndMissionAction::main_menu;
    }
    if (panel_selected_is(panel, "Difficulty")) {
        play(context, kDifficultySound);
        if (context.difficulty >= 0 && context.difficulty < kDifficultyCount) {
            context.difficulty = (context.difficulty + 1) % kDifficultyCount;
            context.skirmish_difficulty = context.difficulty;
        }
    }
    panel_clear_selection(panel);
    return EndMissionAction::none;
}

void end_mission_open(
    Panel& panel, EndMissionContext& context, campaign::ScoreLayout& scores
) noexcept {
    end_mission_enter(panel, context);
    if (context.world != nullptr) {
        campaign::layout_score_entries(*context.world, &scores);
        const auto host = panel_host(panel);
        campaign::update_end_mission_buttons(context.campaign, context.world->game, &host);
    }
    panel.dirty = true;
    if (context.frontend != nullptr && context.state != nullptr) {
        context.frontend->set_app_mode(*context.state, kEndGameMode);
        context.frontend->set_endgame_state(*context.state, OA_ENDGAME_STAT_BARS);
    }
}

} // namespace oa::ui::frontend
