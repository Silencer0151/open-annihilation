// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Game session lifecycle.
#include "oa/sim/session.hpp"

#include "oa/data/campaign/campaign_file.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace oa::sim::session {
namespace {

constexpr uint16_t kOptionFlagsClearedAtInit =
    OA_CONSOLE_FLAG_FULL_RADAR | OA_CONSOLE_FLAG_DOUBLE_SHOT | OA_CONSOLE_FLAG_HALF_SHOT;
constexpr uint16_t kOutcomeClearedAtInit = 0x0001 | 0x0002;
constexpr uint16_t kOutcomeClearedAtFrontend = 0x0010 | 0x0004;
constexpr uint8_t kSessionLiveGame = 0x01;
constexpr uint8_t kSessionLoading = 0x04;
constexpr uint8_t kSessionSinglePlayer = 0x08;
constexpr uint16_t kSimPaused = 0x0001;
constexpr uint16_t kLoadMissionStarted = 0x0002;
constexpr int32_t kCursorPanelReady = 0x13;
constexpr int32_t kMaxSlots = OA_PLAYER_COUNT;
// Game.message_filter at session start-up: kinds 1, 4 and 8.
constexpr int32_t kInitialMessageFilter = 3;

void run(const SessionHost* host, SessionStep step) {
    if (host != nullptr && host->step != nullptr)
        host->step(host->context, step);
}

void app_mode(const SessionHost* host, AppMode mode) {
    if (host != nullptr && host->set_app_mode != nullptr)
        host->set_app_mode(host->context, mode);
}

bool slot_active(const SessionHost* host, uint32_t player) {
    return host != nullptr && host->slot_active != nullptr &&
           host->slot_active(host->context, player);
}

oa::data::campaign::SessionKind session_kind(const Session* session) {
    return session->campaign != nullptr ? session->campaign->kind
                                        : oa::data::campaign::SessionKind::none;
}

void clear_frontend_flags(Game* game) {
    game->session_flags &= static_cast<uint8_t>(~kSessionLoading);
    game->session_flags &= static_cast<uint8_t>(~kSessionSinglePlayer);
    game->session_flags &= static_cast<uint8_t>(~kSessionLiveGame);
}

} // namespace

void session_init(Session* session, const SessionHost* host) {
    Game* game = &session->world->game;
    run(host, SessionStep::query_display_size);
    run(host, SessionStep::create_offscreen_surface);
    game->outcome_flags &= static_cast<uint16_t>(~kOutcomeClearedAtInit);
    game->restart_requested = 0;
    run(host, SessionStep::reset_mode_state);
    game->units_per_player = game->max_units_setting;
    game->max_units = game->units_per_player;
    game->message_filter = kInitialMessageFilter;
    game->console_flags &= static_cast<uint16_t>(~kOptionFlagsClearedAtInit);
    run(host, SessionStep::clear_session_flag);
    run(host, SessionStep::load_hud_resources);
    run(host, SessionStep::init_audio_devices);
    run(host, SessionStep::reset_ui_state);
    run(host, SessionStep::query_system_info);
    run(host, SessionStep::init_record_length_table);
    run(host, SessionStep::register_status_labels);
    run(host, SessionStep::init_common_fonts);
    run(host, SessionStep::load_active_palette);
    run(host, SessionStep::load_all_sounds);
    run(host, SessionStep::load_palette_tables);
    std::free(session->skirmish_info);
    session->skirmish_info = static_cast<uint8_t*>(std::calloc(1, kSkirmishInfoBytes));
    run(host, SessionStep::load_game_settings);
    run(host, SessionStep::load_cd_list);
    run(host, SessionStep::apply_cd_settings);
    run(host, SessionStep::apply_saved_volumes);
    run(host, SessionStep::load_commander_hud);
    run(host, SessionStep::load_logo_textures);
    run(host, SessionStep::load_gui_palette);
    run(host, SessionStep::init_gui_context);
    run(host, SessionStep::load_gui_fonts);
    game->debug_overlay = 0;
    game->capture_enabled = 0;
    game->output_directory_changed = 0;
    game->capture_rate_changed = 0;
    game->cleared_with_change_flags = 0;
    game->saved_game = 0;
    session->saved_game = nullptr;
    game->playing_movie = 0;
    app_mode(host, AppMode::boot);
    run(host, SessionStep::clear_state_buffer);
    game->gui_clear_quick_keys_on_draw = 1;
    int32_t limit = host != nullptr && host->read_setting != nullptr
                        ? host->read_setting(host->context, "UnitLimit", kDefaultUnitLimit)
                        : kDefaultUnitLimit;
    if (limit > kMaxUnitLimit)
        limit = kMaxUnitLimit;
    else if (limit < kMinUnitLimit)
        limit = kMinUnitLimit;
    session->unit_limit = limit;
    game->max_units_setting = static_cast<uint16_t>(limit);
}

