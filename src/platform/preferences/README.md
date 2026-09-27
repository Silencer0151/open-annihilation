# User preferences

This platform boundary stores the game's preference values as strings in a
per-user file outside the installation. The preference names, values and
defaults belong to the frontend.

| Platform | Location |
| --- | --- |
| macOS | User Application Support / `net.coreprime.open-annihilation/preferences.conf` |
| Windows | Local AppData / `CorePrime/Open Annihilation/preferences.conf` |
| Linux | `$XDG_CONFIG_HOME/open-annihilation/preferences.conf`, or `$HOME/.config/open-annihilation/preferences.conf` |

Data the engine keeps for the player, such as game data it unpacks, goes in a
per-user data folder (`data_directory()`), beside the preferences file on
macOS and Windows and in the XDG data folder on Linux:

| Platform | Location |
| --- | --- |
| macOS | User Application Support / `net.coreprime.open-annihilation` |
| Windows | Local AppData / `CorePrime/Open Annihilation` |
| Linux | `$XDG_DATA_HOME/open-annihilation`, or `$HOME/.local/share/open-annihilation` |

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
values. Reads are bounded and reject corrupt files and duplicate keys. Saves
write a complete temporary file beside the destination, flush it, and replace
the destination. A failed write preserves the last complete settings file;
concurrent game instances use last-writer-wins replacement, not merging.

The native application imports the earlier `open-annihilation.ini` from the game
directory only when the new preference file is absent. That migration reads the
legacy file without modifying it. Subsequent saves use the platform location.

Settings the engine adds for itself use keys with the `open-annihilation.` prefix
and no `|`, so they never collide with the game's `<section>|<name>` keys.
`open-annihilation.game-directory` holds the Total Annihilation folder chosen
in the first-start dialog as a UTF-8 path. It is written after that migration,
once the game starts from the folder.

Platform references:

- [Apple Application Support directory](https://developer.apple.com/documentation/foundation/url/applicationsupportdirectory)
- [Windows Known Folder API](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath)
- [XDG Base Directory Specification](https://specifications.freedesktop.org/basedir/0.8/)
