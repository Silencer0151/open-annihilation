# Installing on Windows

Open Annihilation has three Windows packages. One of them runs on every
Windows from XP SP3 to Windows 11. No game data is included: you also need
the game data from your own copy of Total Annihilation.

## 1. Choose your package

| Your computer | Package |
|---|---|
| 64-bit Windows 7, 8, 10 or 11: most PCs | `open-annihilation-<version>-windows-x64.zip` |
| 32-bit Windows, including Windows XP SP3, on a Pentium III, Athlon XP or newer processor | `open-annihilation-<version>-windows-x86.zip` |
| Windows 11 on ARM | `open-annihilation-<version>-windows-arm64-experimental.zip` (experimental) |

To see which Windows you have:

- **Windows 10 and 11:** open **Settings › System › About** and read
  **System type**.
- **Windows 7 and 8:** open **Control Panel › System** and read
  **System type**.
- **Windows XP:** right-click **My Computer**, choose **Properties** and read
  the **General** tab. Unless it says "x64 Edition", it is 32-bit.

If you are not sure, the 32-bit package runs on 64-bit Windows too.

## 2. Download it

1. Open the latest release on GitHub:
   [github.com/open-annihilation/open-annihilation/releases/latest](https://github.com/open-annihilation/open-annihilation/releases/latest).
2. Under **Assets**, download your package.

## 3. Unzip it

1. Right-click the downloaded `.zip` and choose **Extract All…**. On
   Windows XP this opens the Extraction Wizard.
2. Choose where to put it, for example `C:\Games`, and extract.

This makes a folder such as `open-annihilation-<version>-windows-x64` that
holds `open-annihilation.exe`. Always start the game from this folder,
never from inside the `.zip`.

## 4. Get the game data

Open Annihilation needs the folder of an installed Total Annihilation with
the 3.1 update: the folder that holds `totala1.hpi`.

- **From GOG:** install **Total Annihilation: Commander Pack** with GOG
  Galaxy or with GOG's offline installer, as you would to play it. Note
  the folder it installs to: GOG's installer suggests one under
  `C:\GOG Games`.
- **From the original CDs:** install the game and update it to version
  3.1.
- **The free demo:** to try Open Annihilation without the full game, see
  [Playing the demo](../../README.md#playing-the-demo).

## 5. Start the game

1. Double-click `open-annihilation.exe` in the folder from step 3.
2. Windows may say **"Windows protected your PC"**, because the program is
   new to it. Click **More info**, then **Run anyway**. On Windows XP, a
   security warning may ask whether to run the file: click **Run**.
3. The first time, it asks for your Total Annihilation folder. Choose the
   folder from step 4. It remembers your choice.

To choose a different folder later, start it with `--choose-game-dir`:

1. Right-click `open-annihilation.exe` and choose **Create shortcut**.
2. Right-click the shortcut and choose **Properties**.
3. In **Target**, add ` --choose-game-dir` after the program's name.

Or, in a Command Prompt in the game's folder:

```bat
open-annihilation.exe --choose-game-dir
```

Open Annihilation's own settings open from the **OA** button at the bottom
right of the main menu, or with **Ctrl+,**. See
[Settings](../../README.md#settings).

The first time you host or join a multiplayer game, Windows Defender
Firewall may ask whether Open Annihilation may communicate on networks:
allow it, at least on private networks. On Windows XP, a security alert
may ask the same: click **Unblock**. A firewall between the players must
let through UDP port 47624, which other computers use to find a hosted
game, TCP ports 2300 to 2400 and UDP ports 2350 to 2400.

## Windows XP and older computers

- On a computer with a single processor, no SSE2 or less than 512 MB of
  memory, the game starts at 800×600, at up to 60 frames a second and
  without enhanced anti-aliasing. These can all be changed in the settings.
- If you hear no sound, start the game with the engine's own sound output.
  In a Command Prompt in the game's folder:

  ```bat
  set OA_SOUND_OUTPUT=waveout
  open-annihilation.exe
  ```

## Where it keeps its files

Your settings, the log files (in `logs`) and the unpacked demo are in
`%LOCALAPPDATA%\CorePrime\Open Annihilation`. On Windows XP the folder is
`Local Settings\Application Data\CorePrime\Open Annihilation` in your user
folder.

## Updating and removing

- **To update:** unzip the newer package and use its folder. Your settings
  are kept.
- **To remove:** delete the game's folder. To remove your settings and logs
  too, delete the folder above.
