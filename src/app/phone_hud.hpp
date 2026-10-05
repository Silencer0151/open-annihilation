// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The phone layout's own state, kept in Runtime::TouchState, and its access
// to the runtime: the placed regions, and the 3.1c pictures the pad's build
// ring shows on every layout (docs/touch-controls.md).
#pragma once

#include "oa/ui/display_layout.hpp"
#include "oa/ui/touch_hud.hpp"

#include <array>
#include <stdint.h>

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

/// Where a build ring wedge's 3.1c picture comes from and where it goes: the source rectangle
/// a placed region of the phone's drawer would show, fitted whole into the wedge's picture.
struct RingPicture {
    oa::ui::display_layout::Rect source{}; ///< the gadget in the 640x480 HUD layer; empty for none
    oa::ui::display_layout::Rect canvas{}; ///< canvas pixels it is drawn at
};

/// The phone layout's helpers that reach the runtime's private members: static functions that
/// take Runtime&.
struct PhoneHudAccess {
    /// Returns where each build ring wedge's 3.1c picture comes from in the HUD layer and where
    /// it is drawn, by the placed regions' rule (display_layout::fit_inside), on any layout.
    ///
    /// @param runtime the runtime
    /// @param ring the open build ring
    /// @return the pictures by slot; an empty source for a wedge with no gadget
    [[nodiscard]] static std::array<RingPicture, oa::ui::touch_hud::build_ring_slot_count>
    build_ring_pictures(const Runtime& runtime, const oa::ui::touch_hud::BuildRing& ring);

    /// Returns a signature of the HUD layer's pixels under the build ring's pictures, so the
    /// touch layer is drawn again when the 3.1c panel redraws one (a queue count).
    ///
    /// @param runtime the runtime
    /// @param ring the open build ring
    /// @return the signature; 0 when no picture shows
    [[nodiscard]] static uint64_t
    build_ring_picture_signature(const Runtime& runtime, const oa::ui::touch_hud::BuildRing& ring);

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
