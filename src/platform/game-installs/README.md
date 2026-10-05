# platform/game-installs

Total Annihilation folders found where Steam, Heroic, Lutris or Bottles put
them, so that a player who installed the game with one of those programs
starts playing without choosing a folder. Resolution
([src/app](../../app/README.md), `resolve_game_directory`) plays the one
usable folder found and remembers it, and offers several in the in-engine
folder chooser; `--game-dir` and a usable remembered folder always come
first. The module also gives a folder browser its starting places: the home
folder, and the drives that are mounted.

`oa/platform/game_installs.hpp` (namespace `oa::platform::game_installs`):

- `search_roots_under(home)` gives the usual places under a home folder:
  Steam's folders (`~/.local/share/Steam`, `~/.steam/steam` and Flatpak
  Steam's `~/.var/app/com.valvesoftware.Steam/.local/share/Steam`), Heroic's
  config folders (`~/.config/heroic` and Flatpak Heroic's), the folders whose
  children are Wine prefixes (`~/Games` for Lutris, then
  `~/.local/share/bottles/bottles` and Flatpak Bottles' bottles), and
  `/run/media`, where removable drives such as an SD card mount.
  `default_search_roots()` gives them for this machine: on Linux under
  `$HOME`, with `XDG_DATA_HOME` and `XDG_CONFIG_HOME` honoured when they are
  absolute; on every other system none, so nothing is searched there.
- `find_candidates(roots)` returns every folder holding `totala1.hpi`
  (matched without regard to case), in this order:
  - each Steam folder's libraries: the folder itself and every library its
    `steamapps/libraryfolders.vdf` and `config/libraryfolders.vdf` name
    (never a guessed path), each library's
    `steamapps/appmanifest_298030.acf` and the folder its `installdir`
    names under `steamapps/common` (a name with a separator is refused);
  - each `install_path` of Heroic's `gog_store/installed.json`;
  - each Lutris prefix's `drive_c`, then each Bottles bottle's, searched
    `prefix_search_depth` (4) folders deep, leaving out drive_c's `windows`
    and `users` folders and every link to a folder, and not below a folder
    that holds the archive.

  Each folder is returned once, by its canonical path, marked removable when
  it lies under a removable drive's mount, and at most `most_candidates` (32)
  are returned. A place that does not exist or cannot be read is passed over.
- `library_folders`, `manifest_install_dir` and `heroic_install_paths` read
  the three files: Steam's text key-values (quoted strings with their
  escapes undone, bare words, braces, `//` comments; the older library list
  that names each library by a number is read too) and Heroic's JSON (a
  reader of its own: strings with every escape, `\u` pairs included, as
  UTF-8). Text that breaks off or breaks its format ends the reading, and
  what was read before the fault is kept.
- `source_words` gives the words for where a folder was found ("your Steam
  library", "your Steam library on the SD card", "Heroic", "Lutris",
  "Bottles"), which the main menu's notice and the chooser show.
- `holds_game_archive(folder)` tells whether a folder holds `totala1.hpi`;
  the chooser's browser marks game folders with it.
- `home_folder()` and `drive_folders()` give the browser's places: the home
  folder (`HOME`, or `USERPROFILE` on Windows) and the drives other than the
  system's (on Linux the folders under `/run/media/<user>` and
  `/media/<user>`, on macOS the volumes under `/Volumes` but the start-up
  disk, on Windows the drive letters from D: on).

## Bounds

Every read is bounded by a named limit: a library list or manifest is read up
to `library_file_limit` (1 MiB) and Heroic's list up to `heroic_file_limit`
(4 MiB); at most `most_library_entries` (256) libraries or install paths are
taken from one file; the key-values and JSON readers follow at most
`most_nesting` (64) nested blocks; a folder is listed up to
`prefix_entries_limit` (4096) entries, and at most `prefix_folders_limit`
(2048) folders are looked into below one prefix's `drive_c`.

The module reads files and lists folders; it writes nothing and reads no
game state. It does not decide what can be played: resolution checks each
folder found with the engine's own inspection (`inspect_game_install`).

## Tests

`platform-game-installs` builds synthetic home folders in a temporary
folder: a native Steam with its own library and an SD-card library under a
stand-in for `/run/media` (named twice, beside a library that is not there,
one whose manifest names a folder outside `steamapps/common` and one with no
manifest), `~/.steam/steam` as a link to the same Steam, a Flatpak Steam with
no library list, Heroic native and Flatpak (another game's folder, a folder
also found in Steam, a relative path, a list cut short), a Lutris prefix
with copies of the archive where the search never looks, and a Bottles
bottle with the game four folders below `drive_c`. It checks what is found,
in what order, each folder once, the removable mark, `TOTALA1.HPI` in any
case and the `most_candidates` limit; and it feeds each reader well-formed
and malformed text (cut short, an extra close, a key with no value, too deep,
bad escapes, a lone surrogate, a missing comma).

## Limitations

Lutris's own database, which also names the folder, is not read: its
prefixes are searched instead. Heroic's lists of other stores are not read,
since Total Annihilation is sold on GOG and Steam. A game copied to a plain
folder (not a Wine prefix) is found only by the chooser's browser,
`--game-dir` or the system's folder dialog.
