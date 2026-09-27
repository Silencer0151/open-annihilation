// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Game and Player fields the console reads and writes by byte offset, and
// the named flag bits of the console-owned option words.
#pragma once

#include "oa/core/world.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace oa::ui::console {

// Byte offsets of the Game fields that game_load, game_store and game_bytes
// reach.
namespace game_offset {
inline constexpr size_t cursor_cell_x = offsetof(Game, cursor_cell_x);
inline constexpr size_t cursor_cell_z = offsetof(Game, cursor_cell_z);
inline constexpr size_t cursor_position = offsetof(Game, cursor_position);
inline constexpr size_t cursor_feature = offsetof(Game, cursor_feature); // >= 0xfffb: none
inline constexpr size_t command_mode = offsetof(Game, pointer_command);  // 1: no command armed
inline constexpr size_t command_button_flags = offsetof(Game, pointer_flags);
inline constexpr size_t interface_mode = offsetof(Game, interface_type); // "IFace"
inline constexpr size_t screen_chat = offsetof(Game, screen_chat);
inline constexpr size_t gamma = offsetof(Game, gamma); // tenths
inline constexpr size_t console_flags = offsetof(Game, console_flags);
inline constexpr size_t output_directory = offsetof(Game, output_directory);
inline constexpr size_t output_directory_changed = offsetof(Game, output_directory_changed);
inline constexpr size_t capture_rate_changed = offsetof(Game, capture_rate_changed);
// The debug keys' pinned-unit slots: Shift+F1 pins the unit under the cursor
// in slot A, Shift+F2 or Shift+Tab in slot B. Each valid word is 1 once its
// slot holds a unit and 0 after a pin with no unit under the cursor.
inline constexpr size_t pinned_unit_a_valid = offsetof(Game, pinned_unit_a_valid); // ? 0 or 1
inline constexpr size_t pinned_unit_a = offsetof(Game, pinned_unit_a);             // ? unit id
inline constexpr size_t pinned_unit_b_valid = offsetof(Game, pinned_unit_b_valid); // ? 0 or 1
inline constexpr size_t pinned_unit_b = offsetof(Game, pinned_unit_b);             // ? unit id
inline constexpr size_t show_ranges = offsetof(Game, show_ranges);
inline constexpr size_t show_bandwidth = offsetof(Game, show_bandwidth);
} // namespace game_offset

inline constexpr size_t kOutputDirectoryBytes = 0x100;

// Bits of the console option word (Game.console_flags).
namespace console_flag {
inline constexpr uint16_t no_drop = 0x0001;   // the stalled-player check is skipped
inline constexpr uint16_t developer = 0x0002; // passphrase accepted
inline constexpr uint16_t selection_boxes = 0x0004;
inline constexpr uint16_t tree_death = 0x0008;
inline constexpr uint16_t no_shake = 0x0010;
inline constexpr uint16_t clock = 0x0040;
inline constexpr uint16_t double_shot = 0x0080;
inline constexpr uint16_t half_shot = 0x0100;
inline constexpr uint16_t full_radar = 0x0200;
inline constexpr uint16_t shoot_all = 0x0400;
} // namespace console_flag

// Bits of Game.graphics_flags toggled from the console and hotkeys.
namespace graphics_flag {
inline constexpr uint16_t damage_bars = 0x0001; // DamageBars; '!', '#', '*', '`', '~' toggle it
inline constexpr uint16_t anti_alias = 0x0002;
inline constexpr uint16_t shadow = 0x0004;
inline constexpr uint16_t vehicle_shadow = 0x0008; // the VehicleShadows option
inline constexpr uint16_t feature_shadow = 0x0010;
inline constexpr uint16_t shading = 0x0020;
inline constexpr uint16_t dither = 0x0040;
inline constexpr uint16_t kill_board = 0x0080; // F4 pins the kills board open
inline constexpr uint16_t switch_alt = 0x0100; // digits select squads without Alt
} // namespace graphics_flag

// Bits of Game.visibility_flags toggled by the LOS commands.
namespace visibility_flag {
inline constexpr uint8_t mapping = 0x01;
inline constexpr uint8_t line_of_sight = 0x02;
inline constexpr uint8_t los_type = 0x04;
} // namespace visibility_flag

// Bits of Game.outcome_flags the console writes.
namespace outcome_flag {
// Flipped by debug key 'i', and cleared when F11 turns debug_keys off and when
// a session starts; the engine never reads it.
inline constexpr uint16_t debug_toggle_i = 0x0001;
inline constexpr uint16_t debug_keys = 0x0002; // F11: debug hotkeys active
inline constexpr uint16_t finished = 0x0004;
inline constexpr uint16_t won = 0x0010;
inline constexpr uint16_t victory_transition = 0x0020;
inline constexpr uint16_t defeat_transition = 0x0040;
} // namespace outcome_flag

