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

## How the project works

These documents set out how the project is run and how changes are made:

| Document | What it covers | Link |
|---|---|---|
| Contributing guide | The developer guide: what the engine does today, building, testing and running it from source, the layout of the tree, the checks every change passes, and how a pull request is reviewed and merged | [View Document](CONTRIBUTING.md) |
| Code conventions | The rules every change follows, each with its reason, examples and the directories it covers | [View Document](docs/conventions.md) |
| Testing guide | Running the tests and checks, and writing a new test | [View Document](docs/testing.md) |
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

## Download

Download the package for your platform from the
[Releases](https://github.com/open-annihilation/open-annihilation/releases)
page. On macOS, the `.pkg` installer puts **Open Annihilation.app** in
Applications; the zip holds the same app.

| Package | Runs on |
|---|---|
| `macos-universal` | macOS 11 or later, Intel and Apple silicon |
| `windows-x64` | 64-bit Windows 7 and later (built to run on XP x64 and Vista too, untested there) |
| `windows-x86` | 32-bit Windows: XP SP3 and later, on a Pentium III, Athlon XP or later processor |
| `windows-arm64-experimental` | Windows 11 on ARM |
| `linux-x86_64` | 64-bit PC Linux with glibc 2.28 or later: Debian 10, Ubuntu 20.04 and later |
| `linux-arm64` | 64-bit ARM Linux with glibc 2.28 or later, such as 64-bit Raspberry Pi OS |
| `linux-armhf` | 32-bit ARMv7 Linux with glibc 2.36 or later: Debian 12, 32-bit Raspberry Pi OS Bookworm |

## Running

Install the `.pkg` or unzip the file and start the game: **Open
Annihilation.app** on macOS, `open-annihilation.exe` on Windows,
`open-annihilation` on Linux.

The first time it starts, Open Annihilation asks you to choose the folder
where Total Annihilation is installed, and remembers your choice. To choose a
different folder later, start it with `--choose-game-dir`. To use a folder
for one run only, start it with `--game-dir <folder>`.

- **macOS:** the app and the installer are signed with a Developer ID and
  notarized by Apple, so they open without a warning.
- **Linux:** run it from an X11 or Wayland desktop, or from the text
  console with no desktop running. The folder dialog uses your desktop's
  file chooser (the XDG portal, or `zenity`); from the console, name the
  folder with `--game-dir`.
- **Raspberry Pi:** see [Raspberry Pi](#raspberry-pi) below.

Open Annihilation writes its log to a `logs` folder in its own per-user
folder, not to the terminal:

- **macOS:** `~/Library/Application Support/net.coreprime.open-annihilation/logs`
- **Linux:** `~/.local/share/open-annihilation/logs`, or
  `$XDG_DATA_HOME/open-annihilation/logs`
- **Windows:** `%LOCALAPPDATA%\CorePrime\Open Annihilation\logs`; on
  Windows XP, `Local Settings\Application Data\CorePrime\Open Annihilation\logs`
  in your user folder

A new file begins every UTC day, or when the current one reaches 10 MB. The
folder keeps at most 25 files, none older than seven days, so the logs never
take more than about 250 MB. An error that stops the game still shows in the
terminal it was started from, or in an error box when it was started from
the desktop. When a script or another program captures the game's output,
the output goes there instead.

To build and run it from source instead, see
[CONTRIBUTING.md](CONTRIBUTING.md#build-and-test).

### Settings

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
- **Graphics:** Maximum frame rate, 40 to 120 frames a second. Enhanced
  anti-aliasing, off or 2× to 16×: units are drawn at a higher resolution
  and scaled down for smoother edges; 8× and 16× need a fast CPU. Screen
  size, from the next start: Desktop, the default, leaves the screen at the
  desktop's size; 640×480, 800×600, 1024×768 or 1280×1024 switches the
  screen to that size in full screen, and opens the window at that size.
- **Developer:** Show performance statistics: frame and tick times over the
  battlefield, as the `+stats` console command shows them.

Pathfinding cycles and Unit limit cannot change during a game; in a
multiplayer game the host's settings apply. `--max-fps N` on the command
line sets the frame rate for that run without changing the setting.

On a light computer, one with a single processor, a processor without SSE2
(a Pentium III or an Athlon XP) or less than 512 MB of memory, the game
starts at a screen size of 800×600 (640×480 when the desktop is smaller),
with the maximum frame rate at 60 frames a second and Enhanced
anti-aliasing off. Each can be changed in the settings, and **Restore
defaults** puts these back.

### Raspberry Pi

Open Annihilation runs on a Raspberry Pi 4, Pi 400 or Pi 5 with the 64-bit
Raspberry Pi OS (Bookworm or later): download the Linux ARM64 package,
`open-annihilation-<version>-linux-arm64.zip`. A Pi 2, Pi 3 or Pi 4 with the
32-bit Raspberry Pi OS (Bookworm or later) needs the Linux 32-bit ARM package,
`open-annihilation-<version>-linux-armhf.zip`. To see which system your Pi runs, type
`dpkg --print-architecture` in a terminal: `arm64` is 64-bit, `armhf` is
32-bit.

1. Copy the game data to the Pi. Copy the whole folder of an installed
   Total Annihilation 3.1c, the one that holds `totala1.hpi`, from a PC,
   for example with a USB stick to `~/TotalA`:

   ```sh
   cp -r "/media/$USER/<stick>/Total Annihilation" ~/TotalA
   ```

   The 1997 demo works too: see [Playing the demo](#playing-the-demo).
2. Unzip the package, for example into your home folder (with the 32-bit
   package, `linux-armhf` in place of `linux-arm64`):

   ```sh
   unzip open-annihilation-<version>-linux-arm64.zip
   cd open-annihilation-<version>-linux-arm64
   ```

3. Start the game from that folder:

   ```sh
   ./open-annihilation --game-dir ~/TotalA
   ```

   - **From the desktop:** run it in a terminal. Started without
     `--game-dir`, it asks for the Total Annihilation folder the first time
     and remembers it.
   - **From the console** (Raspberry Pi OS Lite, or with the desktop
     switched off): log in and run it there; the game draws straight to the
     screen. There is no folder dialog on the console, so name the folder
     with `--game-dir` each time.

On a Raspberry Pi the game starts with the maximum frame rate at 60 frames
a second and Enhanced anti-aliasing off, which the Pi's graphics keep up
with. Both can be changed in the [settings](#settings) like on any other
computer, and **Restore defaults** puts the Pi's back.

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
