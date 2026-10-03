// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Display-mode list ordering and the in-game resolution cycle hotkey.

#include <cstdint>

namespace oa::present::world_renderer {

// 12-byte entry filled by the display-mode scan.
struct DisplayMode {
    int32_t width{};
    int32_t height{};
    int32_t bits{};
};

static_assert(sizeof(DisplayMode) == 12);

inline constexpr int32_t minimum_mode_width = 640;
inline constexpr int32_t minimum_mode_height = 480;
/// The shortest mode listed when a mod's display rules keep only modes of
/// 768 rows or more (ui.display-modes min-height-768).
inline constexpr int32_t tall_minimum_mode_height = 768;

/// Orders modes by width then height and removes modes narrower than 640
/// or shorter than a minimum height.
///
/// Uses an exchange sort in place.
///
/// @param[in,out] modes mode list
/// @param count number of modes
/// @param minimum_height the shortest mode kept, in rows: 480 in 3.1c
/// @return the new count
int32_t sort_display_modes(
    DisplayMode* modes, int32_t count, int32_t minimum_height = minimum_mode_height
) noexcept;

/// Steps from the current resolution to the next (or previous) sorted mode, wrapping.
///
/// Sorts and filters the list first. Mode scanning and applying the choice are
/// the caller's.
///
/// @param[in,out] modes mode list, sorted in place
/// @param count number of modes
/// @param current_width current screen width
/// @param current_height current screen height
/// @param backwards step to the previous mode instead of the next
/// @param[out] chosen the mode to switch to
/// @return false when the current resolution is not listed
bool cycle_display_mode(
    DisplayMode* modes,
    int32_t count,
    int32_t current_width,
    int32_t current_height,
    bool backwards,
    DisplayMode& chosen
) noexcept;

} // namespace oa::present::world_renderer
