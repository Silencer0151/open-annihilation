// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// End-of-game screen: score table, outcome background, palette fade and the
// ENDMSN.GUI button set.
#include "oa/ui/campaign/endgame.hpp"
#include "oa/base/game_math.hpp"

#include "oa/formats/fnt.hpp"
#include "oa/ui/frontend_state/dispatcher.hpp"
#include "oa/ui/gui_layout.hpp"
#include "oa/sim/simulation_state.hpp"
#include "oa/base/text.hpp"

#include <climits>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace oa::ui::campaign {
using base::game_math::truncate_low32;

namespace {

constexpr uint16_t kOutcomeVictory = 0x10; // Game.outcome_flags
constexpr uint32_t kTicksPerScoreStep = 60;
constexpr int32_t kBarInset = 2;
constexpr unsigned char kFirstGlyph = 0x20; // lower bytes are skipped when text is drawn
// The gadget's progress_interval: ticks between two count steps.
constexpr uint32_t kBarStepInterval = 1;
constexpr uint8_t kRejectWatching = 2; // Player.reject_reason that shows no message
constexpr int kStatusRepeats = 2;
constexpr const char* kActivateAllSound = "ActivateAllStatBars";
constexpr const char* kStatBarSound = "EndGameStatBar";
constexpr const char* kScoreSound = "EndGameScore";
constexpr int32_t kScoreBarX[score_column_count] = {0x70, 0xba, 0x104, 0x14e, 0x198, 0x1e2, 0x22c};
constexpr float kBarRateScale = 0.06666667f;
constexpr int32_t kCdCheckContinue = 5;
constexpr uint8_t kArmSide = 0;            // PlayerSetupInfo.side
constexpr int32_t kGlamourSoundVolume = 0; // mixer attenuation
// MainMenu's top edge when the game cannot continue: inside the single button
// housing of the Outcome0 background. ENDMSN.GUI's own y, 395, fits the
// MainMenu slot of Outcome1, the background of a continuing campaign.
constexpr int16_t finished_main_menu_y = 416;

bool named(const char* control, const char* name) {
    return control != nullptr && std::strcmp(control, name) == 0;
}

bool watcher(const World& world, const Player& player) {
    const PlayerSetupInfo* info = world_player_info(&world, &player);
    return info != nullptr && (info->options & OA_SETUP_OPTION_WATCHER) != 0;
}

} // namespace

void build_score_summary(World& world, const oa::data::campaign::CampaignFile* campaign) {
    Game& game = world.game;
    game.victory = (game.outcome_flags & kOutcomeVictory) >> 4;
    if (campaign != nullptr &&
        oa::data::campaign::campaign_kind(campaign) == oa::data::campaign::SessionKind::campaign) {
        game.mission_index = oa::data::campaign::campaign_mission_index(campaign);
        if (game.mission_index >= 0 &&
            static_cast<std::size_t>(game.mission_index) < sizeof(game.mission_results))
            game.mission_results[game.mission_index] = game.victory != 0 ? 'W' : 'L';
    }
    game.score_maxima[score_kills] = 10;
    game.score_maxima[score_losses] = 10;
    for (uint32_t column = score_energy_produced; column < score_column_count; ++column)
        game.score_maxima[column] = 100;
    std::memset(game.scores, 0, sizeof(game.scores));
    const float time_multiplier = campaign != nullptr ? campaign->time_multiplier : 0.0f;
    const float kill_multiplier = campaign != nullptr ? campaign->kill_multiplier : 0.0f;
    for (uint32_t index = 0; index < kScorePlayers; ++index) {
        const Player& player = game.players[index];
        const bool listed = oa::sim::simulation_state::player_active(player) &&
                            (player.in_use == 0 || !watcher(world, player));
        if (!listed && player.units_created == 0)
            continue;
        ScoreEntry& entry = game.scores[index];
        oa::base::text::copy_padded(entry.name, player.name, kScoreNameBytes);
        entry.name[kScoreNameBytes - 1] = '\0';
        entry.values[score_kills] = player.kills;
        entry.values[score_losses] = player.losses;
        entry.values[score_energy_produced] = truncate_low32(player.energy_produced_total);
        entry.values[score_metal_produced] = truncate_low32(player.metal_produced_total);
        entry.values[score_energy_wasted] = truncate_low32(player.energy_wasted_total);
        entry.values[score_metal_wasted] = truncate_low32(player.metal_wasted_total);
        const auto steps = static_cast<double>(game.tick / kTicksPerScoreStep);
        const auto score = static_cast<int32_t>(
            static_cast<uint32_t>(truncate_low32(steps * static_cast<double>(time_multiplier))) +
            static_cast<uint32_t>(truncate_low32(
                static_cast<double>(entry.values[score_kills]) *
                static_cast<double>(kill_multiplier)
            ))
        );
        entry.values[score_total] = score < 0 ? 0 : score;
        for (uint32_t column = 0; column < score_column_count; ++column)
            if (game.score_maxima[column] < entry.values[column])
                game.score_maxima[column] = entry.values[column];
    }
}

