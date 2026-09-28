// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// SINGLE.GUI menu and the NEWGAME.GUI campaign setup panel.
#include "oa/ui/campaign/single_player.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace oa::ui::campaign {
namespace {

constexpr const char* kArmCampaign = "Arm Campaign";
constexpr const char* kCoreCampaign = "Core Campaign";

bool named(const char* control, const char* name) {
    if (control == nullptr)
        return false;
    for (; *control != '\0' && *name != '\0'; ++control, ++name)
        if (std::tolower(static_cast<unsigned char>(*control)) !=
            std::tolower(static_cast<unsigned char>(*name)))
            return false;
    return *control == *name;
}

void disc_message(const FrontendHost* host, const char* text) {
    host_message(host, host_translate(host, text));
}

// Nth entry of a NUL-separated list.
const char* list_entry(const char* list, int32_t count, int32_t index) {
    if (index < 0 || index >= count)
        return nullptr;
    const char* p = list;
    for (int32_t i = 0; i < index; ++i)
        p += std::strlen(p) + 1;
    return p;
}

} // namespace

void host_sound(const FrontendHost* host, const char* name) {
    if (host != nullptr && host->play_sound != nullptr)
        host->play_sound(host->context, name);
}

void host_cursor(const FrontendHost* host, int32_t index) {
    if (host != nullptr && host->select_cursor_animation != nullptr)
        host->select_cursor_animation(host->context, index);
}

void host_message(const FrontendHost* host, const char* text) {
    if (host != nullptr && host->message_box != nullptr)
        host->message_box(host->context, text, kMessageWidth);
}

bool host_disc_present(const FrontendHost* host) {
    return host == nullptr || host->disc_present == nullptr || host->disc_present(host->context);
}

void host_discover_archives(const FrontendHost* host) {
    if (host != nullptr && host->discover_archives != nullptr)
        host->discover_archives(host->context);
}

void host_signal(const FrontendHost* host, uint8_t value) {
    if (host != nullptr && host->signal != nullptr)
        host->signal(host->context, value);
}

void host_clear_selection(const FrontendHost* host) {
    if (host != nullptr && host->clear_selection != nullptr)
        host->clear_selection(host->context);
}

void host_control_value(const FrontendHost* host, const char* name, int32_t value) {
    if (host != nullptr && host->set_control_value != nullptr)
        host->set_control_value(host->context, name, value);
}

void host_control_y(const FrontendHost* host, const char* name, uint8_t type, int16_t y) {
    if (host != nullptr && host->set_control_y != nullptr)
        host->set_control_y(host->context, name, type, y);
}

void host_select_group(const FrontendHost* host, const char* name) {
    if (host != nullptr && host->select_group != nullptr)
        host->select_group(host->context, name);
}

void host_mark_dirty(const FrontendHost* host) {
    if (host != nullptr && host->mark_dirty != nullptr)
        host->mark_dirty(host->context);
}

void host_set_list(const FrontendHost* host, const char* name, const char* entries, int32_t count) {
    if (host != nullptr && host->set_list != nullptr)
        host->set_list(host->context, name, entries, count);
}

const char* host_translate(const FrontendHost* host, const char* text) {
    if (host == nullptr || host->translate == nullptr)
        return text;
    const char* translated = host->translate(host->context, text);
    return translated != nullptr ? translated : text;
}

void host_stop_narration(const FrontendHost* host) {
    if (host != nullptr && host->stop_narration != nullptr)
        host->stop_narration(host->context);
}

void host_play_narration(const FrontendHost* host, const char* path, uint32_t delay) {
    if (host != nullptr && host->play_narration != nullptr)
        host->play_narration(host->context, path, delay);
}

void host_redraw(const FrontendHost* host) {
    if (host != nullptr && host->redraw_frame != nullptr)
        host->redraw_frame(host->context);
}

void host_stage(const FrontendHost* host, const char* name, uint8_t stage) {
    if (host != nullptr && host->set_stage != nullptr)
        host->set_stage(host->context, name, stage);
}

void campaign_setup_init(CampaignSetup* setup) {
    std::memset(setup, 0, sizeof(*setup));
    setup->player_side[1] = 1;
    setup->player_color[1] = 1;
}

void campaign_setup_free(CampaignSetup* setup) {
    std::free(setup->missions);
    setup->missions = nullptr;
    setup->mission_count = 0;
}