void session_shutdown(Session* session, const SessionHost* host) {
    run(host, SessionStep::save_cd_list);
    run(host, SessionStep::shutdown_resource_table);
    run(host, SessionStep::free_gui_context);
    run(host, SessionStep::free_logo_textures);
    run(host, SessionStep::free_side_records);
    run(host, SessionStep::free_string_list);
    run(host, SessionStep::free_context_pair);
    run(host, SessionStep::shutdown_audio_devices);
    run(host, SessionStep::free_record_list);
    run(host, SessionStep::destroy_offscreen_surface);
    run(host, SessionStep::flush_tag_index);
    std::free(session->skirmish_info);
    session->skirmish_info = nullptr;
    run(host, SessionStep::free_context_resource);
    run(host, SessionStep::free_record_length_table);
    run(host, SessionStep::clear_map_list_cache);
    run(host, SessionStep::free_subsystem_object);
}

void session_teardown(Session* session, const SessionHost* host) {
    Game* game = &session->world->game;
    game->session_flags &= static_cast<uint8_t>(~kSessionLoading);
    run(host, SessionStep::stop_cd_audio);
    run(host, SessionStep::build_score_summary);
    run(host, SessionStep::free_unit_tables);
    run(host, SessionStep::free_explosion_pieces);
    run(host, SessionStep::free_session_orders);
    run(host, SessionStep::clear_player_slots);
    run(host, SessionStep::free_radar_surfaces);
    run(host, SessionStep::free_cell_buffers);
    world_free_tables(session->world);
    run(host, SessionStep::free_unit_data_tables);
    run(host, SessionStep::free_feature_array);
    run(host, SessionStep::free_listener_table);
    if (session_kind(session) == oa::data::campaign::SessionKind::multiplayer)
        run(host, SessionStep::finish_reload_sync);
}

void frontend_teardown(Session* session, const SessionHost* host) {
    run(host, SessionStep::redraw_frame);
    if ((session->world->game.session_flags & kSessionLiveGame) != 0) {
        run(host, SessionStep::report_quit_outcome);
        run(host, SessionStep::teardown_all_slots);
        run(host, SessionStep::close_multiplayer_session);
    }
    session_teardown(session, host);
    run(host, SessionStep::clear_fatal_error);
}

void enter_frontend_mode(Session* session, const SessionHost* host) {
    if (host != nullptr && host->select_cursor_animation != nullptr)
        host->select_cursor_animation(host->context, kCursorPanelReady);
    run(host, SessionStep::draw_cursor);
    Game* game = &session->world->game;
    clear_frontend_flags(game);
    game->outcome_flags &= static_cast<uint16_t>(~kOutcomeClearedAtFrontend);
    run(host, SessionStep::reset_paired_fields);
    app_mode(host, AppMode::frontend);
}

void reset_to_frontend_mode(Session* session, const SessionHost* host) {
    run(host, SessionStep::reset_frontend_state);
    Game* game = &session->world->game;
    clear_frontend_flags(game);
    game->outcome_flags &= static_cast<uint16_t>(~kOutcomeClearedAtFrontend);
    run(host, SessionStep::reset_paired_fields);
    run(host, SessionStep::reset_panel_keyboard);
    app_mode(host, AppMode::frontend);
}

void enter_skirmish_setup(Session* session, const SessionHost* host) {
    Game* game = &session->world->game;
    game->player_count = 2;
    run(host, SessionStep::init_skirmish_slots);
    app_mode(host, AppMode::skirmish_setup);
}

void apply_session_flags(Game* game, const int32_t* record) {
    game->session_rules = record[0];
    game->visibility_flags =
        static_cast<uint8_t>((game->visibility_flags & ~0x04) | ((record[3] & 1) << 2));
    game->visibility_flags =
        static_cast<uint8_t>((game->visibility_flags & ~0x01) | (record[1] & 1));
    game->visibility_flags =
        static_cast<uint8_t>((game->visibility_flags & ~0x02) | ((record[2] & 1) << 1));
}

