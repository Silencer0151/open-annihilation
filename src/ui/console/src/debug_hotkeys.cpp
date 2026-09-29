// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// In-match key-code dispatch and the debug keys.
#include "oa/ui/console/hotkeys.hpp"
#include "oa/ui/console/game_fields.hpp"

#include "oa/sim/simulation_state.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace oa::ui::console {
namespace {

constexpr int32_t kSessionMultiplayer = 3;
constexpr uint8_t kDebugOverlayModes = 5;
constexpr uint8_t kNoAttackerOwner = OA_PLAYER_COUNT;
constexpr size_t kPathBytes = 0x100;
constexpr size_t kMovieNumberAt = 5; // digits follow "MOVIE"

const HotkeyHost kNoHost{};

/// Tests whether Shift is held.
///
/// @param host Key host.
/// @return False without a shift_down callback.
bool shift_down(const HotkeyHost& host) noexcept {
    return host.shift_down != nullptr && host.shift_down(host.context);
}

/// Plays a named interface sound through the host; nothing without a callback.
///
/// @param host Key host.
/// @param name Sound name.
void play(const HotkeyHost& host, const char* name) noexcept {
    if (host.play_sound != nullptr)
        host.play_sound(host.context, name);
}

/// Calls a host action that takes no value; nothing when it is null.
///
/// @param host Key host whose context is passed.
/// @param fn Host action.
void call(const HotkeyHost& host, void (*fn)(void*)) noexcept {
    if (fn != nullptr)
        fn(host.context);
}

/// Calls a host action with one value; nothing when it is null.
///
/// @param host Key host whose context is passed.
/// @param fn Host action.
/// @param value Value passed to the action.
void call(const HotkeyHost& host, void (*fn)(void*, int32_t), int32_t value) noexcept {
    if (fn != nullptr)
        fn(host.context, value);
}

/// Tests whether the passphrase has enabled the developer commands and keys.
///
/// @param game Game block.
/// @return Whether console_flag::developer is set.
bool developer(const Game& game) noexcept {
    return (console_flags(game) & console_flag::developer) != 0;
}

/// Tests whether the speed keys may change the game speed.
///
/// They are ignored in debug-key mode and for a local player that only
/// watches.
///
/// @param world World whose local player is tested.
/// @return Whether the speed may change.
bool speed_change_allowed(World* world) noexcept {
    const Game& game = world->game;
    if ((game.outcome_flags & outcome_flag::debug_keys) != 0)
        return false;
    const auto index = game.local_player_index;
    if (index >= OA_PLAYER_COUNT)
        return true;
    Player* player = &world->game.players[index];
    if (player->in_use == 0)
        return true;
    const PlayerSetupInfo* info = world_player_info(world, player);
    return info == nullptr || (info->options & OA_SETUP_OPTION_WATCHER) == 0;
}

/// Toggles the damage bars and saves the options ('!', '#', '*', '`' and '~').
///
/// @param[in,out] console Console whose Game.graphics_flags and host change.
void toggle_damage_bars(Console* console) noexcept {
    Game& game = console->world->game;
    game.graphics_flags = static_cast<uint16_t>(game.graphics_flags ^ graphics_flag::damage_bars);
    const ConsoleHost* host = console->host;
    if (host != nullptr && host->save_game_options != nullptr)
        host->save_game_options(host->context);
}

struct HighestNumber {
    size_t skip;
    int32_t highest;
};

/// Keeps the highest number a listed file name carries after its prefix (list_files visitor).
///
/// @param user The HighestNumber being gathered.
/// @param name Listed file name; names shorter than the prefix are skipped.
void keep_highest(void* user, const char* name) {
    auto* state = static_cast<HighestNumber*>(user);
    if (std::strlen(name) < state->skip)
        return;
    const int32_t number = std::atoi(name + state->skip);
    if (number > state->highest)
        state->highest = number;
}

/// Finds the highest number the files matching a pattern carry after their prefix.
///
/// @param host Key host that lists the files.
/// @param pattern Wildcard path to list.
/// @param skip Characters of each name before its number.
/// @param start Result when no file carries a higher number.
/// @return The highest number found, or `start`.
int32_t highest_listed_number(
    const HotkeyHost& host, const char* pattern, size_t skip, int32_t start
) noexcept {
    HighestNumber state{skip, start};
    if (host.list_files != nullptr)
        host.list_files(host.context, pattern, keep_highest, &state);
    return state.highest;
}

/// Stops a running movie capture, or starts one in the next MOVIEnnn folder (Ctrl+F10).
///
/// Only after the passphrase. A new capture numbers its folder one past the
/// highest "<output directory>\MOVIE*" folder, creates it, starts the capture
/// there and captures from the current tick on.
///
/// @param[in,out] console Console whose Game capture fields change.
/// @param host Key host that lists folders and starts the capture.
void toggle_movie_capture(Console* console, const HotkeyHost& host) noexcept {
    Game& game = console->world->game;
    if (!developer(game))
        return;
    const int32_t was = game.capture_enabled;
    game.capture_enabled = 0;
    if (was != 0)
        return;
    char output[kOutputDirectoryBytes + 1] = {};
    std::memcpy(output, game_bytes(game, game_offset::output_directory), kOutputDirectoryBytes);
    // Each path is cut to its buffer; one that cannot be formatted is left empty.
    char pattern[kPathBytes];
    if (std::snprintf(pattern, sizeof pattern, "%s\\MOVIE*", output) < 0)
        pattern[0] = '\0';
    game.capture_enabled = highest_listed_number(host, pattern, kMovieNumberAt, 0) + 1;
    if (std::snprintf(
            game.capture_path,
            sizeof game.capture_path,
            "%s\\MOVIE%03i",
            output,
            game.capture_enabled
        ) < 0)
        game.capture_path[0] = '\0';
    if (console->host != nullptr && console->host->create_directories != nullptr)
        console->host->create_directories(console->host->context, game.capture_path);
    if (host.begin_movie_capture != nullptr)
        host.begin_movie_capture(host.context, game.capture_path);
    game.next_capture_tick = game.tick;
}

/// Pins the unit under the cursor into a pinned-unit slot, or clears the slot when there is none (Shift+F1, Shift+F2 or Shift+Tab).
///
/// @param[in,out] game Game block holding the slot.
/// @param valid_offset Game offset of the slot's valid word.
/// @param unit_offset Game offset of the slot's unit index.
void pin_cursor_unit(Game& game, size_t valid_offset, size_t unit_offset) noexcept {
    if (game.cursor_unit_id == 0) {
        game_store<uint32_t>(game, valid_offset, 0u);
        return;
    }
    game_store<uint32_t>(game, valid_offset, 1u);
    game_store<uint16_t>(game, unit_offset, game.cursor_unit_id);
}

/// Handles the options key (Tab outside a multiplayer game, F2).
///
/// Shift pins the cursor unit as pinned unit B; otherwise the options panel
/// opens unless it already is (kFrameFlagOptionsOpen).
///
/// @param[in,out] console Console whose Game block changes.
/// @param host Key host.
void options_key(Console* console, const HotkeyHost& host) noexcept {
    Game& game = console->world->game;
    if (shift_down(host)) {
        pin_cursor_unit(game, game_offset::pinned_unit_b_valid, game_offset::pinned_unit_b);
        return;
    }
    if ((game.frame_flags & kFrameFlagOptionsOpen) != 0)
        return;
    call(host, host.open_options_panel);
    game.frame_flags = static_cast<uint16_t>(game.frame_flags | kFrameFlagOptionsOpen);
}

/// Handles Ctrl+A to Ctrl+Z.
///
/// Ctrl+A selects all, Ctrl+C the commander, Ctrl+D toggles self-destruct,
/// Ctrl+S selects on screen and Ctrl+Z the matching type; any other letter
/// selects category "CTRL_<letter>".
///
/// @param host Key host.
/// @param code Key code, hotkey::control_a to control_a + 25.
/// @param add Whether Shift adds to the selection.
void control_letter_key(const HotkeyHost& host, uint32_t code, bool add) noexcept {
    const char letter = static_cast<char>('A' + (code - hotkey::control_a));
    switch (letter) {
    case 'A':
        call(host, host.select_all);
        return;
    case 'C':
        if (host.select_commander != nullptr)
            host.select_commander(host.context, add);
        return;
    case 'D':
        call(host, host.toggle_self_destruct);
        return;
    case 'S':
        call(host, host.select_on_screen);
        return;
    case 'Z':
        call(host, host.select_matching_type);
        return;
    default:
        break;
    }
    char category[8];
    std::snprintf(category, sizeof category, "CTRL_%c", letter);
    if (host.select_by_category != nullptr)
        host.select_by_category(host.context, category, add);
}

} // namespace

