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
| Graphics | Maximum frame rate | 30 to 120, steps of 5 | 120; 60 on a Raspberry Pi or a light machine with the player's own preferences file | `open-annihilation.max-fps` |
| | Enhanced anti-aliasing | Off, 2×, 3×, 4×, 8×, 16× | Off, a Raspberry Pi and a light machine included | `open-annihilation.anti-aliasing` |
| | Screen size, from the next start | Desktop, 640×480, 800×600, 1024×768, 1280×1024 | Desktop; 800×600 on a light machine with the player's own preferences file, 640×480 when its desktop is smaller | `open-annihilation.screen-size` (`desktop` or `800x600`) |
| | Hardware acceleration | Off, Basic, Full | Full with the player's own preferences file on every machine; Off with `--preferences-file` | `open-annihilation.hardware-acceleration` (`off`, `basic` or `full`) |
| | Vertical sync | Off, On | Off | `open-annihilation.vertical-sync` |
| Developer | Show performance statistics | Off, On | Off | `open-annihilation.frame-stats` |

A light machine (`oa/platform/machine.hpp`, `light_machine`) has one
logical processor, a 32-bit x86 processor without SSE2 (a Pentium III or an
Athlon XP), or less than 512 MiB of physical memory; with the player's own
preferences file it starts at 800×600, 60 frames a second and no enhanced
anti-aliasing, which a machine of the game's own time keeps up with. The
screen size is read before the window opens (`src/app/screen_size.cpp`): a
size other than Desktop opens the window at that size, and full screen
switches the display to the mode nearest it.

The game counts a machine as a Raspberry Pi when Linux names its board's
model, in `/proc/device-tree/model`, starting "Raspberry Pi"
(`oa/platform/machine.hpp`). Its graphics keep up with 60 frames a second at
the game's resolutions, so it starts there; the player can raise the rate and
turn on anti-aliasing in the dialog like anywhere else, and Restore defaults
puts the Pi's defaults back.

Hardware acceleration has three levels (`HardwareAcceleration`). Off is the
game as it always drew: the processor draws and scales every frame. Basic
lets the graphics card scale and compose the frames where it is able to;
the processor still draws every pixel the game decides. Full, where the
card also draws the battlefield, is not in this build: choosing it draws
Basic, and the status says so. Its default depends only on the preferences
file, never on the machine: whether the card is used is decided apart, and
the card is used only with a renderer able to and 2 GiB of memory, which a
machine sold with 2 GB counts as having. Its two hint lines are its status
(`AccelerationStatus`), which the host gives the dialog when it opens and
again each frame: what runs, or why not, and what draws the view, what the
player can do, or what the card does on this machine. Under 2 GiB it says
the machine needs more memory, whatever the setting; otherwise, the
setting or a flag turned it off, the environment names a render driver, a
shared game or a replay waits for its end, naming the level that then
takes effect, the graphics driver failed or the game stopped while using
it, in this run or as the game's records of earlier ones say, no usable
graphics card was found, the card lacks a feature, the game cannot save the
files that guard trying it, Full is not in this build and Basic is in use,
the card is in use, on another driver where a record passed over one, or,
on a renderer nothing has looked at yet, it takes effect from the next
start. Where Full was asked for and Basic draws in its place, the first
line says why: Full's trial could not be written, there is too little
memory for Full, Full's frames were slow, Full stopped for this run, Full
failed before on this driver, the card lacks a feature Full needs, or a
shared game or a replay waits for its end; and Full in use says the card
draws the view, smoothed at every zoom, with its anti-aliasing where there
is any, and with less of it once frames were slow. A driver a record
passed over at this start shows whatever the setting. The key reads `off`, `basic` or `full`; a whole number, as the
setting's earlier On and Off switch wrote it, reads as Full above 0 and Off
otherwise, and any other text gives the default.

Vertical sync has each frame wait for the display, so that no frame tears;
while it is in effect the frame rate keeps just below the display's. Off,
the renderer is never asked, and the game paces its frames as without the
setting.

With every default the game plays as it does without the settings. Pathfinding
cycles and Unit limit are locked during a game; a shared game or a replay
always plays at 1× pathfinding and the host's unit limit. Vertical sync is
locked during a shared game or a replay, its value set before the game kept
in effect; Hardware acceleration never is, so that it can always be set to
Off, and set to Basic or Full there it takes effect from the next game.

## The dialog

`engine_settings/dialog.hpp` holds the dialog (`Dialog`), what pointer, wheel
and key events do to it (`dialog_pointer_down` and the others, `dialog_wheel`,
`dialog_key`), where its parts lie (`dialog_layout`), and how it and the OA
button are drawn (`draw_dialog`, `draw_oa_button`) without the game's art, in
the game's own fonts (`load_dialog_fonts`): its button font for labels,
values, the section list and the title, and its smaller label font for the
section heading, hints, locks, captions and the version, each readied for
text in one colour. `src/geometry.hpp` places every part, so a control is
pressed where it is drawn. `Dialog::section_hooks` (`SectionHooks`) lets the
dialog's tests and the game's checks show rows and locks of their own in
place of a section's; a host never sets it.

It is 480 by 324 source pixels, a dark gunmetal panel with a one-pixel raised
edge and hairline rules, and one green accent for what is selected:

- the header: the OA mark, "OPEN ANNIHILATION SETTINGS" and the version, with
  "Shared game - still running" in amber while a shared game keeps running;
