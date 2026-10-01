# Variances from 3.1c

Open Annihilation plays as Total Annihilation 3.1c does. This document lists
the places where it always differs, on purpose: what 3.1c does, what the
engine does, and why.

## Order markers over the fog

- **3.1c:** draws the smoke and then the fog over the order overlays shown
  while Shift is held. Orders queued onto ground that was never mapped are
  hidden under the black, and those on ground out of sight are grayed.
- **Open Annihilation:** draws the order overlays over the smoke and the fog,
  so every queued order shows as it does on ground in sight: target markers,
  path pips, order lines, labels, build footprints and range circles. The
  overlays show only the player's own orders, so nothing under the fog is
  revealed.
- **Why:** queued orders stay readable wherever they go. This is permanent;
  there is no setting for it.
- **Code:** `oa::app::Runtime::render_match_surface`
  (`src/app/runtime_match_render.cpp`).

## Tab selects the next unit

- **3.1c:** in a game played alone, Tab opens the in-game menu, as F2 does. In
  a multiplayer game it opens the team menu.
- **Open Annihilation:** in a game played alone, Tab makes the next unit of
  the selection the one the unit panel shows; Shift+Tab goes back. With
  nothing selected it does nothing. F2 still opens the in-game menu, and a
  multiplayer game's Tab still opens the team menu.
- **Why:** a quicker way through a selection; the menu keeps its own key.
- **Code:** `oa::app::Runtime::cycle_selected_primary`
  (`src/app/runtime_hotkeys.cpp`).
