<p align="center">
  <img src="https://raw.githubusercontent.com/open-annihilation/branding/main/logos/open-annihilation-header.png" alt="Open Annihilation" width="760">
</p>

<p align="center">
  <a href="https://github.com/open-annihilation/open-annihilation/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/open-annihilation/open-annihilation?style=flat-square&label=release&color=c0392b"></a>
  <a href="https://github.com/open-annihilation/open-annihilation/releases"><img alt="Downloads" src="https://img.shields.io/github/downloads/open-annihilation/open-annihilation/total?style=flat-square&color=2c3e50"></a>
  <a href="LICENSE"><img alt="Licence: GPL v3" src="https://img.shields.io/badge/licence-GPL%20v3-2c3e50?style=flat-square"></a>
  <img alt="Platforms: macOS, Windows, Linux" src="https://img.shields.io/badge/platforms-macOS%20%7C%20Windows%20%7C%20Linux-2c3e50?style=flat-square">
  <a href="https://discord.gg/GWgWTQKuv"><img alt="Discord" src="https://img.shields.io/badge/Discord-join%20us-5865F2?style=flat-square&logo=discord&logoColor=white"></a>
</p>

<p align="center"><b>Runs on</b></p>

<p align="center">
  <img alt="Linux" src="https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black">
  <img alt="macOS" src="https://img.shields.io/badge/macOS-000000?style=for-the-badge&logo=apple&logoColor=white">
  <img alt="Windows" src="https://img.shields.io/badge/Windows-0078D6?style=for-the-badge">
  <img alt="iOS: soon" title="iOS: soon" src="https://img.shields.io/badge/iOS-soon-808080?style=for-the-badge&logo=apple&logoColor=c8c8c8&labelColor=5a5a5a">
  <img alt="Steam Deck: soon" title="Steam Deck: soon" src="https://img.shields.io/badge/Steam%20Deck-soon-808080?style=for-the-badge&logo=steamdeck&logoColor=c8c8c8&labelColor=5a5a5a">
</p>

<p align="center"><b>Built with</b></p>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white">
  <img alt="SDL3" src="https://img.shields.io/badge/SDL3-1B3C73?style=for-the-badge">
  <img alt="FFmpeg" src="https://img.shields.io/badge/FFmpeg-007808?style=for-the-badge&logo=ffmpeg&logoColor=white">
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
| Documentation index | Every document in the repository, the module guides and file format notes among them | [View Document](docs/index.md) |
| Code of Conduct | How everyone taking part is expected to behave, and how to report a problem | [View Document](CODE_OF_CONDUCT.md) |
| Contributor Licence Agreement | The agreement every contributor accepts before a change is merged | [View Document](CLA.md) |
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

Download the zip for your platform from the
[Releases](https://github.com/open-annihilation/open-annihilation/releases)
page. Releases are built for macOS (Intel and Apple silicon), Windows and
Linux.

## Running

Unzip the file and start the game: **Open Annihilation.app** on macOS,
`open-annihilation.exe` on Windows, `open-annihilation` on Linux.

The first time it starts, Open Annihilation asks you to choose the folder
where Total Annihilation is installed, and remembers your choice. To choose a
different folder later, start it with `--choose-game-dir`. To use a folder
for one run only, start it with `--game-dir <folder>`.

- **macOS:** the app is not yet signed by Apple, so macOS asks once before
  it opens. Right-click **Open Annihilation.app** and choose **Open**. On
  macOS 15 or later, open it once, then choose **Open Anyway** in
  System Settings > Privacy & Security.
- **Linux:** an X11 or Wayland desktop is required. The folder dialog uses
  your desktop's file chooser (the XDG portal, or `zenity`).

Open Annihilation writes its log to a `logs` folder in its own per-user
folder, not to the terminal:

- **macOS:** `~/Library/Application Support/net.coreprime.open-annihilation/logs`
- **Linux:** `~/.local/share/open-annihilation/logs`, or
  `$XDG_DATA_HOME/open-annihilation/logs`
- **Windows:** `%LOCALAPPDATA%\CorePrime\Open Annihilation\logs`

A new file begins every UTC day, or when the current one reaches 10 MB. The
folder keeps at most 25 files, none older than seven days, so the logs never
take more than about 250 MB. An error that stops the game still shows in the
terminal it was started from, or in an error box when it was started from
the desktop. When a script or another program captures the game's output,
the output goes there instead.

To build and run it from source instead, see
[CONTRIBUTING.md](CONTRIBUTING.md#build-and-test).

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
  preferences

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

Open Annihilation is dual licensed. The code in this repository is free
software, released under the GNU General Public License version 3: see
[LICENSE](LICENSE). A separate edition of Open Annihilation is also
maintained under closed terms. Contributions are accepted under a
[Contributor Licence Agreement](CLA.md), so that they can be included in both.

The releases include third-party libraries under their own licences. See
[ATTRIBUTIONS.md](ATTRIBUTIONS.md).

### Why Dual Licensing

Open Annihilation is published here under the GNU GPL v3, so that anyone can
play it, study it and improve it.

A separate edition of Open Annihilation, which can play online with the
original game, is maintained alongside this one. That edition is not open
source.
Publishing code that is network-compatible with the original game would make
it easy to build cheats that affect real players in live online matches, and
keeping it closed protects that community.

So that improvements made here can be included in both editions, contributions
are accepted under a [Contributor Licence Agreement](CLA.md) that assigns
copyright in each contribution to the maintainer. Everything published in this
repository remains available under the GNU GPL v3.