void layout_score_entries(World& world, ScoreLayout* layout) {
    Game& game = world.game;
    game.endgame_column = 0;
    layout->row_count = 0;
    int32_t y = kScoreFirstRowY;
    for (uint32_t index = 0; index < kScorePlayers; ++index) {
        const ScoreEntry& entry = game.scores[index];
        if (entry.name[0] == '\0')
            continue;
        ScoreRow& row = layout->rows[layout->row_count++];
        row.player = index;
        row.y = y;
        row.name_x = kScoreNameX;
        const PlayerSetupInfo* info = world_player_info(&world, &game.players[index]);
        row.color = info != nullptr ? info->color : 0;
        for (uint32_t column = 0; column < score_column_count; ++column) {
            ScoreBar& bar = row.bars[column];
            bar.x = kScoreBarX[column];
            bar.value = entry.values[column];
            bar.maximum = game.score_maxima[column];
            bar.rate = static_cast<float>(bar.value) * kBarRateScale;
            if (bar.rate <= 1.0f)
                bar.rate = 1.0f;
            bar.current = 0;
            bar.active = false;
            bar.running = true;
            bar.deadline = 0;
        }
        y += kScoreRowStep;
    }
}

// Every named score entry has a "<column><player>" control whose value is
// set; the rows here are exactly those entries.
void activate_score_column(ScoreLayout* layout, uint32_t column) {
    if (column >= score_column_count)
        return;
    for (uint32_t row = 0; row < layout->row_count; ++row)
        layout->rows[row].bars[column].active = true;
}

void advance_score_bars(ScoreLayout* layout, uint32_t tick) {
    for (uint32_t row = 0; row < layout->row_count; ++row) {
        for (ScoreBar& bar : layout->rows[row].bars) {
            if (!bar.active || !bar.running || bar.current >= bar.value)
                continue;
            if (static_cast<int32_t>(bar.deadline) >= static_cast<int32_t>(tick))
                continue;
            bar.current += truncate_low32(static_cast<double>(bar.rate));
            if (bar.value < bar.current) {
                bar.running = false;
                bar.current = bar.value;
            }
            bar.deadline = tick + kBarStepInterval;
        }
    }
}

bool score_bars_settled(const ScoreLayout* layout) {
    for (uint32_t row = 0; row < layout->row_count; ++row)
        for (const ScoreBar& bar : layout->rows[row].bars)
            if (!bar.active || bar.current < bar.value)
                return false;
    return true;
}

