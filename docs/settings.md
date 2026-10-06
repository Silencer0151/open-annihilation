# Settings

Open Annihilation adds settings of its own to the game: which mod to play,
a few controls, the unit limit, the language and fonts of the game's text,
graphics, touch and gamepad controls, and Developer Mode. They live in one
dialog, apart from the game's own options, which stay in 3.1c's Options
menu. This page shows each section of the dialog and says what every
setting does, its default, when it takes effect and when you might change
it.

The pictures show each section at its defaults on a Mac.

## Contents

- [Opening the settings](#opening-the-settings)
- [OK, Cancel and Restore defaults](#ok-cancel-and-restore-defaults)
- [Mods](#mods)
- [Controls](#controls)
- [Common Tweaks](#common-tweaks)
- [Language](#language)
- [Graphics](#graphics)
- [Touch](#touch)
- [Controller](#controller)
- [Game files](#game-files)
- [Developer](#developer)
- [For one run: the command line](#for-one-run-the-command-line)
- [Where the settings are kept](#where-the-settings-are-kept)

## Opening the settings

The settings open from:

- the **OA** button at the bottom right of the main menu;
- the **OA** button under **Resume** in the in-game menu (F2);
- **Cmd+,** on macOS, or **Settings…** in the application menu;
- **Ctrl+,** on Windows and Linux.

A game played alone stays paused while the settings are open. A
multiplayer game keeps running, and the dialog's header says so in amber:
"Shared game - still running".

The column on the left lists the sections: Mods, Controls, Common Tweaks,
Language and Graphics, then Touch while the game has touch controls,
Controller once a gamepad has been used, and Game files in the main menu
on an iPhone or an iPad, and Developer at its foot under a divider. A
section taller than the dialog, such as Graphics, scrolls with the mouse
wheel, its scroll bar, Page Up, Page Down, Home and End.

A mod's own options are not here: where a mod's profile turns them on,
Ctrl+F2 in a match opens them in a dialog of their own
([ui.options-dialog](mods/standard-hacks/ui.options-dialog.md)).

## OK, Cancel and Restore defaults

Every change shows at once, so you can see what a setting does before you
keep it. A row's hint says when a setting takes effect later instead:
"Applies from the next game" or "Applies from the next start".

- **OK** (Enter) closes the dialog and keeps the changes.
- **Cancel** (Escape) closes it and puts back every setting as it was when
  the dialog opened.
- **Restore defaults** sets every setting back to its default. It leaves
  alone the mod you play, which changes only through the Mods section's
  question, the changes Developer Mode keeps for the standard hacks, and
  any setting that is locked. It also has the graphics card tried afresh (see
  [Hardware acceleration](#graphics)). Cancel still puts everything back.

The keyboard works the whole dialog:

| Key | Does |
|---|---|
| Enter | OK |
| Escape | Cancel |
| Tab, Down | moves to the next control: the section's rows, then Restore defaults, Cancel and OK, then the sections |
| Shift+Tab, Up | moves to the previous control |
| Left, Right | turns a switch Off or On, or moves a slider, a row of choices or a drop-down one step |
| Space | flips a switch, presses a button, opens a section or a drop-down's list |
| Page Up, Page Down | scrolls the section |
| Home, End | scrolls to the section's top or end |

On a touch screen a finger takes the nearest control within reach, so a
small switch is easy to hit.

A setting that cannot change now is faded, with a padlock and the reason:

- **Locked during a game**: it can change only between games.
- **Set by the host**: a multiplayer game or a replay plays the host's
  value.
- **Set on the command line**: an option the game was started with decides
  it for this run ([below](#for-one-run-the-command-line)).
- **Not available here**: this computer or its graphics cannot offer it.
- **Needs modern fonts**: Text size, while the modern fonts are off.
- **Always on here**: Native pixel density on an iPhone or an iPad.

## Mods

![The Mods section: No Mod, marked PLAYING, with the version 3.1c and the line "The game's own rules, as 3.1c plays them.", and below it a mod titled Example Mod, version 1.0; under the list the OPEN MODS FOLDER button and a line naming the folders listed.](images/settings/mods.png)

Mods chooses the mod the game plays, or **No Mod** for 3.1c's own rules,
which is the default. It is a list rather than a column of settings: a row
for each mod, with its badge, its title, its version at the right and a
line that describes it. A mod without a badge shows an empty dashed square.

- **The list** starts with the mod being played, marked PLAYING, then No
  Mod when a mod is playing, then every other mod by title. It holds the
  mods in the game folder's `mods` folder and in your own Mods folder,
  `Documents/Open Annihilation/Mods`. A folder without an `oamod.yaml`
  shows its folder's name, "N/A" and "No oamod.yaml present" in amber; it
  plays with the game's own rules.
- **Choosing a mod.** A click on another row, or Space on it, asks
  **Switch Mod**: "Switch to *title* now? The game reloads its data for
  the new mod and returns to the main menu. Your other settings are kept."
  **Switch** (Enter) keeps the dialog's other changes as OK does, then
  reloads the game for that mod and returns to the main menu, without
  closing the window. **Cancel** (Escape) leaves everything as it was. The
  mod chosen is played at every later start too.
- **ROLL BACK** shows on a mod's row when its folder keeps the version an
  update replaced. It swaps the two versions back, and a second press
  undoes the first.
- **Open Mods Folder** opens your Mods folder in the system's file manager,
  where a mod's folder goes to be listed.

During a game the section is locked ("Locked during a game. Choose the mod
from the main menu."): a game keeps the mod it started with. It is locked
too while `--mod-dir` or `--base-game` chose the run's mod ("The command
line chose this run's mod.").

Change it to play a mod, or to go back to the game as 3.1c plays it.
[Mod support](mods/README.md) covers
[installing a mod](mods/README.md#installing-a-mod),
[installing a .oamod file](mods/README.md#installing-a-oamod-file) and
[choosing one](mods/README.md#choosing-a-mod) in full.

## Controls

![The Controls section: Mouse wheel zoom On, Escape opens the game menu On, Select groups without Alt Off.](images/settings/controls.png)

Each of these takes effect at once.

- **Mouse wheel zoom**, On by default. The mouse wheel, or a trackpad's
  scroll, zooms the battlefield in and out; a camera that follows a unit
  (**T**) keeps the unit in the centre as it zooms. Turned off, the view
  eases back to the game's own scale. Turn it off if the wheel zooms when
  you do not mean it to, or to use a mod's megamap, which takes the wheel
  only while this is off
  ([ui.megamap](mods/standard-hacks/ui.megamap.md)).
- **Escape opens the game menu**, On by default on macOS and Off
  elsewhere. The first Escape cancels an order or clears the selection, as
  in 3.1c; the next opens the in-game menu. Turn it on if F2 is awkward to
  reach, as on many Mac keyboards.
- **Select groups without Alt**, Off by default, as 3.1c plays. Off, Alt
  and a number select that group of units, and a number alone turns the
  selected unit's menu to that page. On, a number alone selects its group,
  and Alt and a number turn the page. Ctrl and a number make a group either
  way. Turn it on if you are used to selecting groups with the number keys
  alone, as most strategy games do. It is 3.1c's own SwitchAlt option, so
  the console's `+switchalt` changes it too.

## Common Tweaks

![The Common Tweaks section: Your files showing /Users/player/Documents/Open Annihilation with its SAVES, SCREENSHOTS and MODS buttons, Unit limit at 250 per player and Pathfinding cycles at 1x.](images/settings/common-tweaks.png)

- **Your files** is not a setting: it shows where your saved games,
  screenshots, films, recordings and mods go, the Open Annihilation folder
  in your Documents folder unless you moved it. **SAVES**, **SCREENSHOTS**
  and **MODS** open those folders in the system's file manager. Films and
  the recordings of network games are in its `Films` and `Recordings`
  folders, which have no button. `Saves`, `Screenshots`, `Films` and
  `Recordings` each hold a folder for each mod, named after its id, and
  `default` for games without a mod. The installation guides say what each
  folder holds and how to move the folder
  ([macOS](installation/macos.md#where-it-keeps-its-files),
  [Windows](installation/windows.md#where-it-keeps-its-files),
  [Linux](installation/linux.md#where-it-keeps-its-files)).
- **Unit limit**, 50 to 1500 units per player in steps of 50 (a mod may
  allow more). By default it is the unit limit your Total Annihilation
  installation sets, or 250 where it sets none; a mod may give a default
  of its own. It applies from the next game: a saved game keeps the limit
  it was saved with, and a multiplayer game plays the host's. Raise it for
  bigger battles; lower it if a slow computer struggles late in a game.
- **Pathfinding cycles**, 1x to 8x, 1x by default, the game's own amount.
  It multiplies how much route-finding work the game does each tick, shared
  by every player. More cycles find routes faster, so units of a large
  army start moving sooner after an order, but use more of the processor.
  It applies from the next game, and a multiplayer game or a replay always
  plays at 1x. Raise it on a fast computer if large groups hesitate before
  they move.

Both sliders are locked during a game, and in a multiplayer game or a
replay they show "Set by the host".

## Language

![The top of the Language section: Language set to System default (English), Use modern fonts for game text On and Text size at 80%.](images/settings/language-1.png)

![The rest of the Language section, scrolled: Text size at 80%, Font outline On, Font shadow On and Game text background Off.](images/settings/language-2.png)

The Language section says which language the game's text is shown in and
how that text is drawn. None of these changes the game itself, so no game
locks them, and players in different languages play together.

- **Language**: System default, English, Deutsch, Español, Français,
  Italiano or 简体中文 (Simplified Chinese), each named in itself. System default, the default, follows your
  operating system's preferred languages, and the list says which it chose,
  as "System default (English)". The game's own text and unit names show
  in that language wherever the game data has them; whatever it leaves
  untranslated stays in English. A change shows at once in what is drawn
  every frame and on the menu under the settings, and in other screens and
  panels as they open. Choose a language to play in one other than your
  system's. 3.1c's own command line, such as `open-annihilation german`,
  decides it for the run.
  [Languages](languages.md) says what the game data and the language packs
  translate.
- **Use modern fonts for game text**, On by default. The game's text is
  drawn in modern fonts, which hold the letters of many languages and stay
  sharp at any size, in place of the game's own 8-bit fonts. Turn it off
  for the game's original look. Simplified Chinese is drawn only in them:
  choosing it turns the switch On, and it stays On, locked, "Set by the
  language", while Simplified Chinese is chosen.
- **Text size**, 50% to 300% of the game fonts' sizes in steps of 10%, 80%
  by default. It sizes the modern fonts, so it is locked while they are
  off: the game's own fonts have fixed sizes. Raise it on a large or dense
  screen, or whenever the text is hard to read.
- **Font outline**, On by default: a dark edge round each letter of the
  modern text.
- **Font shadow**, On by default: a dark shadow under the modern text.
- **Game text background**, Off by default: a shaded box behind each line
  of game text.
- **Enable Unicode Multiplayer Chat**, Off by default. Chat lines in a
  shared game are written in UTF-8, so that players can chat in any
  language: the machines of players who have it on read them as written,
  and the others get each letter their code page lacks as `?`. Lines that
  start with `+` or `.` stay commands, as in 3.1c. A language written
  outside the code page, as Simplified Chinese is, or whose pack asks for
  it, turns it On, locked, "Set by the language", while that language is
  chosen, and so does a mod profile
  that sets [ui.text-rendering](mods/standard-hacks/ui.text-rendering.md)
  `unicode`. It changes no game, so players with it on and off play
  together.

The last three keep text readable over a busy battlefield. Turn on Game
text background where the text still gets lost; turn the outline and
shadow off for plainer text. Each takes effect at once.
[ui.text-rendering](mods/standard-hacks/ui.text-rendering.md) shows where
the game draws its text and how the modern fonts draw it.

## Graphics

![The top of the Graphics section: Maximum frame rate at 120 fps, Enhanced anti-aliasing Off, Screen size Desktop and Hardware acceleration at Full.](images/settings/graphics-1.png)

![The rest of the Graphics section, scrolled to its end: Vertical sync Off, Menu scaling Sharp, Native pixel density Off and Explosion flash Full.](images/settings/graphics-2.png)

- **Maximum frame rate**, 30 to 120 frames a second in steps of 5, 120 by
  default. It caps how many pictures the game draws a second; the game
  itself plays at the same speed whatever it is. Lower it to save power,
  heat or battery, or to keep a slow computer's frame rate steady. It takes
  effect at once.
- **Enhanced anti-aliasing**, Off, 2x, 4x, 8x or 16x, Off by default. It
  smooths the jagged edges of units. While the processor draws the
  battlefield (Hardware acceleration Off or Basic), units are drawn at that
  many times the resolution and scaled down; 8x and 16x need a fast
  processor. While the graphics card draws it (Full), the card draws the
  whole battlefield with that many samples across each pixel, as far as
  the window's size and the card's memory allow, and the hint says when it
  draws fewer. Raise it for smoother edges if the frame rate holds.
- **Screen size**, Desktop, 640x480, 800x600, 1024x768 or 1280x1024,
  Desktop by default; it applies from the next start. Desktop leaves the
  screen at the desktop's size. Another size switches the screen to it in
  full screen, and opens a window of that size otherwise. Choose a size for
  the period look, for an old monitor, or so that a slow computer has fewer
  pixels to draw.
- **Hardware acceleration**, Off, Basic or Full, Full by default. It says
  how much of the drawing the graphics card does:
  - **Off**: the processor draws and scales every frame, as the game always
    did.
  - **Basic**: the graphics card scales and composes the frames, so the
    picture fills the window evenly; the processor still draws them.
  - **Full**: the card also draws the battlefield, smoothed at every zoom,
    and the view can zoom out to a sixth of the game's scale, three times
    as far as the processor's drawing allows.

  The card is used only where it can do the work and the computer has at
  least 2 GB of memory. Where Full cannot run, Basic draws in its place;
  where neither can, the processor does. The row's two status lines say
  which is in use and, if not the one chosen, why. At each start the game
  first has the card draw a few known patterns and reads them back, and
  it does not use a card that draws them wrongly. A graphics driver that
  stopped the game or failed twice in a row is passed over at later starts
  (once is enough on Linux and on Windows XP, where such a stop can halt
  the whole computer), and the main menu says so once. Setting it to Off
  and back, or Restore defaults, tries the card again. Each change takes
  effect at once, except during a multiplayer game or a replay: there Off
  applies at once, and Basic or Full from the next game. Leave it at Full
  for the smoothest picture and zoom; set it to Basic or Off if the
  graphics card or its driver misbehaves.
- **Vertical sync**, Off by default. Each frame waits for the display, so
  no frame tears, and the frame rate keeps just below the display's. Turn
  it on if you see tearing lines when the view scrolls. It takes effect at
  once, keeps its value for the length of a multiplayer game or a replay,
  and shows "Not available here" where the renderer cannot offer it.
- **Menu scaling**, Sharp, Whole steps or Unfiltered, Sharp by default. It
  says how the menus, drawn at 640x480, fill the window, and takes effect
  at once.
  - **Sharp**: as large as the window holds, every pixel as wide as the
    next, where the graphics card can.
  - **Whole steps**: the largest whole-number scale that fits; the menus
    are smaller, with a black border, but every pixel is square.
  - **Unfiltered**: they fill the window as the game always drew them.

  Choose Whole steps for the crispest pixels, or Unfiltered for the
  original look.
- **Native pixel density**, Off by default; it applies from the next start.
  The window opens at the display's own pixel density, the Retina
  resolution on a Mac, in place of the window system's, so the picture can
  use every pixel of a high-density display. It needs at least 2 GB of
  memory, and a run started with `--no-hardware-acceleration` keeps it off.
  Turn it on for a sharper picture on such a display, if the frame rate
  holds. It is always on on an iPhone and an iPad.
- **Explosion flash**, Off, Reduced or Full, Full by default. Every
  explosion lights up the ground around it for under a second, brightest at
  its centre and nearly white where flashes overlap, as the game always
  drew it; when a unit is destroyed each piece that bursts flashes, so a
  big battle can light the battlefield white. Reduced lights it at half
  strength; Off draws no flash at all. A mod can design its explosions to
  flash less ([ui.explosion-flash](mods/standard-hacks/ui.explosion-flash.md)),
  and the lower of the two is drawn, so a mod can never make them flash
  more than you chose. It changes only what your screen shows, never the
  game, and takes effect at once, in a multiplayer game or a replay too.
  Choose Reduced or Off if flashing light is uncomfortable for you.

Some computers start with other defaults, which Restore defaults puts back
and which a value you choose always replaces:

- a **light computer**, one with a single processor, a processor without
  SSE2 (a Pentium III or an Athlon XP) or less than 512 MB of memory,
  starts at 800x600 (640x480 when its desktop is smaller), at 60 frames a
  second, with Enhanced anti-aliasing off;
- a **Raspberry Pi** starts at 60 frames a second;
- a **Steam Deck** starts at its screen's rate, 60 frames a second on the
  LCD model and 90 on the OLED, with the touch controls' Control size at
  Larger ([Steam Deck](installation/steam-deck.md)).

## Touch

![The top of the Touch section: One-finger drag Automatic, Hold delay 350 ms, QUEUE and ADD Stay on and Haptics On.](images/settings/touch.png)

The Touch section shows while the game has touch controls: always on an
iPhone and an iPad, on a touch screen once a finger touches it, and with
`--touch-controls`. Each setting takes effect at once.

- **One-finger drag**, Automatic, Box or Scroll, Automatic by default:
  what a finger dragged across the battlefield does. Automatic draws a
  selection box on a tablet and scrolls the map on a phone. A hold then a
  drag always draws a box, and two fingers always scroll.
- **Hold delay**, 250 to 700 ms in steps of 50, 350 ms by default: how long
  a finger stays down before it counts as a hold, which opens the order
  menu or a control's help. Raise it if holds start when you mean to tap.
- **QUEUE and ADD**, Stay on or One action, Stay on by default: whether a
  tapped QUEUE, ADD or x5 stays on until it is tapped again, or turns off
  after the next order or selection.
- **Haptics**, On by default: a short vibration as a touch control acts.
- **Left-handed layout**, Off by default: the minimap and the thumb
  controls on the right, the orders on the left.
- **Control size**, Standard, Large or Larger, Standard by default: how
  large the touch controls are drawn; the game's own screens keep their
  size.

[Touch controls](touch-controls.md#touch-settings) describes them with the
gestures they change.

## Controller

![The top of the Controller section: Scheme Trackpads, Right trackpad Relative, Pointer speed 100% and Pointer acceleration Low.](images/settings/controller.png)

The Controller section shows once a gamepad has been used in this run.
Each setting takes effect at once.

- **Scheme**, Trackpads or Sticks, Trackpads by default. With Trackpads
  the Steam Deck's right trackpad points and the left one moves the map;
  with Sticks the right stick points and the left one moves the map. A
  gamepad without trackpads plays Sticks.
- **Right trackpad**, Relative or Absolute, Relative by default: the
  pointer moves as the thumb slides, or each point of the pad is a point
  of the view.
- **Pointer speed**, 50% to 300%, 100% by default; **Pointer
  acceleration**, Off, Low or High, Low by default, so that a quick slide
  moves the pointer further; and **Trackpad glide**, Off by default, which
  keeps the pointer moving after a quick flick. Change them if the pointer
  feels too slow, too fast or hard to place.
- **Right stick**, Zoom, Pointer or Nothing, Zoom by default: Zoom zooms
  the view about the pointer and, while building, turns the build page
  with a flick.
- **Magnetism (stick pointer)**, On by default: the stick's pointer
  settles on a lone unit near it.
- **Gyro pointer**, Off by default, or on while the right pad is touched,
  while the right stick is touched, or always, and **Gyro speed**, 50% to
  400%, 100% by default: turning the controller fine-tunes the pointer.
- **Haptics**, Off, Light or Strong, Light by default: small ticks and
  bumps felt through the controller.
- **Button prompts**, Automatic by default, or Steam Deck, Xbox,
  PlayStation, Nintendo or Off: the style of the button pictures the
  controls and rings show. Choose one if Automatic picks the wrong style
  for your gamepad.
- **Left-handed**, Off by default: the two sides' roles swapped, so the
  left pad points.
- **Control size**, **Hold delay** and **QUEUE and ADD**, the same settings
  as in Touch.

While the gamepad reaches the game through Steam Input, the section starts
with a notice in amber: Steam then hands the game the trackpads and back
grips as a mouse and keys, so turn Steam Input off for Open Annihilation to
use them here. [Gamepad controls](controllers.md#controller-settings) gives
each setting's choices and default, and what every input does.

## Game files

On an iPhone or an iPad, the main menu's settings have a Game files
section for the Total Annihilation files copied onto the device:

- **Installed**: what is installed, its size and the free space, and
  **Manage…**, which opens the Game files screen to check, add, replace or
  remove the files.
- **Include in device backups**, Off by default: the game files go into
  the device's backups too. They are large, and can always be copied
  again.
- **Where the files are**: the folder that holds them.

[Game files](game-files.md#managing-the-game-files) describes managing
them.

## Developer

![The Developer section: Enable Developer Mode Off and Show performance statistics Off, then the list of standard hacks by area, each area closed and showing how many of its hacks are on, and under the list Show Active Only and RESTORE PROFILE VALUES.](images/settings/developer.png)

The Developer section sits at the foot of the column, under the divider.

- **Enable Developer Mode**, Off by default. It lets you change the
  standard hacks of the mod being played, or of 3.1c without a mod, without
  editing the mod's `oamod.yaml`: turn a hack on or off and set its values,
  to try them before writing them into a profile. Off, the game plays the
  profile as it ships, and your changes are kept for when you turn it on
  again. On, they apply: a change to a view hack, which changes only what
  you see, at once, even in a running match, and a change to a sim hack,
  which changes the game itself, from the next match. A changed sim hack
  changes the profile's sim hash, as writing it into `oamod.yaml` would,
  so a game saved with it loads only with the same changes, and every
  player of a network game must have them. While it is on, a network
  game's battle room is told so.
- **Show performance statistics**, Off by default: the frame and tick
  times over the battlefield, and the renderer in use, as the `+stats`
  console command shows them. It works whether or not Developer Mode is
  on, and takes effect at once.

Under them the section lists every standard hack, grouped by area, with a
switch for each and a control for each of its values. **Show Active Only**
shows only the hacks that are on, and **Restore profile values** clears
every change. Developer Mode keeps its changes for each mod apart. Restore
defaults turns it off but keeps the changes.

The hacks themselves are described in [Mod support](mods/README.md):
[Developer Mode](mods/README.md#developer-mode) in full,
[the table of every standard hack](mods/README.md#every-standard-hack), and
a page for each in [standard-hacks](mods/standard-hacks/).

## For one run: the command line

These options set a setting for one run without changing what is kept.
The row then shows "Set on the command line", and the Mods section "The
command line chose this run's mod."

| Option | Setting | For the run |
|---|---|---|
| `--max-fps N` | Maximum frame rate | N frames a second, 30 to 1000, or 0 for no limit |
| `--hardware-acceleration=off`, `=basic` or `=full` | Hardware acceleration | that level; `--hardware-acceleration` alone is Full |
| `--no-hardware-acceleration` | Hardware acceleration | Off |
| `--native-density` | Native pixel density | On |
| `--mod-dir PATH` | Mods | plays the mod folder at `PATH` |
| `--base-game` | Mods | plays 3.1c without the mod you chose |
| a language's name, as `german` | Language | that language, as 3.1c's command line names it |

Two more change what the settings show: `--touch-controls` turns the touch
controls on, and with them the Touch section, and `--user-folder PATH`
puts Your files' folder at `PATH`. `SDL_RENDER_DRIVER=software` in the
environment has the processor draw every frame, and Hardware acceleration
then says it is not in use.

## Where the settings are kept

The settings are kept in your preferences file, with the game's own
options. The installation guides say where it is
([macOS](installation/macos.md#where-it-keeps-its-files),
[Windows](installation/windows.md#where-it-keeps-its-files),
[Linux](installation/linux.md#where-it-keeps-its-files)), and
[user preferences](../src/platform/preferences/README.md) lists every key.
A setting you have never changed is not written, and follows its default.
To turn Hardware acceleration off for good without starting the game, for
a graphics driver that stops it, set
`open-annihilation.hardware-acceleration` to `off` in that file; `basic`
and `full` name the other levels.

`--preferences-file FILE` reads and writes the settings in `FILE` instead.
Its defaults are then the game's own behaviour on every computer, so that
a check plays alike everywhere: Escape opens the game menu, Hardware
acceleration and Use modern fonts for game text all start Off, the
language is English, the unit limit is 250 (or the mod's own default when
a mod is played), and no computer has defaults of its own.
[The settings' module](../src/ui/engine-settings/README.md) describes the
dialog and every setting for developers.
