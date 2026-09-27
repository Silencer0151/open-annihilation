// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Frame-time profile behind the "Profile" console command: clock time is
// charged to the category whose work just ended, and every game frame the
// totals become the bar graph's figures.
#pragma once

#include "oa/core/game_state.h"

#include <cstdint>

namespace oa::sim::profile {

enum class Category : int32_t {
    sync = OA_PROFILE_SYNC,
    units = OA_PROFILE_UNITS,
    logic = OA_PROFILE_LOGIC,
    render_static = OA_PROFILE_RENDER_STATIC,
    render_stuff = OA_PROFILE_RENDER_STUFF,
    render_fog = OA_PROFILE_RENDER_FOG,
    sfx = OA_PROFILE_SFX,
    weapon = OA_PROFILE_WEAPON,
    misc = OA_PROFILE_MISC,
};

inline constexpr int32_t category_count = OA_PROFILE_CATEGORY_COUNT;

// Bar graph labels, in category order.
inline constexpr const char* category_labels[category_count] = {
    "Sync",
    "Units",
    "Logic",
    "Render Static",
    "Render Stuff",
    "Render Fog",
    "SFX",
    "Weapon",
    "Misc",
};

/// Charges the clock time since the last sample to a category and samples the clock again.
///
/// @param[in,out] times profile totals
/// @param now current clock reading
/// @param category category whose work just ended
void accumulate(ProfileTimes& times, uint32_t now, Category category) noexcept;

/// Starts a new profile window.
///
/// Moves the running totals into the shown ones and clears them; the shown total (the
/// bar scale) is their sum, at least 1. Samples the clock.
///
/// @param[in,out] times profile totals
/// @param now current clock reading
void begin_window(ProfileTimes& times, uint32_t now) noexcept;

} // namespace oa::sim::profile