void draw_score_bar(
    const ScoreBar& bar, int32_t y, const oa::formats::fnt::Font& font, ScoreBarDraw* out
) {
    out->left = bar.x;
    out->top = y;
    out->right = bar.x + kScoreBarWidth;
    out->bottom = y + kScoreBarHeight;
    out->inner_left = out->left + kBarInset;
    out->inner_top = out->top + kBarInset;
    out->inner_right = out->right - kBarInset;
    out->inner_bottom = out->bottom - kBarInset;
    const double fraction = static_cast<double>(bar.current) / static_cast<double>(bar.maximum);
    out->fill_right =
        truncate_low32(fraction * static_cast<double>(kScoreBarWidth - 2 * kBarInset)) +
        out->inner_left;
    std::snprintf(out->label, sizeof(out->label), "%d", bar.current);
    const auto width = static_cast<int32_t>(oa::formats::fnt::measure_text(font, out->label));
    const int32_t line = oa::formats::fnt::line_height(font);
    out->label_x = kScoreBarWidth / 2 - width / 2 + bar.x;
    out->label_y = kScoreBarHeight / 2 - line / 2 + y;
}

void place_score_name(
    const ScoreRow& row, const char* name, const oa::formats::fnt::Font& font, ScoreNameDraw* out
) {
    const std::string_view text = name != nullptr ? name : "";
    const auto width = static_cast<int32_t>(oa::formats::fnt::measure_text(font, text));
    out->x = kScoreNameWidth / 2 + row.name_x - width / 2;
    out->y = (kScoreRowStep - oa::formats::fnt::line_height(font)) / 2 + row.y;
    // The label's width limits the glyphs drawn: the name stops at the first
    // glyph wider than what is left. Bytes without a glyph take no room.
    int32_t room = kScoreNameWidth;
    std::size_t kept = 0;
    for (; kept < text.size() && kept + 1 < sizeof(out->text); ++kept) {
        const auto code = static_cast<unsigned char>(text[kept]);
        if (code < kFirstGlyph || !font.glyphs[code])
            continue;
        const int32_t advance = font.glyphs[code]->width;
        if (advance > room)
            break;
        room -= advance;
    }
    std::memcpy(out->text, text.data(), kept);
    out->text[kept] = '\0';
}

bool campaign_can_continue(oa::data::campaign::CampaignFile* campaign, const Game& game) {
    if (campaign == nullptr ||
        oa::data::campaign::campaign_kind(campaign) != oa::data::campaign::SessionKind::campaign)
        return false;
    return game.victory == 0 ||
           oa::data::campaign::campaign_has_next_mission(campaign, game.mission_index + 1);
}

void update_end_mission_buttons(
    oa::data::campaign::CampaignFile* campaign, const Game& game, const FrontendHost* host
) {
    if (!campaign_can_continue(campaign, game)) {
        host_control_value(host, "MainMenu", 1);
        host_control_y(
            host,
            "MainMenu",
            static_cast<uint8_t>(gui_layout::GadgetType::button),
            finished_main_menu_y
        );
    } else {
        static constexpr const char* kControls[] = {
            "Start",
            "LoadGame",
            "SaveGame",
            "KNOB",
            "Missions",
            "Difficulty",
            "AdjustDiff",
            "MainMenu"
        };
        for (const char* control : kControls)
            host_control_value(host, control, 1);
    }
    host_mark_dirty(host);
}

void choose_outcome_background(
    const oa::data::campaign::CampaignFile* campaign,
    bool victory,
    const oa::data::campaign::CampaignFiles* files,
    OutcomeBackground* out
) {
    out->glamour[0] = '\0';
    out->palette_screen = nullptr;
    const bool is_campaign = campaign != nullptr && oa::data::campaign::campaign_kind(campaign) ==
                                                        oa::data::campaign::SessionKind::campaign;
    const char* glamour =
        campaign != nullptr
            ? oa::data::campaign::campaign_path(campaign, oa::data::campaign::CampaignPath::glamour)
            : nullptr;
    if (is_campaign && victory && glamour != nullptr) {
        char path[0x100];
        std::snprintf(path, sizeof(path), "bitmaps/glamour/%s", glamour);
        const int32_t size =
            files != nullptr && files->size != nullptr ? files->size(files->context, path) : -1;
        if (size <= 0)
            std::snprintf(out->glamour, sizeof(out->glamour), "glamour/Arm01.PCX");
        else
            std::snprintf(out->glamour, sizeof(out->glamour), "glamour/%s", glamour);
        return;
    }
    out->palette_screen = is_campaign ? "Outcome1" : "Outcome0";
}

