// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The touch controls' state for one runtime: the HUD state and its layout,
// shared by the dispatcher and the drawing, and each part's own state
// (docs/touch-controls.md).
#pragma once

#include "oa/app/runtime.hpp"
#include "oa/ui/touch_gestures.hpp"
#include "oa/ui/touch_hud.hpp"
#include <optional>
#include "phone_hud.hpp"      // struct PhoneHud, the phone layout's state
#include "touch_dispatch.hpp" // struct TouchDispatch, the dispatcher's state
#include "touch_layer.hpp"    // struct TouchLayer, the drawing's state

namespace oa::app {

/// The touch controls' state for one runtime (made on first use, see Runtime::touch_state).
struct Runtime::TouchState {
    oa::ui::touch_hud::HudState hud{};      ///< written by the dispatcher, read by the drawing
    oa::ui::touch_hud::Viewport viewport{}; ///< written by the dispatcher each frame
    oa::ui::touch_hud::Frame frame{};       ///< lay_out(viewport, hud), refreshed by the dispatcher
    bool frame_ready{};                     ///< frame was laid out for the current viewport
    /// Safe-area insets in window points that replace the window's own (the check sets them,
    /// because the dummy video driver reports none); read by Runtime::window_safe_insets.
    std::optional<oa::ui::display_layout::Insets> safe_override{};
    TouchDispatch dispatch{}; ///< the dispatcher's own state
    TouchLayer layer{};       ///< the drawing's own state
    PhoneHud phone{};         ///< the phone layout's own state
};

} // namespace oa::app
