// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Services the single-player, campaign and end-of-game panels call into.
// Every member may be null (absent service). The panels themselves only hold
// the state kept between calls and the game-block values they mirror.
#pragma once

#include <cstdint>

namespace oa::ui::campaign {

// Frontend signals written to the game block's pending-signal byte
// (Game.frontend_pending_signal).
namespace signal {
inline constexpr uint8_t none = 0;
inline constexpr uint8_t start_mission = 2; // briefing states raise the load flag
inline constexpr uint8_t back = 3;
inline constexpr uint8_t new_campaign = 10;
inline constexpr uint8_t skirmish = 11;
inline constexpr uint8_t any_mission = 14;
inline constexpr uint8_t campaign_briefing = 15;
inline constexpr uint8_t any_mission_briefing = 16;
} // namespace signal

// Cursor animation table entries selected around panel changes.
namespace cursor_animation {
inline constexpr int32_t panel_ready = 0x13;
inline constexpr int32_t panel_leaving = 0x14;
} // namespace cursor_animation

inline constexpr int32_t kMessageWidth = 200;

inline constexpr const char* kCampaignDiscMessage =
    "Please insert the Campaign CD (Disc 2) and try again";
inline constexpr const char* kMultiplayerDiscMessage =
    "Please insert the Multiplayer CD (Disc 1) and try again";

struct FrontendHost {
    void* context{};
    void (*play_sound)(void* context, const char* name){};
    void (*select_cursor_animation)(void* context, int32_t index){};
    void (*message_box)(void* context, const char* text, int32_t width){};
    bool (*disc_present)(void* context){};
    void (*discover_archives)(void* context){};
    void (*signal)(void* context, uint8_t signal){};
    void (*open_load_game)(void* context){};
    void (*open_options)(void* context){};
    void (*clear_selection)(void* context){};
    // Sets the active byte of the named button or list.
    void (*set_control_value)(void* context, const char* name, int32_t value){};
    // Selects the named button of a radio group: its status becomes 1 and the
    // other buttons of its group are cleared.
    void (*select_group)(void* context, const char* name){};
    void (*mark_dirty)(void* context){};
    // Fills a scrollable list from NUL-separated entries.
    void (*set_list)(void* context, const char* name, const char* entries, int32_t count){};
    // Keyboard shortcut character of a named control.
    void (*set_quick_key)(void* context, const char* name, char key){};
    void (*write_all_missions)(void* context, bool unlocked){};
    void (*save_game_options)(void* context){};
    const char* (*translate)(void* context, const char* text){};
    void (*stop_narration)(void* context){};
    // Streams a narration file `delay` engine clock ticks (30 a second) later.
    void (*play_narration)(void* context, const char* path, uint32_t delay){};
    void (*redraw_frame)(void* context){};
    // Sets a button's stage byte, which picks a staged caption.
    void (*set_stage)(void* context, const char* name, uint8_t stage){};
    // Clears bits of a control's attribute word (attribs).
    void (*clear_attributes)(void* context, const char* name, uint32_t bits){};
    // Named background for the panel just loaded (nothing deferred).
    void (*load_background)(void* context, const char* name){};
    // Sets the y or the height of the first control of `type` with this name.
    void (*set_control_y)(void* context, const char* name, uint8_t type, int16_t y){};
    void (*set_control_height)(void* context, const char* name, uint8_t type, int16_t height){};
    // Zeroes the hotspot of every frame of the panel's named GAF sequence.
    void (*zero_sequence_origins)(void* context, const char* sequence){};
    // Gives a named control the keyboard focus.
    void (*focus_control)(void* context, const char* name){};
    // Whether a Campaign list selection refills the Missions list.
    void (*set_campaign_refills_missions)(void* context, bool refills){};
};

// Null-safe wrappers used by the panels: a null host or member does nothing.

/// Plays a named interface sound.
///
/// @param host Frontend services; may be null.
/// @param name Sound name.
void host_sound(const FrontendHost* host, const char* name);

/// Selects a cursor animation table entry (cursor_animation::*).
///
/// @param host Frontend services; may be null.
/// @param index Cursor animation index.
void host_cursor(const FrontendHost* host, int32_t index);

/// Shows a message box kMessageWidth pixels wide.
///
/// @param host Frontend services; may be null.
/// @param text Message text.
void host_message(const FrontendHost* host, const char* text);

/// Tells whether the game disc is present.
///
/// @param host Frontend services; may be null.
/// @return true without a host or a disc check.
bool host_disc_present(const FrontendHost* host);

/// Rescans the game archives (after a disc check).
///
/// @param host Frontend services; may be null.
void host_discover_archives(const FrontendHost* host);

/// Writes a frontend signal (signal::*) to the game block's pending-signal byte.
///
/// @param host Frontend services; may be null.
/// @param value Signal value.
void host_signal(const FrontendHost* host, uint8_t value);

/// Clears the panel's selection.
///
/// @param host Frontend services; may be null.
void host_clear_selection(const FrontendHost* host);

/// Sets a button or list control's value by name.
///
/// @param host Frontend services; may be null.
/// @param name Control name.
/// @param value New value.
void host_control_value(const FrontendHost* host, const char* name, int32_t value);

/// Moves the first control of `type` with this name to a new y; its x and
/// size are kept.
///
/// @param host frontend services; may be null
/// @param name control name
/// @param type GUI record type of the control (oa::ui::gui_layout::GadgetType)
/// @param y new top edge, in the panel's coordinates
void host_control_y(const FrontendHost* host, const char* name, uint8_t type, int16_t y);

/// Selects one button of a radio group.
///
/// @param host Frontend services; may be null.
/// @param name Button name.
void host_select_group(const FrontendHost* host, const char* name);

/// Marks the panel for redraw.
///
/// @param host Frontend services; may be null.
void host_mark_dirty(const FrontendHost* host);

/// Fills a scrollable list from NUL-separated entries.
///
/// @param host Frontend services; may be null.
/// @param name List control name.
/// @param entries NUL-separated entries.
/// @param count Number of entries.
void host_set_list(const FrontendHost* host, const char* name, const char* entries, int32_t count);

/// Translates interface text.
///
/// @param host Frontend services; may be null.
/// @param text English text.
/// @return The translation, or `text` when there is none.
const char* host_translate(const FrontendHost* host, const char* text);

/// Stops the briefing narration.
///
/// @param host Frontend services; may be null.
void host_stop_narration(const FrontendHost* host);

/// Streams a briefing narration file after a delay.
///
/// @param host Frontend services; may be null.
/// @param path Narration file path.
/// @param delay Engine clock ticks (30 a second) before the narration is heard.
void host_play_narration(const FrontendHost* host, const char* path, uint32_t delay);

/// Redraws the current frame.
///
/// @param host Frontend services; may be null.
void host_redraw(const FrontendHost* host);

/// Sets a button's stage byte, which picks a staged caption.
///
/// @param host Frontend services; may be null.
/// @param name Button name.
/// @param stage New stage.
void host_stage(const FrontendHost* host, const char* name, uint8_t stage);

} // namespace oa::ui::campaign