void endgame_reset_step_timer(Game& game, uint32_t tick) {
    game.endgame_shade_countdown = kShadeSteps;
    game.endgame_fade_tick = tick + 1;
    game.endgame_fade_done = 0;
}

int32_t endgame_shade_step(Game& game, uint32_t tick) {
    if (!(game.endgame_fade_tick < tick))
        return INT32_MIN;
    const int32_t level = game.endgame_shade_countdown - kShadeBias;
    game.endgame_fade_tick = tick + 1;
    if (--game.endgame_shade_countdown == 0)
        game.endgame_fade_done = 1;
    return level;
}

void endgame_build_fade(
    Game& game, EndgameFade* fade, const uint8_t* desired, const uint8_t* current, int32_t steps
) {
    std::memmove(fade->desired, desired, kPaletteBytes);
    std::memmove(fade->current, current, kPaletteBytes);
    game.endgame_fade_done = 0;
    for (std::size_t i = 0; i < kPaletteBytes; ++i) {
        const int32_t from = current[i];
        const int32_t to = desired[i];
        int32_t step = 0;
        if (to < from) {
            step = steps != 0 ? (to - from) / steps : -1;
            if (step > -1)
                step = -1;
        } else if (to != from) {
            step = steps != 0 ? (to - from) / steps : 1;
            if (step < 1)
                step = 1;
        }
        fade->steps[i] = static_cast<int8_t>(step);
    }
}

bool endgame_fade_step(Game& game, EndgameFade* fade, uint32_t tick) {
    if (tick < game.endgame_fade_tick)
        return false;
    for (std::size_t i = 0; i < kPaletteBytes; ++i) {
        const int32_t step = fade->steps[i];
        int32_t value = static_cast<int32_t>(fade->current[i]) + step;
        const int32_t target = fade->desired[i];
        if (step < 0 ? value < target : target < value)
            value = target;
        fade->current[i] = static_cast<uint8_t>(value);
    }
    if (std::memcmp(fade->current, fade->desired, kPaletteBytes) == 0)
        game.endgame_fade_done = 1;
    game.endgame_fade_tick = tick + 1;
    return true;
}

void set_endgame_state(Game& game, uint8_t state) {
    game.endgame_state = state;
}

void start_glamour_sound(
    const Game& game, const oa::data::campaign::CampaignFile* campaign, const EndgameHost& host
) {
    if (game.mode == oa::ui::frontend_state::mode_id::in_match || campaign == nullptr)
        return;
    const char* path = oa::data::campaign::campaign_path(
        campaign, oa::data::campaign::CampaignPath::glamour_sound
    );
    if (path != nullptr && host.play_stream != nullptr)
        host.play_stream(host.context, path, kGlamourSoundVolume, kGlamourSoundDelay);
}

bool draw_endgame_wait_frame(const Game& game, const EndgameHost& host) {
    if (game.endgame_state != OA_ENDGAME_WAIT_MESSAGE)
        return false;
    if (host.draw_wait_frame != nullptr)
        host.draw_wait_frame(host.context);
    return true;
}

