# Installing on Windows

Open Annihilation has three Windows packages. One of them runs on every
Windows from XP SP3 to Windows 11. No game data is included: you also need
the game data from your own copy of Total Annihilation.

## 1. Choose your package

| Your computer | Package |
|---|---|
| 64-bit Windows 7, 8, 10 or 11: most PCs | `open-annihilation-<version>-windows-x64.zip` |
| 32-bit Windows, including Windows XP SP3, on a Pentium III, Athlon XP or newer processor | `open-annihilation-<version>-windows-x86.zip` |
| Windows 11 on ARM | `open-annihilation-<version>-windows-arm64.zip` |

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

Windows opens paths of up to 259 characters unless long paths are turned
on, which Windows 10, version 1607, and later allow: the **Enable Win32 long
paths** policy (under Computer Configuration, Administrative Templates,
System, Filesystem), or the registry value `LongPathsEnabled` set to 1 under
`HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\FileSystem`. A Total
Annihilation folder whose path, with the names of the files in it, is longer
than that needs long paths turned on, or a folder with a shorter path; the
game says so if it cannot open the folder.

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
[Settings](../settings.md).

Each start makes this copy of the game the opener of `.oamod` files for
your Windows account, with the game's icon on them: double-click one to
install the mod it holds ([mods](../mods/README.md#installing-a-oamod-file)).
Opened while the game runs, the file goes to the running game. A file
sync, such as OneDrive's of Documents, or a virus scanner can hold a new
mod's files for a few seconds, and the install waits for them.

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
- On Windows XP the game plays its sound through its own wave-out output,
  which leaves a single processor more time for the game than SDL's. If you
  hear no sound, start the game with SDL's sound output instead. In a
  Command Prompt in the game's folder:

  ```bat
  set OA_SOUND_OUTPUT=sdl
  open-annihilation.exe
  ```

  On a newer Windows that plays no sound, `set OA_SOUND_OUTPUT=waveout`
  chooses the game's own output in the same way.
- Windows XP draws no icon stored the way the game's is, so the program and
  `.oamod` files show Windows' plain icons there.

## Where it keeps its files

Your saved games, screenshots, films, recordings and mods are in the
**Open Annihilation** folder in your Documents folder (**My Documents** on
Windows XP), wherever Windows keeps it, OneDrive or a folder your
organisation has moved it to included:

| Folder | What it holds |
| --- | --- |
| `Saves` | saved games, a mod's in `Saves/<mod id>` and those without a mod in `Saves/default` |
| `Screenshots` | screenshots (Ctrl+F9) and posters (`MakePoster`), in a folder for each mod as in `Saves` |
| `Films` | films (Ctrl+F10), a `MOVIEnnn` folder each, in a folder for each mod as in `Saves` |
| `Recordings` | recordings of network games, which `--play-demo` plays back, in a folder for each mod as in `Saves` |
| `Mods` | mods you add, each in a folder of its own, which the settings' Mods page lists besides the game folder's `mods` folder ([mods](../mods/README.md#installing-a-mod)) |

A saved game or a recording may be named in any script, Chinese among
them, and the Documents folder may lie under a user name in any script:
both work whatever the system's language for non-Unicode programs, on
Windows XP too ([typing.md](../typing.md#saved-games-and-recordings-named-in-any-script)).

The game makes each folder the first time it needs it. In the settings (the
**OA** button on the main menu), **Common Tweaks** shows the folder under
**Your files**, whose buttons open Saves, Screenshots and Mods in Explorer.

Your settings, the log files (in `logs`) and the unpacked demo stay in
`%LOCALAPPDATA%\CorePrime\Open Annihilation`. On Windows XP that folder is
`Local Settings\Application Data\CorePrime\Open Annihilation` in your user
folder.

**Saved games from earlier versions.** Versions before 0.7 kept saved games
in a `SAVEGAME` folder in `%LOCALAPPDATA%\CorePrime\Open Annihilation`, and
a mod's in `mods/<mod id>/SAVEGAME` there; 0.7.0 kept those without a mod in
`Saves` itself. The first start of a later version moves them into
`Saves/default`, and a mod's into `Saves/<mod id>`, once, and the main menu
then says how many moved and where, with a button that opens the folder.
Nothing is overwritten: a saved game whose name that folder holds already
is moved as `NAME (2).SAV`, keeping both. One that cannot be moved stays where it is, and the
game still lists it there. The log says what moved and what did not. An
earlier version started afterwards no longer sees the saved games that
moved. Recordings, which earlier versions kept in a `demos` folder there,
and a mod's in `mods/<mod id>/demos`, move into `Recordings/default` and
`Recordings/<mod id>` in the same way, once, without a message on the main
menu.

**Putting the folder elsewhere.** Start the game with `--user-folder PATH`,
or add the key `open-annihilation.user-folder` with an absolute path to the
preferences file, and that folder takes the place of the Open Annihilation
folder in Documents. Saved games and recordings from earlier versions move
only into the folder the key or Documents names: a start with
`--user-folder` leaves them where they are and lists the saved games there.
With `--preferences-file`, the folder is the **Open Annihilation** folder
beside that file instead, and saved games and recordings beside that file
are not moved either: the game lists the saved games where they are. An Image Output Directory set in the game, with the console's `Film`
command, still takes the screenshots and films.

## Updating and removing

- **To update:** unzip the newer package and use its folder. Your settings
  are kept.
- **To remove:** delete the game's folder. To remove your settings and logs
  too, delete the Local AppData folder above; your saved games,
  screenshots, films, recordings and mods are in the Open Annihilation
  folder in Documents. To make Windows forget the game as the opener of `.oamod`
  files, delete the keys `HKEY_CURRENT_USER\Software\Classes\.oamod` and
  `HKEY_CURRENT_USER\Software\Classes\OpenAnnihilation.Mod` with the
  Registry Editor.
