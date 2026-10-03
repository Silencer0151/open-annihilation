// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The resource panel of ui.resource-panel: a floating panel with one row
// per listed player (stored metal and energy, their bars and incomes), the
// clock, wind and tidal line, and a watcher's switching to another
// player's view and camera.
#pragma once

#include "oa/ui/hud/shared_views.hpp"

#include "oa/core/world.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oa::ui::hud {

inline constexpr int32_t kResourcePanelWidth = 210;
inline constexpr int32_t kResourcePanelRowHeight = 30;
/// Pixels of a full storage bar, which shows at most one less.
inline constexpr int32_t kResourcePanelBarWidth = 100;
/// The panel's first place: this far down, against the right edge.
inline constexpr int32_t kResourcePanelFirstY = 50;
/// Player slots the panel can list: 1 to 9. Slot 0 stands for the
/// watcher's own view and is never listed.
inline constexpr uint8_t kResourcePanelFirstSlot = 1;
inline constexpr uint8_t kResourcePanelSlotEnd = 10;
/// Columns from the row's left edge: the player's colour square, the name
/// and bars, and the incomes.
inline constexpr int32_t kResourcePanelSquareX = 0x24;
inline constexpr int32_t kResourcePanelBarX = 0x2c;
inline constexpr int32_t kResourcePanelIncomeX = 0x95;
/// Width of one character of the panel's text, by which numbers are aligned.
inline constexpr int32_t kResourcePanelCharWidth = 8;
/// Characters the stored amounts are right-aligned in.
inline constexpr int32_t kResourcePanelAmountChars = 5;
/// Palette indices of the panel.
inline constexpr uint8_t kResourcePanelNameColor = 0x51;
inline constexpr uint8_t kResourcePanelTextColor = 0xfe;
inline constexpr uint8_t kResourcePanelMetalBarColor = 0xe0;
inline constexpr uint8_t kResourcePanelEnergyBarColor = 0xd0;
inline constexpr uint8_t kResourcePanelTroughColor = 0x00;
inline constexpr uint8_t kResourcePanelViewRowColor = 0x34;
/// The locked-on row alternates between these two from one draw to the next.
inline constexpr uint8_t kResourcePanelLockColor = 0x14;
inline constexpr uint8_t kResourcePanelLockFlash = 0x20;
/// Text of the row that returns a watcher to their own view.
inline constexpr const char* kResourcePanelOwnViewRow = "My Original Camera";

/// How the panel's background is drawn (the player's choice).
enum class ResourcePanelBackground : uint8_t {
    none,  ///< no background; the bars get black edges
    text,  ///< no background
    solid, ///< a black panel
};

/// The panel's state kept between frames.
struct ResourcePanel {
    int32_t x{-1}; ///< left edge in source pixels; -1 before the first draw
    int32_t y{kResourcePanelFirstY};
    bool hidden{};    ///< put away by F4
    bool dragging{};  ///< the left button holds it
    int32_t drag_x{}; ///< pointer place the drag last moved from
    int32_t drag_y{};
    uint8_t flash{kResourcePanelLockColor}; ///< the locked-on row's colour this draw
    ResourcePanelBackground background{};
    /// The slot whose view a watcher shows, 0 for their own.
    uint8_t viewed_slot{};
    /// The slot whose camera a watcher's camera follows, 0 for none.
    uint8_t locked_slot{};
};

/// The players a panel lists, in slot order.
struct ResourcePanelRows {
    std::array<uint8_t, OA_PLAYER_COUNT> slots{};
    uint8_t count{};
    bool own_view_row{}; ///< a last row returns a watcher to their own view
};

/// Tells whether the local player watches: a watcher of a multiplayer game.
///
/// @param world players and their setup records
/// @return true when the local player's setup marks it a watcher
[[nodiscard]] bool local_player_watches(const World& world) noexcept;

/// Lists the panel's players.
///
/// Slots 1 to 9 in order: a watcher lists every slot in use that has a
/// name, its own included; anyone else lists the slots that share their map
/// position with this machine.
///
/// @param world players
/// @param watching the local player watches (local_player_watches)
/// @param shared what other machines report
/// @return the rows
[[nodiscard]] ResourcePanelRows
resource_panel_rows(const World& world, bool watching, const SharedPlayerViews& shared) noexcept;

/// Formats a stored amount: whole below 10000, thousands to one decimal
/// with a K below 100000, whole thousands with a K from there.
///
/// @param[out] out destination
/// @param size bytes of `out`
/// @param amount the amount
void format_panel_amount(char* out, std::size_t size, float amount);

/// Returns the filled pixels of a storage bar: the stored share of the
/// capacity of 100 pixels, truncated, at most 99; none for no capacity.
///
/// @param stored the stored amount
/// @param capacity the storage capacity
/// @return the pixels, 0 to 99
[[nodiscard]] int32_t panel_bar_pixels(float stored, float capacity) noexcept;

