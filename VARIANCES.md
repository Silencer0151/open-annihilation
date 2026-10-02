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

## Build pages taller than the side column

- **3.1c:** draws a build page's GUI file at the screen's own resolution,
  one pixel to a pixel. A page taller than the screen, as some mods'
  twelve-button pages are, runs off its bottom edge. The game's own pages
  all end within 480 rows.
- **Open Annihilation:** the side column shows as many rows as the window
  holds at the interface's scale: 480 on a 4:3 window, 540 at 1920x1080 and
  more on taller windows. A page that ends within them is drawn exactly as
  authored, as every page of 3.1c and its add-ons is. A taller page shows
  its build buttons in parts of as many rows as fit, which PREV and NEXT step
  through before the next page. With room for three rows beside them its
  order buttons stay on the page under the rows shown; with less, they move
  to the general page behind an ORDERS tab beside BUILD, as the game's own
  pages have them. The part shown is the player's own and does not change
  the unit's build page.
- **Why:** every build button of such a page stays in sight and in reach on
  every window size. A hidden button can no longer take a click in the blank
  strip under the column.
- **Code:** `oa::ui::hud::fit_build_page` (`src/ui/hud/src/build_page_fit.cpp`),
  `oa::app::Runtime::fit_match_build_page` (`src/app/runtime_match_menus.cpp`).