std::size_t build_side_name_list(
    const char (*names)[kSideNameBytes], uint32_t count, char* out, std::size_t capacity
) {
    std::size_t used = 0;
    if (capacity == 0)
        return 0;
    for (uint32_t i = 0; i < count; ++i) {
        const std::size_t length = strnlen(names[i], kSideNameBytes);
        if (used + length + 2 > capacity)
            break;
        std::memcpy(out + used, names[i], length);
        out[used + length] = '\0';
        for (std::size_t k = 1; k < length; ++k)
            out[used + k] = static_cast<char>(out[used + k] + ' ');
        used += length + 1;
    }
    out[used] = '\0';
    return used + 1;
}

void apply_side_selection(CampaignSetup* setup, const FrontendHost* host) {
    if (setup->side == 0) {
        setup->player_side[0] = 0;
        setup->player_side[1] = 1;
        host_select_group(host, "Arm");
        host_select_group(host, "Side0");
    } else {
        setup->player_side[0] = 1;
        setup->player_side[1] = 0;
        host_select_group(host, "Core");
        host_select_group(host, "Side1");
    }
}

void show_difficulty(int32_t difficulty, const FrontendHost* host) {
    static constexpr const char* kLabels[] = {"Easy", "Medium", "Hard"};
    if (difficulty >= 0 && difficulty <= 2) {
        host_stage(host, "Difficulty", static_cast<uint8_t>(difficulty));
        host_select_group(host, kLabels[difficulty]);
    }
    host_mark_dirty(host);
}

void check_cheat_code(CampaignSetup* setup, const FrontendHost* host, const char* recent_keys) {
    if (std::strncmp(recent_keys, kCheatCode, kCheatCodeLength) != 0)
        return;
    const bool unlocked = (setup->unlock_flags & kAllMissionsUnlocked) == 0;
    host_control_value(host, "AnyMsn", unlocked ? 1 : 0);
    if (unlocked)
        setup->unlock_flags |= kAllMissionsUnlocked;
    else
        setup->unlock_flags &= static_cast<uint16_t>(~kAllMissionsUnlocked);
    if (host != nullptr && host->write_all_missions != nullptr)
        host->write_all_missions(host->context, unlocked);
    host_mark_dirty(host);
}

void single_player_enter(CampaignSetup* setup, const FrontendHost* host, const char* language) {
    if (setup->side == 0) {
        setup->player_side[0] = 0;
        setup->player_side[1] = 1;
    } else if (setup->side == 1) {
        setup->player_side[0] = 1;
        setup->player_side[1] = 0;
    }
    if ((setup->unlock_flags & kAllMissionsUnlocked) != 0)
        host_control_value(host, "AnyMsn", 1);
    if (language != nullptr && named(language, "spanish") && host != nullptr &&
        host->set_quick_key != nullptr)
        host->set_quick_key(host->context, "Skirmish", 's');
    host_cursor(host, cursor_animation::panel_ready);
}

void single_player_click(CampaignSetup* setup, const FrontendHost* host, const char* control) {
    (void)setup;
    if (control == nullptr)
        return;
    if (named(control, "NewCamp")) {
        if (host_disc_present(host)) {
            host_discover_archives(host);
            host_sound(host, "BigButton");
            host_signal(host, signal::new_campaign);
            host_cursor(host, cursor_animation::panel_leaving);
            return;
        }
        disc_message(host, kCampaignDiscMessage);
    } else if (named(control, "Skirmish")) {
        if (host_disc_present(host)) {
            host_discover_archives(host);
            host_sound(host, "skirmish");
            host_signal(host, signal::skirmish);
            host_cursor(host, cursor_animation::panel_leaving);
            return;
        }
        disc_message(host, kMultiplayerDiscMessage);
    } else if (named(control, "LoadGame")) {
        host_sound(host, "BigButton");
        host_cursor(host, cursor_animation::panel_leaving);
        if (host != nullptr && host->open_load_game != nullptr)
            host->open_load_game(host->context);
    } else if (named(control, "Options")) {
        host_sound(host, "options");
        host_clear_selection(host);
        host_cursor(host, cursor_animation::panel_leaving);
        if (host != nullptr && host->open_options != nullptr)
            host->open_options(host->context);
        return;
    } else if (named(control, "PrevMenu")) {
        host_sound(host, "Previous");
        host_cursor(host, cursor_animation::panel_leaving);
        host_signal(host, signal::back);
        return;
    } else if (named(control, "AnyMsn")) {
        if (host_disc_present(host)) {
            host_discover_archives(host);
            host_sound(host, "bigButton");
            host_signal(host, signal::any_mission);
            host_cursor(host, cursor_animation::panel_leaving);
            return;
        }
        disc_message(host, kCampaignDiscMessage);
    }
    host_clear_selection(host);
}