namespace {

uint32_t host_now(const EndgameHost& host) {
    return host.now != nullptr ? host.now(host.context) : 0;
}

uint32_t host_rate(const EndgameHost& host) {
    return host.ticks_per_second != nullptr ? host.ticks_per_second(host.context) : 0;
}

bool host_input(const EndgameHost& host) {
    return host.input != nullptr && host.input(host.context);
}

void endgame_sound(const EndgameHost& host, const char* name) {
    if (host.play_sound != nullptr)
        host.play_sound(host.context, name);
}

bool is_kind(
    const oa::data::campaign::CampaignFile* campaign, oa::data::campaign::SessionKind kind
) {
    return campaign != nullptr && oa::data::campaign::campaign_kind(campaign) == kind;
}

// The frontend state a final campaign victory leaves for: the side's ending
// movies (the first player record's side), or the main menu when movies are off.
uint8_t ending_state(World& world, const EndgameHost& host) {
    if (host.movies_enabled == nullptr || !host.movies_enabled(host.context))
        return oa::ui::frontend_state::state_id::main_menu;
    const PlayerSetupInfo* info = world_player_info(&world, &world.game.players[0]);
    return info == nullptr || info->side == kArmSide
               ? oa::ui::frontend_state::state_id::movies_3_then_5
               : oa::ui::frontend_state::state_id::movies_4_then_5;
}

// ENDMSN.GUI with its score rows and buttons.
void open_end_panel(
    World& world,
    oa::data::campaign::CampaignFile* campaign,
    EndgameScreen& screen,
    const EndgameHost& host
) {
    if (host.open_panel != nullptr)
        host.open_panel(host.context);
    layout_score_entries(world, &screen.layout);
    update_end_mission_buttons(campaign, world.game, host.frontend);
}

} // namespace