// Game.periodic_flags bit set while the observer camera ("BigBrother") runs.
inline constexpr uint8_t kPeriodicFlagObserver = 0x02; /* ? */
// Game.sim_run_flags bit 0.
inline constexpr uint16_t kSimRunPaused = 0x0001;
// Game.frame_flags bits.
inline constexpr uint16_t kFrameFlagOptionsOpen = 0x0001;
inline constexpr uint16_t kFrameFlagChatOpen = 0x0004;
// Command button bit cleared when an armed command is cancelled.
inline constexpr uint8_t kCommandButtonArmed = 0x20; /* ? */

// Bits of the player info record's role word that carry the resource and
// intel sharing settings.
namespace share_flag {
inline constexpr uint16_t metal = 0x0002;
inline constexpr uint16_t energy = 0x0004;
inline constexpr uint16_t mapping = 0x0020;
inline constexpr uint16_t radar = 0x0040;
} // namespace share_flag

// Unit.flags bit the "Selectable" command sets on every live unit.
inline constexpr uint32_t kUnitFlagSelectable = 0x00000020u; /* ? */

// Byte offsets of the Player fields that player_load and player_store reach.
namespace player_offset {
inline constexpr size_t metal_share_threshold = offsetof(Player, metal_share_threshold);
inline constexpr size_t energy_share_threshold = offsetof(Player, energy_share_threshold);
} // namespace player_offset

/// Reads a value of type T from the Game block at a byte offset.
///
/// @param game Game block.
/// @param offset Byte offset into oa::Game (a game_offset value).
/// @return The unaligned value stored there.
template <class T>
[[nodiscard]] inline T game_load(const Game& game, size_t offset) noexcept {
    T value;
    std::memcpy(&value, reinterpret_cast<const uint8_t*>(&game) + offset, sizeof value);
    return value;
}

/// Writes a value of type T into the Game block at a byte offset.
///
/// @param[out] game Game block.
/// @param offset Byte offset into oa::Game (a game_offset value).
/// @param value Value stored unaligned.
template <class T>
inline void game_store(Game& game, size_t offset, T value) noexcept {
    std::memcpy(reinterpret_cast<uint8_t*>(&game) + offset, &value, sizeof value);
}

/// Returns the Game block's bytes at an offset, for text fields.
///
/// @param game Game block.
/// @param offset Byte offset into oa::Game (a game_offset value).
/// @return Pointer to the byte at `offset`.
[[nodiscard]] inline char* game_bytes(Game& game, size_t offset) noexcept {
    return reinterpret_cast<char*>(&game) + offset;
}

/// Reads a value of type T from a Player record at a byte offset.
///
/// @param player Player record.
/// @param offset Byte offset into oa::Player (a player_offset value).
/// @return The unaligned value stored there.
template <class T>
[[nodiscard]] inline T player_load(const Player& player, size_t offset) noexcept {
    T value;
    std::memcpy(&value, reinterpret_cast<const uint8_t*>(&player) + offset, sizeof value);
    return value;
}

/// Writes a value of type T into a Player record at a byte offset.
///
/// @param[out] player Player record.
/// @param offset Byte offset into oa::Player (a player_offset value).
/// @param value Value stored unaligned.
template <class T>
inline void player_store(Player& player, size_t offset, T value) noexcept {
    std::memcpy(reinterpret_cast<uint8_t*>(&player) + offset, &value, sizeof value);
}

/// Reads the console option word (Game.console_flags).
///
/// @param game Game block.
/// @return The console_flag bits.
[[nodiscard]] inline uint16_t console_flags(const Game& game) noexcept {
    return game_load<uint16_t>(game, game_offset::console_flags);
}

/// Writes the console option word (Game.console_flags).
///
/// @param[out] game Game block.
/// @param flags New console_flag bits.
inline void set_console_flags(Game& game, uint16_t flags) noexcept {
    game_store(game, game_offset::console_flags, flags);
}

/// Reads a player's share settings.
///
/// The settings live in the role byte and the unused byte after it.
///
/// @param info Player info record.
/// @return The share_flag bits.
[[nodiscard]] inline uint16_t share_flags(const PlayerSetupInfo& info) noexcept {
    uint16_t value;
    std::memcpy(&value, &info.role, sizeof value);
    return value;
}

/// Writes a player's share settings into the role byte and the byte after it.
///
/// @param[out] info Player info record.
/// @param value New share_flag bits (and the role bits they share the word with).
inline void set_share_flags(PlayerSetupInfo& info, uint16_t value) noexcept {
    std::memcpy(&info.role, &value, sizeof value);
}

} // namespace oa::ui::console