/// Formats an income: metal to one decimal, energy whole, both with a plus.
///
/// @param[out] out destination
/// @param size bytes of `out`
/// @param income the income
/// @param metal true for metal
void format_panel_income(char* out, std::size_t size, float income, bool metal);

/// Drawing services of the panel, in source pixels.
struct ResourcePanelSink {
    void* user{};
    void (*fill)(void* user, int32_t x, int32_t y, int32_t width, int32_t height, uint8_t color){};
    void (*text)(void* user, int32_t x, int32_t y, const char* text, uint8_t color){};
};

/// Returns the panel's height for its rows.
///
/// @param rows the rows
/// @return pixels
[[nodiscard]] int32_t resource_panel_height(const ResourcePanelRows& rows) noexcept;

/// Draws the panel: for each row the highlight of the viewed or locked-on
/// player, the player's colour square, name, stored metal and energy, their
/// bars and incomes; a watcher's last row returns to their own view.
///
/// @param world players and their economies
/// @param[in,out] panel place and highlights; the lock colour alternates
/// @param rows the rows
/// @param sink drawing services
void draw_resource_panel(
    const World& world,
    ResourcePanel& panel,
    const ResourcePanelRows& rows,
    const ResourcePanelSink& sink
);

/// What a press on the panel lands on.
struct ResourcePanelHit {
    bool inside{};   ///< on the panel
    int8_t row{-1};  ///< the row's index in ResourcePanelRows, -1 for none
    bool own_view{}; ///< the row that returns to the watcher's own view
};

/// Finds what a pointer place lands on.
///
/// @param panel place
/// @param rows the rows
/// @param x pointer column in source pixels
/// @param y pointer row
/// @return the hit
[[nodiscard]] ResourcePanelHit resource_panel_hit(
    const ResourcePanel& panel, const ResourcePanelRows& rows, int32_t x, int32_t y
) noexcept;

/// Handles F4 with the game's stats board: with the panel out and the
/// board away F4 puts the panel away; with the panel away and the board
/// out it brings the panel back; otherwise the key goes to the board.
///
/// @param[in,out] panel the panel
/// @param board_out the kill board is pinned out
/// @param has_rows the panel lists anyone
/// @return true when the panel took the key
bool resource_panel_f4(ResourcePanel& panel, bool board_out, bool has_rows) noexcept;

/// What a watcher's double-click on a row asks for.
enum class ViewSwitch : uint8_t {
    none,          ///< nothing: not a watcher, or not a row
    view_player,   ///< show the slot's view
    lock_camera,   ///< the slot is shown already: follow its camera
    unlock_camera, ///< the slot's camera is followed already: stop
    own_view,      ///< back to the watcher's own view
};

/// Decides a watcher's double-click on the panel and records it.
///
/// @param[in,out] panel viewed and locked slots
/// @param rows the rows
/// @param hit what the double-click landed on
/// @param watching the local player watches
/// @return the switch, whose view the caller then shows
ViewSwitch resource_panel_view_switch(
    ResourcePanel& panel, const ResourcePanelRows& rows, const ResourcePanelHit& hit, bool watching
) noexcept;

/// The wind a wind generator shows on the clock line, in energy.
struct WindReadout {
    int32_t current{};
    int32_t minimum{};
    int32_t maximum{};
};

/// The energy a wind generator of the clock line makes when the unit type
/// table names none: 30, and 20 for a solar collector.
inline constexpr int32_t kDefaultWindGenerator = 30;

/// Returns the clock line's wind: each wind strength (now, the map's least
/// and most) times the generator's output over the strength divisor,
/// rounded to nearest and at most the output; all 0 without a divisor.
///
/// @param strength the strength now
/// @param minimum the map's least strength
/// @param maximum the map's most strength
/// @param divisor the strength divisor (Game.wind_strength_divisor)
/// @param generator a wind generator's output (kDefaultWindGenerator without one)
/// @return the wind
[[nodiscard]] WindReadout wind_readout(
    int32_t strength, int32_t minimum, int32_t maximum, int32_t divisor, int32_t generator
) noexcept;

/// Formats "Game Time : hh:mm:ss" for a tick count, 30 ticks a second.
void format_game_time(char* out, std::size_t size, uint32_t tick);

/// Formats "Wind : +<now> (<least>-<most>)", or for a watcher "Wind : (<least>-<most>)".
void format_wind(char* out, std::size_t size, const WindReadout& wind, bool watching);

/// Formats "Tidal : +<strength>", the strength truncated.
void format_tidal(char* out, std::size_t size, float tidal_strength);

} // namespace oa::ui::hud
