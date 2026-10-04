// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The phone layout's own state, kept in Runtime::TouchState, and its access
// to the runtime (docs/touch-controls.md).
#pragma once

#include "oa/ui/display_layout.hpp"
#include "oa/ui/touch_hud.hpp"

namespace oa::ui::frontend_renderer {
struct Surface;
}

namespace oa::app {

class Runtime;

/// The phone layout's cached region data.
struct PhoneHud {
    /// Where the unit info panel's pixels go in the 640x480 HUD layer, from which its placed
    /// region shows it (copied there before each frame is drawn); empty while no unit info
    /// sheet is placed.
    oa::ui::display_layout::Rect unit_info_source{};
};

/// The phone layout's helpers that reach the runtime's private members: static functions that
/// take Runtime&.
struct PhoneHudAccess {
    /// Returns the touch frame the placed regions go in: the dispatcher's frame while it was
    /// laid out for the canvas the match layout has, else one laid out here from the match
    /// layout, the Touch settings and the touch state, so the regions never wait on the
    /// dispatcher.
    ///
    /// @param runtime the runtime
    /// @param[out] hud the controls' state the frame was laid out for
    /// @return the frame, in canvas pixels
    [[nodiscard]] static oa::ui::touch_hud::Frame
    touch_frame(Runtime& runtime, oa::ui::touch_hud::HudState& hud);

    /// Gets a phone frame ready to draw: refreshes the placed regions and copies the unit info
    /// panel's pixels into the HUD layer where its region shows them. No-op off the phone
    /// layout.
    ///
    /// @param runtime the runtime
    static void prepare_frame(Runtime& runtime);

    /// Draws every placed region over a frame, sampling the HUD layer as it is (no gamma).
    ///
    /// @param runtime the runtime
    /// @param[in,out] frame the canvas-sized frame
    static void blit_regions(Runtime& runtime, oa::ui::frontend_renderer::Surface& frame);

    /// Returns the canvas rectangle a panel of a source size takes on the phone layout: the
    /// touch frame's panel sheet, or the safe area less a margin, with the panel fitted whole
    /// and centred, at most 1.5 points per source pixel.
    ///
    /// @param runtime the runtime
    /// @param frame the touch frame
    /// @param source_width the panel's width in source pixels
    /// @param source_height the panel's height in source pixels
    /// @return the rectangle; empty when the panel or the area is empty
    [[nodiscard]] static oa::ui::display_layout::Rect sheet_area(
        const Runtime& runtime,
        const oa::ui::touch_hud::Frame& frame,
        int source_width,
        int source_height
    );
};

} // namespace oa::app
