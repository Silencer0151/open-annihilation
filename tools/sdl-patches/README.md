# SDL patches

Changes Open Annihilation makes to the SDL release it builds against.
`tools/bootstrap_sdl.py` applies them when it unpacks a release, so every
build that bootstraps SDL (`tools/bootstrap_sdl.py` itself, the macOS,
iOS and Windows bootstraps, and `run.sh`) links the same source. The SDL
linked into the packages is therefore the pinned release with these
patches applied, an altered version, which
[ATTRIBUTIONS.md](../../ATTRIBUTIONS.md) marks as such.

Each change is offered upstream to SDL under SDL's own licence (zlib), the
terms of the files it changes, and is dropped here once a pinned release
carries it.

## The patches

| Release | Patch | What it changes |
|---|---|---|
| 3.4.16 | `0001-steam-deck-trackpad-haptics.patch` | The Steam Deck driver accepts `SDL_SendGamepadEffect` with a 65-byte buffer, the report ID byte and the controller's 64-byte feature report, when the report is the trackpad haptic command, and sends it to the controller; any other effect stays unsupported. The game plays its light trackpad ticks this way, and plays the same feels as rumble on an SDL without the change. |

A release with no folder here, such as 3.2.0, the oldest the build
accepts, is built as SDL published it.

## How they are applied

- A release's patches are the `.patch` files in the folder named after it,
  applied in the order of their names.
- Each is a unified diff whose paths are relative to the release's source
  folder, with `a/` and `b/` before them. The text before the first `---`
  line says what the patch does and that it is offered upstream.
- The bootstrap applies them with its own applier, which needs neither
  `patch` nor `git`: every context and removed line must match the release
  exactly, at the line the hunk names, with no fuzz. A patch that does not
  match stops the bootstrap with the file and line.
- It then writes `.oa-patches` in the source folder, naming each applied
  patch with its SHA-256. A source folder whose stamp differs from the
  release's patches, or that has no stamp while the release has patches,
  is removed and unpacked again from the release's archive, whose pinned
  SHA-256 is checked as always, before the patches are applied. A build
  therefore never mixes patched and unpatched files.
- `python3 tools/bootstrap_sdl.py --self-test` (the `sdl-bootstrap-selftest`
  test) checks that every patch reads as a unified diff, names files of the
  release only and applies to a tree made from its own lines, that a
  second application is refused, and that the stamp makes a changed tree
  be unpacked again. It downloads nothing; when the release's archive is
  already in the dependency folder, it also applies the patches to the
  archive's own files, in memory.

## Adding a patch

Write the diff against a freshly unpacked release, for example with
`diff -u` between two copies of the file, put `a/` and `b/` before the
paths, add the explanation before it, and name it with the next number.
The next bootstrap unpacks the release again, because the stamp no longer
matches, and applies the new set.