const char* campaign_list_entry(const CampaignSetup* setup, int32_t index) {
    return list_entry(setup->campaigns, setup->campaign_count, index);
}

void populate_campaign_list(CampaignSetup* setup, const FrontendHost* host) {
    setup->campaigns[0] = '\0';
    host_sound(host, "smlbutton");
    const char* side = setup->side >= 0 && static_cast<uint32_t>(setup->side) < setup->side_count
                           ? setup->side_names[setup->side]
                           : "";
    setup->campaign_count = oa::data::campaign::campaign_load_names(
        setup->env.files, side, setup->campaigns, sizeof(setup->campaigns)
    );
    if (setup->campaign_selected >= setup->campaign_count)
        setup->campaign_selected = 0;
    host_set_list(host, "Campaign", setup->campaigns, setup->campaign_count);
    host_mark_dirty(host);
}

void populate_mission_list(CampaignSetup* setup, const FrontendHost* host) {
    if (setup->missions == nullptr)
        setup->missions = static_cast<char (*)[oa::data::campaign::kCampaignNameBytes]>(std::calloc(
            oa::data::campaign::kMaxCampaignMissions, oa::data::campaign::kCampaignNameBytes
        ));
    setup->mission_count = 0;
    if (setup->campaign == nullptr || setup->missions == nullptr)
        return;
    const char* name = campaign_list_entry(setup, setup->campaign_selected);
    oa::data::campaign::campaign_load_file(
        setup->campaign, &setup->env, name != nullptr ? name : ""
    );
    setup->mission_count = oa::data::campaign::campaign_load_mission_list(
        setup->campaign, setup->missions, oa::data::campaign::kMaxCampaignMissions
    );
    if (setup->mission_count > oa::data::campaign::kMaxCampaignMissions)
        setup->mission_count = oa::data::campaign::kMaxCampaignMissions;
    if (setup->mission_selected >= setup->mission_count)
        setup->mission_selected = 0;
    auto* joined = static_cast<char*>(std::malloc(
        oa::data::campaign::kMaxCampaignMissions * oa::data::campaign::kCampaignNameBytes + 1
    ));
    if (joined == nullptr)
        return;
    std::size_t used = 0;
    for (int32_t i = 0; i < setup->mission_count; ++i) {
        const std::size_t length = std::strlen(setup->missions[i]);
        std::memcpy(joined + used, setup->missions[i], length + 1);
        used += length + 1;
    }
    joined[used] = '\0';
    host_set_list(host, "Missions", joined, setup->mission_count);
    std::free(joined);
    host_mark_dirty(host);
}

void new_game_enter(CampaignSetup* setup, const FrontendHost* host, bool any_mission) {
    host_redraw(host);
    setup->any_mission = any_mission;
    const auto load_background = [host](const char* name) {
        if (host != nullptr && host->load_background != nullptr)
            host->load_background(host->context, name);
    };
    if (!any_mission &&
        oa::data::campaign::campaign_count_files(setup->env.files) <= kSideSelectCampaignLimit) {
        load_background("newcampaign4x");
        setup->fixed_side_campaign = true;
    } else {
        load_background(any_mission ? "playanygame4" : "newcampaign4");
        setup->fixed_side_campaign = false;
        if (any_mission && host != nullptr) {
            if (host->set_control_y != nullptr) {
                host->set_control_y(host->context, "Campaign", kListControl, kAnyMissionCampaignY);
                host->set_control_y(
                    host->context, "CampaignKnob", kScrollControl, kAnyMissionCampaignY
                );
            }
            if (host->set_control_height != nullptr) {
                host->set_control_height(
                    host->context, "Campaign", kListControl, kAnyMissionCampaignHeight
                );
                host->set_control_height(
                    host->context, "CampaignKnob", kScrollControl, kAnyMissionCampaignHeight
                );
                host->set_control_height(
                    host->context, "Missions", kListControl, kAnyMissionMissionsHeight
                );
            }
        }
    }
    if (host != nullptr && host->zero_sequence_origins != nullptr) {
        host->zero_sequence_origins(host->context, "Side0");
        host->zero_sequence_origins(host->context, "Side1");
    }
    apply_side_selection(setup, host);
    show_difficulty(setup->difficulty, host);
    const char* focus = setup->fixed_side_campaign ? "Difficulty" : "Campaign";
    if (!setup->fixed_side_campaign || any_mission) {
        host_control_value(host, "Campaign", 1);
        host_control_value(host, "CampaignKnob", 1);
        if (host != nullptr && host->set_campaign_refills_missions != nullptr)
            host->set_campaign_refills_missions(host->context, any_mission);
        populate_campaign_list(setup, host);
        if (any_mission) {
            host_control_value(host, "Missions", 1);
            host_control_value(host, "MissionsKnob", 1);
            populate_mission_list(setup, host);
            focus = "Missions";
        }
    }
    if (host != nullptr && host->focus_control != nullptr)
        host->focus_control(host->context, focus);
    host_mark_dirty(host);
    host_cursor(host, cursor_animation::panel_ready);
}