void hotkey_dispatch(Console* console, const HotkeyHost* host_in, uint32_t code) noexcept {
    if (code == 0)
        return;
    const HotkeyHost& host = host_in != nullptr ? *host_in : kNoHost;
    World* world = console->world;
    Game& game = world->game;
    const bool add = shift_down(host);
    switch (code) {
    case '!':
    case '#':
    case '*':
    case '`':
    case '~':
        toggle_damage_bars(console);
        break;
    case hotkey::tab:
        if (host.session_kind != nullptr &&
            host.session_kind(host.context) != kSessionMultiplayer) {
            options_key(console, host);
            break;
        }
        if ((game.frame_flags & kFrameFlagChatOpen) == 0)
            call(host, host.open_team_menu);
        break;
    case hotkey::enter:
        play(host, "SmallButton");
        call(host, host.open_chat);
        break;
    case hotkey::escape:
        if ((game.frame_flags & kFrameFlagOptionsOpen) != 0) {
            game.frame_flags = static_cast<uint16_t>(game.frame_flags & ~kFrameFlagOptionsOpen);
            call(host, host.close_options_panel);
        } else if (game_load<uint8_t>(game, game_offset::command_mode) == 1) {
            call(host, host.clear_selection_and_rearm);
        } else {
            hotkey_cancel_command(console, &host);
        }
        break;
    case '+':
    case '=':
        if (speed_change_allowed(world))
            call(host, host.change_game_speed, 1);
        break;
    case '-':
    case '_':
        if (speed_change_allowed(world))
            call(host, host.change_game_speed, -1);
        break;
    case ',':
        call(host, host.step_build_page, -1);
        break;
    case '.':
        call(host, host.step_build_page, 1);
        break;
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': {
        const bool alt = host.alt_down != nullptr && host.alt_down(host.context);
        const bool switch_alt = (game.graphics_flags & graphics_flag::switch_alt) != 0;
        if (alt != switch_alt) {
            if (host.select_squad != nullptr)
                host.select_squad(host.context, static_cast<int32_t>(code - '0'), add);
            play(host, "SelectSquad");
        } else {
            call(host, host.select_build_item, static_cast<int32_t>(code - '1'));
        }
        break;
    }
    case 'T':
        call(host, host.set_debug_index, 1);
        break;
    case 't':
        call(host, host.set_debug_index, 0);
        break;
    case hotkey::repeat_command:
        if (developer(game))
            console_execute(console, nullptr, command_class::all);
        break;
    case 'h':
        if (host.session_kind != nullptr && host.session_kind(host.context) == kSessionMultiplayer)
            call(host, host.open_share_panel);
        break;
    case 'n':
        call(host, host.debug_select_next);
        break;
    default:
        if (code >= hotkey::control_a && code < hotkey::control_zero) {
            control_letter_key(host, code, add);
        } else if (code > hotkey::control_zero && code < hotkey::control_zero + 10) {
            call(host, host.create_squad, static_cast<int32_t>(code - hotkey::control_zero));
            play(host, "CreateSquad");
        } else if (code >= hotkey::control_f5 && code < hotkey::control_f5 + 4) {
            play(host, "SelectSquad");
            call(host, host.store_camera, static_cast<int32_t>(code - hotkey::control_f5));
        } else if (code == hotkey::control_f10) {
            toggle_movie_capture(console, host);
        } else if (code == hotkey::f1) {
            if (!add)
                call(host, host.open_unit_info_panel);
            else
                pin_cursor_unit(game, game_offset::pinned_unit_a_valid, game_offset::pinned_unit_a);
        } else if (code == hotkey::f1 + 1) {
            options_key(console, host);
        } else if (code == hotkey::f1 + 2) {
            call(host, host.cycle_tracked_camera);
        } else if (code == hotkey::f1 + 3) {
            game.graphics_flags =
                static_cast<uint16_t>(game.graphics_flags ^ graphics_flag::kill_board);
        } else if (code >= hotkey::f5 && code < hotkey::f5 + 4) {
            play(host, "SelectSquad");
            call(host, host.recall_camera, static_cast<int32_t>(code - hotkey::f5));
        } else if (code == hotkey::f11) {
            if (developer(game)) {
                game.outcome_flags =
                    static_cast<uint16_t>(game.outcome_flags ^ outcome_flag::debug_keys);
                const bool off = (game.outcome_flags & outcome_flag::debug_keys) == 0;
                if (off) {
                    game.outcome_flags =
                        static_cast<uint16_t>(game.outcome_flags & ~outcome_flag::debug_toggle_i);
                    game.debug_overlay = 0;
                }
                if (host.set_command_panel_debug != nullptr)
                    host.set_command_panel_debug(host.context, off);
            }
        } else if (code == hotkey::f12) {
            call(host, host.clear_messages);
        } else if (code == hotkey::pause) {
            game.sim_run_flags = static_cast<uint16_t>(game.sim_run_flags ^ kSimRunPaused);
            if (host.send_pause != nullptr)
                host.send_pause(host.context, (game.sim_run_flags & kSimRunPaused) != 0);
        }
        break;
    }
    if ((game.outcome_flags & outcome_flag::debug_keys) != 0)
        hotkey_debug(console, &host, code);
}

