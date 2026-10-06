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
| Mods | Mod | No Mod, or one of the mod folders the game finds | No Mod | `open-annihilation.mod-directory` (the folder's path; absent for No Mod) |
| | A mod folder chosen outside the folders the game finds, kept while it is the Mod | any folder | none | `open-annihilation.picked-mod-directory` (the folder's path; absent for none, erased once another mod or No Mod is stored) |
| Controls | Mouse wheel zoom | Off, On | On | `open-annihilation.wheel-zoom` |
| | Maximum zoom out, a drop-down: how far out the wheel, a pinch, the touch zoom buttons and a controller zoom | Automatic (half the game's scale, a sixth while Full draws the battlefield), Whole map, 1/32, 1/16, 1/8, 1/4, 1/2; no choice but Automatic goes past the whole map | Automatic | `open-annihilation.max-zoom-out` (`automatic`, `whole-map`, `1/32`, `1/16`, `1/8`, `1/4` or `1/2`) |
| | Maximum zoom in, a drop-down: how close the same zooms go | None (the game's own scale), 2x, 3x, 4x | 4x | `open-annihilation.max-zoom-in` (`1`, `2`, `3` or `4`) |
| | Escape opens the game menu | Off, On | On on macOS, Off elsewhere and with `--preferences-file` | `open-annihilation.escape-opens-menu` |
| | Select groups without Alt | Off, On | Off | 3.1c's SwitchAlt |
| Common Tweaks | Your files: the player's own folder and buttons that open its Saves, Screenshots and Mods folders | changes no setting | | `open-annihilation.user-folder`, read at start (`src/app/include/oa/app/user_folder.hpp`) |
| | Unit limit | 50 to 1500 per player, steps of 50, or on to a mod's higher maximum | the installation's `totala.ini` UnitLimit, else 250 or a mod's default | `open-annihilation.unit-limit` |
| | Pathfinding cycles | 1× to 8× of 1333 path nodes a tick, or of a mod's budget | 1× | `open-annihilation.path-search-nodes` |
| Language | Language | System default, English, Deutsch, Español, Français, Italiano, 简体中文 | System default with the player's own preferences file; English with `--preferences-file` | `open-annihilation.language` (`system` or a language's tag, as `de`) |
| | Use modern fonts for game text; On, locked, while a language drawn only in them is chosen | Off, On | On with the player's own preferences file; Off with `--preferences-file` | `open-annihilation.modern-fonts` |
| | Text size, of the modern fonts | 50% to 300% of the game fonts' sizes, steps of 10% | 80% | `open-annihilation.text-size` |
| | Font outline | Off, On | On | `open-annihilation.text-outline` |
| | Font shadow | Off, On | On | `open-annihilation.text-shadow` |
| | Game text background | Off, On | Off | `open-annihilation.text-background` |
| | Enable Unicode Multiplayer Chat; On, locked, while a language whose pack asks for it is chosen | Off, On | Off | `open-annihilation.unicode-chat` |
| Graphics | Maximum frame rate | 30 to 120, steps of 5 | 120; 60 on a Raspberry Pi or a light machine with the player's own preferences file; a Steam Deck's screen rate (60 on the LCD model, 90 on the OLED) on a Deck with the player's own preferences file | `open-annihilation.max-fps` |
| | Enhanced anti-aliasing | Off, 2×, 4×, 8×, 16×; a stored level between reads as the one below it, a stored 3 as 2× | Off, a Raspberry Pi and a light machine included | `open-annihilation.anti-aliasing` |
| | Screen size, when OK is pressed | Desktop, then the sizes the display offers from 640×480 (`Dialog::offered_screen_sizes`), in a window shown at the window's own size until moved, as Custom where the display offers no such size; Desktop, 640×480, 800×600, 1024×768 and 1280×1024 where the game does not say | Desktop; 800×600 on a light machine with the player's own preferences file, 640×480 when its desktop is smaller | `open-annihilation.screen-size` (`desktop`, or a size such as `2560x1440`) |
| | Hardware acceleration | Off, Basic, Full | Full with the player's own preferences file on every machine; Off with `--preferences-file` | `open-annihilation.hardware-acceleration` (`off`, `basic` or `full`) |
| | Vertical sync | Off, On | Off | `open-annihilation.vertical-sync` |
| | Menu scaling | Sharp, Whole steps, Unfiltered | Sharp | `open-annihilation.menu-scaling` (`sharp`, `whole-steps` or `unfiltered`) |
| | Native pixel density, from the next start | Off, On; always On where the platform opens every window at native density | Off | `open-annihilation.native-density` |
| | Explosion flash; a mod's profile may draw the flashes lower still (`ui.explosion-flash`), and the lower of the two is drawn | Off, Reduced, Full | Full | `open-annihilation.explosion-flash` (`off`, `reduced` or `full`) |
| Touch, listed only while the game has touch controls | One-finger drag | Automatic (a selection box on a tablet, scrolling on a phone), Box, Scroll | Automatic | `open-annihilation.touch-drag` (`automatic`, `box` or `scroll`) |
| | Hold delay | 250 to 700 ms, steps of 50; a stored delay is held to the range and put on its nearest step | 350 ms | `open-annihilation.touch-hold-delay` |
| | QUEUE and ADD | Stay on, One action | Stay on | `open-annihilation.touch-latches` (`stay-on` or `one-action`) |
| | Haptics | Off, On | On | `open-annihilation.touch-haptics` |
| | Left-handed layout | Off, On | Off | `open-annihilation.touch-left-handed` |
| | Control size, also in Controller | Standard, Large, Larger (the touch layer's points 1, 1.25 or 1.5 times) | Standard; Larger on a Steam Deck with the player's own preferences file | `open-annihilation.touch-control-size` (`standard`, `large` or `larger`) |
| Controller, listed only once a gamepad has sent input in the run | Scheme | Trackpads, Sticks | Trackpads | `open-annihilation.pad-scheme` (`trackpads` or `sticks`) |
| | Right trackpad | Relative, Absolute (the pointer's two ways) | Relative | `open-annihilation.pad-right-trackpad` (`relative` or `absolute`) |
| | Pointer speed | 50% to 300%, steps of 10% | 100% | `open-annihilation.pad-pointer-speed` |
| | Pointer acceleration | Off, Low, High | Low | `open-annihilation.pad-acceleration` (`off`, `low` or `high`) |
| | Trackpad glide | Off, On | Off | `open-annihilation.pad-glide` |
| | Right stick | Zoom (zoom and build pages), Pointer, Nothing | Zoom | `open-annihilation.pad-right-stick` (`zoom`, `pointer` or `nothing`) |
| | Magnetism (stick pointer) | Off, On | On | `open-annihilation.pad-magnetism` |
| | Gyro pointer | Off, While the right pad is touched, While the right stick is touched, Always | Off | `open-annihilation.pad-gyro` (`off`, `right-pad`, `right-stick` or `always`) |
| | Gyro speed | 50% to 400%, steps of 10% | 100% | `open-annihilation.pad-gyro-speed` |
| | Haptics | Off, Light, Strong | Light | `open-annihilation.pad-haptics` (`off`, `light` or `strong`) |
| | Button prompts | Automatic, Steam Deck, Xbox, PlayStation, Nintendo, Off | Automatic | `open-annihilation.pad-prompts` (`automatic`, `steam-deck`, `xbox`, `playstation`, `nintendo` or `off`) |
| | Left-handed | Off, On | Off | `open-annihilation.pad-left-handed` |
| | Control size, Hold delay, QUEUE and ADD | Touch's own rows, shared | | as in Touch |
| Game files, listed only in the main menu's dialog where the platform brings game files in | Include in device backups | Off, On | Off | `open-annihilation.game-files-backed-up` |
| Developer | Enable Developer Mode | Off, On | Off | `open-annihilation.developer-mode` |
| | Show performance statistics | Off, On | Off | `open-annihilation.frame-stats` |
| | The overrides of the profile's standard hacks, in Developer Mode's list | any hack on or off, with its parameters | none | `open-annihilation.hack-overrides.<id>`, the id of the profile the game plays, `ta-3.1c` without a mod, `folder:<path>` for a mod folder without a profile |

Mouse wheel zoom, while on, also leaves a profile's megamap off for the
player: the wheel zooms the battlefield in its place, out to the whole map
with Maximum zoom out at Whole map
([ui.megamap](../../../docs/mods/standard-hacks/ui.megamap.md)). Turning it
on during a match closes an open megamap.

Maximum zoom out and Maximum zoom in hold every zoom of the player's view,
with Mouse wheel zoom off too, since a pinch and the touch zoom buttons
zoom then; a change brings a view past the new limits within them at once.
Neither changes the match: they are never locked.

The Touch section is the same everywhere, every machine and preferences
file alike. A stored One-finger drag or QUEUE and ADD that is none of their
words, like any value that is no whole number for the others, reads as the
default. The touch controls read the settings in effect at every finger and
every frame, so a change in the dialog takes effect at once. One-finger
drag says what a finger dragged on the battlefield does; a hold followed by
a drag draws a selection box whatever it says, and two fingers scroll. The
hold delay is how long a finger stays down before it counts as a hold, which
opens the order menu, gives a build button's right press or shows a
control's help. QUEUE and ADD says whether a tapped QUEUE, ADD or x5 stays on
until it is tapped again or turns off after the next order or selection.

The Controller section is the same everywhere too. A stored choice that is
none of its words reads as the default; Pointer speed and Gyro speed are
held to their ranges and put on their nearest steps, half a step up. The
gamepad reads the settings in effect every frame, so a change takes effect
at once. While the gamepad reaches the game through Steam Input, the
section's first row says so in amber: "Steam Input is on: the trackpads and
back grips reach the game as Steam's mouse and keys. Turn Steam Input off
for Open Annihilation in Steam's controller settings to use them here."

A Steam Deck (`oa/platform/machine.hpp`, `running_steam_deck_model`: Linux
names the maker Valve and the product Jupiter, the LCD model, or Galileo,
the OLED, in `/sys/class/dmi/id`) starts, with the player's own preferences
file, at its screen's rate, 60 or 90 frames a second, with Control size
Larger, so that the touch controls come close to a tablet's in size; nothing
else changes there. Maximum frame rate's hint has a second line on a Deck,
"Steam Deck: starts at the screen's 90 fps." (`Dialog::steam_deck_panel_hz`,
`Inputs::steam_deck_panel_hz`). A stored value always wins, and Restore
defaults puts the Deck's defaults back.

A mod's limits (`oa::data::limits`, handed in through `Inputs::units_per_player`
and `match_path_search_nodes`) change three things: the unit limit a player
starts with, the range a stored or installation limit is clamped to (the
mod's minimum and maximum; 20 to 500 for an installation's limit in 3.1c)
with the slider's highest stop, and the credit the Pathfinding cycles
multiply. Without a mod each is as above.

A light machine (`oa/platform/machine.hpp`, `light_machine`) has one
logical processor, a 32-bit x86 processor without SSE2 (a Pentium III or an
Athlon XP), or less than 512 MiB of physical memory; with the player's own
preferences file it starts at 800×600, 60 frames a second and no enhanced
anti-aliasing, which a machine of the game's own time keeps up with. The
screen size is read before the window opens (`src/app/screen_size.cpp`): a
size other than Desktop opens the window at that size, and full screen
shows it through the display's mode of that size, or draws the game at it
and scales it to the screen. A size chosen applies when OK is pressed
(`src/app/screen_mode.hpp`), never while the slider moves: the dialog holds
the setting as it was until then, and Cancel leaves it. The game sets the
slider's stops once the dialog has opened: Desktop, then the sizes the
display the window is on offers
([display modes](../../platform/display-modes/README.md)), and the size
shown where the display does not offer it, so that the slider shows it
until it is moved. In a window that is the window's own size
(`Dialog::window_screen_size`), which the slider shows until its knob
moves, whatever the setting, as "Custom" where it is not one of the
display's (`Dialog::custom_screen_size`); in full screen it is the size
stored. The preferences keep any size from 640×480 to 8192×8192
(`screen_size_from_text`), so that a size the display offers is kept as
chosen.

The game counts a machine as a Raspberry Pi when Linux names its board's
model, in `/proc/device-tree/model`, starting "Raspberry Pi"
(`oa/platform/machine.hpp`). Its graphics keep up with 60 frames a second at
the game's resolutions, so it starts there; the player can raise the rate and
turn on anti-aliasing in the dialog like anywhere else, and Restore defaults
puts the Pi's defaults back.

Enhanced anti-aliasing's levels keep one key and one strip in every tier,
but mean two things. Off and Basic draw units finer on the processor at
the level's factor, and the row's hint says so, warning of the processor
cost from 8x. While frames are drawn in Full the processor's anti-aliasing
never runs: the graphics card draws the whole battlefield finer and scales
it down, at the level's samples across, 2, 4, 8 or 16, within what the
renderer's texture limit and the memory allow at the window's size, and
the hint says where they allow fewer. The host tells the dialog the factor in use
(`AccelerationStatus::full_supersample`, 0 outside Full), and the hint
then says what the level does in Full at that factor, 1:1 at Off.

Hardware acceleration has three levels (`HardwareAcceleration`). Off is the
game as it always drew: the processor draws and scales every frame. Basic
lets the graphics card scale and compose the frames where it is able to;
the processor still draws every pixel the game decides. Full lets the
card also draw the battlefield. Its default depends only on the preferences
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
files that guard trying it, the card is in use, on another driver where a
record passed over one, or,
on a renderer nothing has looked at yet, it takes effect from the next
start. Where Full was asked for and Basic draws in its place, the first
line says why: Full's trial could not be written, there is too little
memory for Full, Full stopped for this run, Full
failed before on this driver, the card lacks a feature Full needs, or a
shared game or a replay waits for its end; and Full in use says the card
draws the view, smoothed at every zoom, with its anti-aliasing where there
is any. A driver a record
passed over at this start shows whatever the setting. The key reads `off`, `basic` or `full`; a whole number, as the
setting's earlier On and Off switch wrote it, reads as Full above 0 and Off
otherwise, and any other text gives the default.

Vertical sync has each frame wait for the display, so that no frame tears;
while it is in effect the frame rate keeps just below the display's. Off,
the renderer is never asked, and the game paces its frames as without the
setting.

Language says which language the game shows its text in. System default
is the language the operating system's preferred locales choose, which the
drop-down names in itself: "System default (Deutsch)". The other choices
are the languages the game knows and draws, each named in itself, English
first and the others in the order of their names ([the languages
page](../../../docs/languages.md) says what each shows). The key holds
`system` or the language's tag; a file without it, as an earlier version
wrote, reads as System default with the player's own preferences file, and
a value the game does not know reads as the default but stays in the file
until the setting is changed. A named preferences file starts in English,
the game's own default, so that a check plays alike on every machine. The
language changes only what players read, so no game locks it; 3.1c's
command line naming a language (`open-annihilation german`) decides it for
the run, and the row shows "Set on the command line". A new choice shows at
once in what is drawn each frame; screens and panels already open show it
once they open again.

The rest of the Language settings say how game text is drawn. Use modern fonts
for game text draws it in modern fonts, which hold the letters of many
languages, in place of the game's own 8-bit fonts; Text size scales every
size those fonts are drawn at together, from half the game fonts' sizes to
three times them; Font outline gives each letter of that text a dark
outline, Font shadow a dark shadow, and Game text background lays a
shaded box behind each line. `text_style` turns them into the one record
the text drawing reads each frame (`oa::present::TextStyle`,
`oa/present/text_style.hpp`), which the application gives through
`Runtime::text_style`. They change only what is drawn: the simulation, a
saved game and what a shared game sends are the same whatever they hold,
so no game locks them. Modern fonts are On by default with the player's
own preferences file, and Off with `--preferences-file`; the text starts at
80%, a fifth smaller than the game's fonts. Both are deliberate departures
from the game's look.

Text size applies to the modern fonts alone: the game's own fonts have
fixed sizes. While the dialog shows Use modern fonts for game text Off, the
slider is locked, faded with a padlock and "Needs modern fonts"
(`Lock::needs_modern_fonts`, which the dialog sets itself in
`Locks::text_size`), and its second hint line says the game's own fonts
have fixed sizes; turning the modern fonts On lifts the lock at once.
Restore defaults resets the size whatever the switch shows.

A language drawn only in the modern fonts (`TextNeeds::modern_fonts`,
Simplified Chinese) turns Use modern fonts for game text On when it is
chosen, and while it is chosen the switch shows On with a padlock and "Set
by the language" (`Lock::set_by_language`, in `Locks::modern_fonts`).
Enable Unicode Multiplayer Chat is locked the same way (`Locks::unicode_chat`)
while the language chosen is one of `Dialog::unicode_chat_languages`, the
tags whose language packs say `unicode: true`, which the host sets as the
dialog opens; the stored value is the player's own and returns with
another language. A stored size
is clamped to 50% to 300%, and one between the slider's steps is kept as
stored; a file without the key reads as 80%. How each place in the game
draws the size is in the application's README and on the
[ui.text-rendering](../../../docs/mods/standard-hacks/ui.text-rendering.md)
page.

Mods chooses the mod folder the game plays, layered over the game folder
as `--mod-dir` would play it. It lists one row a mod, in a view of its own
that scrolls by the wheel, its scroll bar, Page Up, Page Down, Home and End
while the rows are taller than it, with OPEN MODS FOLDER and the two lines
naming the folders listed fixed under it. The mod the game plays comes
first, marked PLAYING (`ModOffer::playing`); then No Mod, when another mod
plays; then every mod folder the host offers (`ModOffer::names`,
`ModOffer::folders`), by its title, whatever the case of its letters
(`mod_rows`). A row shows the mod's badge, its title, its version at the
right and its description under them (`ModDetails`, read from the folder's
`oamod.yaml` and the `oamod.png` beside it): a dashed square stands for a
mod without a badge, and No Mod shows the OA mark, the version "3.1c" and
"The game's own rules, as 3.1c plays them." A folder without an
`oamod.yaml` shows its folder's name, "N/A" and "No oamod.yaml present" in
amber. A title, a version or a description too long for its place is cut
with "...".

A press on a row other than the one played, or Space on it, asks the
Switch Mod question over the dialog: the mod's badge, title and version,
"Switch to <title> now? The game reloads its data for the new mod and
returns to the main menu. Your other settings are kept.", and, for a
folder without an `oamod.yaml`, "This folder has no oamod.yaml, so the
game's own rules apply." in amber, each broken into the lines that fit,
with CANCEL and SWITCH. Until it is answered the question takes every
pointer event and key: Y, or Enter or Space on a marked SWITCH, answers
SWITCH; N, Escape, or Enter or Space on a marked CANCEL, answers CANCEL;
Left marks CANCEL, Right SWITCH, and Tab and Shift+Tab move the mark,
SWITCH at first; a press and release on a button answers it. Y and N do
nothing while no question shows. CANCEL leaves the settings as they were;
SWITCH sets the Mod to the folder (`EngineSettings::mod_folder`, cleared
for No Mod) and asks the host to keep the settings, close the dialog and
reload the game for that mod on the main menu (`DialogAction::switch_mod`).
OPEN MODS FOLDER asks the host to open the player's own Mods folder in the
system's file manager (`DialogAction::open_folder` with
`FolderButton::mods`), as Your files' MODS button does; a folder that
cannot be opened is said, in amber, in place of the second line under it
(`set_folder_notice`).

During a game (`Locks::mod`, `Lock::in_game`) Mods is locked: "Locked
during a game. Choose the mod from the main menu." shows over the list
beside a padlock, the rows other than the one played are dimmed, no row
takes a press or the focus, and OPEN MODS FOLDER is disabled. While
`--mod-dir` or `--base-game` decides the run's mod
(`GameState::mod_from_command_line`, `Lock::command_line`), it is locked
the same way, with "The command line chose this run's mod." A stored mod
folder the game folder does not offer reads as the stored one
(`EngineSettings::picked_mod_folder`); a file without either key, as
earlier versions wrote, reads No Mod. The picked folder is remembered only
while it is the Mod (`remembered_picked_folder`): a picked folder's key
naming any other folder reads as none, and `write_settings` erases it, so
that a switch to another mod or No Mod forgets the folder. Developer Mode
keeps its overrides under the id of the profile the game plays, and No Mod
the plain 3.1c baseline's (`ta-3.1c`), which no mod's profile can take. A
mod folder without a profile keeps its own under `folder:` and its path
(`folder_overrides_id` in the application), which no profile's kebab-case
id can be either.

Developer Mode is described [below](#developer-mode).

With every default the game plays as it does without the settings. Pathfinding
cycles, Unit limit and Mod are locked during a game; a shared game or a replay
always plays at 1× pathfinding and the host's unit limit. Vertical sync is
locked during a shared game or a replay, its value set before the game kept
in effect; Hardware acceleration never is, so that it can always be set to
Off, and set to Basic or Full there it takes effect from the next game.

## The dialog

`engine_settings/dialog.hpp` holds the dialog (`Dialog`), what pointer, wheel
and key events do to it (`dialog_pointer_down` and the others, `dialog_wheel`,
`dialog_key`), where its parts lie (`dialog_layout`), and how it and the OA
button are drawn (`draw_dialog`, `draw_oa_button`, and `draw_oa_mark` for
the mark alone on other screens) without the game's art, in
the game's own fonts (`load_dialog_fonts`): its button font for labels,
values, the section list and the title, and its smaller label font for the
section heading, hints, locks, captions and the version, each readied for
text in one colour. `src/geometry.hpp` places every part, so a control is
pressed where it is drawn. `Dialog::section_hooks` (`SectionHooks`) lets the
dialog's tests and the game's checks show rows and locks of their own in
place of a section's, on Developer in place of its list and the list's
footer too; a host never sets it.

The host hands both drawing functions the Open Annihilation icon as an
RGBA picture: the game's window icon without its clear margin. They scale
it with `draw_picture` to its place at the surface's own resolution, so a
dialog drawn twice as large shows the icon twice as sharp. The OA button is
a bevelled square with the icon 3 source pixels inside its edge: lit with
a green ring inside the bevel under the pointer, and, while held, its bevel
sunk and the icon a pixel right and down. Without the icon, an empty
picture, the header and the button draw the OA mark instead: green "OA"
letters in a green outlined square. `draw_oa_mark` draws the same icon
filling a square, or the mark the OA button shows at rest, so that other
screens, such as the Game files screen, show the OA mark as the dialog
does.

Where a game font has no ellipsis, as neither of the dialog's has, the
dialog draws and measures each ellipsis in a text as three full stops in
the game font, so that a caption such as MANAGE… keeps the game font's
look and fits its button like every other caption; the mark between a
location's folders (›) is drawn as a greater-than sign the same way. The
texts themselves, and their translations, keep their characters; every
other character the game font lacks is drawn in the modern fonts.

It is 480 by 324 source pixels, a dark gunmetal panel with a one-pixel raised
edge and hairline rules, and one green accent for what is selected:

- the header: the Open Annihilation icon, 20 by 20 source pixels, "OPEN
  ANNIHILATION SETTINGS" and the version, with "Shared game - still
  running" in amber while a shared game keeps running;
- the sections down the left (Mods, Controls, Common Tweaks, Language,
  Graphics, Touch while the game has touch controls, Controller once a
  gamepad has sent input, and Game files where the host lists it),
  Developer at the foot after a line, the open one marked;
- the open section's heading and rows: a label, a hint of one or two lines,
  and an Off/On switch, a level strip (Off, 2x, 4x, 8x, 16x for Enhanced
  anti-aliasing; Off, Basic, Full for Hardware acceleration; Automatic,
  Box, Scroll for One-finger drag; Stay on, One action for QUEUE and ADD;
  Standard, Large, Larger for Control size; and the Controller section's
  Scheme, Right trackpad, Pointer acceleration, Right stick and Haptics),
  a slider with stops and its value under the hint, or a drop-down under
  the hint (Language, Gyro pointer, whose field is wider for its longest
  choice, and Button prompts): a field showing the choice, with an arrow at
  its right; a text row shows only text under its label (Where the files
  are, and Controller's Steam Input notice of up to four lines); Mods shows
  its list of mods in their place;
- Restore defaults, Cancel and OK along the bottom.

A section holds any number of rows. They lie in a view under the section's
heading, from the first row's line at pixel row 54 down to the pixel row
above the footer's line, 236 pixels high; the header, the list, the heading
and the footer never move. A section whose rows, with 8 clear pixels under
the last row's line, are taller than the view scrolls by whole source
pixels, and shows a scroll bar in the margin right of its rows: a well like
a switch's, its thumb as tall as the view's share of the section and never
under 16 pixels. Graphics, with eight rows, is taller than its view by 257
pixels, Language, with the Language drop-down, four switches and the
Text size slider, by 129, Touch, with its three strips, the Hold delay
slider and two switches, by 121, and Controls, with its three switches and
the zoom's two drop-downs, by 84; Common Tweaks fits, its Your files and
two sliders in 210 pixels, and draws as if there were no scrolling,
with no bar. While the dialog's words are drawn in the modern fonts, as in
Simplified Chinese, whose ideographs stand as tall as a hint line, the
lines of a hint or a notice lie three pixels further apart
(`tall_hint_line_gap`), clear of each other: each row with such lines is
taller by three pixels a line after the first, a section that scrolls
scrolls further than the pixels given above, and its scroll ends only
where the view's top edge cuts no hint line (`open_rows`). Each section
keeps its offset while the dialog is open, and every section starts at
its top each time it opens. A row the view cuts shows the part inside it
and takes a press only there;
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

Controls are numbered: the sections' entries 0 to 8, each its place in the
list with Touch and Controller whether or not they are listed (Touch 5,
Controller 6, Developer 7, so a dialog without Touch has no control 5 and
one without Controller no control 6), and Game files 8 after Developer, so
that Touch, Controller and Developer keep their numbers in every dialog (a
mod's options' 0 to 4); Restore defaults 9, Cancel 10, OK 11, the scroll bar
12, and the open section's rows from 13, with no upper end. The list draws
its entries by their place in the list it shows, and the focus walks that
list, so the dialog without Touch, Controller or Game files draws and
answers exactly as it did before they were added.

The Touch section is listed only while the game has touch controls:
`Dialog::touch`, which the host gives `open_dialog` and keeps with
`set_touch_controls` each frame, so that Touch shows from the moment a
finger turns the touch controls on (`dialog_pages(kind, touch)`). A dialog
asked to open on Touch without them opens on its first section, Mods.

The Controller section is listed only once a gamepad has sent input in the
run: `Dialog::controller`, which the host gives `open_dialog` and keeps with
`set_controller_section` each frame, beside `Dialog::steam_input`, so that
Controller shows from the moment a gamepad is used and its Steam Input
notice while it applies (`dialog_pages(kind, touch, game_files,
controller)`). It stands after Touch and before Game files. A dialog asked
to open on Controller without it opens on its first section, Mods; one that
stops listing it while it shows it shows Mods, and the focus leaves its
controls. The notice coming or going while Controller shows moves each row
by one, and the focus with its row. Its rows: Scheme, Right trackpad,
Pointer speed, Pointer acceleration, Trackpad glide, Right stick, Magnetism
(stick pointer), Gyro pointer, Gyro speed, Haptics, Button prompts,
Left-handed, then Touch's own Control size, Hold delay and QUEUE and ADD.

The Game files section is listed, between Controller (or Touch, or
Graphics) and Developer, only where the host says so (`Dialog::game_files`, given to
`open_dialog`): the main menu's dialog of a game whose platform brings game
files in. A dialog asked to open on Game files without it opens on its
first section, Mods. Its three rows:

- Installed: what is installed and, under it, the sizes line, both the
  host's texts (`Dialog::game_files_summary`, `game_files_sizes`), with
  MANAGE… at the label line's right, a green button like OK with its
  caption, drawn "MANAGE...", centred inside it. MANAGE… is a button: a click on it, or
  Space while it has the focus, returns `DialogAction::manage_game_files`,
  and the host opens the Game files screen with the dialog left open under
  it. Left and Right do nothing to it.
- Include in device backups: a switch, Off by default, whose hint names
  the device ("After restoring this tablet from a backup, add the game
  files again.", from `Dialog::game_files_device`, "device" without one).
  It is put in effect as it changes; Restore defaults turns it Off, and a
  dialog that does not list Game files keeps it through Restore defaults.
- Where the files are: the host's text (`Dialog::game_files_location`) in
  up to two lines of at most 50 characters, broken between words, after
  the mark between a location's folders where one stands in a line's
  second half. It takes no press and no focus.

The host's texts keep to their lines' columns as they are drawn.

`open_language_text_dialog` opens a dialog of the third kind,
`DialogKind::language_text`: Language alone, its entry at the top
of the list, with Restore defaults, which restores that section's
settings alone (each locked one kept), Cancel and OK. The Game files
screen opens it before the game's files are installed: given
`DialogFonts` that hold no glyphs, every text of the dialog is drawn and
measured in the modern fonts (the game-text hooks the host installs), the
regular font's in the message face and the small font's in the status
face, each at the game fonts' size and centred on its box as a game font's
capitals are. A character a game font lacks is drawn in the modern fonts
at the game fonts' size.

A finger's press (`dialog_finger_down`) that lands on no control takes the
nearest control within a reach the host gives, in the dialog's own pixels,
pressed at its pixel nearest the finger; while a drop-down's list is open,
the nearest of its items. The press's moves and its release are moved as
far as the press was (`Dialog::finger_shift_x`, `finger_shift_y`), so that
a release where the finger landed acts on the control it took: a switch's
nearer half, a strip's nearer level, a slider dragged along its track. A
mouse press is never moved. The game's hosts give it the touch controls'
pick distance, 22 points, as the dialog's pixels on the screen shown.

The game fonts have no "×" or "·", so the dialog writes "x" and "-". Its
texts are UTF-8: each character the game font has a glyph for, a
language's "ç" or "ñ" among them, is drawn with it, and any other in the
modern fonts. The dialog's own words pass through the interface catalogue
(`oa/data/languages/interface_text.hpp`), which shows them in the language
chosen once a translation is given for it; none ships yet, so they show in
English.

A locked setting is faded, takes no press and no keyboard focus, and shows a
padlock with "Locked during a game", "Set by the host", "Set on the command
line" (the frame rate under `--max-fps`, Hardware acceleration under
`--hardware-acceleration`, in any of its forms, or
`--no-hardware-acceleration`, Native pixel density under
`--native-density`), "Not available here" (Hardware
acceleration when nothing in the game could help the run, and Vertical
sync on SDL's software renderer or where each change would reset the
graphics device), "Needs modern fonts" (Text size while the modern fonts
are Off) or "Always on here" (Native pixel density where the platform
opens every window at native density). A locked slider shows the padlock at the right of its
label line. A locked switch keeps its switch, faded, with the padlock left
of it, so that its value still shows: Vertical sync's. A locked row whose
hint lines are its status, Hardware acceleration's strip, shows the padlock
where its control was, and fades only its label line, so that the status
keeps its strength.

Changes show at once; OK keeps them, Cancel puts back what the dialog opened
with, Restore defaults resets every setting that is not locked. A click on a
switch's half sets it, and a click on a strip's level chooses it; a press on
a slider moves its knob to the nearest stop and drags it. A click on a
drop-down's field opens its list under the field, or over it where the list
would reach below the footer's line: an item a line, as many as eight, the
chosen one marked as the section list marks its open section and the one
under the pointer lit. A click on an item chooses it and closes the list; a
press anywhere else, the field included, closes the list and does nothing
more. A longer list scrolls with the wheel, a notch an item, and shows a
thumb at its right edge; the wheel over an open list never scrolls the
section. Every switch reads
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
| Left, Right | a switch Off or On, a slider, a level strip or a drop-down one step; along the footer's buttons |
| Space | flips a switch, presses a button, opens a section or a drop-down's list |
| Page Down, Page Up | scroll the section 200 pixels down or up |
| End, Home | scroll the section to its end or its top |

While a drop-down's list is open, the keys work the list: Up and Down mark
the item above or below, Page Up and Page Down a list's height of items
away, Home and End the first and the last; Enter and Space choose the
marked item and close the list; Escape closes it unchanged, leaving the
dialog open; Tab and Shift+Tab close it and move the focus.

The focus shows once a key moves it; the first key to the dialog only shows
it. Page Up, Page Down, Home and End scroll whatever has the focus, and
never show or move it. A key that moves the focus onto a row, or acts on a
focused row, first scrolls the least that shows the row whole; a key that
moves it to a button or a section's entry does not scroll.

## Developer Mode

Developer Mode lets the player change the standard hacks of the profile
the game plays: turn any hack on or off and set its parameters, without
touching the mod's `oamod.yaml`. With no mod it changes the plain 3.1c
baseline, so it can turn hacks on over 3.1c.

It lies in the Developer section, which holds, top to bottom, in the
dialog's own fonts, colours, switches and sliders:

- at its top, **Enable Developer Mode**, an Off/On switch, Off by default,
  with its hint, and under it **Show performance statistics**, which
  Developer Mode leaves alone: two rows that never scroll, with half the
  clear pixels of a section's rows round them, so that the list has room;
- under them, the list of every standard hack of the registry
  (`oa::data::mod_profile::standard_hacks`), in a view of its own that
  scrolls as a long section does, with its scroll bar in the margin, and
  two levels of parts that open and close, every one closed at first: an
  area of the registry (`developer_areas`; its title, such as "Interface",
  and how many of its hacks are on), and under it each hack: its title,
  such as "Deterministic Wind", and its Off/On switch, and open, its id
  (`economy.deterministic-wind`) in a quieter colour, the registry's
  summary, "Applies at next match" in amber for a rule (sim-scope) hack,
  and a control for each parameter. The areas, and the hacks within each,
  follow their titles alphabetically as the language shown writes them;
  the titles are the registry's, in English, and pass through the
  interface catalogue like the dialog's other words;
- at its foot, which never scrolls, **Show Active Only (X/Y)**, a switch
  that shows only the hacks that are on and the areas that hold one, X the
  hacks on as the section shows them and Y every standard hack (89), and
  **Restore profile values**, which clears every override.

A parameter's control follows its type: a switch for a boolean; a slider
for a whole number, from its least to its most by its step, the hack's
constraints with the other parameters (such as `min <= max`) narrowing it,
and for an int-or-none with a first stop for none; a slider of the
registry's values for an enumeration; for a decimal, a slider whose step
is the power of ten that gives it from 1,000 to 10,000 stops across its
range, with the registry's and the profile's own values among its stops;
a slider of the values the registry, its
presets and the profile give for a string; a switch for each value of a
set; and for a list, a slider for each item, an ascending list's items
kept in order, a list of distinct words swapping a word in with the item
that held it, and a slider of its length when the registry lets it vary.
Each value shows with its unit, and a hack that is off shows "Off: it plays
as 3.1c." in place of its parameters. A summary is broken into lines of at
most 45 characters, which fit the section in the game's small font; its
degree, middle dot, multiplication and plus-minus signs, which the game's
fonts lack, are written as " degrees", "*", "x" and "+/-".

Off, the list is static: it shows the profile as it ships, and the
overrides the settings keep are not laid over it. Its areas and hacks
still open and close and its filter still works, so its values can be
read, but no switch or slider of a hack takes a change, each fading as a
locked setting does, and Restore profile values takes no press; Show
performance statistics takes changes whatever Developer Mode is. On, the
list's controls take changes and the overrides apply. Every change goes
into `EngineSettings::hack_overrides`: one override for each hack whose
state differs from the profile's, holding the parameters set to values
the hack does not already have; a hack set back to the profile's state
drops its override. Turning a hack off drops the
parameters it set. Its changes show at once and Cancel puts them back, as
the other settings' do; OK keeps them. An override that does not fit its
hack as the resolver checks one (a parameter unknown or out of its bounds,
a constraint broken) is shown as the profile's state. Restore defaults
turns Developer Mode Off and keeps the overrides; only Restore profile
values clears them. The host keeps which areas and hacks are open and the
filter while the game runs (`DeveloperList`).

Developer's controls are numbered from the open section's first row's:
Enable Developer Mode 11, Show performance statistics 12, Show Active Only
13, Restore profile values 14, and the list's rows that take input from
15, in the list's order. Every header takes the focus and a press, which
opens or closes it; a press on a hack's switch sets it. Space opens or
closes an area or a hack and flips a switch; Left and Right close and open
an area, turn a hack or a switch Off and On, and move a slider a stop.
Page Up, Page Down, Home and End scroll the list, as they scroll a long
section, the wheel over the dialog too; the two rows over it and its
footer stay where they are. The focus moves through Enable Developer Mode,
Show performance statistics, the list's rows, Show Active Only and, while
Developer Mode is on, Restore profile values, then the footer's buttons
and the sections.

## Mod options

A mod whose profile turns ui.options-dialog on has its own options, which
the same dialog shows as a second kind (`DialogKind::mod_options`,
`open_mod_options_dialog`): its list holds five sections of its own and no
divider, and only `EngineSettings::mod_options` (`ModOptions`) change.

| Section | Setting | Range |
|---|---|---|
| Keys | Snap override key, Autoclick key, Rotate build key | one of `option_keys` |
| Patrolling | Hold position, Maneuver, Roam | Reclaim only, Both, Assist only |
| Guarding | Hold position, Maneuver, Roam | Stay, Normal, Scatter |
| Build tools | Optimize DT rows, Full rings | Off, On |
| | Mex snap radius | 0 to the mod's most cells (`ModOptions::mex_snap_most`, at most `most_snap_radius`) |
| Snap & chat | Wreck snap radius | 0 to the mod's most cells |
| | Accessible chat | Off, On |
| | Resource bar background | None, Text, Solid |

A snap radius the mod gives no room shows "Set by the mod"
(`Lock::set_by_mod`, `Locks::mex_snap` and `wreck_snap`). Restore defaults
there resets the mod options alone, and the engine's settings keep their
values. The application keeps the mod options with the player's other view
settings; this library neither reads nor writes them.

## ROLL BACK

A row of Mods whose folder keeps an earlier version of its mod
(`ModDetails::roll_back_from` and `roll_back_to`, which the host fills for
the player's own Mods folder) shows a ROLL BACK button at the right of its
description line, which is cut shorter for it. Its control is OPEN MODS
FOLDER's and one more for each row before it and itself; the focus walks a
row, then its ROLL BACK. A press, or Space, asks the Roll Back Mod question
in the Switch Mod question's box (`Dialog::mod_question`): its heading, the
row's badge and title, the versions "from to to" and what it does, CANCEL
and ROLL BACK, ROLL BACK marked. ROLL BACK names the folder
(`Dialog::roll_back_folder`) and asks the host to roll it back
(`DialogAction::roll_back_mod`); the dialog stays open. A locked page draws
it dimmed and inert.

## Your files and the notice

Common Tweaks' first row, Your files (`Setting::user_folder`, `is_buttons`),
changes no setting. Its first hint line shows the player's own folder
(`Dialog::user_folder`, which the host sets as the dialog opens) by its
tail that fits (`shown_hint_text`, `path_tail`); its second says "Saved
games, screenshots, films and mods.", or in amber why the last folder asked
for could not be shown (`set_folder_notice`). Three buttons, SAVES,
SCREENSHOTS and MODS, stand right-aligned on its label line, drawn as
Cancel is. A click released on the button it pressed, or Space on the
button the keys mark, asks the host to show that folder
(`DialogAction::open_folder`, `Dialog::folder_to_open`) and leaves the
dialog open; Left and Right move the mark along the buttons.

`notice.hpp` is a notice of Open Annihilation's own in the dialog's look,
which the main menu shows over itself: a raised panel 400 pixels wide, a
header with the icon (or the OA mark) and a title, its text in white in
the small font, wrapped between words (`wrap_text`), a folder's path in the
regular font broken after its separators (`wrap_path`), why its folder
could not be opened in amber, and a footer with a button that opens the
folder, drawn as Cancel is, and OK; it grows with its text from 150 to 440
pixels (`notice_height`, `notice_layout`, `draw_notice`). A click released
on the button it pressed presses it; a finger's press on neither button
takes the nearer one within reach, and its release where the finger landed
presses it (`notice_finger_down`); Enter and Escape close it; Space
presses the marked button, OK at first; Left, Right, Up, Down, Tab and
Shift+Tab move the mark (`notice_pointer_*`, `notice_key`).

`prompt.hpp` is a prompt in the notice's look that asks with one to three
buttons of its own captions, as a mod package's install does: its text,
given finished in the language shown and drawn as given, is placed as a
notice's (`place_text` in `notice_geometry.hpp`), and an optional progress
bar lies under it. Its buttons stand right-aligned in the footer, five
columns apart, each as wide as its caption at the estimated character
width and 16 more, at least 52, so that a press lands where a button is
drawn whatever the fonts; an accent button is drawn as OK, the others as
Cancel (`prompt_height`, `prompt_layout`, `prompt_fits`, `draw_prompt`).
Enter and Space answer the marked button, Escape and N its cancel button, Y
its primary one; Left, Up and Shift+Tab mark the one before, Right, Down
and Tab the one after; a finger's press takes the nearest button within
reach (`prompt_pointer_*`, `prompt_finger_down`, `prompt_key`).

## Tests

The dialog's tests cover ROLL BACK: shown only on a row whose folder keeps
a version, its control and focus, its question by pointer and keys, and
inert while locked. `ui-engine-settings-prompt` lays out prompts of one, two and three buttons,
their keys, a pointer's press and release and a finger's within reach, the
progress bar's part and a text found cut. `ui-engine-settings` covers the defaults, a Raspberry Pi's and a light
machine's included, the keys read and written, Hardware acceleration's
words and the numbers its switch once wrote, words that are no number, and
the locks of a game, the flags and the renderer, and the Language
switches and text size: their defaults, 80% among them, a file without
them, a file with CR LF line ends, the mod and the picked folder stored as
their paths, the picked folder kept while it is the mod and through
Restore defaults, forgotten when another mod or No Mod is chosen and when
its key names a folder that is not the mod, a mod folder not offered read
as the picked one, and files without the keys; the round trip, the
size's range and
clamping, Restore defaults and the text style they make; the language:
its defaults, a file without it, the values read, a tag the game does not
know kept in the file, the round trip, Restore defaults and the command
line's lock; Developer Mode's switch and overrides: a file without
them, the overrides kept under each profile's id and read back, erased
when none are left, and kept by Restore defaults; and the Touch section's
keys, its defaults on every machine, its words read and others dropped, the
hold delay held to its range and put on its nearest step, and its round
trip and Restore defaults; the backups switch: Off by default on every
machine, read as every switch, written alone and erased by Restore
defaults; a Steam Deck's defaults at 60 and 90 frames a second with
Control size Larger, nothing else moved, a named file without them, a
screen's rate held to the range and put on a step, a stored value winning
and Restore defaults bringing them back; and Control size and the
Controller section: their keys, defaults on every machine, words read and
others dropped, the speeds held to their ranges and steps, and the round
trip and Restore defaults;
`ui-engine-settings-dialog` the dialog's layout (every part inside the
panel and none overlapping), its sections, switches and their one table,
slider stops, both level strips, keys, footer buttons, locks and the faces
it draws; the OA button's icon at its size in both buttons, at rest, under
the pointer, held and twice as large, and the mark without it; with the
installed game, the header's icon in its place and the mark without it;
the sections' order and names, Developer at the foot under the divider,
and Common Tweaks holding Your files over the unit limit and pathfinding;
Language's drop-down, four switches and Text size slider,
their place in the list, their texts, clicks, drags and keys on them, the
text style they set, the slider's 26 stops from 50% to 300%, and its lock
while the modern fonts are Off, lifted as they turn On; the Language
drop-down's choices, each language named in itself, its field and list,
the pointer and the keys on them, a press off the list, the command line's
lock, and lists that scroll or open over their field; Mods: the mod
played first, then No Mod while another plays, then the others by title,
each row's badge, dashed square or OA mark, title, version and
description, a folder without an `oamod.yaml` shown by its name with
"N/A" and "No oamod.yaml present", long texts cut with "...", the list
scrolled by the wheel, the scroll bar, Page Up, Page Down, Home and End
under twenty mods while OPEN MODS FOLDER and its lines stay put, the
Switch Mod question asked by a press or Space, its parts over the dialog,
CANCEL leaving the settings, SWITCH choosing the mod or clearing it for No
Mod, the note for a folder without an `oamod.yaml`, the keys Y, N, Enter,
Escape, Left, Right, Tab and Shift+Tab on it, the rest of the dialog taking
nothing meanwhile, the locks during a game and by the command line with
their notes, inert rows and disabled button, and OPEN MODS FOLDER asking
for the Mods folder;
the Graphics section's eight rows, their places at every offset
and under every lock, the focus scrolling them into view, both forms of a
locked row, every status of Hardware acceleration, Full's included, and
the requests to try the graphics card afresh; on sections of the test's own taller than the
view (`SectionHooks`), its scrolling: the view and its limit, the wheel, the
scroll bar, the scroll keys, the focus brought into view, rows the view
cuts and the control numbers; and the mod options' sections, stops,
Restore defaults and locks; Developer: its two rows and their places over
the list, Show performance statistics taking changes whatever Developer
Mode is, the list's areas closed at first and opening, the list static
while Off, a hack turned on and off, a switch, a slider and a set's value
set through the pointer and the keys, the overrides kept and dropped as
the profile's own values come back, Restore profile values, Show Active
Only and its count, the list's scrolling and focus, and its summaries'
lines; Controller: listed only once a gamepad has sent input, after
Graphics and Touch and before Game files, each entry's number kept, the
focus walking it, `set_controller_section` listing and unlisting it, its
fifteen rows with their strips, sliders (26 and 36 stops), switches and
drop-downs by pointer and keys, Control size, Hold delay and QUEUE and ADD
as Touch's own settings, Restore defaults (a Steam Deck's frame rate and
Control size Larger included) and Cancel, the Steam Input notice's exact
text in amber taking no press or focus, the focus kept on its row as the
notice comes and goes, the dialog with Controller drawn alike but for the
list from its entry down and without it as ever, and Maximum frame rate's
Steam Deck line; Touch: listed only with touch controls, at its place in the list
with the line and Developer under it, the list without it where it always
was, each entry's number the same with and without it, the focus walking
it, its rows, their texts, strips, slider stops and switches, Restore
defaults and Cancel, the dialog with it drawn alike but for the list from
its entry down, and a finger's press taking the nearest control within
reach (15 points off a switch takes it, 30 points off nothing, a mouse
press 15 points off nothing) at several scales, a slider dragged from under
its track and an open list's nearest item; Game files: listed only with
its flag, between Touch or Graphics and Developer, the line and
Developer under it, its entry numbered 8 with Touch 5, Controller 6 and
Developer 7, the
focus walking it, its three rows and their texts, MANAGE… returning
`manage_game_files` to a click, to Space and to a finger beside it, the
switch by either half and the keys, Restore defaults and Cancel, where the
files are taking no press and no focus, and the lines a location breaks
into; the Language dialog: its one entry at the top, its focus,
Restore defaults restoring its section alone and keeping a locked
language, and with fonts that hold no glyphs every text drawn and measured
in scripted modern fonts; Your files: its place, its buttons under the
pointer and the keys, its amber line and a long folder's tail; the notice:
its text wrapped and its path broken at its separators, its parts kept
apart, its buttons, keys and colours; and
`ui-engine-settings-dialog-data` its fonts, and every text
fitting its place in them, a scrolled section's at every offset included,
Touch's with each way of its strips, Controller's with each way of its
strips and drop-downs, with and without the notice and with its lists
open, Graphics on a Steam Deck, every status line in the 309 columns
of a hint, the Game files section with a host's usual texts and the
Language dialog, Mods under each lock and with a notice, the Switch Mod
question for every mod, a long title's among them, and its note for a
folder without an `oamod.yaml` whole, Your files with a long folder and
each reason a folder could not be shown, the notice with a long path and a
failure, and every row of
Developer Mode's list with every area and hack open and every hack on,
over the installed game. `native-engine-settings` sends the wheel and the
scroll keys through the main menu's and the match's dialog, turns Vertical
sync On in the dialog and reads it back from the renderer, and sets
Hardware acceleration to Full and to Basic in a shared game, where each
waits for the game's end. It also finds Text size locked with the modern
fonts Off, turns them On and raises the size a stop, which the text style
reads at once and OK saves, and chooses Deutsch from the Language
drop-down, which puts German in effect at once, and steps back to English.
With touch controls on it finds Touch listed between Graphics and
Developer with its rows at their defaults, and a finger's press beside
Haptics' switch taking it where a mouse press there does nothing; in the
match, it checks that on a phone the dialog and the OA button fit the
safe area.
On Mods it finds No Mod played first and a folder stored by an earlier
version's Pick Folder... listed by its oamod.yaml's name; a click on that
row asks the Switch Mod question, which Escape and CANCEL put away with the
Mod unchanged; --base-game locks the page with its note; SWITCH from that
folder played to No Mod erases its keys, after which the next start no
longer lists it, and a start erases a picked folder's key that names a
folder other than the mod stored; and a start that plays a folder without
a profile keeps the overrides under the folder's own id. Over a match it
finds Mods locked with its note, the mod played first, and neither a press
nor a key asking to switch. `native-mod-switch` switches the mod ten times
through the question, each a soft restart.