bool begin_mission(Session* session, const SessionHost* host) {
    Game* game = &session->world->game;
    run(host, SessionStep::seed_random);
    game->tick = 0;
    const auto kind = session_kind(session);
    if (kind == oa::data::campaign::SessionKind::campaign) {
        session->multiplayer_rules = 0;
        int32_t record[4];
        std::memcpy(record, game->session_record, sizeof(record));
        apply_session_flags(game, record);
        run(host, SessionStep::load_unit_availability);
    } else if (kind == oa::data::campaign::SessionKind::skirmish) {
        game->units_per_player = game->max_units_setting;
        session->multiplayer_rules = 1;
        if (session->skirmish_info != nullptr) {
            int32_t record[4];
            std::memcpy(record, session->skirmish_info + kSkirmishInfoFlagsOffset, sizeof(record));
            apply_session_flags(game, record);
        }
    } else if (kind == oa::data::campaign::SessionKind::multiplayer) {
        game->units_per_player = game->max_units_setting;
        session->multiplayer_rules = 1;
        game->sim_run_flags &= static_cast<uint16_t>(~kSimPaused);
        run(host, SessionStep::wait_multiplayer_ready);
        run(host, SessionStep::select_multiplayer_mission);
    }
    if (session->saved_game != nullptr && !session->saved_between_missions) {
        run(host, SessionStep::load_saved_controllers);
        if (kind == oa::data::campaign::SessionKind::skirmish)
            run(host, SessionStep::init_slots_from_roster);
    }
    if (!world_alloc_tables(session->world, &session->capacity))
        return false;
    run(host, SessionStep::init_mission_state);
    if (kind == oa::data::campaign::SessionKind::multiplayer) {
        game->load_flags |= 0x0004;
        run(host, SessionStep::wait_multiplayer_units);
        run(host, SessionStep::spawn_multiplayer_commanders);
        run(host, SessionStep::center_camera);
        run(host, SessionStep::report_start_outcome);
    } else if (
        kind == oa::data::campaign::SessionKind::skirmish && session->saved_game == nullptr
    ) {
        const bool fixed_starts = session->skirmish_info != nullptr &&
                                  session->skirmish_info[kSkirmishInfoFixedStartsOffset] != 0;
        if (!fixed_starts) {
            int32_t starts[kMaxSlots];
            for (auto& value : starts)
                value = -1;
            int32_t count = 0;
            for (int32_t slot = 0; slot < kMaxSlots; ++slot)
                if (slot_active(host, static_cast<uint32_t>(slot)))
                    starts[count++] = slot;
            bool shuffle = count >= 3;
            if (!shuffle && host != nullptr && host->random != nullptr) {
                const int64_t roll = static_cast<int64_t>(host->random(host->context)) * 2;
                shuffle = roll / 0x8000 != 0;
            }
            if (shuffle && count > 0 && host != nullptr && host->shuffle != nullptr)
                host->shuffle(host->context, starts, count);
            int32_t next = 0;
            for (int32_t slot = 0; slot < kMaxSlots; ++slot)
                if (slot_active(host, static_cast<uint32_t>(slot)) &&
                    host->spawn_commander != nullptr)
                    host->spawn_commander(
                        host->context, static_cast<uint32_t>(slot), starts[next++]
                    );
        } else {
            for (int32_t slot = 0; slot < kMaxSlots; ++slot)
                if (slot_active(host, static_cast<uint32_t>(slot)) &&
                    host->spawn_commander != nullptr)
                    host->spawn_commander(host->context, static_cast<uint32_t>(slot), slot);
        }
        run(host, SessionStep::compute_start_positions);
    }
    run(host, SessionStep::reset_slot_buffers);
    bool create_units = false;
    if (session->saved_game == nullptr)
        create_units = kind == oa::data::campaign::SessionKind::campaign;
    else if (session->saved_between_missions)
        create_units = true;
    else
        run(host, SessionStep::load_saved_session);
    if (create_units) {
        run(host, SessionStep::create_mission_units);
        run(host, SessionStep::camera_to_start);
    }
    run(host, SessionStep::reset_palette);
    run(host, SessionStep::load_mission_start_panel);
    run(host, SessionStep::mark_local_player_ready);
    run(host, SessionStep::reset_player_pings);
    run(host, SessionStep::send_player_status);
    run(host, SessionStep::update_player_slots);
    run(host, SessionStep::compute_start_positions);
    if (session->saved_game != nullptr) {
        run(host, SessionStep::free_saved_game);
        session->saved_game = nullptr;
    }
    run(host, SessionStep::dispatch_flagged_state);
    game->load_flags |= kLoadMissionStarted;
    return true;
}

} // namespace oa::sim::session
