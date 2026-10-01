# Open Annihilation settings

The settings Open Annihilation adds to the game, and the dialog that shows
them. The application opens the dialog from the OA button on the main menu
and in the in-game menu's column, and with Cmd+, on macOS (also the
application menu's Settings… item) or Ctrl+, elsewhere.

## The settings

`engine_settings.hpp` holds the settings' values (`EngineSettings`), their
defaults (`default_settings`), the preferences keys they are stored under
(`key::`), reading and writing them (`read_settings`, `write_settings`) and
the locks a running game puts on them (`settings_locks`).

| Section | Setting | Range | Default | Key |
|---|---|---|---|---|
| AI & Pathfinding | Pathfinding cycles | 1× to 8× of 1333 path nodes a tick | 1× | `open-annihilation.path-search-nodes` |
| Controls & Input | Mouse wheel zoom | Off, On | On | `open-annihilation.wheel-zoom` |
| | Escape opens the game menu | Off, On | On on macOS, Off elsewhere and with `--preferences-file` | `open-annihilation.escape-opens-menu` |
| | Select groups without Alt | Off, On | Off | 3.1c's SwitchAlt |
| Gameplay | Unit limit | 50 to 1500 per player, steps of 50 | the installation's `totala.ini` UnitLimit, else 250 | `open-annihilation.unit-limit` |
| Graphics | Maximum frame rate | 40 to 120, steps of 5 | 120 | `open-annihilation.max-fps` |
| | Enhanced anti-aliasing | Off, 2×, 3×, 4×, 8×, 16× | Off | `open-annihilation.anti-aliasing` |
| Developer | Show performance statistics | Off, On | Off | `open-annihilation.frame-stats` |

With every default the game plays as it does without the settings. Pathfinding
cycles and Unit limit are locked during a game; a shared game or a replay
always plays at 1× pathfinding and the host's unit limit.

## The dialog

`engine_settings/dialog.hpp` holds the dialog (`Dialog`), what pointer and key
events do to it (`dialog_pointer_down` and the others, `dialog_key`), where its
parts lie (`dialog_layout`), and how it and the OA button are drawn
(`draw_dialog`, `draw_oa_button`) without the game's art, in the game's own
fonts (`load_dialog_fonts`): its button font for labels, values, the section
list and the title, and its smaller label font for the section heading, hints,
locks, captions and the version, each readied for text in one colour.
`src/geometry.hpp` places every part, so a control is pressed where it is
drawn.

It is 480 by 324 source pixels, a dark gunmetal panel with a one-pixel raised
edge and hairline rules, and one green accent for what is selected:

- the header: the OA mark, "OPEN ANNIHILATION SETTINGS" and the version, with
  "Shared game - still running" in amber while a shared game keeps running;
- the sections down the left, Developer after a line, the open one marked;
- the open section's heading and rows: a label, a hint of one or two lines,
  and an Off/On switch, a level strip (Off, 2x, 3x, 4x, 8x, 16x) or a slider
  with stops and its value under the hint;
- Restore defaults, Cancel and OK along the bottom.

The game fonts have no "×" or "·", so the dialog writes "x" and "-".

A locked setting is faded, takes no press and no keyboard focus, and shows a
padlock with "Locked during a game", "Set by the host" or, for the frame rate
under `--max-fps`, "Set on the command line".

Changes show at once; OK keeps them, Cancel puts back what the dialog opened
with, Restore defaults resets every setting that is not locked. A click on a
switch's half sets it; a press on a slider moves its knob to the nearest stop
and drags it.

| Key | Does |
|---|---|
| Enter | OK |
| Escape | Cancel |
| Tab, Down | the focus to the next control: the section's rows, Restore defaults, Cancel, OK, then the sections |
| Shift+Tab, Up | the focus to the previous control |
| Left, Right | a switch Off or On, a slider or the level strip one step; along the footer's buttons |
| Space | flips a switch, presses a button, opens a section |

The focus shows once a key moves it; the first key to the dialog only shows
it.

## Tests

`ui-engine-settings` covers the defaults, the keys read and written and the
locks; `ui-engine-settings-dialog` the dialog's layout (every part inside the
panel and none overlapping), its sections, switches, slider stops, level
strip, keys, footer buttons, locks and the faces it draws; and
`ui-engine-settings-dialog-data` its fonts, and every text fitting its place
in them, over the installed game.
