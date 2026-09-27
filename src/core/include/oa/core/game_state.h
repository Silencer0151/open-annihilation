// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

/* The game-state block. */
#ifndef OA_CORE_GAME_STATE_H
#define OA_CORE_GAME_STATE_H

#include "oa/core/types.h"
#include "oa/core/player.h"
#include "oa/core/side.h"
#include "oa/core/weapon_def.h"

#define OA_SOUND_SLOTS 256
#define OA_SOUND_NAME_BYTES 32
#define OA_CHAT_LINE_COUNT 30
#define OA_CHAT_LINE_BYTES 0x48

/* Values of Game.difficulty. */
#define OA_DIFFICULTY_EASY 0
#define OA_DIFFICULTY_MEDIUM 1
#define OA_DIFFICULTY_HARD 2

/* Values of Game.chat_mode. */
#define OA_CHAT_MODE_EVERYONE 0
#define OA_CHAT_MODE_ALLIES 1
#define OA_CHAT_MODE_ENEMIES 2
#define OA_CHAT_MODE_CHOSEN 3

/* Game.visibility_flags bit above the session's mapping, line-of-sight and
 * altitude-sight rules (bits 0..2): set once the fog edge mask is rebuilt for
 * the view, cleared by camera moves and by stamps of the viewer's sight. */
#define OA_VISIBILITY_FOG_MASK_CURRENT 0x08u

/* Game.radar_blink_flags bit: the mapped radar image is rebuilt from the
 * sight grids when the radar is next drawn. */
#define OA_RADAR_MAPPED_DIRTY 0x0004u

/* Game.console_flags bits: the options the console's commands switch. */
#define OA_CONSOLE_FLAG_NO_DROP 0x0001u         /* "Drop 0": stalled players are not dropped */
#define OA_CONSOLE_FLAG_DEVELOPER 0x0002u       /* the developer passphrase was accepted */
#define OA_CONSOLE_FLAG_SELECTION_BOXES 0x0004u /* "SelBoxes" */
#define OA_CONSOLE_FLAG_TREE_DEATH 0x0008u      /* weapons damage features */
#define OA_CONSOLE_FLAG_NO_SHAKE 0x0010u        /* blasts do not shake the screen */
#define OA_CONSOLE_FLAG_CLOCK 0x0040u           /* "Clock": the game clock shows */
#define OA_CONSOLE_FLAG_DOUBLE_SHOT 0x0080u     /* weapon damage doubled */
#define OA_CONSOLE_FLAG_HALF_SHOT 0x0100u       /* weapon damage halved */
#define OA_CONSOLE_FLAG_FULL_RADAR 0x0200u      /* the radar shows every unit */
#define OA_CONSOLE_FLAG_SHOOT_ALL 0x0400u       /* automatic targeting takes every unit type */

/* Values of Game.endgame_state. */
#define OA_ENDGAME_CAPTURE 0
#define OA_ENDGAME_WAIT_MESSAGE 1
#define OA_ENDGAME_SHADE_START 2
#define OA_ENDGAME_SHADING 3
#define OA_ENDGAME_DISC_CHECK 4
#define OA_ENDGAME_OUTCOME 5
#define OA_ENDGAME_GLAMOUR 6
#define OA_ENDGAME_STAT_BARS 7
#define OA_ENDGAME_PANEL 8

#define OA_SCORE_COLUMNS 7
#define OA_SCORE_NAME_BYTES 30

/* Frame-time categories of the "Profile" bar graph. */
#define OA_PROFILE_SYNC 0
#define OA_PROFILE_UNITS 1
#define OA_PROFILE_LOGIC 2
#define OA_PROFILE_RENDER_STATIC 3
#define OA_PROFILE_RENDER_STUFF 4
#define OA_PROFILE_RENDER_FOG 5
#define OA_PROFILE_SFX 6
#define OA_PROFILE_WEAPON 7
#define OA_PROFILE_MISC 8
#define OA_PROFILE_CATEGORY_COUNT 9

OA_CORE_BEGIN

#pragma pack(push, 1)

/* One player's row of the end-of-game score table: kills, losses, energy and
 * metal produced, energy and metal wasted, total. */
typedef struct ScoreEntry {
    char name[OA_SCORE_NAME_BYTES];
    int32_t values[OA_SCORE_COLUMNS];
} ScoreEntry;

/* Clock milliseconds spent per profile category: this window's running
 * totals and the last window's, which the bar graph draws. */
typedef struct ProfileTimes {
    uint32_t sampled_at; /* clock at the last accumulate or window start */
    int32_t shown_total; /* sum of shown, at least 1 */
    int32_t shown[OA_PROFILE_CATEGORY_COUNT];
    int32_t pending[OA_PROFILE_CATEGORY_COUNT];
} ProfileTimes;

/* What the top resource bar last drew: the stores ease toward the viewed
 * player's, the rates and capacities are that player's copies. */
typedef struct ResourceReadout {
    uint8_t player; /* Player.index */
    float energy;
    float energy_produced;
    float energy_requested;
    float metal;
    float metal_produced;
    float metal_requested;
    float energy_storage;
    float metal_storage;
} ResourceReadout;

/* An entry of the list Game.hot_radar_units refers to: a unit the radar
 * drew and its blip on the 640x480 screen. */
typedef struct RadarHotUnit {
    uint16_t unit_id;
    int32_t x;
    int32_t y;
} RadarHotUnit;

/* The 0x3924d-byte game-state block: one per running game. Players, weapon
 * definitions, sides and the score table sit inline at fixed offsets; the
 * larger tables are referenced from it (see World).
 *
 * Blocks the engine zero-initialises and never reads or writes are named for
 * what 3.1c keeps there where that is known (a '?' marks a tentative name),
 * and otherwise for the field they follow. */
