# Installing on macOS

Open Annihilation runs on macOS 11 (Big Sur) or later, on Intel and Apple
silicon Macs alike. No game data is included: you also need the game data
from your own copy of Total Annihilation, and step 3 shows how to get it on
a Mac.

## 1. Download Open Annihilation

1. Open the latest release on GitHub:
   [github.com/open-annihilation/open-annihilation/releases/latest](https://github.com/open-annihilation/open-annihilation/releases/latest).
2. Under **Assets**, download
   `open-annihilation-<version>-macos-universal.pkg`, the installer. The
   `open-annihilation-<version>-macos-universal.zip` beside it holds the same
   app without an installer.

## 2. Install it

- **With the installer:** double-click the `.pkg` and follow its steps. It
  puts **Open Annihilation.app** in your Applications folder.
- **With the zip:** double-click the `.zip` to unpack it, then drag
  **Open Annihilation.app** from the folder it makes into Applications.

The app and the installer are signed with a Developer ID and notarized by
Apple, so macOS opens them without a warning.

## 3. Get the game data

Open Annihilation needs the folder of an installed Total Annihilation with
the 3.1 update: the folder that holds `totala1.hpi`. Choose one of the ways
below.

### From GOG's Mac version

GOG sells **Total Annihilation: Commander Pack** with a macOS version, which
holds the game's folder inside its app.

1. Install GOG's Mac version of Total Annihilation, with GOG Galaxy or with
   GOG's installer.
2. In Finder, find the game's icon (in Applications), right-click it and
   choose **Show Package Contents**.
3. Open the **drive_c** folder, then **Program Files**, then **GOG.com**.
4. Copy the **Total Annihilation** folder somewhere convenient, for example
   your **Documents** folder. Hold **Option** while you drag it, so that
   Finder copies it and GOG's version keeps working.

### From a Windows PC

Copy the whole folder of an installed Total Annihilation, the one that
holds `totala1.hpi`, from the PC to the Mac, for example with a USB stick,
into your **Documents** folder. A copy from the original CDs must be
installed on Windows and updated to 3.1 first.

### The free demo

To try Open Annihilation without the full game, use the 1997 demo instead:
see [Playing the demo](../../README.md#playing-the-demo).

## 4. Start the game

1. Open **Open Annihilation** from Applications or Launchpad.
2. The first time, it asks for your Total Annihilation folder. Choose the
   folder from step 3. It remembers your choice.

To choose a different folder later, start it once from Terminal with
`--choose-game-dir`:

```sh
open -a "Open Annihilation" --args --choose-game-dir
```

Open Annihilation's own settings open from the **OA** button at the bottom
right of the main menu, or with **Cmd+,**. See
[Settings](../../README.md#settings).

With the macOS firewall on, the first time you host or join a multiplayer
game macOS asks whether Open Annihilation may accept incoming network
connections: click **Allow**. A firewall between the players must let
through UDP port 47624, which other computers use to find a hosted game,
TCP ports 2300 to 2400 and UDP ports 2350 to 2400.

## Where it keeps its files

Your settings, the log files (in `logs`) and the unpacked demo are in
`~/Library/Application Support/net.coreprime.open-annihilation`.

## Updating and removing

- **To update:** download the newer `.pkg` and install it over the old one,
  or replace the app with the newer one. Your settings are kept.
- **To remove:** drag **Open Annihilation.app** to the Trash. To remove your
  settings and logs too, delete the folder above.
