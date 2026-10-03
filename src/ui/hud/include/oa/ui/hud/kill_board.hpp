// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The kills/losses board that slides in from the right edge of the screen.
#pragma once

#include "oa/core/world.h"

#include <cstdint>

namespace oa::ui::hud {

inline constexpr int32_t kBoardWidth = 0x7d;
inline constexpr int32_t kBoardTop = 0x20;
inline constexpr int32_t kBoardRowHeight = 0x28;
/// Ticks a kill or loss count stays highlighted (it fades 2 per tick).
inline constexpr uint8_t kBoardFlashTicks = 0x1e;
/// Game.graphics_flags bit that keeps the board open.
inline constexpr uint16_t kGraphicsBoardPinned = 0x0080u;
/// Game.session_rules value (deathmatch) whose board counts commanders.
inline constexpr int32_t kSessionRulesCommanderCounts = 2;
/// The shade level the board darkens the battlefield under it with
/// (KillBoardSink::shade): shade table row 8.
inline constexpr int32_t kBoardShadeLevel = -0x18;
/// The light levels the local player's row is lit with, one after the other.
inline constexpr int32_t kLocalRowLightLevel = 0x1f;
inline constexpr int32_t kLocalRowInnerLightLevel = 0x14;

/// Board animation state kept between frames.
struct KillBoard {
    uint8_t kill_flash[OA_PLAYER_COUNT]{};
    uint8_t loss_flash[OA_PLAYER_COUNT]{};
    int32_t next_fade_tick{};
    int32_t slide{}; // 0 hidden .. kBoardWidth fully out
};

/// Highlights the killer's kill count and the victim's loss count for kBoardFlashTicks.
///
/// @param[in,out] board Board whose flash counters are set.
/// @param killer Player index of the killer; outside 0..OA_PLAYER_COUNT-1 is ignored.
/// @param victim Player index of the victim; outside 0..OA_PLAYER_COUNT-1 is ignored.
void flash_kill(KillBoard& board, int32_t killer, int32_t victim) noexcept;

/// Drawing and sound services the board uses.
struct KillBoardSink {
    void* user{};
    void (*play_sound)(void* user, const char* name){};
    /// Darkens (negative) or lightens a rectangle.
    void (*shade)(void* user, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t level){};
    /// Text in the board font; `flash` brightens a recent change.
    void (*text)(
        void* user, const char* text, int32_t x, int32_t y, int32_t width, uint8_t flash
    ){};
    int32_t (*text_width)(void* user, const char* text){};
    /// Logo of the player's colour, less a one-pixel border, stretched into x0,y0-x1,y1.
    void (*logo)(
        void* user, const Player& player, int32_t x0, int32_t y0, int32_t x1, int32_t y1
    ){};
    /// Localised interface string.
    const char* (*localize)(void* user, const char* text){};
};

/// Tells whether the board should be out: pinned by F4, or the space bar held while no text box has the keyboard.
///
/// @param game Game block holding kGraphicsBoardPinned.
/// @param hold_key Whether the hold key (space bar) is down.
/// @param typing Whether a text box has the keyboard.
/// @return Whether the board slides out.
[[nodiscard]] bool kill_board_wanted(const Game& game, bool hold_key, bool typing) noexcept;

/// Fades the highlights, slides the board toward `wanted` and draws it against the right edge of the screen.
///
/// Highlights fade by two at most once every two ticks. The slide moves a
/// quarter of the remaining distance per frame (at least one pixel), playing
/// "Panel" as it leaves an end and "Options" as it reaches one; a hidden
/// board draws nothing. The board shows "Kills" and "Losses" headings and a
/// row per player in board-row order with logo, name, kills and losses
/// (commanders killed and lost under deathmatch rules), the local player's
/// row highlighted. A row with no player closes the gap for the next frame by
/// moving the later rows up.
///
/// @param[in,out] world World whose players are drawn; board rows may be renumbered.
/// @param[in,out] board Flash counters, fade timer and slide position.
/// @param now Current game tick.
/// @param wanted Whether the board should be out (kill_board_wanted).
/// @param screen_width Screen width in pixels.
/// @param sink Drawing, sound and localisation callbacks.
void draw_kill_board(
    World& world,
    KillBoard& board,
    uint32_t now,
    bool wanted,
    int32_t screen_width,
    const KillBoardSink& sink
);

} // namespace oa::ui::hud