void campaign_setup_click(CampaignSetup* setup, const FrontendHost* host, const char* control) {
    if (control == nullptr) {
        setup->campaign_count = 0;
        setup->mission_count = 0;
        return;
    }
    const bool start = setup->any_mission ? named(control, "Missions") || named(control, "Start")
                                          : named(control, "Campaign") || named(control, "Start");
    if (!start) {
        if (named(control, "PrevMenu")) {
            host_sound(host, "Previous");
            host_signal(host, signal::back);
            host_cursor(host, cursor_animation::panel_leaving);
            return;
        }
        if (named(control, "Difficulty")) {
            host_sound(host, "SmlButton");
            if (setup->difficulty == 0)
                setup->difficulty = 1;
            else if (setup->difficulty == 1)
                setup->difficulty = 2;
            else if (setup->difficulty == 2)
                setup->difficulty = 0;
        } else if (named(control, "Side0") || named(control, "Arm")) {
            host_select_group(host, "Arm");
            host_select_group(host, "Side0");
            host_sound(host, "SideSelect");
            setup->side = 0;
            setup->player_side[0] = 0;
            setup->player_side[1] = 1;
            populate_campaign_list(setup, host);
            host_control_value(host, "Campaign", setup->fixed_side_campaign ? 0 : 1);
            if (setup->any_mission)
                populate_mission_list(setup, host);
        } else if (named(control, "Side1") || named(control, "Core")) {
            host_select_group(host, "Core");
            host_select_group(host, "Side1");
            host_sound(host, "SideSelect2");
            setup->side = 1;
            setup->player_side[0] = 1;
            setup->player_side[1] = 0;
            if (!setup->fixed_side_campaign)
                populate_campaign_list(setup, host);
            if (setup->any_mission)
                populate_mission_list(setup, host);
        }
        host_clear_selection(host);
        return;
    }
    host_sound(host, "bigButton");
    if (!host_disc_present(host)) {
        disc_message(host, kCampaignDiscMessage);
        host_clear_selection(host);
        return;
    }
    host_discover_archives(host);
    const char* name = nullptr;
    if (!setup->fixed_side_campaign)
        name = campaign_list_entry(setup, setup->campaign_selected);
    else
        name = setup->player_side[0] == 0 ? kArmCampaign : kCoreCampaign;
    if (setup->campaign == nullptr) {
        host_clear_selection(host);
        return;
    }
    // The object is in campaign mode whenever this panel is showing.
    setup->campaign->kind = oa::data::campaign::SessionKind::campaign;
    setup->env.difficulty = setup->difficulty;
    oa::data::campaign::campaign_load_file(
        setup->campaign, &setup->env, name != nullptr ? name : ""
    );
    const int32_t index = setup->any_mission ? setup->mission_selected : 0;
    if (oa::data::campaign::campaign_bind_mission(setup->campaign, &setup->env, index)) {
        host_cursor(host, cursor_animation::panel_leaving);
        setup->player_color[0] = 0;
        setup->player_color[1] = 1;
        if (host != nullptr && host->save_game_options != nullptr)
            host->save_game_options(host->context);
        host_signal(
            host, setup->any_mission ? signal::any_mission_briefing : signal::campaign_briefing
        );
        return;
    }
    host_clear_selection(host);
}

} // namespace oa::ui::campaign