typedef struct Game {
    /* ? Its second and third bytes hold the build version, 3 and 1, that unit
     * files and the other players are checked against; the engine keeps the
     * version as constants. Zero-initialised and never read. */
    uint8_t version_block[0x14];
    /* Multiplayer session settings, the session's player limit among them. */
    uint8_t session_description[0x4bd];
    uint8_t block_after_session_description[0x1c]; /* zero-initialised and never read */
    /* Nonzero once the "Compression" console command has turned compression off. */
    uint32_t compression_off;
    /* ? Ends with the start of the GUI context the panels draw through: its
     * fonts, panel stack, cursor, pointer and hover state. The context runs
     * on through ui_colors to pending_background; the application keeps it
     * itself. Zero-initialised and never read. */
    uint8_t gui_context_block[0x98];
    /* ? The GUI context's switch that makes a panel's first draw clear its
     * buttons' quick keys; session start-up sets it to 1. Never read. */
    int32_t gui_clear_quick_keys_on_draw;
    /* ? The GUI context's caret and drag state, timing, keyboard switch and
     * cycling captions, up to its colour map, ui_colors. Zero-initialised
     * and never read. */
    uint8_t gui_context_state[0x83e];
    uint8_t ui_colors[0x100]; /* guipal colour -> nearest index of the frontend palette */
    /* ? The GUI context's GUI, GAF and font directories and its quick-key,
     * redraw and pressed-status words. Zero-initialised and never read. */
    uint8_t gui_context_paths[0x320];
    oa_ref32 pending_background; /* named background loaded while no panel was open */
    char background_name[0x100]; /* last named background asked for; empty for none */
    char chat_lines[30][0x48];   /* chat message ring, see chat_head/chat_tail */
    uint32_t next_status_tick;   /* next per-player status update (every 60 ticks) */
    Player players[OA_PLAYER_COUNT];
    Player no_player;         /* the eleventh record, at index OA_PLAYER_COUNT */
    uint32_t shared_machines; /* nonzero while one machine group holds two active players */
    oa_ref32 skirmish_info;   /* persistent skirmish/campaign settings block */
    /* ? The multiplayer load barrier: per player whether its load finished,
     * whether it acknowledged the start and the start position it was given,
     * and whether the positions are given out. The engine keeps none of it.
     * Zero-initialised and never read. */
    uint8_t load_barrier[0x94];
    oa_ref32 update_buffer; /* ? */
    uint16_t player_count;
    uint16_t chat_head; /* chat_lines ring */
    uint16_t chat_tail;
    uint8_t local_player_index;
    uint8_t viewpoint_player;                /* ? */
    uint8_t session_flags;                   /* bit0 live game, bit2 loading, bit3 single player */
    uint8_t block_after_session_flags[0x6a]; /* zero-initialised and never read */
    uint8_t connection_flags;
    uint8_t block_after_connection_flags[0x10e]; /* zero-initialised and never read */
    uint8_t frontend_state;
    uint8_t frontend_signal;
    uint8_t frontend_pending_signal;
    char game_name[17];
    char nickname[17];
    char password[11];
    uint8_t gui_flags; /* ? */
    uint8_t chat_flags;
    uint8_t chat_mode;        /* OA_CHAT_MODE_* */
    uint8_t chat_targets[10]; /* nonzero: player receives chat */
    /* ? The eleventh record's chat target: a chat to one chosen player clears
     * it with chat_targets. */
    uint8_t chat_target_no_player;
    uint8_t block_after_chat_targets[0x2c]; /* zero-initialised and never read */
    uint32_t slot_table[OA_PLAYER_COUNT];   /* per slot its player id, 0 free, 0xffffffff closed */
    uint8_t block_after_slot_table[0x24];   /* zero-initialised and never read */
    uint16_t setup_options;                 /* bit0 locked/report game */
    uint32_t pointer_state[6];
    int16_t cursor_cell_x; /* map cell of the ground under the cursor */
    int16_t cursor_cell_z;
    /* The box drag the left button started on the battlefield: x, height and
     * z of the ground under the pointer where it began and where it is now,
     * in whole map pixels. */
    int32_t drag_start[3];
    int32_t drag_end[3];
    FixedVec3 cursor_position; /* 16.16 world position of the ground under the cursor */
    uint8_t block_after_cursor_position[0x4]; /* zero-initialised and never read */
    uint16_t cursor_unit_id;
    uint16_t cursor_feature; /* feature word of the plot under the cursor; 0xfffb and above: none */
    /* ? Starts with the index of the cursor animation shown, which the
     * application keeps itself. Zero-initialised and never read. */
    uint8_t cursor_animation_block[0x5];
    /* The order command the pointer gives; 1, the default order, when none is
     * armed. */
    uint8_t pointer_command;
    uint8_t block_after_pointer_command[0x2]; /* zero-initialised and never read */
    uint8_t pointer_flags;                    /* where the pointer is and what it drags */
    uint32_t saved_pointer_state[6];
    int32_t mouse_look_active;
    int32_t mouse_look_anchor_x;
    int32_t mouse_look_anchor_y;
    int32_t mouse_look_cell_x;
    int32_t mouse_look_cell_y;
    WeaponDef weapon_defs[256];
    int32_t projectile_count;
    oa_ref32 projectiles; /* Projectile[300] */
    /* ? Holds the path-search context and the unit record 3D features are
     * drawn through, which the engine keeps itself. Zero-initialised and never
     * read. */
    uint8_t search_context_block[0x18];
    /* First pool slot of each list of placed features, -1 for an empty list:
     * the animating, burning or falling ones, the 3DO wrecks at rest, and the
     * free slots. */
    int32_t feature_active_head;
    int32_t feature_settled_head;
    int32_t feature_free_head;
    uint8_t block_after_feature_heads[0x4]; /* zero-initialised and never read */
    int32_t map_width_world;
    int32_t map_height_world;
    int32_t map_pixel_width;
    int32_t map_pixel_height;
    int32_t map_width;  /* cells */
    int32_t map_height; /* cells */
    int32_t view_cells_width;
    int32_t view_cells_height;
    uint8_t block_after_view_cells_height[0x10]; /* zero-initialised and never read */
    int32_t feature_def_count;
    uint8_t reproduce_cursor[0x4];
    int32_t wind_min;
    int32_t wind_max;
    oa_fixed gravity;
    float tidal_strength;
    oa_ref32 minimap;
    oa_ref32 feature_defs; /* FeatureDef[feature_def_count] */
    oa_ref32 sight_grid;   /* uint16 per cell, bit per player */
    /* ? The count of remembered sightings and the block of twenty records
     * that holds them. Zero-initialised and never read. */
    uint8_t remembered_sight[0x8];
    uint8_t sea_level;
    uint8_t debug_overlay;                     /* debug key 'm': grid view 0 (off) to 4 */
    uint8_t visibility_flags;                  /* ? bit1 selects Player.coverage_grid */
    uint8_t block_after_visibility_flags[0x5]; /* zero-initialised and never read */
    oa_ref32 map_cells;                        /* MapPlot[map_width * map_height] */
    oa_ref32 tile_map;
    uint8_t block_after_tile_map[0x14]; /* zero-initialised and never read */
    uint32_t bucket_width;
    /* ? Ends with the bucket units outside the map sit in. Zero-initialised
     * and never read. */
    uint8_t block_after_bucket_width[0x14];
    Rect32 radar_picture_rect;
    Rect32 radar_view_rect;
    oa_ref32 radar_final_surface;
    oa_ref32 radar_mapped_surface;
    oa_ref32 radar_picture_surface;
    int16_t radar_offset_x;
    int16_t radar_offset_y;
    int16_t radar_width;
    int16_t radar_height;
    int16_t radar_blink_countdown;
    uint16_t radar_blink_flags;
    oa_ref32 follow_unit;   /* Unit */
    oa_ref32 follow_target; /* a record with a FixedVec3 after its first 32-bit word */
    int32_t camera_slot_x[4];
    int32_t camera_slot_y[4];
    uint8_t camera_slot_valid[4];
    uint32_t camera_x; /* map pixels */
    uint32_t camera_y; /* map pixels */
    int32_t camera_target_x;
    int32_t camera_target_y;
    int32_t shake_duration;
    int32_t shake_remaining;
    int32_t shake_amplitude_x;
    int32_t shake_amplitude_y;
    FixedVec3 follow_point;
    int16_t follow_point_ticks;
    uint8_t scroll_speed;
    uint8_t camera_flags;
    uint16_t pool_units_per_player;
    uint16_t unit_slot_count;
    int32_t active_unit_count;
    oa_ref32 units;      /* Unit[unit_slot_count] */
    oa_ref32 units_last; /* Unit; inclusive */
    oa_ref32 hot_units;
    oa_ref32 hot_radar_units;
    int32_t hot_unit_count;
    int32_t hot_radar_unit_count;
    uint16_t cycle_unit_id; /* ? last unit reached by select-next */
    int16_t periodic_countdown;
    uint8_t periodic_flags;
    /* ? Holds the claim on the render arena that the console's arena release
     * and display-option toggles make; the application keeps it itself.
     * Zero-initialised and never read. */
    uint8_t render_arena_block[0x1b];
    int32_t unit_def_count;
    int32_t unit_def_id_bits;
    int32_t
        unit_defs_stale; /* the full unit load replaced the header table; the frontend reloads it */
    oa_ref32 unit_defs;  /* UnitDef[unit_def_count] */
    /* ? The animation, FX, cursor, logo and texture tables, then the
     * explosion pool and the pieces of shattered models, which the engine
     * keeps in World and in the application. Zero-initialised and never
     * read. */
    uint8_t sprite_and_effect_tables[0x1f670];
    int32_t sound_count;
    oa_ref32 sounds[256];
    char sound_names[256][32];
    char sound_files[256][32];
    uint8_t block_after_sound_files[0x8]; /* zero-initialised and never read */
    oa_ref32 offscreen_surface;
    uint32_t offscreen_width;
    uint32_t offscreen_height;
    /* The game view on the off-screen surface, in inclusive screen pixels,
     * set when the game screen is laid out. */
    Rect32 battlefield_rect;
    int32_t viewport_width;
    int32_t viewport_height;
    ResourceReadout resource_readout;
    uint8_t block_after_resource_readout[0x30]; /* zero-initialised and never read */
    int32_t status_panel_offset;                /* space-bar strip: 0 hidden, -31 fully raised */
    oa_ref32 status_lightbar; /* the strip's picture, LIGHTBAR frame 1 of COMMONGUI.GAF */
    uint8_t block_after_status_lightbar[0x4]; /* zero-initialised and never read */
    uint16_t panel_unit_id;                   /* unit shown in the order panel; 0 none */
    uint16_t panel_unit_type;
    uint8_t mission_panel_name[0x1e];
    uint16_t frame_flags;
    uint16_t order_summary;     /* selection summary of standing orders and abilities */
    uint16_t order_summary_ext; /* bit0 selection can d-gun */
    uint32_t wind_change_tick;
    int32_t wind_strength_divisor;
    FixedVec3 wind_vector;
    oa_angle wind_direction;
    int32_t wind_strength;
    float wind_factor; /* strength normalised to 0..1 */
    uint32_t wind_changed;
    uint16_t units_per_player;
    uint16_t word_after_units_per_player; /* zero-initialised and never read */
    uint16_t max_units;                   /* limit in effect for this game */
    uint16_t max_units_setting;           /* configured limit */
    int32_t difficulty;                   /* OA_DIFFICULTY_* */
    int32_t campaign_side;                /* 0 ARM, 1 CORE */
    int32_t session_rules;                /* copy of session_record[0] */
    int32_t interface_type; /* the [Interface] preference: 0 left-click, 1 right-click */
    /* Which message-log lines show: 1 chat only, 2 all but kind 8, 3 kinds
     * 1, 4 and 8 (every kind while screen_chat is on); anything else hides
     * the log. Session start-up sets 3. */
    int32_t message_filter;
    /* "ScreenChat": nonzero shows every line under message filter 3; the
     * console command flips bit 0. */
    uint32_t screen_chat;
    uint16_t graphics_flags; /* bit4 shadows, bit5 shading */
    uint32_t gamma;          /* "Gamma", in tenths */
    uint32_t fx_volume;
    uint32_t music_volume;
    uint16_t music_flags;
    uint8_t cd_mode;
    uint8_t unit_sound_volume;
    uint8_t unit_text_volume;
    uint16_t sound_flags;
    int32_t screen_width;
    int32_t screen_height;
    int32_t text_scroll; /* "TextScroll": seconds a message line stays on screen, less one */
    int32_t text_lines;  /* "TextLines": message lines the log keeps and shows; 0 turns it off */
    uint8_t block_after_text_lines[0x4]; /* zero-initialised and never read */
    uint16_t console_flags;              /* OA_CONSOLE_FLAG_* */
    int32_t player_timeout_seconds;
    int32_t send_error_percent; /* 0..100, the "Senderror" console setting */
    uint32_t side_count;
    Side sides[5];
    uint32_t last_frame_time;
    int32_t pending_ticks;
    uint32_t frame_elapsed;
    float tick_remainder;
    uint32_t tick;
    uint16_t requested_speed;
    uint16_t current_speed;
    int16_t speed_adaptation;
    uint16_t sim_run_flags; /* bit0 paused */
    /* The directory posters (in its screenshots folder) and movie captures
     * are written under; the console's "Film" sets it. */
    char output_directory[0x100];
    char capture_path[256];
    int32_t capture_enabled;
    int32_t capture_rate;
    uint32_t next_capture_tick;
    /* Set when the console changes output_directory or capture_rate; the
     * next save of the settings writes the value and clears the flag. */
    uint32_t output_directory_changed;
    uint32_t capture_rate_changed;
    /* Session start-up clears it with the two change flags before it; never
     * read. */
    int32_t cleared_with_change_flags;
    uint8_t block_before_saved_game[0x100]; /* zero-initialised and never read */
    oa_ref32 saved_game;                    /* loaded save being resumed */
    /* ? Loading-screen progress of each of its six rows, 0 to 100.
     * Zero-initialised and never read. */
    uint8_t load_progress[0x6];
    uint16_t load_flags;
    /* ? Refers to the ten effect emitter lists, which the engine keeps in
     * World. Zero-initialised and never read. */
    uint8_t effect_layers[0x4];
    /* ? The movie being played, which plays until movie_skip changes;
     * session start-up clears it. Never read. */
    int32_t playing_movie;
    uint16_t campaign_unlock_flags; /* bit0 every mission playable */
    int32_t slot_count;
    ProfileTimes profile_times;
    int32_t profiling; /* "Profile": the bar graph shows */
    ScoreEntry scores[10];
    /* Zero-initialised and never read; as large as one ScoreEntry. */
    uint8_t block_after_scores[0x3a];
    int32_t endgame_state;           /* OA_ENDGAME_* step of the end-of-game screen */
    uint32_t endgame_next_tick;      /* next stat-bar column, or the click-to-continue delay */
    uint32_t endgame_fade_tick;      /* next shade or palette-fade step */
    int32_t endgame_fade_done;       /* the shade or palette fade has finished */
    int32_t endgame_shade_countdown; /* shade steps left */
    int32_t endgame_column;          /* stat-bar column the screen activates next */
    uint8_t block_after_endgame_column[0x4]; /* zero-initialised and never read */
    int32_t no_movie;                        /* skip mission movies */
    /* ? The end-of-game screen's pictures: the copy of the last frame, the
     * outcome picture and its palette, and the palette fade's buffers, which
     * the application keeps itself. Zero-initialised and never read. */
    uint8_t endgame_pictures[0x118];
    int32_t score_maxima[OA_SCORE_COLUMNS];
    int32_t mission_index;
    int32_t victory; /* outcome flag bit 4 of the last game */
    /* Units the debug keys pin: Shift+F1 pins the unit under the cursor as the
     * first, Shift+F2 as the second, and each valid word says the slot
     * holds one. */
    uint32_t pinned_unit_a_valid; /* ? */
    uint16_t pinned_unit_a;       /* ? */
    uint32_t pinned_unit_b_valid; /* ? */
    uint16_t pinned_unit_b;       /* ? */
    /* The next two are on while nonzero; their console commands flip bit 0. */
    uint32_t show_ranges;    /* "ShowRanges": every unit's ranges are drawn */
    uint32_t show_bandwidth; /* "BPS": the send and receive rates show */
    /* ? The count and table of the download menus, which the engine keeps
     * itself. Zero-initialised and never read. */
    uint8_t download_menus[0x8];
    char mission_results[26];              /* 'W' or 'L' per campaign mission */
    oa_ref32 game_options;                 /* campaign object */
    uint8_t block_after_game_options[0x4]; /* zero-initialised and never read */
    int32_t mode;
    oa_ref32 mode_callback;
    /* ? The COMIX and smlfont fonts start-up loads, which the application
     * keeps itself. Zero-initialised and never read. */
    uint8_t common_fonts[0x8];
    uint8_t provider_guid[16];              /* multiplayer connection kind */
    uint8_t block_after_provider_guid[0x8]; /* zero-initialised and never read */
    int32_t session_record[4];              /* rules kind, mapping, line of sight, enabled */
    int32_t start_options[4];               /* commander rule, unmapped, LOS limited, LOS true */
    int16_t outcome_countdown;
    uint16_t outcome_flags;
    uint8_t block_after_outcome_flags[0x4]; /* zero-initialised and never read */
    uint32_t movie_skip;                    /* nonzero once the player skipped the playing movie */
    uint8_t block_after_movie_skip[0x4];    /* zero-initialised and never read */
    /* Nonzero while a restart of the game is asked for; session start-up
     * clears it. */
    int32_t restart_requested;
} Game;

