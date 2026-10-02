# Installing on Linux and Raspberry Pi

Open Annihilation has three Linux packages: one for 64-bit PCs, and two for
ARM computers such as the Raspberry Pi. No game data is included: you also
need the game data from your own copy of Total Annihilation.

## 1. Choose your package

| Your computer | Package | Needs |
|---|---|---|
| 64-bit PC | `open-annihilation-<version>-linux-x86_64.zip` | glibc 2.28 or later: Debian 10, Ubuntu 20.04, Fedora 29, RHEL 8 or newer |
| 64-bit ARM, including 64-bit Raspberry Pi OS | `open-annihilation-<version>-linux-arm64.zip` | glibc 2.28 or later |
| 32-bit ARMv7, including 32-bit Raspberry Pi OS | `open-annihilation-<version>-linux-armhf.zip` | glibc 2.36 or later: Debian 12, Raspberry Pi OS Bookworm or newer |

To see which you need, run these in a terminal:

```sh
uname -m          # x86_64, aarch64 (64-bit ARM) or armv7l (32-bit ARM)
ldd --version     # the first line ends with the glibc version
```

On a Raspberry Pi, `dpkg --print-architecture` prints `arm64` for the
64-bit system and `armhf` for the 32-bit one.

The game runs on an X11 or Wayland desktop, or from the text console with
no desktop. For sound it uses PipeWire, PulseAudio or ALSA, whichever your
system has.

## 2. Download it

Open the latest release on GitHub,
[github.com/open-annihilation/open-annihilation/releases/latest](https://github.com/open-annihilation/open-annihilation/releases/latest),
and download your package under **Assets**. Or download it in a terminal,
with the release's version in place of `v0.6.1`:

```sh
curl -LO https://github.com/open-annihilation/open-annihilation/releases/download/v0.6.1/open-annihilation-v0.6.1-linux-x86_64.zip
```

## 3. Unzip it

```sh
unzip open-annihilation-<version>-linux-x86_64.zip
```

This makes the folder `open-annihilation-<version>-linux-x86_64`, which
holds the game, `open-annihilation`. If `unzip` is missing, install it, for
example with `sudo apt install unzip`.

## 4. Get the game data

Open Annihilation needs the folder of an installed Total Annihilation with
the 3.1 update: the folder that holds `totala1.hpi`. The examples keep it in
`~/Games/Total Annihilation`; any folder works.

### From a Windows PC

Copy the whole folder of an installed Total Annihilation, the one that
holds `totala1.hpi`, for example with a USB stick:

```sh
mkdir -p ~/Games
cp -r "/media/$USER/<stick>/Total Annihilation" ~/Games/"Total Annihilation"
```

A copy from the original CDs must be installed on Windows and updated to
3.1 first.

### From GOG's Mac version

GOG sells **Total Annihilation: Commander Pack** for Windows and macOS, not
for Linux. If you have GOG's Mac version on a Mac, copy the game's folder
from it: right-click the game's icon in Finder, choose **Show Package
Contents**, open **drive_c**, **Program Files**, then **GOG.com**, and copy
the **Total Annihilation** folder to the Linux computer as above.

### The free demo

To try Open Annihilation without the full game, see
[Playing the demo](../../README.md#playing-the-demo).

## 5. Start the game

From the game's folder:

```sh
cd open-annihilation-<version>-linux-x86_64
./open-annihilation
```

- **On a desktop:** the first time, it asks for your Total Annihilation
  folder with your desktop's file chooser. Choose the folder from step 4.
  It remembers your choice. To choose another folder later, start it with
  `--choose-game-dir`.
- **From the text console** (for example Raspberry Pi OS Lite, or a Pi with
  the desktop switched off): there is no folder dialog, so name the folder
  each time:

  ```sh
  ./open-annihilation --game-dir ~/Games/"Total Annihilation"
  ```

  The game draws straight to the screen.

If the shell says `Permission denied`, the unzip tool lost the program's
permission: run `chmod +x open-annihilation` once.

Open Annihilation's own settings open from the **OA** button at the bottom
right of the main menu, or with **Ctrl+,**. See
[Settings](../../README.md#settings).

For multiplayer, a firewall on the computer must let through UDP port
47624, which other computers use to find a hosted game, TCP ports 2300 to
2400 and UDP ports 2350 to 2400.

## Raspberry Pi

Open Annihilation runs on a Raspberry Pi 4, Pi 400 or Pi 5 with the 64-bit
Raspberry Pi OS (Bookworm or later), with the `linux-arm64` package. A
Pi 2, Pi 3 or Pi 4 with the 32-bit Raspberry Pi OS (Bookworm or later)
needs the `linux-armhf` package. The steps are the ones above: copy the game
data to the Pi, unzip the package and start the game.

On a Raspberry Pi the game starts at 60 frames a second, without enhanced
anti-aliasing, which the Pi's graphics keep up with. Both can be changed in
the settings like on any other computer.

## Where it keeps its files

Your settings, the log files (in `logs`) and the unpacked demo are in
`~/.local/share/open-annihilation`, or in `$XDG_DATA_HOME/open-annihilation`
when `XDG_DATA_HOME` is set.

## Updating and removing

- **To update:** unzip the newer package and start the game from its
  folder. Your settings are kept.
- **To remove:** delete the game's folder. To remove your settings and logs
  too, delete the folder above.
