<p align="center">
  <img src="https://raw.githubusercontent.com/open-annihilation/branding/main/logos/open-annihilation-header.png" alt="Open Annihilation" width="760">
</p>

<p align="center">
  <a href="https://github.com/open-annihilation/open-annihilation/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/open-annihilation/open-annihilation?style=flat-square&label=release&color=c0392b"></a>
  <a href="https://github.com/open-annihilation/open-annihilation/releases"><img alt="Downloads" src="https://img.shields.io/github/downloads/open-annihilation/open-annihilation/total?style=flat-square&color=2c3e50"></a>
  <a href="LICENSE"><img alt="Licence: GPL v3" src="https://img.shields.io/badge/licence-GPL%20v3-2c3e50?style=flat-square"></a>
  <a href="https://github.com/open-annihilation/open-annihilation/releases"><img alt="Platforms: macOS, Windows, Linux" src="https://img.shields.io/badge/platforms-macOS%20%7C%20Windows%20%7C%20Linux-2c3e50?style=flat-square"></a>
  <a href="https://discord.gg/GWgWTQKuv"><img alt="Discord" src="https://img.shields.io/badge/Discord-join%20us-5865F2?style=flat-square&logo=discord&logoColor=white"></a>
</p>

<p align="center"><b>Runs on</b></p>

<p align="center">
  <a href="https://github.com/open-annihilation/open-annihilation/releases"><img alt="Linux" src="https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black"></a>
  <a href="https://github.com/open-annihilation/open-annihilation/releases"><img alt="macOS" src="https://img.shields.io/badge/macOS-000000?style=for-the-badge&logo=apple&logoColor=white"></a>
  <a href="https://github.com/open-annihilation/open-annihilation/releases"><img alt="Windows" src="https://img.shields.io/badge/Windows-0078D6?style=for-the-badge"></a>
  <a href="https://github.com/open-annihilation/open-annihilation/releases"><img alt="iOS: soon" title="iOS: soon" src="https://img.shields.io/badge/iOS-soon-808080?style=for-the-badge&logo=apple&logoColor=c8c8c8&labelColor=5a5a5a"></a>
  <a href="https://github.com/open-annihilation/open-annihilation/releases"><img alt="Steam Deck: soon" title="Steam Deck: soon" src="https://img.shields.io/badge/Steam%20Deck-soon-808080?style=for-the-badge&logo=steamdeck&logoColor=c8c8c8&labelColor=5a5a5a"></a>
</p>

<p align="center"><b>Built with</b></p>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white">
  <img alt="SDL3" src="https://img.shields.io/badge/SDL3-1B3C73?style=for-the-badge">
  <img alt="CMake" src="https://img.shields.io/badge/CMake-064F8C?style=for-the-badge&logo=cmake&logoColor=white">
  <img alt="Claude" src="https://img.shields.io/badge/Claude-D97757?style=for-the-badge&logo=claude&logoColor=white">
</p>