#pragma pack(pop)

OA_ASSERT_SIZE(ScoreEntry, 0x3a);
OA_ASSERT_SIZE(ProfileTimes, 0x50);
OA_ASSERT_SIZE(ResourceReadout, 0x21);
OA_ASSERT_SIZE(RadarHotUnit, 0xa);
OA_ASSERT_SIZE(Game, 0x3924d);
OA_ASSERT_OFFSET(Game, version_block, 0x0);
OA_ASSERT_OFFSET(Game, session_description, 0x14);
OA_ASSERT_OFFSET(Game, block_after_session_description, 0x4d1);
OA_ASSERT_OFFSET(Game, compression_off, 0x4ed);
OA_ASSERT_OFFSET(Game, gui_context_block, 0x4f1);
OA_ASSERT_OFFSET(Game, gui_clear_quick_keys_on_draw, 0x589);
OA_ASSERT_OFFSET(Game, gui_context_state, 0x58d);
OA_ASSERT_OFFSET(Game, ui_colors, 0xdcb);
OA_ASSERT_OFFSET(Game, gui_context_paths, 0xecb);
OA_ASSERT_OFFSET(Game, pending_background, 0x11eb);
OA_ASSERT_OFFSET(Game, background_name, 0x11ef);
OA_ASSERT_OFFSET(Game, chat_lines, 0x12ef);
OA_ASSERT_OFFSET(Game, next_status_tick, 0x1b5f);
OA_ASSERT_OFFSET(Game, players, 0x1b63);
OA_ASSERT_OFFSET(Game, no_player, 0x2851);
OA_ASSERT_OFFSET(Game, shared_machines, 0x299c);
OA_ASSERT_OFFSET(Game, skirmish_info, 0x29a0);
OA_ASSERT_OFFSET(Game, load_barrier, 0x29a4);
OA_ASSERT_OFFSET(Game, update_buffer, 0x2a38);
OA_ASSERT_OFFSET(Game, player_count, 0x2a3c);
OA_ASSERT_OFFSET(Game, chat_head, 0x2a3e);
OA_ASSERT_OFFSET(Game, chat_tail, 0x2a40);
OA_ASSERT_OFFSET(Game, local_player_index, 0x2a42);
OA_ASSERT_OFFSET(Game, viewpoint_player, 0x2a43);
OA_ASSERT_OFFSET(Game, session_flags, 0x2a44);
OA_ASSERT_OFFSET(Game, block_after_session_flags, 0x2a45);
OA_ASSERT_OFFSET(Game, connection_flags, 0x2aaf);
OA_ASSERT_OFFSET(Game, block_after_connection_flags, 0x2ab0);
OA_ASSERT_OFFSET(Game, frontend_state, 0x2bbe);
OA_ASSERT_OFFSET(Game, frontend_signal, 0x2bbf);
OA_ASSERT_OFFSET(Game, frontend_pending_signal, 0x2bc0);
OA_ASSERT_OFFSET(Game, game_name, 0x2bc1);
OA_ASSERT_OFFSET(Game, nickname, 0x2bd2);
OA_ASSERT_OFFSET(Game, password, 0x2be3);
OA_ASSERT_OFFSET(Game, gui_flags, 0x2bee);
OA_ASSERT_OFFSET(Game, chat_flags, 0x2bef);
OA_ASSERT_OFFSET(Game, chat_mode, 0x2bf0);
OA_ASSERT_OFFSET(Game, chat_targets, 0x2bf1);
OA_ASSERT_OFFSET(Game, chat_target_no_player, 0x2bfb);
OA_ASSERT_OFFSET(Game, block_after_chat_targets, 0x2bfc);
OA_ASSERT_OFFSET(Game, slot_table, 0x2c28);
OA_ASSERT_OFFSET(Game, block_after_slot_table, 0x2c50);
OA_ASSERT_OFFSET(Game, setup_options, 0x2c74);
OA_ASSERT_OFFSET(Game, pointer_state, 0x2c76);
OA_ASSERT_OFFSET(Game, cursor_cell_x, 0x2c8e);
OA_ASSERT_OFFSET(Game, cursor_cell_z, 0x2c90);
OA_ASSERT_OFFSET(Game, drag_start, 0x2c92);
OA_ASSERT_OFFSET(Game, drag_end, 0x2c9e);
OA_ASSERT_OFFSET(Game, cursor_position, 0x2caa);
OA_ASSERT_OFFSET(Game, block_after_cursor_position, 0x2cb6);
OA_ASSERT_OFFSET(Game, cursor_unit_id, 0x2cba);
OA_ASSERT_OFFSET(Game, cursor_feature, 0x2cbc);
OA_ASSERT_OFFSET(Game, cursor_animation_block, 0x2cbe);
OA_ASSERT_OFFSET(Game, pointer_command, 0x2cc3);
OA_ASSERT_OFFSET(Game, block_after_pointer_command, 0x2cc4);
OA_ASSERT_OFFSET(Game, pointer_flags, 0x2cc6);
OA_ASSERT_OFFSET(Game, saved_pointer_state, 0x2cc7);
OA_ASSERT_OFFSET(Game, mouse_look_active, 0x2cdf);
OA_ASSERT_OFFSET(Game, mouse_look_anchor_x, 0x2ce3);
OA_ASSERT_OFFSET(Game, mouse_look_anchor_y, 0x2ce7);
OA_ASSERT_OFFSET(Game, mouse_look_cell_x, 0x2ceb);
OA_ASSERT_OFFSET(Game, mouse_look_cell_y, 0x2cef);
OA_ASSERT_OFFSET(Game, weapon_defs, 0x2cf3);
OA_ASSERT_OFFSET(Game, projectile_count, 0x141f3);
OA_ASSERT_OFFSET(Game, projectiles, 0x141f7);
OA_ASSERT_OFFSET(Game, search_context_block, 0x141fb);
OA_ASSERT_OFFSET(Game, feature_active_head, 0x14213);
OA_ASSERT_OFFSET(Game, feature_settled_head, 0x14217);
OA_ASSERT_OFFSET(Game, feature_free_head, 0x1421b);
OA_ASSERT_OFFSET(Game, block_after_feature_heads, 0x1421f);
OA_ASSERT_OFFSET(Game, map_width_world, 0x14223);
OA_ASSERT_OFFSET(Game, map_height_world, 0x14227);
OA_ASSERT_OFFSET(Game, map_pixel_width, 0x1422b);
OA_ASSERT_OFFSET(Game, map_pixel_height, 0x1422f);
OA_ASSERT_OFFSET(Game, map_width, 0x14233);
OA_ASSERT_OFFSET(Game, map_height, 0x14237);
OA_ASSERT_OFFSET(Game, view_cells_width, 0x1423b);
OA_ASSERT_OFFSET(Game, view_cells_height, 0x1423f);
OA_ASSERT_OFFSET(Game, block_after_view_cells_height, 0x14243);
OA_ASSERT_OFFSET(Game, feature_def_count, 0x14253);
OA_ASSERT_OFFSET(Game, reproduce_cursor, 0x14257);
OA_ASSERT_OFFSET(Game, wind_min, 0x1425b);
OA_ASSERT_OFFSET(Game, wind_max, 0x1425f);
OA_ASSERT_OFFSET(Game, gravity, 0x14263);
OA_ASSERT_OFFSET(Game, tidal_strength, 0x14267);
OA_ASSERT_OFFSET(Game, minimap, 0x1426b);
OA_ASSERT_OFFSET(Game, feature_defs, 0x1426f);
OA_ASSERT_OFFSET(Game, sight_grid, 0x14273);
OA_ASSERT_OFFSET(Game, remembered_sight, 0x14277);
OA_ASSERT_OFFSET(Game, sea_level, 0x1427f);
OA_ASSERT_OFFSET(Game, debug_overlay, 0x14280);
OA_ASSERT_OFFSET(Game, visibility_flags, 0x14281);
OA_ASSERT_OFFSET(Game, block_after_visibility_flags, 0x14282);
OA_ASSERT_OFFSET(Game, map_cells, 0x14287);
OA_ASSERT_OFFSET(Game, tile_map, 0x1428b);
OA_ASSERT_OFFSET(Game, block_after_tile_map, 0x1428f);
OA_ASSERT_OFFSET(Game, bucket_width, 0x142a3);
OA_ASSERT_OFFSET(Game, block_after_bucket_width, 0x142a7);
OA_ASSERT_OFFSET(Game, radar_picture_rect, 0x142bb);
OA_ASSERT_OFFSET(Game, radar_view_rect, 0x142cb);
OA_ASSERT_OFFSET(Game, radar_final_surface, 0x142db);
OA_ASSERT_OFFSET(Game, radar_mapped_surface, 0x142df);
OA_ASSERT_OFFSET(Game, radar_picture_surface, 0x142e3);
OA_ASSERT_OFFSET(Game, radar_offset_x, 0x142e7);
OA_ASSERT_OFFSET(Game, radar_offset_y, 0x142e9);
OA_ASSERT_OFFSET(Game, radar_width, 0x142eb);
OA_ASSERT_OFFSET(Game, radar_height, 0x142ed);
OA_ASSERT_OFFSET(Game, radar_blink_countdown, 0x142ef);
OA_ASSERT_OFFSET(Game, radar_blink_flags, 0x142f1);
OA_ASSERT_OFFSET(Game, follow_unit, 0x142f3);
OA_ASSERT_OFFSET(Game, follow_target, 0x142f7);
OA_ASSERT_OFFSET(Game, camera_slot_x, 0x142fb);
OA_ASSERT_OFFSET(Game, camera_slot_y, 0x1430b);
OA_ASSERT_OFFSET(Game, camera_slot_valid, 0x1431b);
OA_ASSERT_OFFSET(Game, camera_x, 0x1431f);
OA_ASSERT_OFFSET(Game, camera_y, 0x14323);
OA_ASSERT_OFFSET(Game, camera_target_x, 0x14327);
OA_ASSERT_OFFSET(Game, camera_target_y, 0x1432b);
OA_ASSERT_OFFSET(Game, shake_duration, 0x1432f);
OA_ASSERT_OFFSET(Game, shake_remaining, 0x14333);
OA_ASSERT_OFFSET(Game, shake_amplitude_x, 0x14337);
OA_ASSERT_OFFSET(Game, shake_amplitude_y, 0x1433b);
OA_ASSERT_OFFSET(Game, follow_point, 0x1433f);
OA_ASSERT_OFFSET(Game, follow_point_ticks, 0x1434b);
OA_ASSERT_OFFSET(Game, scroll_speed, 0x1434d);
OA_ASSERT_OFFSET(Game, camera_flags, 0x1434e);
OA_ASSERT_OFFSET(Game, pool_units_per_player, 0x1434f);
OA_ASSERT_OFFSET(Game, unit_slot_count, 0x14351);
OA_ASSERT_OFFSET(Game, active_unit_count, 0x14353);
OA_ASSERT_OFFSET(Game, units, 0x14357);
OA_ASSERT_OFFSET(Game, units_last, 0x1435b);
OA_ASSERT_OFFSET(Game, hot_units, 0x1435f);
OA_ASSERT_OFFSET(Game, hot_radar_units, 0x14363);
OA_ASSERT_OFFSET(Game, hot_unit_count, 0x14367);
OA_ASSERT_OFFSET(Game, hot_radar_unit_count, 0x1436b);
OA_ASSERT_OFFSET(Game, cycle_unit_id, 0x1436f);
OA_ASSERT_OFFSET(Game, periodic_countdown, 0x14371);
OA_ASSERT_OFFSET(Game, periodic_flags, 0x14373);
OA_ASSERT_OFFSET(Game, render_arena_block, 0x14374);
OA_ASSERT_OFFSET(Game, unit_def_count, 0x1438f);
OA_ASSERT_OFFSET(Game, unit_def_id_bits, 0x14393);
OA_ASSERT_OFFSET(Game, unit_defs_stale, 0x14397);
OA_ASSERT_OFFSET(Game, unit_defs, 0x1439b);
OA_ASSERT_OFFSET(Game, sprite_and_effect_tables, 0x1439f);
OA_ASSERT_OFFSET(Game, sound_count, 0x33a0f);
OA_ASSERT_OFFSET(Game, sounds, 0x33a13);
OA_ASSERT_OFFSET(Game, sound_names, 0x33e13);
OA_ASSERT_OFFSET(Game, sound_files, 0x35e13);
OA_ASSERT_OFFSET(Game, block_after_sound_files, 0x37e13);
OA_ASSERT_OFFSET(Game, offscreen_surface, 0x37e1b);
OA_ASSERT_OFFSET(Game, offscreen_width, 0x37e1f);
OA_ASSERT_OFFSET(Game, offscreen_height, 0x37e23);
OA_ASSERT_OFFSET(Game, battlefield_rect, 0x37e27);
OA_ASSERT_OFFSET(Game, viewport_width, 0x37e37);
OA_ASSERT_OFFSET(Game, viewport_height, 0x37e3b);
OA_ASSERT_OFFSET(Game, resource_readout, 0x37e3f);
OA_ASSERT_OFFSET(Game, block_after_resource_readout, 0x37e60);
OA_ASSERT_OFFSET(Game, status_panel_offset, 0x37e90);
OA_ASSERT_OFFSET(Game, status_lightbar, 0x37e94);
OA_ASSERT_OFFSET(Game, block_after_status_lightbar, 0x37e98);
OA_ASSERT_OFFSET(Game, panel_unit_id, 0x37e9c);
OA_ASSERT_OFFSET(Game, panel_unit_type, 0x37e9e);
OA_ASSERT_OFFSET(Game, mission_panel_name, 0x37ea0);
OA_ASSERT_OFFSET(Game, frame_flags, 0x37ebe);
OA_ASSERT_OFFSET(Game, order_summary, 0x37ec0);
OA_ASSERT_OFFSET(Game, order_summary_ext, 0x37ec2);
OA_ASSERT_OFFSET(Game, wind_change_tick, 0x37ec4);
OA_ASSERT_OFFSET(Game, wind_strength_divisor, 0x37ec8);
OA_ASSERT_OFFSET(Game, wind_vector, 0x37ecc);
OA_ASSERT_OFFSET(Game, wind_direction, 0x37ed8);
OA_ASSERT_OFFSET(Game, wind_strength, 0x37eda);
OA_ASSERT_OFFSET(Game, wind_factor, 0x37ede);
OA_ASSERT_OFFSET(Game, wind_changed, 0x37ee2);
OA_ASSERT_OFFSET(Game, units_per_player, 0x37ee6);
OA_ASSERT_OFFSET(Game, word_after_units_per_player, 0x37ee8);
OA_ASSERT_OFFSET(Game, max_units, 0x37eea);
OA_ASSERT_OFFSET(Game, max_units_setting, 0x37eec);
OA_ASSERT_OFFSET(Game, difficulty, 0x37eee);
OA_ASSERT_OFFSET(Game, campaign_side, 0x37ef2);
OA_ASSERT_OFFSET(Game, session_rules, 0x37ef6);
OA_ASSERT_OFFSET(Game, interface_type, 0x37efa);
OA_ASSERT_OFFSET(Game, message_filter, 0x37efe);
OA_ASSERT_OFFSET(Game, screen_chat, 0x37f02);
OA_ASSERT_OFFSET(Game, graphics_flags, 0x37f06);
OA_ASSERT_OFFSET(Game, gamma, 0x37f08);
OA_ASSERT_OFFSET(Game, fx_volume, 0x37f0c);
OA_ASSERT_OFFSET(Game, music_volume, 0x37f10);
OA_ASSERT_OFFSET(Game, music_flags, 0x37f14);
OA_ASSERT_OFFSET(Game, cd_mode, 0x37f16);
OA_ASSERT_OFFSET(Game, unit_sound_volume, 0x37f17);
OA_ASSERT_OFFSET(Game, unit_text_volume, 0x37f18);
OA_ASSERT_OFFSET(Game, sound_flags, 0x37f19);
OA_ASSERT_OFFSET(Game, screen_width, 0x37f1b);
OA_ASSERT_OFFSET(Game, screen_height, 0x37f1f);
OA_ASSERT_OFFSET(Game, text_scroll, 0x37f23);
OA_ASSERT_OFFSET(Game, text_lines, 0x37f27);
OA_ASSERT_OFFSET(Game, block_after_text_lines, 0x37f2b);
OA_ASSERT_OFFSET(Game, console_flags, 0x37f2f);
OA_ASSERT_OFFSET(Game, player_timeout_seconds, 0x37f31);
OA_ASSERT_OFFSET(Game, send_error_percent, 0x37f35);
OA_ASSERT_OFFSET(Game, side_count, 0x37f39);
OA_ASSERT_OFFSET(Game, sides, 0x37f3d);
OA_ASSERT_OFFSET(Game, last_frame_time, 0x38a37);
OA_ASSERT_OFFSET(Game, pending_ticks, 0x38a3b);
OA_ASSERT_OFFSET(Game, frame_elapsed, 0x38a3f);
OA_ASSERT_OFFSET(Game, tick_remainder, 0x38a43);
OA_ASSERT_OFFSET(Game, tick, 0x38a47);
OA_ASSERT_OFFSET(Game, requested_speed, 0x38a4b);
OA_ASSERT_OFFSET(Game, current_speed, 0x38a4d);
OA_ASSERT_OFFSET(Game, speed_adaptation, 0x38a4f);
OA_ASSERT_OFFSET(Game, sim_run_flags, 0x38a51);
OA_ASSERT_OFFSET(Game, output_directory, 0x38a53);
OA_ASSERT_OFFSET(Game, capture_path, 0x38b53);
OA_ASSERT_OFFSET(Game, capture_enabled, 0x38c53);
OA_ASSERT_OFFSET(Game, capture_rate, 0x38c57);
OA_ASSERT_OFFSET(Game, next_capture_tick, 0x38c5b);
OA_ASSERT_OFFSET(Game, output_directory_changed, 0x38c5f);
OA_ASSERT_OFFSET(Game, capture_rate_changed, 0x38c63);
OA_ASSERT_OFFSET(Game, cleared_with_change_flags, 0x38c67);
OA_ASSERT_OFFSET(Game, block_before_saved_game, 0x38c6b);
OA_ASSERT_OFFSET(Game, saved_game, 0x38d6b);
OA_ASSERT_OFFSET(Game, load_progress, 0x38d6f);
OA_ASSERT_OFFSET(Game, load_flags, 0x38d75);
OA_ASSERT_OFFSET(Game, effect_layers, 0x38d77);
OA_ASSERT_OFFSET(Game, playing_movie, 0x38d7b);
OA_ASSERT_OFFSET(Game, campaign_unlock_flags, 0x38d7f);
OA_ASSERT_OFFSET(Game, slot_count, 0x38d81);
OA_ASSERT_OFFSET(Game, profile_times, 0x38d85);
OA_ASSERT_OFFSET(Game, profiling, 0x38dd5);
OA_ASSERT_OFFSET(Game, scores, 0x38dd9);
OA_ASSERT_OFFSET(Game, block_after_scores, 0x3901d);
OA_ASSERT_OFFSET(Game, endgame_state, 0x39057);
OA_ASSERT_OFFSET(Game, endgame_next_tick, 0x3905b);
OA_ASSERT_OFFSET(Game, endgame_fade_tick, 0x3905f);
OA_ASSERT_OFFSET(Game, endgame_fade_done, 0x39063);
OA_ASSERT_OFFSET(Game, endgame_shade_countdown, 0x39067);
OA_ASSERT_OFFSET(Game, endgame_column, 0x3906b);
OA_ASSERT_OFFSET(Game, block_after_endgame_column, 0x3906f);
OA_ASSERT_OFFSET(Game, no_movie, 0x39073);
OA_ASSERT_OFFSET(Game, endgame_pictures, 0x39077);
OA_ASSERT_OFFSET(Game, score_maxima, 0x3918f);
OA_ASSERT_OFFSET(Game, mission_index, 0x391ab);
OA_ASSERT_OFFSET(Game, victory, 0x391af);
OA_ASSERT_OFFSET(Game, pinned_unit_a_valid, 0x391b3);
OA_ASSERT_OFFSET(Game, pinned_unit_a, 0x391b7);
OA_ASSERT_OFFSET(Game, pinned_unit_b_valid, 0x391b9);
OA_ASSERT_OFFSET(Game, pinned_unit_b, 0x391bd);
OA_ASSERT_OFFSET(Game, show_ranges, 0x391bf);
OA_ASSERT_OFFSET(Game, show_bandwidth, 0x391c3);
OA_ASSERT_OFFSET(Game, download_menus, 0x391c7);
OA_ASSERT_OFFSET(Game, mission_results, 0x391cf);
OA_ASSERT_OFFSET(Game, game_options, 0x391e9);
OA_ASSERT_OFFSET(Game, block_after_game_options, 0x391ed);
OA_ASSERT_OFFSET(Game, mode, 0x391f1);
OA_ASSERT_OFFSET(Game, mode_callback, 0x391f5);
OA_ASSERT_OFFSET(Game, common_fonts, 0x391f9);
OA_ASSERT_OFFSET(Game, provider_guid, 0x39201);
OA_ASSERT_OFFSET(Game, block_after_provider_guid, 0x39211);
OA_ASSERT_OFFSET(Game, session_record, 0x39219);
OA_ASSERT_OFFSET(Game, start_options, 0x39229);
OA_ASSERT_OFFSET(Game, outcome_countdown, 0x39239);
OA_ASSERT_OFFSET(Game, outcome_flags, 0x3923b);
OA_ASSERT_OFFSET(Game, block_after_outcome_flags, 0x3923d);
OA_ASSERT_OFFSET(Game, movie_skip, 0x39241);
OA_ASSERT_OFFSET(Game, block_after_movie_skip, 0x39245);
OA_ASSERT_OFFSET(Game, restart_requested, 0x39249);

OA_CORE_END

#endif