- the sections down the left, Developer after a line, the open one marked;
- the open section's heading and rows: a label, a hint of one or two lines,
  and an Off/On switch, a level strip (Off, 2x, 3x, 4x, 8x, 16x for Enhanced
  anti-aliasing; Off, Basic, Full for Hardware acceleration) or a slider
  with stops and its value under the hint;
- Restore defaults, Cancel and OK along the bottom.

A section holds any number of rows. They lie in a view under the section's
heading, from the first row's line at pixel row 54 down to the pixel row
above the footer's line, 236 pixels high; the header, the list, the heading
and the footer never move. A section whose rows, with 8 clear pixels under
the last row's line, are taller than the view scrolls by whole source
pixels, and shows a scroll bar in the margin right of its rows: a well like
a switch's, its thumb as tall as the view's share of the section and never
under 16 pixels. Graphics, with five rows, is the one section taller than
its view, by 80 pixels; every other section fits and draws as if there were
no scrolling, with no bar. Each section keeps its offset while the
dialog is open, and every section starts at its top each time it opens. A
row the view cuts shows the part inside it and takes a press only there;
while the section is scrolled from its top, the view's first pixel row keeps
a hairline, the same as a row's own line. `dialog_layout` lists only the
parts wholly in the view.

The mouse wheel over the dialog scrolls the section 24 pixels a notch,
carrying a fraction of a pixel to the next turn; what is carried towards an
end the section has reached is dropped, and all of it when another section
shows. The scroll bar takes a press anywhere in the margin, on its thumb to
drag it or on its well to bring the thumb's middle there and drag it from
there; the thumb follows the pointer's row only. The bar takes no keyboard
focus, and a press on it leaves the focus where it is. While a press is held
the wheel and the scroll keys do nothing, so only a drag of the scroll bar
scrolls then. No scroll changes a setting or moves the focus.

Controls are numbered: the sections' entries 0 to 4, Restore defaults 5,
Cancel 6, OK 7, the scroll bar 8, and the open section's rows from 9, with
no upper end.

The game fonts have no "×" or "·", so the dialog writes "x" and "-".

A locked setting is faded, takes no press and no keyboard focus, and shows a
padlock with "Locked during a game", "Set by the host", "Set on the command
line" (the frame rate under `--max-fps`, Hardware acceleration under
`--hardware-acceleration`, in any of its forms, or
`--no-hardware-acceleration`) or "Not available here" (Hardware
acceleration when nothing in the game could help the run, and Vertical
sync on SDL's software renderer or where each change would reset the
graphics device). A locked slider shows the padlock at the right of its
label line. A locked switch keeps its switch, faded, with the padlock left
of it, so that its value still shows: Vertical sync's. A locked row whose
hint lines are its status, Hardware acceleration's strip, shows the padlock
where its control was, and fades only its label line, so that the status
keeps its strength.

Changes show at once; OK keeps them, Cancel puts back what the dialog opened
with, Restore defaults resets every setting that is not locked. A click on a
switch's half sets it, and a click on a strip's level chooses it; a press on
a slider moves its knob to the nearest stop and drags it. Every switch reads
and sets its value through one table, and every strip through another. Each
press of Restore defaults, and each time Hardware acceleration passes to a
higher level, from Off to Basic or Full or from Basic to Full, adds one to
`Dialog::forget_renderer_failures`, the
player's requests to have the graphics card tried afresh; Restore defaults
reports a change every time, even when no setting moved, so that the host
acts on it.

| Key | Does |
|---|---|
| Enter | OK |
| Escape | Cancel |
| Tab, Down | the focus to the next control: the section's rows, Restore defaults, Cancel, OK, then the sections |
| Shift+Tab, Up | the focus to the previous control |
| Left, Right | a switch Off or On, a slider or a level strip one step; along the footer's buttons |
| Space | flips a switch, presses a button, opens a section |
| Page Down, Page Up | scroll the section 200 pixels down or up |
| End, Home | scroll the section to its end or its top |

The focus shows once a key moves it; the first key to the dialog only shows
it. Page Up, Page Down, Home and End scroll whatever has the focus, and
never show or move it. A key that moves the focus onto a row, or acts on a
focused row, first scrolls the least that shows the row whole; a key that
moves it to a button or a section's entry does not scroll.

## Tests

`ui-engine-settings` covers the defaults, a Raspberry Pi's and a light
machine's included, the keys read and written, Hardware acceleration's
words and the numbers its switch once wrote, words that are no number, and
the locks of a game, the flags and the renderer;
`ui-engine-settings-dialog` the dialog's layout (every part inside the
panel and none overlapping), its sections, switches and their one table,
slider stops, both level strips, keys, footer buttons, locks and the faces
it draws; the Graphics section's five rows, their places at every offset
and under every lock, the focus scrolling them into view, both forms of a
locked row, every status of Hardware acceleration, Full's included, and
the requests to try the graphics card afresh; and, on sections of the test's own taller than the
view (`SectionHooks`), its scrolling: the view and its limit, the wheel, the
scroll bar, the scroll keys, the focus brought into view, rows the view
cuts and the control numbers; and `ui-engine-settings-dialog-data` its
fonts, and every text fitting its place in them, a scrolled section's at
every offset included, and every status line in the 309 columns of a hint,
over the installed game. `native-engine-settings` sends the wheel and the
scroll keys through the main menu's and the match's dialog, turns Vertical
sync On in the dialog and reads it back from the renderer, and sets
Hardware acceleration to Full and to Basic in a shared game, where each
waits for the game's end.
