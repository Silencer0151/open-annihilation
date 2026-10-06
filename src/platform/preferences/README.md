# User preferences

This platform boundary stores the game's preference values as strings in a
per-user file outside the installation. The preference names, values and
defaults belong to the frontend.

| Platform | Location |
| --- | --- |
| macOS | User Application Support / `net.coreprime.open-annihilation/preferences.conf` |
| Windows | Local AppData / `CorePrime/Open Annihilation/preferences.conf` |
| Linux | `$XDG_CONFIG_HOME/open-annihilation/preferences.conf`, or `$HOME/.config/open-annihilation/preferences.conf` |

Beside the preferences file the game keeps two files of its own, in the
same format, which it writes itself and the player never needs to edit:
`renderer-state.conf`, what it has seen of each graphics driver on this
machine (`src/app/include/oa/app/renderer_state.hpp`), and
`renderer-sentinel.conf`, the stage a start has reached, which a clean exit
deletes. Deleting either loses nothing but that record. With
`--preferences-file` neither is read or written.

Data the engine keeps for the player, such as game data it unpacks, goes in a
per-user data folder (`data_directory()`), beside the preferences file on
macOS and Windows and in the XDG data folder on Linux:

| Platform | Location |
| --- | --- |
| macOS | User Application Support / `net.coreprime.open-annihilation` |
| Windows | Local AppData / `CorePrime/Open Annihilation` |
| Linux | `$XDG_DATA_HOME/open-annihilation`, or `$HOME/.local/share/open-annihilation` |

The player's own files, their saved games, screenshots, films, recordings
and mods, go in a folder named `Open Annihilation` (`user_folder_name`) in their
Documents folder (`documents_directory()`, `default_user_folder()`), where
they can find them:

| Platform | Documents folder |
| --- | --- |
| macOS | the user's Documents directory, as Foundation resolves it (it does not follow `$HOME`) |
| Windows | the Documents folder (My Documents on Windows XP), wherever it has been redirected, through `SHGetFolderPathW(CSIDL_PERSONAL)` |
| Linux | the folder `XDG_DOCUMENTS_DIR` names in `user-dirs.dirs` under `$XDG_CONFIG_HOME`, or `$HOME/.config` when that is unset or relative; else `$HOME/Documents` |

On Linux, `xdg_documents_directory()` reads the `user-dirs.dirs` text as
`xdg-user-dirs` writes it: `XDG_DOCUMENTS_DIR="$HOME/name"` or an absolute
`"/path"`, a backslash keeping the character after it, the last such line
counting; any other line, a relative path or another variable is passed
over. The folder need not exist: the game makes what it needs when it first
needs it. What the folder holds, the one-time move of saved games into it
and the keys below are the game's (`src/app/include/oa/app/user_folder.hpp`).

Earlier versions named the macOS folder `com.coreprime.open-annihilation`. When
the engine resolves the folder and finds only that one, it renames it to
`net.coreprime.open-annihilation`, once, so the preferences file and the
unpacked demo data (`demo-1997`) keep working. When both exist, it uses the new
folder and leaves the earlier one alone. When the rename fails, it uses the
earlier folder under its old name for that run, says so on standard error, and
tries again on the next start; the files are never copied, merged or replaced.
`apple_data_directory()` does this for a given Application Support folder,
which lets the test run it in a temporary one.

Apple's Foundation API resolves the application-support directory, including the
container location in a sandboxed application. Windows uses the Known Folder
API, rather than assuming a drive or user-profile layout. XDG overrides must be
absolute. Missing or invalid user-directory resolution produces an error, never
a fallback into game assets or the working directory.

The versioned UTF-8-compatible text representation quotes and escapes keys and
values. Its lines may end in LF or CR LF, so a file edited on Windows reads
as the game wrote it; the game writes LF. Reads are bounded and reject corrupt files and duplicate keys. Saves
write a complete temporary file beside the destination, flush it, and replace
the destination. A failed write preserves the last complete settings file;
concurrent game instances use last-writer-wins replacement, not merging.
`save(..., SyncFolder::yes)` also syncs the folder after the replace on
POSIX systems, so that a system crash leaves the old file or the new one;
Windows writes the replace through to the disk either way. `overwrite()`
writes the same format straight into the file, with no temporary file,
rename or flush, for a small file rewritten often whose loss in a crash is
acceptable: a write cut short leaves a file `load()` rejects.

The native application imports the earlier `open-annihilation.ini` from the game
directory only when the new preference file is absent. That migration reads the
legacy file without modifying it. Subsequent saves use the platform location.

