// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The in-game message log: the 30-line ring in Game.chat_lines that the
// match screen shows, chat lines, wrapped notices and the "player destroyed"
// announcement.
#pragma once

#include "oa/core/world.h"

#include <cstdint>

namespace oa::sim::messages {

inline constexpr uint32_t text_bytes = 0x40;
// Sender value for lines that come from no player (no arrival sound).
inline constexpr uint8_t sender_none = 10;
// Line kinds (low nibble of the last byte of a line).
inline constexpr uint8_t kind_unit_report = 1; // a unit's speech caption
inline constexpr uint8_t kind_status = 2;      // game speed changes, console notices
inline constexpr uint8_t kind_elimination = 4;
// A chat line another player sent, stored with the sender's Player.index:
// shown under filter 3 even while ScreenChat is off, hidden under filter 2.
inline constexpr uint8_t kind_player_chat = 8;
inline constexpr uint8_t kind_notice = 0x10; // masks to 0 in the stored line
// The bits of MessageLine.kind that hold the kind.
inline constexpr uint8_t kind_mask = 0x0f;
// Bits above the kind in a stored line: its unit was already gone to by the
// reported-unit key, and the line is drawn highlighted (the last one gone to).
inline constexpr uint8_t line_flag_visited = 0x10;
inline constexpr uint8_t line_flag_highlight = 0x20;
// The reported-unit key glides the camera rather than jumping it.
inline constexpr int32_t reported_unit_camera_glide = 1;
// Width a notice is wrapped to, and how far back a break looks for a space.
inline constexpr int32_t wrap_width = 0x3f;
inline constexpr int32_t wrap_lookback = 0xc;
inline constexpr uint32_t elimination_message_count = 3;

inline constexpr const char* sound_message_arrived = "MessageArrived";
inline constexpr const char* elimination_messages[elimination_message_count] = {
    "forces have been obliterated",
    "vermin have been exterminated",
    "forces have gone to a better place",
};
inline constexpr const char* side_name_arm = "Arm";
inline constexpr const char* side_name_core = "Core";

// Game.message_filter every session starts with: kinds 1, 4 and 8, and every
// kind while ScreenChat is on.
inline constexpr int32_t filter_session_start = 3;
inline constexpr uint32_t ticks_per_second = 30;

// Game kind (CampaignFile.kind) whose chat is also recorded.
inline constexpr int32_t game_kind_multiplayer = 3;
// Chat modes that are neither shared nor recorded (see OA_CHAT_MODE_*).
inline constexpr uint8_t chat_mode_local_only = 4;

#pragma pack(push, 1)

// One 0x48-byte line of Game.chat_lines.
struct MessageLine {
    char text[text_bytes]{}; // always terminated at text[0x3f]
    uint32_t tick{};
    uint16_t value{};
    uint8_t sender{}; // Player.index, or sender_none
    uint8_t kind{};   // low nibble
};

#pragma pack(pop)
static_assert(sizeof(MessageLine) == OA_CHAT_LINE_BYTES);

struct Hooks {
    void* context{};
    void (*play_sound)(void* context, const char* name){};
    // Redraw TIMEOUT.GUI when it is on screen.
    void (*refresh_panel)(void* context){};
    const char* (*translate)(void* context, const char* text){};
    // The match's linear congruential stream; picks the elimination taunt.
    uint32_t (*random)(void* context){};
    // Hands a chat line to the other players.
    void (*share_chat)(void* context, const char* line){};
    // Records a chat line of a multiplayer game.
    void (*record_chat)(void* context, const char* line){};
    // CampaignFile.kind.
    int32_t (*game_kind)(void* context){};
    // Centre the camera on a map pixel.
    void (*center_camera)(void* context, int32_t x, int32_t y, int32_t glide){};
};

/// Returns the number of lines the ring keeps (Game.text_lines).
///
/// @param game game record
/// @return the TextLines setting; 0 disables the log
[[nodiscard]] int32_t line_capacity(const Game& game) noexcept;
/// Returns one line of the ring.
///
/// @param game game record
/// @param index ring index
/// @return the line, or null past the ring's storage
[[nodiscard]] MessageLine* message_line(Game& game, uint32_t index) noexcept;

/// Returns the seconds a line stays on screen, less one (Game.text_scroll).
///
/// @param game game record
/// @return the TextScroll setting
[[nodiscard]] int32_t text_scroll(const Game& game) noexcept;

/// Stores the log options the settings keep in Game.
///
/// @param[in,out] game game record
/// @param lines TextLines, stored in Game.text_lines
/// @param scroll TextScroll, stored in Game.text_scroll
/// @param filter line filter, stored in Game.message_filter
/// @param screen_chat ScreenChat, stored in Game.screen_chat
void set_log_options(
    Game& game, int32_t lines, int32_t scroll, int32_t filter, uint32_t screen_chat
) noexcept;

/// Translates a text through the hooks.
///
/// @param hooks translation hook
/// @param text text to translate
/// @return hooks.translate's text, or `text` when there is none
[[nodiscard]] const char* translate(const Hooks& hooks, const char* text) noexcept;

/// Empties the log (F12).
///
/// @param[in,out] game game record
void clear_messages(Game& game) noexcept;

/// Drops the oldest line once it has been up for TextScroll + 1 seconds.
///
/// The frame runs it once after its simulated ticks.
///
/// @param[in,out] game game record
/// @return true when a line went
bool expire_oldest_message(Game& game) noexcept;

/// Goes to the unit of the oldest unvisited line that names a live unit.
///
/// Marks the line visited and highlighted and centres the camera on the unit.
///
/// @param[in,out] world message log and units
/// @param hooks camera service
/// @return false when no such line is left
bool track_next_reported_unit(World& world, const Hooks& hooks);

/// Handles the reported-unit key (F3).
///
/// Drops every line's highlight and goes to the next reported unit; once all have been
/// visited, forgets the visits and starts again from the oldest line.
///
/// @param[in,out] world message log and units
/// @param hooks camera service
void cycle_reported_units(World& world, const Hooks& hooks);

/// Appends a line to the log, dropping the oldest when the ring is full.
///
/// @param[in,out] world message log
/// @param text line text; truncated to 63 characters
/// @param kind line kind (kind_*)
/// @param value value stored with the line (the reported unit for unit reports)
/// @param sender Player.index, or sender_none for no arrival sound
/// @param hooks sound and panel services
void post_message(
    World& world, const char* text, uint8_t kind, uint16_t value, uint8_t sender, const Hooks& hooks
);

/// Posts a notice, word-wrapped into 63-character lines.
///
/// @param[in,out] world message log
/// @param text notice text
/// @param hooks sound and panel services
void post_notice(World& world, const char* text, const Hooks& hooks);

/// Announces that a player was destroyed with one of three random taunts.
///
/// @param[in,out] world message log
/// @param player destroyed player
/// @param hooks random stream, translation, sound and panel services
void post_elimination(World& world, const Player& player, const Hooks& hooks);

/// Formats a chat line, hands it to the other players and adds it to the log.
///
/// The line is "<name->target> text". It is not shared in the local-only chat mode, and
/// is recorded in a multiplayer game unless sent to chosen players or allies.
///
/// @param[in,out] world message log and chat mode
/// @param speaker player speaking
/// @param text chat text
/// @param kind line kind
/// @param target recipient name, or null for everyone
/// @param hooks sharing, recording, sound and panel services
void post_chat(
    World& world,
    const Player& speaker,
    const char* text,
    uint8_t kind,
    const char* target,
    const Hooks& hooks
);

} // namespace oa::sim::messages
