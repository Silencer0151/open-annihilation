// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The darkening a panel opened with ui::gui_input::panel_flag::shade_below
// gives the panel under it, over a frame the application keeps in RGB.
#pragma once

#include "oa/app/app.hpp"
#include "oa/present/display.hpp"
#include "oa/ui/display_layout.hpp"

#include <cstdint>

namespace oa::app {

/// Shade level the panel loader darkens the panel below a shade_below panel with.
inline constexpr int32_t kShadeBelowLevel = -0x18;

/// Darkens a rectangle of an RGB frame as the panel loader darkens the panel
/// below a shade_below panel.
///
/// Each pixel goes back to its palette entry (the lowest one holding its
/// colour, else the nearest by summed channel difference), the rectangle is
/// remapped as an 8-bit surface through the display's shade table at
/// kShadeBelowLevel (present::shade_rect_level, whose entries 0x80 to 0xFF
/// read the row before), and the entries come out through the palette again.
/// The part of the rectangle outside the frame is left; nothing changes
/// without a shade table.
///
/// @param[in,out] frame RGB frame
/// @param panel rectangle darkened, in the frame's pixels
/// @param palette palette the frame's colours come from
/// @param display display whose shade table darkens the entries
void shade_panel_below(
    renderer::Surface& frame,
    oa::ui::display_layout::Rect panel,
    const oa::PaletteBytes& palette,
    oa::present::DisplayContext& display
);

} // namespace oa::app