**Open Annihilation** is an open-source game engine for playing
**Total Annihilation** on modern macOS, Windows and Linux, using the game
data from your own copy of the original game. Support for
**Total Annihilation: Kingdoms** is on the [roadmap](#roadmap).

No game data is included. You need an installed copy of Total Annihilation
with the 3.1 update, such as the GOG edition. To explore the project without
the full game, you can use the content of the free Total Annihilation demo
instead: see [Playing the demo](#playing-the-demo).

Open Annihilation is an independent project. It is not affiliated with or
endorsed by the owners of Total Annihilation, Total Annihilation: Kingdoms or
the Boneyards online service. Total Annihilation, Total Annihilation: Kingdoms
and Boneyards, including their names, game data and other content, are the
copyright and trademarks of their respective owners.

## Installation guides

Step-by-step guides to downloading Open Annihilation, getting the game data
from your copy of Total Annihilation and starting the game:

| | Platform | Guide |
|:---:|---|---|
| <img alt="macOS" src="docs/images/platforms/macos.svg"> | macOS 11 or later, Intel and Apple silicon | [Installing on macOS](docs/installation/macos.md) |
| <img alt="Windows" src="docs/images/platforms/windows.svg"> | Windows XP SP3 to Windows 11: 64-bit, 32-bit and ARM | [Installing on Windows](docs/installation/windows.md) |
| <img alt="Linux" src="docs/images/platforms/linux.svg"><br><img alt="Raspberry Pi" src="docs/images/platforms/raspberry-pi.svg"> | Linux and Raspberry Pi: 64-bit PC, 64-bit ARM and 32-bit ARM | [Installing on Linux](docs/installation/linux.md) |

## How the project works

These documents set out how the project is run and how changes are made:

| Document | What it covers | Link |
|---|---|---|
| Contributing guide | The developer guide: what the engine does today, building, testing and running it from source, the layout of the tree, the checks every change passes, and how a pull request is reviewed and merged | [View Document](CONTRIBUTING.md) |
| Code conventions | The rules every change follows, each with its reason, examples and the directories it covers | [View Document](docs/development/conventions.md) |
| Testing guide | Running the tests and checks, and writing a new test | [View Document](docs/development/testing.md) |
| Variances from 3.1c | Where the engine always differs from 3.1c, on purpose | [View Document](VARIANCES.md) |
| Documentation index | Every document in the repository, the module guides and file format notes among them | [View Document](docs/index.md) |
| Code of Conduct | How everyone taking part is expected to behave, and how to report a problem | [View Document](CODE_OF_CONDUCT.md) |
| Security policy | How to report a security problem privately | [View Document](SECURITY.md) |
| Licence | The GNU General Public License version 3, which the code is released under | [View Document](LICENSE) |
| Copyright | Who holds the copyright in the repository's files, which each file's header points to | [View Document](COPYRIGHT) |
| Attributions | The third-party components and their licences | [View Document](ATTRIBUTIONS.md) |
| Contributors | Credits | [View Document](CONTRIBUTORS.md) |
| Agent instructions | Instructions for AI coding agents working in the repository | [View Document](AGENTS.md) |

## Related projects

Open Annihilation is part of a family of projects for Total Annihilation and
Total Annihilation: Kingdoms:

- **[CorePrime](https://coreprime.net/)** is bringing Total Annihilation's
  Boneyards online service back to life. The services launch in the coming
  weeks: **[register at coreprime.net](https://coreprime.net/)** in advance.
- **[KBot](https://github.com/coreprime/kbot)** is an open-source toolkit with
  full support for both Total Annihilation and TA: Kingdoms game assets: a
  command-line tool for the games' file formats, and a browser-based studio
  with an asset explorer, map editor, unit viewer and live sandbox.

## Roadmap

- Completing the rest of Total Annihilation's single-player game.
- Support for **Total Annihilation: Kingdoms**.

## Settings

Open Annihilation's own settings open from the **OA** button at the bottom
right of the main menu, or under Resume in the in-game menu (F2), and with
**Cmd+,** on macOS (also **Settings…** in the application menu) or
**Ctrl+,** elsewhere. Changes show at once. **OK** (Enter) keeps them,
**Cancel** (Escape) puts back what was there, and **Restore defaults** sets
every setting back to its default. A game played alone stays paused while
they are open; a multiplayer game keeps running.

- **AI & Pathfinding:** Pathfinding cycles, 1× to 8×. More cycles find
  routes faster but use more CPU.
- **Controls & Input:** Mouse wheel zoom (on by default). Escape opens the
  game menu (on by default on macOS): the first press cancels an order or
  clears the selection, the next opens the in-game menu. Select groups
  without Alt: a number key selects its group on its own.
- **Gameplay:** Unit limit, 50 to 1500 units per player, from the next game.
  It starts at the unit limit your Total Annihilation installation sets, or
  250. A saved game keeps the limit it was saved with.
- **Graphics:** Maximum frame rate, 30 to 120 frames a second. Enhanced
  anti-aliasing, off or 2× to 16×: units are drawn at a higher resolution
  and scaled down for smoother edges; 8× and 16× need a fast CPU. Screen
  size, from the next start: Desktop, the default, leaves the screen at the
  desktop's size; 640×480, 800×600, 1024×768 or 1280×1024 switches the
  screen to that size in full screen, and opens the window at that size.
  Hardware acceleration (on by default): the graphics card scales the
  picture, only where it is able to and the computer has at least 2 GB of
  memory; a short test first draws known patterns on the card and reads
  them back, and a card that draws them wrongly is not used. The game runs
  that test at start, and keeps beside its preferences file what it saw of
  each graphics driver: a driver that stopped the game, or failed, at two
  starts or in two runs in a row is passed over, or left to the processor,
  and on Linux and on Windows XP, where such a stop can halt the whole
  computer, one stop while the card is being tried is enough; the main
  menu says so once. Switching the setting Off and On, or Restore defaults,
  tries them again. Its two status lines say whether it is in use, and why not. Off,
  or wherever it is not in use, the processor draws and scales every frame,
  as before; turned Off or On it applies at once. `--hardware-acceleration`
  and `--no-hardware-acceleration` decide it for one run. Vertical sync
  (off by default): each frame waits for the display, so that none tears,
  and the frame rate keeps just below the display's; SDL's software
  renderer does not offer it, and neither does SDL's `direct3d` renderer,
  whose graphics device each change would reset. The Graphics
  section is taller than the dialog and scrolls, with the mouse wheel, its
  scroll bar, Page Up, Page Down, Home and End.
- **Developer:** Show performance statistics: frame and tick times over the
  battlefield, and the renderer in use, as the `+stats` console command
  shows them.

Pathfinding cycles and Unit limit cannot change during a game; in a
multiplayer game the host's settings apply. Vertical sync keeps its value
for the length of a multiplayer game or a replay. Hardware acceleration is
each player's own: turned off during such a game it applies at once, and
turned on it takes effect from the next game. `--max-fps N` on the command
line sets the frame rate for that run without changing the setting, and
`--hardware-acceleration` or `--no-hardware-acceleration` sets Hardware
acceleration for that run. To turn Hardware acceleration off for good
outside the game, set the key `open-annihilation.hardware-acceleration` to
`0` in the preferences file
([src/platform/preferences](src/platform/preferences/README.md)): only a
number is read, so `off` leaves the default.
`SDL_RENDER_DRIVER=software` in the environment draws and presents every
frame without the graphics card.

On a light computer, one with a single processor, a processor without SSE2
(a Pentium III or an Athlon XP) or less than 512 MB of memory, the game
starts at a screen size of 800×600 (640×480 when the desktop is smaller),
with the maximum frame rate at 60 frames a second and Enhanced
anti-aliasing off. Each can be changed in the settings, and **Restore
defaults** puts these back.

## Playing the demo

The Total Annihilation demo, released free in 1997, is enough to try Open
Annihilation without the full game. It holds the first three missions of the
Arm campaign and 32 of the game's Arm and Core units. The demo has no skirmish
maps, multiplayer, Core campaign, saved games, movies or music, so those parts
of the game are not available with it. As in the demo, the menus gray out or
hide the entries it cannot open, and Skirmish and Multiplayer open a notice
that says so.

### Get the demo

Download **Total Annihilation.exe** from the Internet Archive:
[archive.org/details/TotalAnnihilation_201405](https://archive.org/details/TotalAnnihilation_201405).
The file is 21,540,864 bytes. To check that you have the same file, compare
its SHA-256 checksum with this one:

```text
5e41cf05226c274b4ac9e4398f74f6b321506bd7a4317ee1744ff7aceba34c49
```

- **macOS:** `shasum -a 256 "Total Annihilation.exe"`
- **Linux:** `sha256sum "Total Annihilation.exe"`
- **Windows:** `certutil -hashfile "Total Annihilation.exe" SHA256`

Open Annihilation checks the file's size and SHA-256 itself before it
unpacks the game data, whatever the file is named, and unpacks from no other
file.

### Set it up

1. Make a new folder, for example `TA Demo`, and put
   **Total Annihilation.exe** in it. Do this on macOS and Linux too. You never
   run the `.exe`, and it does not need Windows: Open Annihilation only reads
   the game data packed inside it.
2. Start Open Annihilation and choose that folder when it asks for the Total
   Annihilation folder. If you have already chosen another folder, start it
   with `--choose-game-dir` to choose again, or with `--game-dir <folder>` to
   use the demo for one run only.

The first time, Open Annihilation checks the file and unpacks the demo's
game data, about 20 MB, into a `demo-1997` folder in its per-user data folder:

- **macOS:** `~/Library/Application Support/net.coreprime.open-annihilation`,
  beside its preferences. Earlier versions named it
  `com.coreprime.open-annihilation`; Open Annihilation renames that folder
  the next time it starts.
- **Linux:** `~/.local/share/open-annihilation`, or
  `$XDG_DATA_HOME/open-annihilation` when `XDG_DATA_HOME` is set
- **Windows:** `%LOCALAPPDATA%\CorePrime\Open Annihilation`, beside its
  preferences (on Windows XP, `Local Settings\Application Data\CorePrime\Open Annihilation`
  in your user folder)

Later starts check the unpacked data and use it directly, and unpack it again
only if it is missing or damaged. The folder you chose is left as it is, and it
is the one Open Annihilation remembers. Keep **Total Annihilation.exe** in
that folder: Open Annihilation recognises the folder by it every time it
starts.

If you installed the demo on Windows with its own installer, choose the
folder that holds `TADemo.hpi` instead.

When you build from source, `./run.sh --game-dir "<folder>"` builds the
engine and plays the demo from that folder.

## Community

Join the Open Annihilation Discord server to follow development, report
problems and talk about the game:
**[discord.gg/GWgWTQKuv](https://discord.gg/GWgWTQKuv)**

Everyone taking part is expected to follow the [Code of Conduct](CODE_OF_CONDUCT.md).
Credits are in [CONTRIBUTORS.md](CONTRIBUTORS.md).

## Licence

Open Annihilation is free software, released under the GNU General Public
License version 3 only: see [LICENSE](LICENSE) and [COPYRIGHT](COPYRIGHT).
Contributions are accepted under the same licence, and their authors keep the
copyright in them.

The releases include third-party libraries under their own licences. See
[ATTRIBUTIONS.md](ATTRIBUTIONS.md).