Settings the engine adds for itself use keys with the `open-annihilation.` prefix
and no `|`, so they never collide with the game's `<section>|<name>` keys.
`open-annihilation.game-directory` holds the Total Annihilation folder chosen
in the first-start dialog as a UTF-8 path. It is written after that migration,
once the game starts from the folder.
`open-annihilation.mod-directory` holds the mod folder the player chose on
the Open Annihilation settings' Mods page to layer over that folder, as an
absolute UTF-8 path; absent, the game folder plays as it is.
`open-annihilation.picked-mod-directory` holds the folder an earlier
version's Pick Folder... chose, as an absolute UTF-8 path, which the Mods
page lists while the folder still exists. It is kept only while
`open-annihilation.mod-directory` names the same folder: once another mod
or No Mod is chosen, it is erased. Absent, there is none.
`open-annihilation.user-folder` holds the folder the player keeps their
saved games, screenshots, films, recordings and mods in, as an absolute
UTF-8 path, in place of `Open Annihilation` in Documents; absent or
relative, the default holds, and `--user-folder` sets another for one run,
into which no saved games or recordings are moved. With
`--preferences-file` the default is the
`Open Annihilation` folder beside that file, so that a check never reaches
the player's Documents folder, and nothing beside that file is moved.
`open-annihilation.saves-moved` records that the saved games kept beside
the preferences file by earlier versions (`SAVEGAME`, and
`mods/<id>/SAVEGAME`) were moved into that folder's `Saves`, as the saved
games moved and the ones left where they were, `"12 0"`; while it is there
the move is not made again. `open-annihilation.saves-moved-notice` is `due`
while the main menu's notice of a move that moved or left a saved game
waits to be shown, and `told` once it has shown.
`open-annihilation.loose-saves-moved` and
`open-annihilation.loose-saves-moved-notice` do the same for the move of
3.1c's saved games loose in `Saves` into `Saves/default`.
`open-annihilation.recordings-moved` records, in the same way, that the
recordings kept beside the preferences file by earlier versions (`demos`,
and `mods/<id>/demos`) were moved into that folder's `Recordings`; no
notice follows that move.
A mod whose profile names a registry root of its own keeps the game's
`<section>|<name>` settings under `registry:<root>\<section>|<name>` keys
instead, as the mod keeps them under its own registry key, and its first run
seeds the values its profile names there.

The Open Annihilation settings (`oa/ui/engine_settings.hpp`) keep these keys,
written when the player changes a setting and erased again by the settings
dialog's Restore defaults. Values are decimal, but for the screen size's; a
key that is absent or not a whole number gives the default, and a value out
of range is clamped. A switch is On for any number above 0: a word such as
`off` is not a number, so it gives the default; write `0` to turn one off.

| Key | Values | Absent means |
| --- | --- | --- |
| `open-annihilation.path-search-nodes` | path nodes a game tick, 1333 to 10664 | 1333 |
| `open-annihilation.wheel-zoom` | 0 or 1 | 1 |
| `open-annihilation.escape-opens-menu` | 0 or 1 | 1 on macOS with the player's own file, else 0 |
| `open-annihilation.unit-limit` | units per player, 20 to 1500, or to a mod's higher maximum (`oa::data::limits::UnitsPerPlayer`) | the game folder's `totala.ini` `[Preferences] UnitLimit`, clamped to 20 to 500, with the player's own file; else 250 |
| `open-annihilation.max-fps` | frames a second, 30 to 120 | 120 |
| `open-annihilation.anti-aliasing` | 1 (off), 2, 3, 4, 8 or 16 | 1 |
| `open-annihilation.frame-stats` | 0 or 1 | 0 |
| `open-annihilation.screen-size` | `desktop`, or `640x480`, `800x600`, `1024x768` or `1280x1024` | `desktop`; with the player's own file on a light machine, `800x600`, or `640x480` on a smaller desktop |
| `open-annihilation.hardware-acceleration` | `off`, `basic` or `full`; a whole number from an earlier version reads as `off` at 0 or below, else `full` | `full` with the player's own file, else `off` |
| `open-annihilation.vertical-sync` | 0 or 1 | 0 |
| `open-annihilation.menu-scaling` | `sharp`, `whole-steps` or `unfiltered` | `sharp` |
| `open-annihilation.native-density` | 0 or 1 | 0; 1 where the platform opens every window at native density |
| `open-annihilation.explosion-flash` | `off`, `reduced` or `full` | `full` |
| `open-annihilation.modern-fonts` | 0 or 1 | 1 with the player's own file, else 0 |
| `open-annihilation.text-outline` | 0 or 1 | 1 |
| `open-annihilation.text-shadow` | 0 or 1 | 1 |
| `open-annihilation.text-background` | 0 or 1 | 0 |
| `open-annihilation.text-size` | percent of the game fonts' sizes, 50 to 300 | 80 |
| `open-annihilation.language` | `system`, or a language's BCP-47 tag (`en`, `de`, `es`, `fr`, `it`) | `system` with the player's own file, else `en` |

The settings dialog's Select groups without Alt is the game's own
`Total Annihilation|SwitchAlt`, which `+switchalt` also sets. `+stats`
without an argument shows or hides the performance statistics and writes
`open-annihilation.frame-stats`; `+stats 1` and `+stats 0` write nothing.
`--max-fps` sets the frame rate for its run and writes nothing, and so do
`--hardware-acceleration[=off|basic|full]` and `--no-hardware-acceleration`
for Hardware acceleration. With
`--preferences-file` every default is the game's own behaviour on every
platform and `totala.ini` is not read.

Platform references:

- [Apple Application Support directory](https://developer.apple.com/documentation/foundation/url/applicationsupportdirectory)
- [Windows Known Folder API](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath)
- [Windows SHGetFolderPathW](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetfolderpathw)
- [XDG Base Directory Specification](https://specifications.freedesktop.org/basedir/0.8/)
- [xdg-user-dirs](https://www.freedesktop.org/wiki/Software/xdg-user-dirs/)
