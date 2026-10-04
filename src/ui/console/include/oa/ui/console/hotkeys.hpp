// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// In-match key-code dispatch: selection, squads, cameras, panels, game speed,
// capture, and the debug keys that work after the console passphrase.
#pragma once

#include "oa/ui/console/console.hpp"
#include "oa/data/campaign/campaign_file.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace oa::ui::console {

// Engine key codes the dispatcher recognises beyond printable ASCII.
namespace hotkey {
inline constexpr uint32_t tab = 0x09;
inline constexpr uint32_t enter = 0x0d;
inline constexpr uint32_t escape = 0x1b;
inline constexpr uint32_t repeat_command = '\\';
inline constexpr uint32_t control_a = 0xaa;    // Ctrl+A .. Ctrl+Z are 0xaa..0xc3
inline constexpr uint32_t control_zero = 0xc4; // Ctrl+0 .. Ctrl+9 are 0xc4..0xcd
inline constexpr uint32_t control_f5 = 0xd2;   // Ctrl+F1 .. Ctrl+F12 are 0xce..0xd9
inline constexpr uint32_t control_f10 = 0xd7;
inline constexpr uint32_t f1 = 0xe2; // F1 .. F12 are 0xe2..0xed
inline constexpr uint32_t f5 = 0xe6;
inline constexpr uint32_t f10 = 0xeb;
inline constexpr uint32_t f11 = 0xec;
inline constexpr uint32_t f12 = 0xed;
inline constexpr uint32_t insert = 0xee;
inline constexpr uint32_t pause = 0xf8;
} // namespace hotkey

// Systems the key dispatcher drives. Null pointers are skipped.
struct HotkeyHost {
    void* context{};
    bool (*shift_down)(void* context){};
    bool (*alt_down)(void* context){};
    oa::data::campaign::SessionKind (*session_kind)(void* context){}; // game options state
    void (*play_sound)(void* context, const char* name){};
    void (*step_build_page)(void* context, int32_t direction){};
    void (*open_team_menu)(void* context){};
    void (*open_chat)(void* context){};
    void (*clear_selection_and_rearm)(void* context){};
    void (*clear_command_button)(void* context){};
    void (*close_options_panel)(void* context){};
    void (*change_game_speed)(void* context, int32_t direction){};
    void (*select_squad)(void* context, int32_t squad, bool add){};
    void (*select_build_item)(void* context, int32_t index){};
    void (*set_debug_index)(void* context, int32_t index){};
    void (*open_share_panel)(void* context){};
    void (*debug_select_next)(void* context){};
    void (*select_all)(void* context){};
    void (*select_by_category)(void* context, const char* category, bool add){};
    void (*select_commander)(void* context, bool add){};
    void (*toggle_self_destruct)(void* context){};
    void (*select_on_screen)(void* context){};
    void (*select_matching_type)(void* context){};
    void (*create_squad)(void* context, int32_t squad){};
    void (*store_camera)(void* context, int32_t slot){};
    void (*recall_camera)(void* context, int32_t slot){};
    void (*begin_movie_capture)(void* context, const char* path){};
    void (*open_unit_info_panel)(void* context){};
    void (*open_options_panel)(void* context){};
    void (*cycle_tracked_camera)(void* context){};
    void (*clear_messages)(void* context){};
    void (*send_pause)(void* context, bool paused){};
    void (*set_command_panel_debug)(void* context, bool enabled){};
    void (*set_video_debug)(void* context, int32_t mode){};
    // Lists the names matching a wildcard path; each name is passed to visit.
    void (*list_files)(
        void* context, const char* pattern, void (*visit)(void* user, const char* name), void* user
    ){};
};

