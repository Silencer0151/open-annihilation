# Documentation

Every document in the repository, by what you want to do.

## Installing and playing

- [README.md](../README.md): what Open Annihilation is, multiplayer and
  playing the free demo.
- Installation guides, step by step: downloading the release, getting the
  game data from your copy of Total Annihilation, and starting the game.
  - [macOS](installation/macos.md)
  - [Windows](installation/windows.md), including Windows XP
  - [Linux and Raspberry Pi](installation/linux.md)
  - [Steam Deck](installation/steam-deck.md), experimental:
    the Linux package in Desktop Mode, adding it to Steam as a non-Steam
    game, the Steam Input layouts and artwork installed by hand, the
    controls, the first start, the Deck's settings and the logs
  - [iPhone and iPad](installation/ios.md), built on your own Mac
- [settings.md](settings.md): Open Annihilation's own settings, with a
  picture of each section: opening them, OK, Cancel and Restore defaults,
  and what every setting in Mods, Controls, Common Tweaks, Language,
  Graphics, Touch, Controller, Game files and Developer does, its default,
  when it takes effect and when to change it; the command-line options
  that set them for one run, and where they are kept.
- [touch-controls.md](touch-controls.md): playing with fingers on an iPad,
  an iPhone or a touch screen: the gestures, the tablet and phone layouts,
  QUEUE, ADD and x5, the order wheel, building, the Touch settings, the
  keyboard's Cmd keys and the Pencil.
- [controllers.md](controllers.md): playing with a gamepad: the Steam
  Deck's pad pointer (the right trackpad as the mouse, the grips as QUEUE,
  ADD, FORCE and the groups, the order and build rings), sticks only for
  other gamepads, the fallback map, menus, the pad HUD and button prompts,
  the Controller settings, haptics, the Steam Input templates and the
  `--check-pad-controls` check.
- [game-files.md](game-files.md): the Game files screen that brings your
  Total Annihilation files onto an iPhone or an iPad: its three routes,
  stopping and continuing a copy, Settings › Game files and managing the
  files, and the `--check-game-files` check.
- [typing.md](typing.md): typing in any script, Chinese among them, in
  the chat lines, a saved game's name and the whiteboard: input methods,
  commands, saves and recordings named in any script, the iPhone and iPad
  keyboard and the Steam Deck's.
- [languages.md](languages.md): the language the game shows its text in:
  how it is chosen (3.1c's command line, the Language setting, the
  operating system), what the game data translates in German, French,
  Italian and Spanish, the engine's own words, and adding a language.
