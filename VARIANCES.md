# Variances from 3.1c

Open Annihilation plays as Total Annihilation 3.1c does. This document lists
the places where it deliberately differs. Each entry is a candidate setting
for a future options screen: it says what 3.1c does, what the engine does, and
the value that keeps 3.1c's behaviour.

## Order markers over the fog

- **3.1c:** draws the smoke and then the fog over the order overlays shown
  while Shift is held. Orders queued onto ground that was never mapped are
  hidden under the black, and those on ground out of sight are grayed.
- **Open Annihilation:** draws the order overlays over the smoke and the fog,
  so every queued order shows as it does on ground in sight: target markers,
  path pips, order lines, labels, build footprints and range circles. The
  overlays show only the player's own orders, so nothing under the fog is
  revealed.
- **As a setting:** on by default; off draws the fog over them, as 3.1c does.
- **Code:** `oa::app::Runtime::render_match_surface`
  (`src/app/runtime_match_render.cpp`).

## Mouse-wheel zoom

- **3.1c:** has no zoom. The battlefield is always drawn at one map pixel to
  one screen pixel.
- **Open Annihilation:** the mouse wheel zooms the battlefield between half
  and four times that scale, by 15% a notch. The view eases to the new scale
  and keeps the ground under the pointer in place. Scrolling moves at the same
  speed on screen at any zoom.
- **As a setting:** on by default; off keeps the battlefield at one map pixel
  to one screen pixel, as 3.1c does.
- **Code:** `oa::app::Runtime::handle_match_zoom` and
  `oa::app::Runtime::step_match_zoom` (`src/app/runtime_camera.cpp`); the
  limits are `kMinBattlefieldZoom`, `kMaxBattlefieldZoom` and
  `kZoomWheelFactor` (`src/app/include/oa/app/app.hpp`).
