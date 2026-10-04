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

## Pages taller than the side column

- **3.1c:** draws a unit's page, its build, weapon or order page or the
  general page, from its GUI file at the screen's own resolution, one pixel
  to a pixel. A page taller than the screen, as some mods' pages are, runs
  off its bottom edge. The game's own pages all end within 480 rows.
- **Open Annihilation:** the side column is one picture, as 3.1c's screen
  lays it out: the radar picture in its top 128 rows and each page under it
  where its GUI file places it, down to the tallest unit page the game's GUI
  pages hold. Where those rows fit the window at the interface's scale, as
  every page of 3.1c and its add-ons does, the column is drawn at that scale
  and keeps its width. Otherwise the whole column, radar, panel art and
  page, is drawn at the one smaller scale at which the tallest page ends on
  the window's last row, by the same factor across and down, so that every
  part keeps its place relative to the others; the column is then exactly as
  wide as its 128 columns at that scale, and the bars and the battlefield
  start at its right edge and take the width it gives up: the bars keep
  their scale and still reach the window's right edge, their art going on
  past the interface's 640 columns as 3.1c draws it on a wider screen. The
  scale is chosen once for the game and the window's size, so the column
  keeps its width whichever unit is selected. The pointer takes each
  control, the radar and the battlefield where they are drawn.
- **Why:** every control of such a page stays in sight and in reach on
  every window size, laid out as the page's author placed it, and no blank
  strip is left beside it.
- **Code:** `oa::ui::display_layout::fit_side_column`
  (`src/ui/display-layout/include/oa/ui/display_layout.hpp`),
  `oa::app::Runtime::side_column_page_rows` (`src/app/runtime_order_panel.cpp`),
  `oa::app::Runtime::match_hud_strips` (`src/app/runtime_present.cpp`),
  `oa::app::Runtime::extend_match_bars` (`src/app/runtime_match_menus.cpp`),
  `oa::app::Runtime::hud_source_point` (`src/app/runtime_scroll_bars.cpp`).

## Modern fonts for game text

- **3.1c:** draws its text in its own 8-bit pixel fonts, which hold at most
  256 characters each.
- **Open Annihilation:** draws game text in modern fonts by default, at 80%
  of the game fonts' sizes, a fifth smaller, with a dark outline and a dark
  shadow and no background. The Language section of the OA settings turns
  each part on or off: Use modern fonts for game text (On), Text size
  (80%, from 50% to 300% in steps of 10%), Font outline (On), Font shadow
  (On) and Game text background (Off), the shaded box behind each line.
  Larger text grows over the battlefield, where the message log breaks long
  lines into rows and the chat line rises over the battlefield when the
  bottom bar is too short for it; the bars, the labels under units and the
  menus' screens keep it to the game fonts' size at most. With Use modern
  fonts for game text Off, game text keeps the game's own fonts at their
  own sizes, and the loading screen keeps them whatever the settings say. A
  preferences file named with `--preferences-file` starts with modern fonts
  Off.
- **Why:** modern fonts for in-game text, including internationalization:
  the letters of other languages show, and the outline and shadow keep the
  text as legible over the battlefield as the game's own outlined font. The
  text starts a fifth smaller than the game's fonts and reaches three times
  their size, so that players who need larger text can read it. The loading
  screen's few words are part of the game's look. The settings change only
  what is drawn, never the simulation, saved games or what a shared game
  sends.
- **Code:** `oa::ui::engine_settings::text_style`
  (`src/ui/engine-settings/include/oa/ui/engine_settings.hpp`),
  `oa::app::Runtime::text_style` (`src/app/runtime_engine_settings.cpp`),
  the drawing in `src/present/include/oa/present/game_text.hpp` and
  `src/app/runtime_game_text.cpp`.

## Engine line in the battle room

- **3.1c:** says nothing in a network game's battle room until a player
  types a chat line.
