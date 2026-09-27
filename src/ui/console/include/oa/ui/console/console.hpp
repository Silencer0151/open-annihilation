// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// In-game command console: the "+command" chat line, the three static
// command lists (options, cheats, developer), the AI-profile directives and
// the spawn-by-name fallback. Commands change match state through the World
// and reach systems owned elsewhere through ConsoleHost, which can also let an
// extension add commands of its own.
#pragma once

#include "oa/core/world.h"
#include "oa/ui/services/commands.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::ui::console {

// Dispatch masks. A command runs when its registered mask shares a bit with
// the caller's mask; the dispatch result is the command's full mask.
namespace command_class {
inline constexpr uint32_t option = 0x01;
inline constexpr uint32_t cheat = 0x02;
inline constexpr uint32_t developer = 0x04;
inline constexpr uint32_t ai_profile = 0x08;
inline constexpr uint32_t private_echo = 0x10; // chat echo goes to the local player only
inline constexpr uint32_t all = 0xffffffffu;
} // namespace command_class

// Chat modes the '+' line switches to for its echo (Game.chat_mode values plus
// the local-only mode).
inline constexpr uint8_t kChatModeEveryone = 0;
inline constexpr uint8_t kChatModeLocalOnly = 4;

// Message kinds passed to ConsoleHost::post_message.
inline constexpr uint8_t kMessageNotice = 2;
inline constexpr uint8_t kMessageService = 4;

// Crash-test modes of the DebugBreak command.
enum class CrashTest : uint8_t { break_into_debugger, exhaust_heap, exhaust_tagged_heap, divide };

struct Console;

// Systems owned outside the console. Every pointer may be null; the command
// then only performs its own state changes.
struct ConsoleHost {
    void* context;
    // Option persistence and presentation.
    void (*save_game_options)(void* context);
    void (*post_message)(void* context, const char* text, uint8_t kind);
    void (*compact_render_cache)(void* context);
    void (*set_gamma)(void* context, float gamma);
    void (*set_lighting)(void* context, int32_t a, int32_t b, int32_t c);
    void (*reset_sight_buffers)(void* context, bool refill_grid);
    uint16_t (*logo_count)(void* context);
    // Audio.
    void (*play_cd_track)(void* context, int32_t track);
    void (*stop_cd)(void* context);
    void (*set_music_mode)(void* context, int32_t mode);
    void (*toggle_sound_3d)(void* context);
    void (*toggle_novelty_voice)(void* context);
    // A share toggle changed the local player's info record.
    void (*player_info_changed)(void* context);
    // Simulation actions.
    void (*transfer_metal)(void* context, uint8_t from, uint8_t to, float amount);
    void (*transfer_energy)(void* context, uint8_t from, uint8_t to, float amount);
    void (*kill_player_units)(void* context, uint8_t player);
    void (*kill_all_units)(void* context);
    void (*disable_mission_conditions)(void* context);
    void (*snap_build_position)(void* context, uint16_t unit_type, FixedVec3* position);
    void (*create_unit)(
        void* context, uint8_t player, uint16_t unit_type, const FixedVec3* position
    );
    uint16_t (*find_unit_type)(void* context, const char* name); // 0: unknown
    void (*kill_units_of_type)(void* context, uint16_t unit_type);
    void (*reload_unit_type)(void* context, uint16_t unit_type);
    // The group order issuer with a mission-table kind and no position: every
    // selected local unit gets the mission with the two parameter words.
    void (*issue_group_mission)(
        void* context, uint8_t mission, int32_t parameter_1, int32_t parameter_2
    );
    void (*start_meteor_storm)(void* context);
    void (*set_meteor_enabled)(void* context, bool enabled);
    void (*clear_all_features)(void* context);
    void (*clear_feature_at)(void* context, int16_t cell_x, int16_t cell_z);
    bool (*place_feature_at)(void* context, const char* feature, int16_t cell_x, int16_t cell_z);
    // The path search's per-tick node credit and its base heuristic weight
    // (16.16).
    void (*set_search_node_credit)(void* context, int32_t nodes);
    void (*set_search_heuristic)(void* context, int32_t weight);
    // AI profiles.
    void (*reload_ai_profiles)(void* context);
    // Opens `path` for writing ("w+b") and writes the player's weight report.
    void (*write_ai_weights)(void* context, uint8_t player, const char* path);
    void (*apply_ai_weight)(void* context, uint8_t player, const char* unit_type, float percent);
    void (*apply_ai_limit)(void* context, uint8_t player, const char* unit_type, int32_t limit);
    // Files, capture and diagnostics.
    char* (*read_text_file)(void* context, const char* path, int32_t* length);
    void (*free_text_file)(void* context, char* text);
    void (*create_directories)(void* context, const char* path);
    void (*touch_file)(void* context, const char* path);
    void (*save_game)(void* context, const char* path, const char* description, int32_t game_id);
    void (*render_poster)(
        void* context,
        const char* directory,
        const char* prefix,
        int32_t x,
        int32_t y,
        int32_t width,
        int32_t height
    );
    uint32_t (*now_ms)(void* context);
    void (*crash_test)(void* context, CrashTest test);
    // Adds an extension's commands to console->commands; called once at the
    // end of console_init.
    void* extension_context{};
    void (*extend)(void* extension_context, Console* console){};
};

inline constexpr size_t kCommandTextBytes = 0x110;
inline constexpr size_t kChatLineBytes = 0x100;

