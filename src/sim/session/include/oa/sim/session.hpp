// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Game session lifecycle: process-level session init/shutdown, the per-match
// begin/teardown that owns the World tables, and the frontend mode resets.
//
// Subsystem work (audio, palettes, GUI, multiplayer, simulation allocation)
// is reached through SessionHost steps in a fixed order, so the sequence is
// testable without those subsystems.
#pragma once

#include "oa/core/world.h"

#include <cstddef>
#include <cstdint>

namespace oa::data::campaign {
struct CampaignFile;
}

namespace oa::sim::session {

enum class SessionStep : uint16_t {
    // session_init
    query_display_size,
    create_offscreen_surface,
    reset_mode_state,
    clear_session_flag,
    load_hud_resources,
    init_audio_devices,
    reset_ui_state,
    query_system_info,
    init_record_length_table,
    register_status_labels,
    init_common_fonts,
    load_active_palette,
    load_all_sounds,
    load_palette_tables,
    load_game_settings,
    load_cd_list,
    apply_cd_settings,
    apply_saved_volumes,
    load_commander_hud,
    load_logo_textures,
    load_gui_palette,
    init_gui_context,
    load_gui_fonts,
    clear_state_buffer,
    // session_shutdown
    save_cd_list,
    shutdown_resource_table,
    free_gui_context,
    free_logo_textures,
    free_side_records,
    free_string_list,
    free_context_pair,
    shutdown_audio_devices,
    free_record_list,
    destroy_offscreen_surface,
    flush_tag_index,
    free_context_resource,
    free_record_length_table,
    clear_map_list_cache,
    free_subsystem_object,
    // session_teardown
    stop_cd_audio,
    build_score_summary,
    free_unit_tables,
    free_explosion_pieces,
    free_session_orders,
    clear_player_slots,
    free_radar_surfaces,
    free_cell_buffers,
    free_unit_data_tables,
    free_feature_array,
    free_listener_table,
    finish_reload_sync,
    // frontend_teardown
    redraw_frame,
    report_quit_outcome,
    teardown_all_slots,
    close_multiplayer_session,
    clear_fatal_error,
    // mode resets
    draw_cursor,
    reset_paired_fields,
    reset_frontend_state,
    reset_panel_keyboard,
    init_skirmish_slots,
    // begin_mission
    seed_random,
    load_unit_availability,
    wait_multiplayer_ready,
    select_multiplayer_mission,
    load_saved_controllers,
    init_slots_from_roster,
    init_mission_state,
    wait_multiplayer_units,
    spawn_multiplayer_commanders,
    center_camera,
    report_start_outcome,
    compute_start_positions,
    reset_slot_buffers,
    load_saved_session,
    create_mission_units,
    camera_to_start,
    reset_palette,
    load_mission_start_panel,
    mark_local_player_ready,
    reset_player_pings,
    send_player_status,
    update_player_slots,
    free_saved_game,
    dispatch_flagged_state,
};

// App modes a session selects through SessionHost::set_app_mode.
enum class AppMode : int32_t { boot = 0, frontend = 2, skirmish_setup = 5, end_mission = 7 };

struct SessionHost {
    void* context{};
    void (*step)(void* context, SessionStep step){};
    void (*set_app_mode)(void* context, AppMode mode){};
    // Reads an integer from the game settings file.
    int32_t (*read_setting)(void* context, const char* key, int32_t fallback){};
    uint32_t (*performance_counter)(void* context){};
    int32_t (*random)(void* context){}; // a random value from 0 to 32767
    void (*shuffle)(void* context, int32_t* values, int32_t count){};
    void (*spawn_commander)(void* context, uint32_t player, int32_t start){};
    bool (*slot_active)(void* context, uint32_t player){};
    void (*select_cursor_animation)(void* context, int32_t index){};
};

// The skirmish settings block Game.skirmish_info points at: its size, where
// its four rule words start (commander death, mapping, line of sight and the
// altitude-sight rule, as begin_mission applies them) and where its
// start-location word sits (nonzero: fixed start positions).
inline constexpr std::size_t kSkirmishInfoBytes = 0x22c;
inline constexpr std::size_t kSkirmishInfoFlagsOffset = 0x108;
inline constexpr std::size_t kSkirmishInfoFixedStartsOffset = 0x118;
// [UnitLimit]: its default and the range it is clamped to.
inline constexpr int32_t kDefaultUnitLimit = 250;
inline constexpr int32_t kMinUnitLimit = 21;
inline constexpr int32_t kMaxUnitLimit = 500;

/// Returns an installation's unit limit as a game plays it.
///
/// @param limit the [UnitLimit] value as read, in units per player
/// @return `limit` clamped to kMinUnitLimit..kMaxUnitLimit
[[nodiscard]] constexpr int32_t clamp_installed_unit_limit(int32_t limit) noexcept {
    return limit > kMaxUnitLimit ? kMaxUnitLimit : limit < kMinUnitLimit ? kMinUnitLimit : limit;
}

struct Session {
    World* world{};                               // live game block and match tables
    oa::data::campaign::CampaignFile* campaign{}; // Game.game_options
    uint8_t* skirmish_info{};                     // Game.skirmish_info, kSkirmishInfoBytes
    void* saved_game{};                           // Game.saved_game, loaded save being resumed
    bool saved_between_missions{};                // save's Summary has BetweenMissions
    int32_t unit_limit{};                         // Game.max_units_setting, [UnitLimit] clamped
    int32_t multiplayer_rules{};                  // rules flag set per session kind
    WorldCapacity capacity;
};

// World lifetime and tables (world_create, world_alloc_tables, ...) live in
// oa/core/world.h; a session allocates tables when a mission begins.

/// Starts the process-level session: display, audio, palettes, GUI context and settings.
///
/// Runs the session_init steps in order, clears bits 0 and 1 of
/// Game.outcome_flags and bits 7 to 9 of the console option word, allocates a
/// fresh skirmish info block, enters the boot app mode, then reads [UnitLimit]
/// clamped to 21..500 (default 250).
///
/// @param[in,out] session Session whose Game fields, skirmish info block and unit_limit are set.
/// @param host Step runner and callbacks; null runs no steps and uses the default unit limit.
void session_init(Session* session, const SessionHost* host);

/// Ends the process-level session: runs the session_shutdown steps and frees the skirmish info block.
///
/// @param[in,out] session Session whose skirmish info block is freed.
/// @param host Step runner; null runs no steps.
void session_shutdown(Session* session, const SessionHost* host);

/// Ends a match: stops CD audio, records the score summary and releases the match tables.
///
/// Clears the loading flag first; multiplayer games finish their reload
/// handshake last.
///
/// @param[in,out] session Session whose World tables are freed.
/// @param host Step runner; null runs no steps.
void session_teardown(Session* session, const SessionHost* host);

/// Leaves a live game (reporting the quit and leaving the multiplayer session) before the match teardown.
///
/// @param[in,out] session Session whose match is torn down.
/// @param host Step runner; null runs no steps.
void frontend_teardown(Session* session, const SessionHost* host);

/// Enters the frontend: selects the panel-ready cursor, clears the session and outcome flags and switches
/// to the frontend app mode.
///
/// @param[in,out] session Session whose Game flags are cleared.
/// @param host Step runner and callbacks; null runs no steps.
void enter_frontend_mode(Session* session, const SessionHost* host);

/// Returns to the frontend from a finished screen, resetting the frontend and panel keyboard state.
///
/// @param[in,out] session Session whose Game flags are cleared.
/// @param host Step runner and callbacks; null runs no steps.
void reset_to_frontend_mode(Session* session, const SessionHost* host);

/// Sets up two players (slot 0 human, slot 1 computer, which the host marks as the second controller), then
/// the skirmish setup mode.
///
/// @param[in,out] session Session whose Game.player_count becomes 2.
/// @param host Step runner and callbacks; null runs no steps.
void enter_skirmish_setup(Session* session, const SessionHost* host);

/// Copies a {rules, mapping, line of sight, altitude sight} record into the game block.
///
/// @param[in,out] game Game receiving the rules word and visibility flag bits 0..2.
/// @param record Four int32: the rules word, then the mapping, line-of-sight and altitude-sight
///        rules (bit 0 of each).
void apply_session_flags(Game* game, const int32_t* record);

/// Starts the bound mission for the session kind.
///
/// Campaigns apply the mission's flags and unit restrictions; skirmishes
/// shuffle start positions among the active players (always with three or
/// more, otherwise on a coin flip) and spawn commanders; multiplayer games
/// wait for the other players and their units. The World tables are then
/// allocated, a fresh campaign places the mission's units, and the in-game
/// panel is loaded.
///
/// @param[in,out] session Session holding the World, campaign and any saved game being resumed; the saved
///        game is released.
/// @param host Step runner and callbacks; null runs no steps.
/// @return False when the World tables cannot be allocated.
bool begin_mission(Session* session, const SessionHost* host);

} // namespace oa::sim::session