- [Network games from the command line](../README.md#network-games-from-the-command-line)
  in the README: hosting a TCP/IP game with `--host` or joining one with
  `--join ADDRESS`, with `--player-name`, `--game-name` and
  `--game-password`, and what the game prints as it goes.
- [Demo Recorder](../README.md#demo-recorder) in the README: playing back a
  recorded game (`.tad`) from the command line with `--play-demo`, what a
  recording needs to play, and recording network games.
- [capture.md](capture.md): capturing the game as an MP4 video with
  `--capture-video` (it needs the `ffmpeg` program), and the scripted run
  `--showcase` plays for showcase videos.
- [director.md](director.md): director scripts (`.oascript`) and bundles
  (`.oamovie`): planning a script's camera shots from a recorded game with
  `--generate-script`, rendering it to video with its sound with
  `--render-script`, the script format, the files a render writes, and what
  stays the same on every platform.
- [automation.md](automation.md): the automation endpoint, through which a
  program on the same machine drives the game and watches it: `--fark`,
  `--fark-address` and `--fark-file`, connecting with the token, the
  requests, input through the event queue, events, what it never does,
  and the ` REMOTED` line in the battle room.
- [mods/README.md](mods/README.md): mod support: installing a mod in a mod
  folder or as a copied install, choosing it in the settings or with
  `--mod-dir`, network play and saves under a mod, and the tables of every
  standard hack, each with its own page under
  [mods/standard-hacks/](mods/standard-hacks), and of every script
  extension, each with its own page under
  [mods/script-extensions/](mods/script-extensions).
- [mods/oamod-standard.md](mods/oamod-standard.md): the OAMOD standard, the
  format of a mod profile (`oamod.yaml`) and its rules: parameters,
  presets, settings, limits, script extensions, data keys, hashes and
  network compatibility.

## Developing

- [CONTRIBUTING.md](../CONTRIBUTING.md): the developer guide: what the engine
  does today, what it needs, how to build, test and run it from source, the
  layout of the tree, the checks and what to do when one fails, and how a
  change gets in (the licence of contributions, pull requests and review).
- [development/conventions.md](development/conventions.md): the rules every
  change follows, each with its reason, examples and the directories it
  covers.
- [development/testing.md](development/testing.md): running the tests and
  checks, and writing a new test: the kinds of test, the installed game
  (`OA_GAME_DIR`), network play's tests, pinned values and malformed
  input.
- [AGENTS.md](../AGENTS.md): the working process for AI coding agents; the
  rules it points to are in [development/conventions.md](development/conventions.md).
- [platforms/ios/README.md](../platforms/ios/README.md): building and running
  the game for iPad and iPhone: the Xcode project, the simulator and device
  builds, and copying the game data onto the device.
- [development/releasing.md](development/releasing.md): building, signing,
  notarizing and checking the macOS release packages with
  `tools/release_macos.sh`, on the maintainer's Mac, and the `steam-deck`
  folder of the Linux x86_64 package.
- [tools/sdl-patches](../tools/sdl-patches/README.md): the changes
  the build makes to the pinned SDL release, which
  [tools/bootstrap_sdl.py](../tools/bootstrap_sdl.py) applies, each offered
  upstream.
- [development/formats.md](development/formats.md): the HPI archive (its
  encryption and SQSH compression) and PCX image formats as the asset reader
  implements them, and which archive's copy of a file wins.
- [development/platform.md](development/platform.md): `oa-platform`, the
  SDL3 presentation and audio smoke tool.
- [development/resolution-proofs/](development/resolution-proofs):
  diagrams of the match screen's layout (radar, orders panel, resource bars
  and battlefield) at nine window sizes from 640x480 to 7680x4320, with
  their bounds in `bounds.txt`. They are drawn by
  `tools/resolution_proofs.py`, which mirrors the layout rules of the match
  display layout; run it again after changing those rules.

## Project

- [LICENSE](../LICENSE): the GNU General Public License version 3.
- [ATTRIBUTIONS.md](../ATTRIBUTIONS.md) and [licenses/](../licenses): the
  third-party components and their licences.
- [CODE_OF_CONDUCT.md](../CODE_OF_CONDUCT.md): how everyone taking part
  behaves.
- [SECURITY.md](../SECURITY.md): reporting a security problem privately.
- [CONTRIBUTORS.md](../CONTRIBUTORS.md): the people behind the project.

## Modules

Each module's README says what it is for, its entry points, the state it
reads and writes, and its quirks and limits.

Core and arithmetic:

- [src/core](../src/core/README.md): the canonical game records every system
  shares.
- [src/base/game-math](../src/base/game-math/README.md): integer and floating-point
  helpers whose results match 3.1c bit for bit.

Game data formats:

- [3DO models](../src/formats/objects3d/README.md),
  [COB unit scripts](../src/formats/cob/README.md),
  [fonts](../src/formats/fnt/README.md),
  [GAF sprites](../src/formats/gaf/README.md),
  [languages and translations](../src/data/languages/README.md),
  [unit-order names](../src/data/mission-types/README.md),
  [Smacker intro movies](../src/formats/smacker/README.md),
  [TDF and unit definitions](../src/formats/tdf/README.md),
  [TNT maps](../src/formats/tnt/README.md).

Simulation:

- [combat state](../src/sim/combat-state/README.md),
  [game loop](../src/base/game-loop/README.md),
  [match runtime](../src/sim/match-runtime/README.md),
  [simulation state](../src/sim/simulation-state/README.md),
  [unit activation](../src/sim/unit-activation/README.md),
  [unit effects](../src/sim/unit-effects/README.md),
  [unit health](../src/sim/unit-health/README.md),
  [unit spawn](../src/sim/unit-spawn/README.md),
  [weapon execution](../src/sim/weapon-execution/README.md).
- [ground orders](../src/sim/ground-orders/README.md),
  [unit movement](../src/sim/unit-movement/README.md).
- [scenario state and victory conditions](../src/sim/scenario/README.md).
- [unit-script save state](../src/sim/script-state/README.md),
  [unit-script virtual machine](../src/sim/script-vm/README.md).
- [map runtime](../src/sim/map-runtime/README.md),
  [3DO model runtime](../src/sim/model-runtime/README.md),
  [spatial registration](../src/sim/spatial-state/README.md),
  [visibility](../src/sim/visibility-state/README.md),
  [wind and environment](../src/sim/world-environment/README.md).

Presentation, interface and sound:

- [terrain renderer](../src/present/world-renderer/README.md),
  [card-drawn world](../src/present/gpu-world/README.md),
  [sprite animation](../src/sim/sprite-animation/README.md).
- [game audio](../src/audio/README.md).
- [frontend renderer](../src/ui/frontend-renderer/README.md),
  [frontend state](../src/ui/frontend-state/README.md),
  [GUI input](../src/ui/gui-input/README.md),
  [GUI layout](../src/ui/gui-layout/README.md).

Platform and application:

- [system locale](../src/platform/locale/README.md),
  [display modes](../src/platform/display-modes/README.md),
  [user preferences](../src/platform/preferences/README.md),
  [job pool](../src/platform/job-pool/README.md),
  [render probe](../src/platform/render-probe/README.md),
  [text font](../src/platform/text-font/README.md).
- [src/app](../src/app/README.md): the game application, `open-annihilation`,
  adding a screen or overlay, the extension table, and network play's
  place in the game.
- [card command lists](../src/app/card/README.md): what a frame asks the
  graphics card to draw, and the executor that runs it on SDL's renderer,
  for the Full tier.
- [automation endpoint](../src/app/automation/README.md): `--fark`, the
  loopback service a program on the same machine drives the game through
  by its screens' controls and device input, and the automation protocol's
  frames.

Modules without a README are described by the comments at the top of their
public headers.

## Not written yet

An architecture overview (layers, module map, the frame and tick flow), a
glossary of game and project terms, a building guide covering every option
and platform, a reference for the game data an installation must hold, and a
page of known gaps. Until they exist, [CONTRIBUTING.md](../CONTRIBUTING.md)
and the module READMEs are the best guide.