struct Console {
    World* world;
    const ConsoleHost* host;
    ui::services::CommandTable commands;
    char last_command[kCommandTextBytes]; // re-run by the '\' hotkey
    // The session's cheat flag: a skirmish, or a multiplayer game whose host
    // allows cheats.
    bool cheats_enabled;
    bool sfx_flag;             // "SFX": the emitter pool refuses new particle emitters
    int32_t contour_values[2]; // "Contour" spacing and phase in heights << 8
    bool ai_plan_matches;      // the last "plan" directive named this difficulty
};

/// Clears `console`, registers every command list and the spawn fallback, then lets the host's extend add its commands.
///
/// Registers the option, cheat and developer lists and the AI-profile
/// directives ("plan", "weight", "limit"), then console_spawn_by_pattern as the
/// developer-class fallback for unknown names. ConsoleHost::extend runs last,
/// with ConsoleHost::extension_context.
///
/// @param[out] console Console to set up; every field is reset first.
/// @param world World the commands change.
/// @param host Systems owned outside the console; null runs the commands on
///             their own state changes only.
/// @return False if the command table refused one of the console's own entries;
///         entries the extension adds do not count.
bool console_init(Console* console, World* world, const ConsoleHost* host) noexcept;

/// Tokenises one command line and dispatches it under `mask`.
///
/// The line is first kept in Console::last_command (truncated to fit), so the
/// '\' hotkey can run it again. The console is console_active() while the
/// handler runs.
///
/// @param[in,out] console Console to dispatch in.
/// @param text Command line; null re-runs Console::last_command.
/// @param mask Command classes the caller allows (command_class bits).
/// @return The dispatched command's full registered mask, or 0 when nothing ran.
uint32_t console_execute(Console* console, const char* text, uint32_t mask) noexcept;

/// Computes the mask the chat line dispatches with.
///
/// @param console Console whose world and cheat flag decide the mask.
/// @return Options (with the private echo) always; cheat and developer commands
///         once the passphrase set console_flag::developer; cheats when
///         Console::cheats_enabled is set.
[[nodiscard]] uint32_t console_chat_mask(const Console* console) noexcept;

/// Runs the '+' branch of chat entry: a line starting with '+' after leading spaces is dispatched with console_chat_mask.
///
/// This is the command branch of the HUD's chat-target handler; the rest of
/// that handler belongs to the HUD.
///
/// @param[in,out] console Console to dispatch in.
/// @param line Typed chat text.
/// @param current_mode Chat mode (Game.chat_mode value) the echo uses by default.
/// @param[out] echo Receives the text after the leading spaces; may be null.
/// @return kChatModeEveryone after a cheat, kChatModeLocalOnly after an option
///         command, otherwise `current_mode` (also for a line without '+').
uint8_t console_submit_chat_line(
    Console* console, const char* line, uint8_t current_mode, const char** echo
) noexcept;

/// Returns the console whose command is being dispatched.
///
/// Command handlers take only their token line and find their console here.
///
/// @return The dispatching console, or null outside a dispatch.
Console* console_active() noexcept;

/// Posts a message through the console's ConsoleHost::post_message; nothing without one.
///
/// @param console Console whose host receives the message.
/// @param text Message text.
/// @param kind kMessageNotice or kMessageService.
void console_post(Console* console, const char* text, uint8_t kind) noexcept;

/// Runs the debugdat script "debugdat\<name>.txt" with every command class allowed.
///
/// Each line of the script is dispatched with the console active and its %N
/// tokens replaced by token N of `arguments`. The cursor position
/// (Game.cursor_position) is saved before and restored after, and the
/// script text is handed back through ConsoleHost::free_text_file.
///
/// @param[in,out] console Console to run the script in.
/// @param name Script name without directory or extension.
/// @param arguments Tokens substituted for %N; null has none.
/// @return False if the host cannot read files or the file could not be read.
bool console_run_debug_script(
    Console* console, const char* name, ui::services::TokenLine* arguments
) noexcept;

/// Creates every unit type whose name matches a pattern, in rows from the cursor.
///
/// Unit types 1 up to the smaller of Game.unit_def_count and
/// World::unit_def_count are tested with console_name_matches. Each match is
/// snapped to a build position and created for the player; x then advances
/// past the unit's footprint bounds plus a 32-pixel gap, and a row that
/// reaches the map's right edge continues 160 pixels lower at x = 160. With
/// no match the debugdat script named by the pattern runs instead, with the
/// line as its arguments. This is the developer-class fallback console_init
/// registers for unknown command names.
///
/// @param[in,out] console Console whose world and host create the units.
/// @param line Tokens: the pattern (token 0) and the owning player (token 1,
///             default 0).
void console_spawn_by_pattern(Console* console, ui::services::TokenLine* line) noexcept;

/// Matches a unit name against a spawn pattern, ignoring case.
///
/// '?' matches any one character and '*' any run, including none; the whole
/// name must match. At most 100 partial matches are followed at once.
///
/// @param name Unit name.
/// @param pattern Pattern with '?' and '*' wildcards.
/// @return Whether the name matches the pattern.
[[nodiscard]] bool console_name_matches(const char* name, const char* pattern) noexcept;

/// Marks the AI plan as matching (used by the AI unit-token parsers).
///
/// @param[out] console Console whose ai_plan_matches is set.
void console_set_ai_plan_match(Console* console) noexcept;

} // namespace oa::ui::console