/// Dispatches one in-match key code, then passes it to hotkey_debug while the debug keys are on.
///
/// The keys, with Shift adding to a selection where one is made:
///  - '!', '#', '*', '`', '~' toggle the damage bars and save the options.
///  - Tab opens the options panel outside a multiplayer game (Shift pins the
///    cursor unit as pinned unit B); in one it opens the team menu unless the
///    chat line is open.
///  - Enter plays "SmallButton" and opens the chat line.
///  - Escape closes an open options panel; otherwise it clears the selection
///    when no command is armed, or cancels the armed one (hotkey_cancel_command).
///  - '+'/'=' and '-'/'_' change the game speed, except in debug-key mode or
///    for a watching local player; ',' and '.' step the build page.
///  - '1'..'9' select that squad when Alt is held (without Alt under
///    SwitchAlt), playing "SelectSquad"; otherwise build item 0..8.
///  - 'T'/'t' set the debug index to 1/0; 'n' calls debug_select_next; '\'
///    re-runs the last console line once the passphrase is accepted; 'h'
///    opens the share panel in a multiplayer game.
///  - Ctrl+A selects all, Ctrl+C the commander, Ctrl+D toggles self-destruct,
///    Ctrl+S selects on screen, Ctrl+Z the matching type; any other Ctrl+letter
///    selects category "CTRL_<letter>". Ctrl+1..Ctrl+9 create that squad
///    ("CreateSquad"); Ctrl+F5..F8 store camera 0..3 ("SelectSquad"); Ctrl+F10
///    starts or stops a movie capture once the passphrase is accepted.
///  - F1 opens the unit info panel (Shift+F1 pins the cursor unit as pinned
///    unit A); F2 is the options key as Tab; F3 cycles the tracked camera; F4
///    toggles graphics_flag::kill_board; F5..F8 recall camera 0..3
///    ("SelectSquad"); F11 toggles the debug keys after the passphrase,
///    clearing debug toggle 'i' and the debug overlay when they go off, and
///    tells set_command_panel_debug whether they are off; F12 clears the
///    messages; Pause toggles the simulation pause and reports it through
///    send_pause.
///  - Under Console::key_remaps, Insert re-runs the last console line as '\'
///    did and F10 toggles the debug keys as F11 does, while '\' does nothing.
///  - Under Console::team_menu_every_game, Tab opens the team menu in every
///    game type.
///
/// @param[in,out] console Console whose world's Game block the keys change.
/// @param host Systems the keys drive; null skips every host call.
/// @param pressed Engine key code (ASCII or a hotkey:: value); 0 does nothing.
void hotkey_dispatch(Console* console, const HotkeyHost* host, uint32_t pressed) noexcept;

/// Handles the keys of debug-key mode (F11 after the passphrase).
///
/// '=' refills every active player's energy and metal to storage; 'P'/'p' set
/// the video debug mode to 1/0; ']' marks the unit under the cursor for death
/// with no attacker credited; 'i' toggles outcome_flag::debug_toggle_i; 'm' steps the
/// debug overlay through its five modes. Other codes do nothing.
///
/// @param[in,out] console Console whose world's Game block and units the keys change.
/// @param host Systems the keys drive; null skips every host call.
/// @param code Engine key code.
void hotkey_debug(Console* console, const HotkeyHost* host, uint32_t code) noexcept;

/// Cancels the armed command (Escape with a command armed).
///
/// Sets the command mode (Game.pointer_command) to 1, no command armed, clears
/// kCommandButtonArmed from the command button flags (Game.pointer_flags) and
/// has the host clear the command's button.
///
/// @param[in,out] console Console whose world's Game block changes.
/// @param host Clears the button; null skips it.
void hotkey_cancel_command(Console* console, const HotkeyHost* host) noexcept;

/// Builds the next free "<directory>\<prefix>NNNN.<extension>" file name.
///
/// The number is one past the highest already present: the host lists
/// "<directory>\<prefix>*.<extension>" and each name is read as a decimal
/// number after the prefix. A directory without a trailing backslash gets one;
/// an empty directory gets none. The name is kept whole, however long the
/// directory is.
///
/// @param host Lists the existing files; null counts from 0, giving NNNN = 0001.
/// @param directory Directory of the files.
/// @param prefix File name before the number.
/// @param extension File extension without the dot.
/// @return The file name.
std::string next_indexed_file_name(
    const HotkeyHost* host, const char* directory, const char* prefix, const char* extension
);

} // namespace oa::ui::console