- **Open Annihilation:** says, as the local player's chat line, which
  engine the machine runs: `[Engine: OpenAnnihilation v<version>]`, the
  version as the settings dialog shows it, or
  `[Engine: OpenAnnihilation v<version> DEV MODE]` while Developer Mode, in
  the settings' Developer section
  ([docs/mods](docs/mods/README.md#developer-mode)), is on. It goes out
  once as the battle room is entered, hosting or joining, with the base
  game or a mod, and again whenever Developer Mode is turned on or off
  there. It is plain ASCII sent through the battle room's own chat, so
  every player sees it, the original game's clients included.
- **Why:** every player can tell that Open Annihilation is in use, and
  whether its Developer Mode may have changed the game's hacks. This is
  permanent; there is no setting for it.
- **Code:** `oa::ui::frontend_multiplayer::engine_banner_line` and
  `multiplayer_bind_engine_banner`
  (`src/ui/frontend-multiplayer/src/screens.cpp`), bound by network play
  (`src/app/netgame/runtime_net.cpp`).

## The language follows the operating system

- **3.1c:** shows its text in the language a word on its command line
  names (`german`), else in the one the `language` value of the
  game's own registry key names, which the game's installer writes, else
  in English. It never asks the operating system.
- **Open Annihilation:** a word on the command line still decides, as in
  3.1c. Without one, the Language setting in the Language section of the
  OA settings decides, and its default, System default, takes the
  first of the operating system's preferred languages, in their order, that
  the game knows: English, German, French, Italian or Spanish, whatever the
  region (`de-AT` is German), else English. The setting offers each of them
  by name. A preferences file named with `--preferences-file` starts in
  English. The registry value is not read.
- **Why:** players see the game in their own language without a command
  line, on every system the engine runs on, and can change it while the
  game runs. The language changes only what players read, never the
  simulation, saved games or what a shared game sends.
- **Code:** `oa::data::languages::preferred_language`
  (`src/data/languages/include/oa/data/languages.hpp`),
  `oa::platform::locale::preferred_locales`
  (`src/platform/locale/include/oa/platform/locale.hpp`),
  `oa::app::Runtime::apply_language` (`src/app/runtime_language.cpp`).

## Cheats in a campaign

- **3.1c:** runs the chat line's cheats (`+ATM`, `+DoubleShot`, `+HalfShot`,
  `+LOS`, `+MakePoster`, `+Mapping`, `+Meteor`, `+NowISee`, `+Radar` and
  `+View`) in a skirmish, and in a multiplayer game while the host's CHEATING
  option is on. In a campaign mission it refuses them unless the developer
  passphrase was typed first: a cheat goes out as an ordinary chat line and
  changes nothing.
- **Open Annihilation:** a campaign mission runs cheats as a skirmish does,
  from its start, after a restart and in a loaded game. A multiplayer game
  still runs them only while the host's CHEATING option is on, and the
  option commands, such as `+Sing` and `+Clock`, run everywhere, as in 3.1c.
- **Why:** cheats work in every game played alone. This is permanent; there
  is no setting for it.
- **Code:** `oa::sim::scenario::session_cheats_allowed`
  (`src/sim/scenario/src/commander_rules.cpp`).

## The save dialog centred in a match

- **3.1c:** opens the save dialog from the in-game menu where its GUI file
  places it, 81 pixels from the screen's left edge and 27 from its top, on
  a screen of any size, while it centres the load dialog on the screen. On
  a screen larger than 640x480 the save dialog sits in the top left corner.
- **Open Annihilation:** opens the save dialog from the in-game menu
  centred on the screen, as the load dialog is, at every window size and
  interface scale. Opened at the end of a mission, it keeps its place from
  the GUI file, as in 3.1c.
- **Why:** the dialog a game is saved through opens where the player
  looks, where the load dialog opens. This is permanent; there is no
  setting for it.
- **Code:** `oa::app::Runtime::enter_load_game`
  (`src/app/runtime_load_game.cpp`).

## The nanoframes' pulse in a view zoomed out

- **3.1c:** pulses each unit under construction through the nano colours,
  bright green to black and back, by the game's ticks: the band about once a
  second and the outline nearly twice a second, each unit from its own
  place. 3.1c has no zoom, so a view holds only the nanoframes of a 1x view.
- **Open Annihilation:** pulses the nanoframes exactly as 3.1c does at zoom
  1 and zoomed in. Zoomed out, the pulse slows with the units' size on
  screen: half as fast at zoom 0.5, a quarter as fast at four times out and
  beyond. It still runs by the game's ticks, never by the frames drawn.
  Back at zoom 1, the colours are 3.1c's again at once.
- **Why:** a zoomed-out view shows many small nanoframes at once, each at
  its own place in the pulse, and at 3.1c's rate they flicker across the
  screen. This is permanent; there is no setting for it.
- **Code:** `oa::present::model::advance_build_pulse`
  (`src/present/model/src/model_draw.cpp`).