void hotkey_debug(Console* console, const HotkeyHost* host_in, uint32_t code) noexcept {
    const HotkeyHost& host = host_in != nullptr ? *host_in : kNoHost;
    World* world = console->world;
    Game& game = world->game;
    switch (code) {
    case '=':
        // Refill every active player's energy and metal to storage.
        for (uint8_t i = 0; i < OA_PLAYER_COUNT; ++i) {
            Player& player = game.players[i];
            if (!sim::simulation_state::player_slot_active(i, player))
                continue;
            player.energy = player.energy_storage;
            player.metal = player.metal_storage;
        }
        break;
    case 'P':
        call(host, host.set_video_debug, 1);
        break;
    case 'p':
        call(host, host.set_video_debug, 0);
        break;
    case ']': {
        // Kill the unit under the cursor, with no attacker credited.
        Unit* unit = world_unit_at(world, game.cursor_unit_id);
        if (unit == nullptr || unit->type_index == 0)
            break;
        unit->flags |= OA_UNIT_FLAG_DEATH_PENDING;
        unit->last_attacker_id = 0;
        unit->last_attacker_owner = kNoAttackerOwner;
        break;
    }
    case 'i':
        game.outcome_flags =
            static_cast<uint16_t>(game.outcome_flags ^ outcome_flag::debug_toggle_i);
        break;
    case 'm': {
        auto mode = static_cast<uint8_t>(game.debug_overlay + 1);
        if (mode == kDebugOverlayModes)
            mode = 0;
        game.debug_overlay = mode;
        break;
    }
    default:
        break;
    }
}

void hotkey_cancel_command(Console* console, const HotkeyHost* host) noexcept {
    Game& game = console->world->game;
    game_store<uint8_t>(game, game_offset::command_mode, 1);
    const auto buttons = game_load<uint8_t>(game, game_offset::command_button_flags);
    game_store<uint8_t>(
        game,
        game_offset::command_button_flags,
        static_cast<uint8_t>(buttons & ~kCommandButtonArmed)
    );
    if (host != nullptr && host->clear_command_button != nullptr)
        host->clear_command_button(host->context);
}

void next_indexed_file_name(
    char* out,
    size_t capacity,
    const HotkeyHost* host,
    const char* directory,
    const char* prefix,
    const char* extension
) noexcept {
    const size_t length = std::strlen(directory);
    const char* separator = length != 0 && directory[length - 1] != '\\' ? "\\" : "";
    std::snprintf(out, capacity, "%s%s%s*.%s", directory, separator, prefix, extension);
    const int32_t highest =
        host != nullptr ? highest_listed_number(*host, out, std::strlen(prefix), 0) : 0;
    std::snprintf(
        out, capacity, "%s%s%s%04i.%s", directory, separator, prefix, highest + 1, extension
    );
}

} // namespace oa::ui::console