void endgame_tick(
    World& world,
    oa::data::campaign::CampaignFile* campaign,
    EndgameScreen& screen,
    const EndgameHost& host
) {
    Game& game = world.game;
    const bool multiplayer = is_kind(campaign, oa::data::campaign::SessionKind::multiplayer);
    const bool in_campaign = is_kind(campaign, oa::data::campaign::SessionKind::campaign);
    switch (game.endgame_state) {
    case OA_ENDGAME_CAPTURE: {
        if (!multiplayer) {
            screen.frame_captured = false;
            game.endgame_state = OA_ENDGAME_SHADE_START;
            break;
        }
        screen.frame_captured = host.capture_frame != nullptr && host.capture_frame(host.context);
        if (host.report_end != nullptr)
            host.report_end(host.context);
        game.endgame_state = OA_ENDGAME_WAIT_MESSAGE;
        const uint8_t local_index =
            game.local_player_index < kScorePlayers ? game.local_player_index : 0;
        Player& local = game.players[local_index];
        if (local.reject_reason != 0 && local.reject_reason != kRejectWatching) {
            if (host.disconnect_message != nullptr)
                host.disconnect_message(host.context, local.reject_reason);
            local.reject_reason = 0;
        }
        break;
    }
    case OA_ENDGAME_WAIT_MESSAGE:
        if (host.message_open != nullptr && host.message_open(host.context)) {
            draw_endgame_wait_frame(game, host);
            break;
        }
        game.endgame_state = OA_ENDGAME_SHADE_START;
        break;
    case OA_ENDGAME_SHADE_START:
        endgame_reset_step_timer(game, host_now(host));
        game.endgame_state = OA_ENDGAME_SHADING;
        break;
    case OA_ENDGAME_SHADING:
        if (game.endgame_fade_done == 0) {
            const int32_t level = endgame_shade_step(game, host_now(host));
            if (level != INT32_MIN && host.apply_shade != nullptr)
                host.apply_shade(host.context, level);
        } else {
            if (host.finish_shade != nullptr)
                host.finish_shade(host.context);
            game.endgame_state = OA_ENDGAME_DISC_CHECK;
        }
        break;
    case OA_ENDGAME_DISC_CHECK:
        if (in_campaign && !host_disc_present(host.frontend)) {
            if (host.open_cd_check != nullptr)
                host.open_cd_check(host.context);
            game.endgame_state = OA_ENDGAME_PANEL;
        } else {
            game.endgame_state = OA_ENDGAME_OUTCOME;
        }
        break;
    case OA_ENDGAME_OUTCOME: {
        if (host.show_outcome != nullptr)
            host.show_outcome(host.context);
        const bool more_missions =
            campaign != nullptr &&
            oa::data::campaign::campaign_has_next_mission(campaign, game.mission_index + 1);
        const bool won = (game.outcome_flags & kOutcomeVictory) != 0;
        if (in_campaign && won && !more_missions && game.no_movie == 0) {
            if (host.play_ending != nullptr)
                host.play_ending(host.context, ending_state(world, host));
            break;
        }
        const uint8_t* glamour = in_campaign && won && host.glamour_palette != nullptr
                                     ? host.glamour_palette(host.context)
                                     : nullptr;
        if (glamour != nullptr) {
            static const uint8_t kBlack[kPaletteBytes]{};
            endgame_build_fade(game, &screen.fade, glamour, kBlack, kGlamourFadeSteps);
            if (host.apply_palette != nullptr)
                host.apply_palette(host.context, screen.fade.current);
            game.endgame_fade_tick = host_now(host) + 1;
            game.endgame_state = OA_ENDGAME_GLAMOUR;
            break;
        }
        open_end_panel(world, campaign, screen, host);
        game.endgame_state = OA_ENDGAME_STAT_BARS;
        break;
    }
    case OA_ENDGAME_GLAMOUR: {
        if (game.endgame_fade_done == 0) {
            if (endgame_fade_step(game, &screen.fade, host_now(host)) &&
                host.apply_palette != nullptr)
                host.apply_palette(host.context, screen.fade.current);
            game.endgame_next_tick = host_now(host) + host_rate(host);
            screen.glamour_sound_started = false;
            break;
        }
        if (!screen.glamour_sound_started) {
            start_glamour_sound(game, campaign, host);
            screen.glamour_sound_started = true;
        }
        if (game.endgame_next_tick >= host_now(host))
            break;
        const bool held = host.button_held != nullptr && host.button_held(host.context);
        if (host_input(host) || held) {
            if (host.stop_stream != nullptr)
                host.stop_stream(host.context);
            open_end_panel(world, campaign, screen, host);
            game.endgame_state = OA_ENDGAME_STAT_BARS;
        }
        if (host_rate(host) * kContinueDelaySeconds + game.endgame_next_tick < host_now(host) &&
            host.draw_continue != nullptr)
            host.draw_continue(host.context, kClickToContinue);
        break;
    }
    case OA_ENDGAME_STAT_BARS: {
        if (score_bars_settled(&screen.layout)) {
            if (host.leave_to_panel != nullptr)
                host.leave_to_panel(host.context);
            game.endgame_state = OA_ENDGAME_PANEL;
            break;
        }
        if (host.draw_panel != nullptr)
            host.draw_panel(host.context);
        const bool input = host_input(host);
        const bool skip = input && !multiplayer;
        if (host_now(host) <= game.endgame_next_tick && !skip)
            break;
        if (input) {
            for (uint32_t column = 0; column < score_column_count; ++column)
                activate_score_column(&screen.layout, column);
            endgame_sound(host, kActivateAllSound);
        }
        const int32_t column = game.endgame_column;
        if (column >= 0 && column < static_cast<int32_t>(score_column_count)) {
            activate_score_column(&screen.layout, static_cast<uint32_t>(column));
            endgame_sound(host, column == score_total ? kScoreSound : kStatBarSound);
        }
        if (multiplayer && host.send_status != nullptr)
            for (int repeat = 0; repeat < kStatusRepeats; ++repeat)
                host.send_status(host.context);
        game.endgame_next_tick = host_now(host) + kStatColumnDelay;
        ++game.endgame_column;
        break;
    }
    case OA_ENDGAME_PANEL:
        if (host.draw_panel != nullptr)
            host.draw_panel(host.context);
        break;
    default:
        break;
    }
}

int32_t cd_check_click(const FrontendHost* host, const char* control, int32_t state) {
    if (control == nullptr)
        return state;
    if (named(control, "OK")) {
        host_sound(host, "Options");
        if (host_disc_present(host))
            return kCdCheckContinue;
        host_message(host, host_translate(host, kCampaignDiscMessage));
    }
    host_clear_selection(host);
    return state;
}

void copy_status_text(char* status, const char* text) {
    oa::base::text::copy_padded(status, text != nullptr ? text : "", kStatusTextBytes);
    status[kStatusTextBytes - 1] = '\0';
}

} // namespace oa::ui::campaign
